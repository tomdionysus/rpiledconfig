// The LED decoder: luminance samples with timestamps in, a machine ID out.
// Pure logic, fed one sample at a time by the camera's frame processor (or a
// test's synthesizer), following docs/protocol.md (The LED channel,
// Decoding), frame version 4.
//
// Levels: an adaptive threshold (running minimum and maximum decaying over
// about 2 s, "dark" below a minimum contrast) turns each sample into a signed
// deviation, on positive.
//
// Reading: the frame repeats unchanged, so the stream is cyclic and a sample
// from any part of any cycle belongs to one known half-bit of the frame. The
// decoder does not wait for a sync or for a second pass: it keeps the last two
// frame periods of samples and, as soon as it has half a frame, matches them
// against the frame at every alignment (where the frame starts, and the LED's
// clock within 5%). The sync, the start bit and the idle are known and must
// agree; each data cell adds how decisive it is. The best few alignments are
// read out, a byte whose cells are unseen or undecided is an erasure, and the
// Reed-Solomon code fills and repairs the rest. A word is accepted only with
// the right magic, version and CRC, which is what makes trying many
// alignments safe. A read therefore takes as long as it takes to see enough
// of the cycle, from wherever the camera came in: under one frame when the
// code can fill what is missing.
import { FRAME_BITS, FRAME_T, IDLE_T, LED_CODEWORD_BYTES, LED_PARITY_BYTES, SYNC_OFF_T, SYNC_ON_T, T_MS } from '@/protocol/constants';
import { readCodeword } from './codeword';

export interface Sample {
  /** Milliseconds, any origin, monotonic. */
  t: number;
  /** Brightness of the LED region, 0..255. */
  y: number;
}

export type DecoderPhase = 'dark' | 'seeking' | 'reading';

export interface DecodedId {
  idBytes: Uint8Array;
  idHex: string;
  /** Wrong bytes the code repaired, and erased bytes it filled, in the decode that read it. */
  errors: number;
  erasures: number;
  /** Frame periods of samples the read drew on, a part period counting as one. */
  framesCombined: number;
}

export interface DecoderState {
  phase: DecoderPhase;
  /** Whether the frame has been placed in the samples (the best alignment so far). */
  syncFound: boolean;
  /** Cells covered so far at the best alignment, out of `FRAME_BITS`. */
  bits: number;
  /** The same: cells seen at least once, out of `FRAME_BITS`. */
  seen: number;
  /** Frame periods of samples held, a part period counting as one. */
  framesCombined: number;
  /** Bytes erased (unseen or undecided) at the best alignment, out of 14; the code fills up to 7. */
  erasures: number;
  /** Bytes repaired in the successful decode. */
  repaired: number;
  /** Decodes that succeeded, and whole frame periods seen that did not decode. */
  framesRead: number;
  framesFailed: number;
  /** The read: one decode that passed magic, version and CRC. */
  result?: DecodedId;
  /**
   * Whether the region holds a light blinking as this protocol does: in the
   * last 2 s, at least four runs of 1 to 5 half-bits and none held much
   * longer. A dark camera's frame-to-frame noise and a slow blink are not.
   */
  ledSeen: boolean;
  /**
   * The fraction of the code seen, 0..1: of the frame's half-bit slots
   * (the frame repeats, so every sample falls in one, wherever the camera
   * came in), those with a sample taken while `ledSeen`. 0 with no LED, back
   * to 0 when the LED is lost, 1 when every bit has been seen.
   */
  coverage: number;
  /** The last sample's level, for a UI meter. */
  level?: boolean;
  contrast: number;
}

/**
 * Time constant of the running minimum and maximum, in milliseconds: well
 * over the longest steady stretch in a frame (5T = 250 ms), so neither extreme
 * decays into the threshold mid-frame.
 */
const RANGE_TAU_MS = 2000;
/** Below this swing the region is not looking at a blinking LED. */
const MIN_CONTRAST = 10;
/** Hysteresis of the UI's on/off level, as a fraction of the range. */
const HYSTERESIS = 0.15;
/** The LED's clock may be this far off; the protocol asks for 5%. */
const PERIOD_TOLERANCE = 0.05;
/** One frame, nominal, in milliseconds. */
const FRAME_MS = FRAME_T * T_MS;
/** Samples kept: two frame periods at the slowest clock. */
const KEEP_MS = 2 * FRAME_MS * (1 + PERIOD_TOLERANCE);
/** Matching starts once this much of a frame has been seen; the code can fill the rest. */
const FIRST_TRY_MS = FRAME_MS / 2;
/** How often matching runs, in milliseconds of samples: often enough to read promptly, rarely enough to leave the JS thread alone. */
const TRY_EVERY_MS = 1000;
/** Folding: bins per half-bit, the bins left out at each edge of a half-bit, and bins in a cycle. */
const BINS_PER_HALF_BIT = 10;
const EDGE_BINS = 1;
const FOLD_BINS = FRAME_T * BINS_PER_HALF_BIT;
/** The coarse search's clock rates: 1% apart across the tolerance. */
const COARSE_RATES = Array.from({ length: 11 }, (_, i) => 1 - PERIOD_TOLERANCE + i * 0.01);
/** Coarse alignments refined, and refined alignments read out. */
const REFINE_TOP = 4;
const READ_TOP = 3;
/** The fine search around a coarse alignment: rate either way in small steps, start in bins either way. */
const FINE_RATE_SPAN = 0.006;
const FINE_RATE_STEP = 0.001;
const FINE_SHIFT_BINS = 6;
/**
 * Telling the protocol's blinking from anything else, over the last
 * LED_WINDOW_MS: runs of 1T to 5T (50 to 250 ms) with a camera frame's slack
 * either way, at least LED_MIN_RUNS of them, and none longer than about 6T.
 */
const LED_WINDOW_MS = 2000;
const LED_SHORTEST_RUN_MS = 30;
const LED_LONGEST_RUN_MS = 6 * T_MS + 40;
const LED_MIN_RUNS = 4;
/** The most of the window's samples that may sit in the middle half of the range for it to be an LED's two levels. */
const LED_MAX_MIDDLING = 0.35;
/** A normalised cell score below this is too uncertain to guess: its byte is erased. */
const ERASE_BELOW = 0.3;

/** Half-bit h of the frame: +1 known on, -1 known off, 0 a data half-bit. */
const TEMPLATE: Int8Array = (() => {
  const template = new Int8Array(FRAME_T);
  for (let h = 0; h < FRAME_T; h += 1) {
    if (h < SYNC_ON_T) template[h] = 1;
    else if (h < SYNC_ON_T + SYNC_OFF_T) template[h] = -1;
    else if (h === SYNC_ON_T + SYNC_OFF_T) template[h] = 1; // the start bit, a Manchester 1
    else if (h === SYNC_ON_T + SYNC_OFF_T + 1) template[h] = -1;
    else if (h >= FRAME_T - IDLE_T) template[h] = -1;
  }
  return template;
})();
/** The half-bit at which cell 0 (the start bit) begins. */
const FIRST_CELL_H = SYNC_ON_T + SYNC_OFF_T;

interface Deviation {
  t: number;
  /** Brightness above the threshold, so on is positive. */
  d: number;
}

/** Where the frame sits in the samples: frame start (any cycle) and the LED's half-bit, in ms. */
interface Alignment {
  start: number;
  halfBit: number;
  score: number;
}

/** One alignment's view of the cells: scores, normalised, and which cells were seen in both halves. */
interface Cells {
  scores: Float64Array;
  covered: Uint8Array;
  count: number;
}

export class LedDecoder {
  private lo = 0;
  private hi = 0;
  private haveRange = false;
  private lastT = 0;
  private level = false;
  private phase: DecoderPhase = 'dark';
  private samples: Deviation[] = [];
  private firstT = 0;
  private lastTry = -Infinity;
  private aligned = false;
  private covered = 0;
  private runStart = 0;
  /** Recent runs of the on/off level: when each ended and how long it was, in ms. */
  private runs: { end: number; length: number }[] = [];
  private ledSeen = false;
  /** Half-bit slots of the cycle with a sample taken while the LED was seen. */
  private slots = new Uint8Array(FRAME_T);
  private slotsSeen = 0;
  private erasures = LED_CODEWORD_BYTES;
  private repaired = 0;
  private framesRead = 0;
  private framesFailed = 0;
  private result?: DecodedId;

  /** Feed one sample; the returned state is the decoder's after it. */
  push(sample: Sample): DecoderState {
    const { t, y } = sample;
    if (!this.haveRange) {
      this.lo = y;
      this.hi = y;
      this.haveRange = true;
      this.lastT = t;
      return this.state();
    }

    const dt = Math.max(0, t - this.lastT);
    this.lastT = t;
    const decay = 1 - Math.exp(-dt / RANGE_TAU_MS);
    this.lo = Math.min(y, this.lo + (y - this.lo) * decay);
    this.hi = Math.max(y, this.hi - (this.hi - y) * decay);
    const range = this.hi - this.lo;

    if (range < MIN_CONTRAST) {
      // Nothing is blinking in the region; forget what was gathered.
      if (this.phase !== 'dark') this.reset();
      return this.state();
    }

    const mid = (this.lo + this.hi) / 2;
    const band = range * HYSTERESIS;
    if (this.phase === 'dark') {
      this.phase = 'seeking';
      this.firstT = t;
      this.runStart = t;
    }
    const wasOn = this.level;
    if (!this.level && y > mid + band) this.level = true;
    else if (this.level && y < mid - band) this.level = false;
    if (this.level !== wasOn) {
      this.runs.push({ end: t, length: t - this.runStart });
      this.runStart = t;
    }
    while (this.runs.length > 0 && this.runs[0].end < t - LED_WINDOW_MS) this.runs.shift();
    // Judged over a full window only: a few samples fall into plausible runs and levels by chance.
    const wasSeen = this.ledSeen;
    this.ledSeen = t - this.firstT >= LED_WINDOW_MS && blinksLikeTheProtocol(this.runs, t - this.runStart) && isTwoLevel(this.samples, t, range);
    if (this.ledSeen && !wasSeen) {
      // The window that showed it is an LED was the LED: its samples are code seen.
      for (const earlier of this.samples) if (earlier.t >= t - LED_WINDOW_MS) this.markSlot(earlier.t);
    }
    if (!this.ledSeen && this.slotsSeen > 0) {
      // Lost: what was seen may have been a different light or a moved phone.
      this.slots.fill(0);
      this.slotsSeen = 0;
    }
    if (this.ledSeen) this.markSlot(t);

    this.samples.push({ t, d: y - mid });
    while (this.samples.length > 0 && this.samples[0].t < t - KEEP_MS) this.samples.shift();

    const span = t - this.samples[0].t;
    if (!this.result && span >= FIRST_TRY_MS && t - this.lastTry >= TRY_EVERY_MS) {
      this.lastTry = t;
      this.phase = 'reading';
      const failedBefore = Math.floor((t - this.firstT) / FRAME_MS);
      if (!this.tryRead()) this.framesFailed = Math.max(this.framesFailed, failedBefore);
    }

    return this.state();
  }

  state(): DecoderState {
    const span = this.samples.length > 0 ? this.lastT - this.samples[0].t : 0;
    return {
      phase: this.phase,
      syncFound: this.aligned,
      bits: this.covered,
      seen: this.covered,
      framesCombined: this.samples.length > 0 ? Math.max(1, Math.ceil(span / FRAME_MS - 1e-9)) : 0,
      erasures: this.erasures,
      repaired: this.repaired,
      framesRead: this.framesRead,
      framesFailed: this.framesFailed,
      result: this.result,
      ledSeen: this.ledSeen,
      coverage: this.slotsSeen / FRAME_T,
      level: this.haveRange ? this.level : undefined,
      contrast: this.hi - this.lo,
    };
  }

  /**
   * Match the samples against the frame, read out the best alignments, and
   * keep the first that decodes. Two windows: the latest frame period alone,
   * so that older samples from a bad moment cannot hold a read back, and
   * everything held, so that two partial looks can fill each other in.
   */
  private tryRead(): boolean {
    const last = this.samples[this.samples.length - 1].t;
    const latest = this.samples.filter((sample) => sample.t >= last - FRAME_MS * (1 + PERIOD_TOLERANCE));
    const windows = latest.length < this.samples.length ? [latest, this.samples] : [this.samples];
    let fewest = LED_CODEWORD_BYTES;
    let mostCovered = 0;
    for (const window of windows) {
      const read = this.readWindow(window);
      if (read.done) return true;
      fewest = Math.min(fewest, read.erasures);
      mostCovered = Math.max(mostCovered, read.covered);
    }
    this.covered = mostCovered;
    this.erasures = fewest;
    return false;
  }

  private readWindow(samples: Deviation[]): { done: boolean; erasures: number; covered: number } {
    const candidates = bestAlignments(samples);
    if (candidates.length === 0) return { done: false, erasures: LED_CODEWORD_BYTES, covered: 0 };
    this.aligned = true;

    let fewest = LED_CODEWORD_BYTES;
    let mostCovered = 0;
    for (const alignment of candidates.slice(0, READ_TOP)) {
      const cells = normalise(cellScores(samples, alignment));
      mostCovered = Math.max(mostCovered, cells.count);
      const read = decodeCells(cells);
      fewest = Math.min(fewest, read.erasures);
      if (!read.ok) continue;
      this.covered = cells.count;
      this.erasures = read.erasures;
      this.repaired = read.errors + read.erasures;
      this.framesRead += 1;
      const span = samples[samples.length - 1].t - samples[0].t;
      const idHex = Array.from(read.idBytes, (b) => b.toString(16).padStart(2, '0')).join('');
      this.result = {
        idBytes: read.idBytes,
        idHex,
        errors: read.errors,
        erasures: read.erasures,
        framesCombined: Math.max(1, Math.ceil(span / FRAME_MS - 1e-9)),
      };
      return { done: true, erasures: read.erasures, covered: cells.count };
    }
    return { done: false, erasures: fewest, covered: mostCovered };
  }

  /** The half-bit slot of the cycle a sample at `t` falls in, now seen. */
  private markSlot(t: number): void {
    let slot = Math.floor(t / T_MS) % FRAME_T;
    if (slot < 0) slot += FRAME_T;
    if (!this.slots[slot]) {
      this.slots[slot] = 1;
      this.slotsSeen += 1;
    }
  }

  private reset(): void {
    this.phase = 'dark';
    this.samples = [];
    this.lastTry = -Infinity;
    this.aligned = false;
    this.covered = 0;
    this.runs = [];
    this.ledSeen = false;
    this.slots.fill(0);
    this.slotsSeen = 0;
    this.erasures = LED_CODEWORD_BYTES;
    this.repaired = 0;
  }
}

/**
 * The samples folded onto one cycle of the frame at half-bit `halfBit`:
 * BINS_PER_HALF_BIT bins per half-bit, each the sum of the deviations that
 * fall in it, from every cycle. Returned as prefix sums over two cycles, so
 * any run of bins is two lookups.
 */
function fold(samples: Deviation[], origin: number, halfBit: number): Float64Array {
  const bins = new Float64Array(FOLD_BINS);
  for (const sample of samples) {
    let position = (sample.t - origin) / halfBit;
    position -= Math.floor(position / FRAME_T) * FRAME_T;
    bins[Math.min(FOLD_BINS - 1, Math.floor(position * BINS_PER_HALF_BIT))] += sample.d;
  }
  const prefix = new Float64Array(2 * FOLD_BINS + 1);
  for (let i = 0; i < 2 * FOLD_BINS; i += 1) prefix[i + 1] = prefix[i] + bins[i % FOLD_BINS];
  return prefix;
}

/**
 * How well the folded samples fit the frame starting at bin `start`: the
 * known half-bits' agreement plus each data cell's decisiveness. Each half-bit
 * leaves out its edge bins, where bloom and blur put light that belongs to
 * the neighbour.
 */
function foldScore(prefix: Float64Array, start: number): number {
  const half = (h: number): number => {
    const from = start + h * BINS_PER_HALF_BIT + EDGE_BINS;
    return prefix[from + BINS_PER_HALF_BIT - 2 * EDGE_BINS] - prefix[from];
  };
  let score = 0;
  for (let h = 0; h < FRAME_T; h += 1) if (TEMPLATE[h] !== 0) score += TEMPLATE[h] * half(h);
  for (let k = 1; k < FRAME_BITS; k += 1) {
    const h = FIRST_CELL_H + 2 * k;
    score += Math.abs(half(h) - half(h + 1));
  }
  return score;
}

/**
 * The alignments the samples fit best, best first: every start at a spread
 * of clock rates, then a finer rate and start around the leaders.
 */
function bestAlignments(samples: Deviation[]): Alignment[] {
  if (samples.length === 0) return [];
  const origin = samples[0].t;
  const coarse: Alignment[] = [];
  for (const rate of COARSE_RATES) {
    const halfBit = T_MS * rate;
    const prefix = fold(samples, origin, halfBit);
    for (let start = 0; start < FOLD_BINS; start += 2) {
      coarse.push({ start: origin + (start / BINS_PER_HALF_BIT) * halfBit, halfBit, score: foldScore(prefix, start) });
    }
  }
  coarse.sort((a, b) => b.score - a.score);

  // Leaders a few half-bits apart, not the neighbours of one peak.
  const leaders: Alignment[] = [];
  for (const candidate of coarse) {
    if (leaders.length >= REFINE_TOP) break;
    if (leaders.some((leader) => cyclicGap(leader.start, candidate.start, FRAME_T * candidate.halfBit) < 3 * candidate.halfBit)) continue;
    leaders.push(candidate);
  }

  const refined: Alignment[] = [];
  for (const lead of leaders) {
    let best = lead;
    for (let rate = -FINE_RATE_SPAN; rate <= FINE_RATE_SPAN + 1e-9; rate += FINE_RATE_STEP) {
      const halfBit = lead.halfBit * (1 + rate);
      if (Math.abs(halfBit / T_MS - 1) > PERIOD_TOLERANCE + 1e-9) continue;
      // The lead's start, carried to this rate at the window's middle, where a rate change moves it least.
      const middle = (samples[0].t + samples[samples.length - 1].t) / 2;
      const cycles = Math.round((middle - lead.start) / (FRAME_T * lead.halfBit));
      const anchored = middle - (middle - lead.start - cycles * FRAME_T * lead.halfBit) * (halfBit / lead.halfBit) - cycles * FRAME_T * halfBit;
      const prefix = fold(samples, anchored, halfBit);
      for (let shift = -FINE_SHIFT_BINS; shift <= FINE_SHIFT_BINS; shift += 1) {
        const bin = ((shift % FOLD_BINS) + FOLD_BINS) % FOLD_BINS;
        const score = foldScore(prefix, bin);
        if (score > best.score) best = { start: anchored + (shift / BINS_PER_HALF_BIT) * halfBit, halfBit, score };
      }
    }
    refined.push(best);
  }
  refined.sort((a, b) => b.score - a.score);
  return refined;
}

/** The distance between two times on a cycle of `period`. */
function cyclicGap(a: number, b: number, period: number): number {
  const d = (((a - b) % period) + period) % period;
  return Math.min(d, period - d);
}

/** Each data cell's evidence for a `1` at `alignment`, every cycle's samples together. A cell is covered when both its halves have a sample. */
function cellScores(samples: Deviation[], alignment: Alignment): Cells {
  const scores = new Float64Array(FRAME_BITS);
  const halves = new Uint8Array(FRAME_BITS);
  for (const sample of samples) {
    let position = (sample.t - alignment.start) / alignment.halfBit;
    position -= Math.floor(position / FRAME_T) * FRAME_T;
    const h = Math.floor(position);
    if (h < FIRST_CELL_H || h >= FIRST_CELL_H + 2 * FRAME_BITS) continue;
    const cell = Math.floor((h - FIRST_CELL_H) / 2);
    const firstHalf = (h - FIRST_CELL_H) % 2 === 0;
    const weight = Math.sin(Math.PI * (position - h));
    scores[cell] += firstHalf ? sample.d * weight : -sample.d * weight;
    halves[cell] |= firstHalf ? 1 : 2;
  }
  const covered = new Uint8Array(FRAME_BITS);
  let count = 0;
  for (let k = 0; k < FRAME_BITS; k += 1) {
    covered[k] = halves[k] === 3 ? 1 : 0;
    count += covered[k];
  }
  return { scores, covered, count };
}

/** Scores divided by the typical covered score, so the erasure threshold means the same at any contrast. */
function normalise(cells: Cells): Cells {
  const sizes: number[] = [];
  for (let k = 0; k < FRAME_BITS; k += 1) if (cells.covered[k]) sizes.push(Math.abs(cells.scores[k]));
  if (sizes.length === 0) return cells;
  sizes.sort((a, b) => a - b);
  const typical = sizes[Math.floor(sizes.length / 2)];
  if (typical <= 0) return { scores: new Float64Array(FRAME_BITS), covered: new Uint8Array(FRAME_BITS), count: 0 };
  const scores = new Float64Array(FRAME_BITS);
  for (let k = 0; k < FRAME_BITS; k += 1) scores[k] = cells.covered[k] ? cells.scores[k] / typical : 0;
  return { scores, covered: cells.covered, count: cells.count };
}

/** The cells read as a codeword: a byte with a cell unseen or undecided is an erasure. */
/**
 * Whether the recent runs are this protocol's: at least LED_MIN_RUNS of 1 to
 * 5 half-bits (with a camera frame's slack either way), none and no current
 * run held much longer than the protocol ever holds.
 */
function blinksLikeTheProtocol(runs: { end: number; length: number }[], currentRun: number): boolean {
  if (currentRun > LED_LONGEST_RUN_MS) return false;
  let plausible = 0;
  for (const run of runs) {
    if (run.length > LED_LONGEST_RUN_MS) return false;
    if (run.length >= LED_SHORTEST_RUN_MS) plausible += 1;
  }
  return plausible >= LED_MIN_RUNS;
}

/**
 * Whether the last LED_WINDOW_MS of samples sit at two levels, as an LED
 * switching on and off does (only the frames that catch an edge fall
 * between), rather than spread across the range, as a dark camera's noise
 * does.
 */
function isTwoLevel(samples: Deviation[], now: number, range: number): boolean {
  let total = 0;
  let middling = 0;
  for (let i = samples.length - 1; i >= 0 && samples[i].t >= now - LED_WINDOW_MS; i -= 1) {
    total += 1;
    if (Math.abs(samples[i].d) < range / 4) middling += 1;
  }
  return total > 0 && middling / total <= LED_MAX_MIDDLING;
}

function decodeCells(cells: Cells): { ok: boolean; idBytes: Uint8Array; errors: number; erasures: number; sure: boolean[] } {
  const sure = new Array<boolean>(LED_CODEWORD_BYTES).fill(true);
  const bytes = new Uint8Array(LED_CODEWORD_BYTES);
  const erased: number[] = [];
  for (let j = 0; j < LED_CODEWORD_BYTES; j += 1) {
    let byte = 0;
    let doubtful = false;
    for (let b = 0; b < 8; b += 1) {
      const k = 1 + 8 * j + b;
      if (!cells.covered[k] || Math.abs(cells.scores[k]) < ERASE_BELOW) doubtful = true;
      byte = (byte << 1) | (cells.scores[k] > 0 ? 1 : 0);
    }
    bytes[j] = byte;
    if (doubtful) {
      erased.push(j);
      sure[j] = false;
    }
  }
  if (erased.length > LED_PARITY_BYTES) return { ok: false, idBytes: new Uint8Array(), errors: 0, erasures: erased.length, sure };
  const read = readCodeword(bytes, erased);
  if (!read.ok) return { ok: false, idBytes: new Uint8Array(), errors: 0, erasures: erased.length, sure };
  return { ok: true, idBytes: read.idBytes, errors: read.errors, erasures: read.erasures, sure };
}
