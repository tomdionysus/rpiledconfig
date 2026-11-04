// The LED decoder: luminance samples with timestamps in, a machine ID out.
// Pure logic, fed one sample at a time by the camera's frame processor (or a
// test's synthesizer), following docs/protocol.md (The LED channel,
// Decoding), frame version 5.
//
// Levels: an adaptive threshold (running minimum and maximum decaying over
// about 2 s, "dark" below a minimum contrast) turns each sample into a signed
// deviation, on positive. Whether the region holds the LED at all is judged
// over the last second from run lengths and how clearly the samples sit at
// two levels, and is what the progress shown waits for.
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
//
// Cost: this runs on the JS thread, about once a second, and Hermes
// interprets. Scoring every start is a few lookups into running totals
// (cumulative, alternateDecisiveness), not a pass over the frame, and once
// the sync has been placed with confidence the clock rate is searched only
// near it. On a Mac under V8 a try costs 5 to 10 ms, against 80 to 150 ms
// before; the cost on a phone is logged in a development build.
import { FRAME_BITS, FRAME_T, IDLE_T, LED_CODEWORD_BYTES, LED_PARITY_BYTES, SYNC_OFF_T, SYNC_ON_T, T_MS } from '@/protocol/constants';
import { readCodeword } from './codeword';

export interface Sample {
  /** Milliseconds, any origin, monotonic. */
  t: number;
  /** Brightness of the LED region, 0..255. */
  y: number;
}

export type DecoderPhase = 'dark' | 'seeking' | 'reading';

export interface DecoderOptions {
  /**
   * At most this many milliseconds of matching per pushed sample: a try is
   * then spread over as many camera frames as it needs, so the thread that
   * pushes is never held for long. Unset, each try runs to its end inside the
   * push that starts it, which keeps tests and replays deterministic.
   */
  sliceMs?: number;
  /** The clock the slices are measured on; Date.now by default. */
  now?: () => number;
}

/** A computation done a slice at a time: it yields where it may be paused. */
type Steps<T> = Generator<void, T, void>;

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
/**
 * The coarse search's clock rates, 0.5% apart across the tolerance: at most
 * 0.25% off, two syncs a frame apart fold within 1T of each other, inside
 * the sync's 4T runs.
 */
const COARSE_RATES = Array.from({ length: 21 }, (_, i) => 1 - PERIOD_TOLERANCE + i * 0.005);
/** The coarse search tries half-bits starting at every this many bins (20 ms), within the fine search's shifts. */
const COARSE_OFFSET_STEP = 4;
/** Coarse alignments refined, and refined alignments read out. */
const REFINE_TOP = 4;
const READ_TOP = 3;
/**
 * The fine search around a coarse alignment: rate either way in
 * FINE_RATE_STEPS steps and then half a step around the best, and start
 * FINE_SHIFT_BINS bins either way in steps of FINE_SHIFT_STEP (10 ms, a third
 * of a camera frame).
 */
const FINE_RATE_SPAN = 0.006;
const FINE_RATE_STEPS = 3;
const FINE_SHIFT_BINS = 6;
const FINE_SHIFT_STEP = 2;
/**
 * Once the best alignment's known half-bits agree this well, the sync has
 * been seen and the clock rate is known to the coarse step: later tries
 * search only within RATE_LOCK_SPAN of it.
 */
const RATE_LOCK_AGREEMENT = 0.9;
const RATE_LOCK_SPAN = 0.01;
/**
 * Telling the protocol's blinking from anything else, over the last
 * LED_WINDOW_MS: runs of 1T to 5T (50 to 250 ms) with a camera frame's slack
 * either way, at least LED_MIN_RUNS of them, and none longer than about 6T.
 */
const LED_WINDOW_MS = 1000;
const LED_SHORTEST_RUN_MS = 30;
const LED_LONGEST_RUN_MS = 6 * T_MS + 40;
const LED_MIN_RUNS = 4;
/**
 * Two measures of whether a window's brightnesses sit at two levels (see
 * levelSeparation and nearExtremes), with bars to find the LED and lower
 * ones to keep it. Measured over 1 s windows. A Blackview A85 on a Pi 5's
 * LED: separation median 14, lowest 2.05; near the extremes median 0.65 to
 * 0.74, lowest 0.45 (a third of its frames straddle an edge, at 30 fps and
 * T = 50 ms, and fall between). Uniform and Gaussian noise: separation median
 * 2, up to 5.2; near the extremes median 0.23 to 0.35, up to 0.58. Neither
 * measure alone parts them; both to find and either to keep does: no real or
 * synthetic LED window fails to keep, and noise passes the bar to find only
 * in isolated windows, never for longer than 533 ms in 40 minutes of it,
 * hence LED_FIND_HOLD_MS.
 */
const LED_FIND_SEPARATION = 4;
const LED_FIND_NEAR_EXTREMES = 0.5;
const LED_KEEP_SEPARATION = 2.5;
const LED_KEEP_NEAR_EXTREMES = 0.45;
const LED_FIND_HOLD_MS = 800;
/** nearExtremes: the levels are the 5th and 95th percentiles, and near is within this share of the distance between them. */
const NEAR_EXTREMES_BAND = 0.12;
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
  /** For a refined alignment, how well its known half-bits agree, 0..1. */
  agreement?: number;
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
  /** The LED's clock rate, once a try has placed the sync with confidence. */
  private lockedRate?: number;
  private covered = 0;
  private runStart = 0;
  /** Recent runs of the on/off level: when each ended and how long it was, in ms. */
  private runs: { end: number; length: number }[] = [];
  /** The last LED_WINDOW_MS of brightnesses, for telling an LED from noise. */
  private recent: Sample[] = [];
  /** Since when, while not yet seen, it has looked like the LED without a break. */
  private findingSince?: number;
  private ledSeen = false;
  /** Half-bit slots of the cycle with a sample taken while the LED was seen. */
  private slots = new Uint8Array(FRAME_T);
  private slotsSeen = 0;
  private erasures = LED_CODEWORD_BYTES;
  private repaired = 0;
  private framesRead = 0;
  private framesFailed = 0;
  private result?: DecodedId;
  /** The try in progress, run a slice at a time, and the frame periods that had passed when it began. */
  private job?: Steps<boolean>;
  private jobFailedBefore = 0;
  private readonly sliceMs: number;
  private readonly now: () => number;

  constructor(options: DecoderOptions = {}) {
    this.sliceMs = options.sliceMs ?? Infinity;
    this.now = options.now ?? (() => Date.now());
  }

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
    this.recent.push({ t, y });
    while (this.recent[0].t < t - LED_WINDOW_MS) this.recent.shift();
    // Judged over a full window only: a few samples fall into plausible runs and levels by chance.
    const wasSeen = this.ledSeen;
    let blinking = t - this.firstT >= LED_WINDOW_MS && blinksLikeTheProtocol(this.runs, t - this.runStart);
    if (blinking) {
      const values = this.recent.map((sample) => sample.y);
      const separation = levelSeparation(values);
      const near = nearExtremes(values);
      blinking = wasSeen
        ? separation >= LED_KEEP_SEPARATION || near >= LED_KEEP_NEAR_EXTREMES
        : separation >= LED_FIND_SEPARATION && near >= LED_FIND_NEAR_EXTREMES;
    }
    if (wasSeen) {
      this.ledSeen = blinking;
    } else {
      // Found only once it has looked like the LED for LED_FIND_HOLD_MS on end.
      if (!blinking) this.findingSince = undefined;
      else if (this.findingSince === undefined) this.findingSince = t;
      this.ledSeen = this.findingSince !== undefined && t - this.findingSince >= LED_FIND_HOLD_MS;
    }
    if (this.ledSeen && !wasSeen) {
      // The windows that showed it is an LED were the LED: their samples are code seen.
      const from = (this.findingSince ?? t) - LED_WINDOW_MS;
      for (const earlier of this.samples) if (earlier.t >= from) this.markSlot(earlier.t);
      this.findingSince = undefined;
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
    if (!this.result) {
      if (!this.job && span >= FIRST_TRY_MS && t - this.lastTry >= TRY_EVERY_MS) {
        this.lastTry = t;
        this.phase = 'reading';
        this.jobFailedBefore = Math.floor((t - this.firstT) / FRAME_MS);
        // A copy: the samples move on while a sliced try runs.
        this.job = this.tryRead(this.samples.slice());
      }
      if (this.job) this.runJob();
    }

    return this.state();
  }

  /** Runs the try in hand for up to one slice, or to its end. */
  private runJob(): void {
    const job = this.job;
    if (!job) return;
    const until = this.now() + this.sliceMs;
    for (;;) {
      const step = job.next();
      if (step.done) {
        this.job = undefined;
        if (!step.value) this.framesFailed = Math.max(this.framesFailed, this.jobFailedBefore);
        return;
      }
      if (this.now() >= until) return;
    }
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
  private *tryRead(samples: Deviation[]): Steps<boolean> {
    // Where the frame sits is a property of the LED, not of the window, so it
    // is found once, on everything held, where the most cycles agree.
    const candidates = yield* alignmentSteps(samples, this.lockedRate);
    if (candidates.length === 0) return false;
    this.aligned = true;
    if ((candidates[0].agreement ?? 0) >= RATE_LOCK_AGREEMENT) this.lockedRate = candidates[0].halfBit / T_MS;
    const last = samples[samples.length - 1].t;
    const latest = samples.filter((sample) => sample.t >= last - FRAME_MS * (1 + PERIOD_TOLERANCE));
    const windows = latest.length < samples.length ? [latest, samples] : [samples];
    let fewest = LED_CODEWORD_BYTES;
    let mostCovered = 0;
    for (const window of windows) {
      yield;
      const read = this.readWindow(window, candidates);
      if (read.done) return true;
      fewest = Math.min(fewest, read.erasures);
      mostCovered = Math.max(mostCovered, read.covered);
    }
    this.covered = mostCovered;
    this.erasures = fewest;
    return false;
  }

  private readWindow(samples: Deviation[], candidates: Alignment[]): { done: boolean; erasures: number; covered: number } {

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
    this.lockedRate = undefined;
    this.covered = 0;
    this.runs = [];
    this.recent = [];
    this.findingSince = undefined;
    this.job = undefined;
    this.ledSeen = false;
    this.slots.fill(0);
    this.slotsSeen = 0;
    this.erasures = LED_CODEWORD_BYTES;
    this.repaired = 0;
  }
}

/**
 * The samples folded onto one cycle of the frame at half-bit `halfBit`, as
 * half-bit sums for each of `offsets`: with half-bits starting `offset`
 * tenths of a half-bit after `origin`, the sum of the deviations in each
 * half-bit, leaving out a tenth at each edge, where bloom and blur put light
 * that belongs to the neighbour. Each sum array holds the cycle twice, so a
 * run from any half-bit needs no wrapping. Written into `out`, one array per
 * offset, which must be 2 * FRAME_T long.
 *
 * Straight from the samples, with indexed loops and no allocation: this is
 * the inner loop of every try, and Hermes interprets.
 */
function foldHalves(samples: Deviation[], origin: number, halfBit: number, offsets: ArrayLike<number>, out: Float64Array[]): void {
  // Bins of a tenth of a half-bit first (one step per sample), then a running
  // total over them, so each half-bit sum is two lookups: cheaper than adding
  // every sample into every offset's sums once there are more than a few
  // hundred samples.
  const bins = FOLD_SCRATCH;
  bins.fill(0);
  const n = samples.length;
  for (let i = 0; i < n; i += 1) {
    const sample = samples[i];
    let position = (sample.t - origin) / halfBit;
    position -= Math.floor(position / FRAME_T) * FRAME_T;
    let bin = Math.floor(position * BINS_PER_HALF_BIT);
    if (bin >= FOLD_BINS) bin = FOLD_BINS - 1;
    bins[bin + 1] += sample.d;
  }
  for (let i = 1; i <= FOLD_BINS; i += 1) bins[i] += bins[i - 1];
  const inner = BINS_PER_HALF_BIT - 2 * EDGE_BINS;
  for (let k = 0; k < offsets.length; k += 1) {
    const sums = out[k];
    const offset = offsets[k];
    for (let h = 0; h < FRAME_T; h += 1) {
      const from = offset + h * BINS_PER_HALF_BIT + EDGE_BINS;
      const to = from + inner;
      // The last half-bit of a cycle whose half-bits start past bin 0 runs on into the next cycle.
      const sum = to <= FOLD_BINS ? bins[to] - bins[from] : bins[FOLD_BINS] - bins[from] + bins[to - FOLD_BINS];
      sums[h] = sum;
      sums[h + FRAME_T] = sum;
    }
  }
}

/** foldHalves' bins, as running totals: entry b is the sum of bins before b. Shared, as nothing else runs while it is used. */
const FOLD_SCRATCH = new Float64Array(FOLD_BINS + 1);

/** Fresh half-bit sum arrays for foldHalves. */
function sumArrays(count: number): Float64Array[] {
  return Array.from({ length: count }, () => new Float64Array(2 * FRAME_T));
}

/** The known half-bits (sync, start bit, idle), as offsets into the frame and signs, and as runs of one sign. */
const KNOWN_H = Int16Array.from(Array.from(TEMPLATE.keys()).filter((h) => TEMPLATE[h] !== 0));
const KNOWN_SIGN = Float64Array.from(KNOWN_H, (h) => TEMPLATE[h]);
const KNOWN_RUNS: { from: number; to: number; sign: number }[] = [];
for (let i = 0; i < KNOWN_H.length; i += 1) {
  const run = KNOWN_RUNS[KNOWN_RUNS.length - 1];
  if (run && run.to === KNOWN_H[i] && run.sign === KNOWN_SIGN[i]) run.to += 1;
  else KNOWN_RUNS.push({ from: KNOWN_H[i], to: KNOWN_H[i] + 1, sign: KNOWN_SIGN[i] });
}
const RUN_COUNT = KNOWN_RUNS.length;
const RUN_FROM = Int16Array.from(KNOWN_RUNS, (run) => run.from);
const RUN_TO = Int16Array.from(KNOWN_RUNS, (run) => run.to);
const RUN_SIGN = Float64Array.from(KNOWN_RUNS, (run) => run.sign);

/** Running totals of half-bit sums into `totals` (one longer), so a run of half-bits is two lookups. */
function cumulative(sums: Float64Array, totals: Float64Array): void {
  totals[0] = 0;
  for (let h = 0; h < sums.length; h += 1) totals[h + 1] = totals[h] + sums[h];
}

/**
 * Running totals of every other cell's decisiveness |s[h] - s[h + 1]| into
 * `alternate` (one longer): `alternate[b] - alternate[a]` is the sum over a,
 * a + 2, ..., b - 2. With these and `cumulative`, the full score of any start
 * is a handful of lookups, so every start at every coarse rate is scored in
 * full.
 */
function alternateDecisiveness(sums: Float64Array, alternate: Float64Array): void {
  alternate[0] = 0;
  alternate[1] = 0;
  for (let h = 0; h + 1 < sums.length; h += 1) alternate[h + 2] = alternate[h] + Math.abs(sums[h] - sums[h + 1]);
}

/** The first and past-the-last half-bit of the data cells' decisiveness in fullScore, at j = 0. */
const DATA_FROM_H = FIRST_CELL_H + 2;
const DATA_TO_H = FIRST_CELL_H + 2 * FRAME_BITS;

/** How well the half-bits agree with the known ones of a frame starting at half-bit `j`. */
function knownScore(sums: Float64Array, j: number): number {
  let score = 0;
  for (let i = 0; i < KNOWN_H.length; i += 1) score += KNOWN_SIGN[i] * sums[KNOWN_H[i] + j];
  return score;
}

/** The known half-bits' agreement plus each data cell's decisiveness, for a frame starting at half-bit `j`. */
function fullScore(sums: Float64Array, j: number): number {
  let score = knownScore(sums, j);
  for (let h = DATA_FROM_H + j; h < DATA_TO_H + j; h += 2) score += Math.abs(sums[h] - sums[h + 1]);
  return score;
}

/** How far the known half-bits agree with the frame starting at half-bit `j`: 1 when every one has the right sign, 0 when none does. */
function agreement(sums: Float64Array, j: number): number {
  let magnitude = 0;
  for (let i = 0; i < KNOWN_H.length; i += 1) magnitude += Math.abs(sums[KNOWN_H[i] + j]);
  return magnitude > 0 ? (knownScore(sums, j) / magnitude + 1) / 2 : 0;
}

/** The coarse search's offsets, and the fine search's shifts as offsets and starting half-bits. */
const COARSE_OFFSETS = Int32Array.from({ length: Math.ceil(BINS_PER_HALF_BIT / COARSE_OFFSET_STEP) }, (_, i) => i * COARSE_OFFSET_STEP);
const FINE_SHIFTS = Int32Array.from({ length: Math.floor((2 * FINE_SHIFT_BINS) / FINE_SHIFT_STEP) + 1 }, (_, i) => -FINE_SHIFT_BINS + i * FINE_SHIFT_STEP);
// Plain arrays in between: Hermes' typed-array from() does not take a Set.
const FINE_OFFSETS = Int32Array.from(
  Array.from(new Set(Array.from(FINE_SHIFTS, (shift) => (((shift % FOLD_BINS) + FOLD_BINS) % FOLD_BINS) % BINS_PER_HALF_BIT))),
);

/**
 * The alignments the samples fit best, best first: every start at a spread of
 * clock rates scored on the known half-bits and the data cells'
 * decisiveness, then a finer rate and start searched around the leaders.
 * Yields after each rate, where a sliced try may pause.
 */
function* alignmentSteps(samples: Deviation[], nearRate?: number): Steps<Alignment[]> {
  if (samples.length === 0) return [];
  const origin = samples[0].t;
  // The best rate and offset for each half-bit the frame could start at.
  const bestScore = new Float64Array(FRAME_T).fill(-Infinity);
  const bestHalfBit = new Float64Array(FRAME_T);
  const bestOffset = new Uint8Array(FRAME_T);
  const rates = nearRate === undefined ? COARSE_RATES : COARSE_RATES.filter((rate) => Math.abs(rate - nearRate) <= RATE_LOCK_SPAN + 1e-9);
  const halves = sumArrays(COARSE_OFFSETS.length);
  const totals = new Float64Array(2 * FRAME_T + 1);
  const alternate = new Float64Array(2 * FRAME_T + 1);
  for (const rate of rates) {
    const halfBit = T_MS * rate;
    foldHalves(samples, origin, halfBit, COARSE_OFFSETS, halves);
    for (let k = 0; k < COARSE_OFFSETS.length; k += 1) {
      const sums = halves[k];
      cumulative(sums, totals);
      alternateDecisiveness(sums, alternate);
      for (let j = 0; j < FRAME_T; j += 1) {
        let score = alternate[DATA_TO_H + j] - alternate[DATA_FROM_H + j];
        for (let r = 0; r < RUN_COUNT; r += 1) score += RUN_SIGN[r] * (totals[RUN_TO[r] + j] - totals[RUN_FROM[r] + j]);
        if (score <= bestScore[j]) continue;
        bestScore[j] = score;
        bestHalfBit[j] = halfBit;
        bestOffset[j] = COARSE_OFFSETS[k];
      }
    }
    yield;
  }
  const coarse: Alignment[] = Array.from(bestScore, (score, j) => ({
    start: origin + (bestOffset[j] / BINS_PER_HALF_BIT + j) * bestHalfBit[j],
    halfBit: bestHalfBit[j],
    score,
  })).sort((a, b) => b.score - a.score);

  // Leaders a few half-bits apart, not the neighbours of one peak.
  const leaders: Alignment[] = [];
  for (const candidate of coarse) {
    if (leaders.length >= REFINE_TOP) break;
    if (leaders.some((leader) => cyclicGap(leader.start, candidate.start, FRAME_T * candidate.halfBit) < 3 * candidate.halfBit)) continue;
    leaders.push(candidate);
  }

  // The fine search, coarse to fine in rate: FINE_RATE_STEPS across the span,
  // then half a step either way of the best.
  const middle = (samples[0].t + samples[samples.length - 1].t) / 2;
  const fine = sumArrays(FINE_OFFSETS.length);
  const refined: Alignment[] = [];
  for (const lead of leaders) {
    let best = lead;
    let bestRate = 0;
    const rateSteps: number[] = [];
    for (let i = -FINE_RATE_STEPS; i <= FINE_RATE_STEPS; i += 1) rateSteps.push((i * FINE_RATE_SPAN) / FINE_RATE_STEPS);
    for (let pass = 0; pass < 2; pass += 1) {
      for (const rate of rateSteps) {
        const found = refineAtRate(samples, lead, lead.halfBit * (1 + rate), middle, fine);
        if (found && found.score > best.score) {
          best = found;
          bestRate = rate;
        }
        yield;
      }
      const half = FINE_RATE_SPAN / FINE_RATE_STEPS / 2;
      rateSteps.length = 0;
      rateSteps.push(bestRate - half, bestRate + half);
    }
    refined.push(best);
  }
  refined.sort((a, b) => b.score - a.score);
  return refined;
}

/**
 * The best start near `lead`'s at half-bit `halfBit`, within FINE_SHIFT_BINS
 * bins either way, or undefined if the rate is outside the tolerance. The
 * lead's start is carried to the new rate at `middle`, the window's middle,
 * where a rate change moves it least. `fine` is scratch for foldHalves.
 */
function refineAtRate(samples: Deviation[], lead: Alignment, halfBit: number, middle: number, fine: Float64Array[]): Alignment | undefined {
  if (Math.abs(halfBit / T_MS - 1) > PERIOD_TOLERANCE + 1e-9) return undefined;
  const cycles = Math.round((middle - lead.start) / (FRAME_T * lead.halfBit));
  const anchored = middle - (middle - lead.start - cycles * FRAME_T * lead.halfBit) * (halfBit / lead.halfBit) - cycles * FRAME_T * halfBit;
  foldHalves(samples, anchored, halfBit, FINE_OFFSETS, fine);
  let best: Alignment | undefined;
  for (let s = 0; s < FINE_SHIFTS.length; s += 1) {
    const shift = FINE_SHIFTS[s];
    const bin = ((shift % FOLD_BINS) + FOLD_BINS) % FOLD_BINS;
    const sums = fine[FINE_OFFSETS.indexOf(bin % BINS_PER_HALF_BIT)];
    const j = Math.floor(bin / BINS_PER_HALF_BIT);
    const score = fullScore(sums, j);
    if (!best || score > best.score) best = { start: anchored + (shift / BINS_PER_HALF_BIT) * halfBit, halfBit, score, agreement: agreement(sums, j) };
  }
  return best;
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
  for (let i = 0; i < samples.length; i += 1) {
    const sample = samples[i];
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
 * How clearly the brightnesses sit at two levels, as an LED switching on and
 * off does, rather than spread across the range, as a dark camera's noise
 * does: split at the middle of the range, the distance between the two
 * halves' medians over the sum of their median absolute deviations. Medians,
 * so the few frames that catch an edge, which fall between, do not count.
 * Uniform noise gives about 2; 0 when either half has under three samples.
 */
export function levelSeparation(values: readonly number[]): number {
  let lo = Infinity;
  let hi = -Infinity;
  for (const v of values) {
    lo = Math.min(lo, v);
    hi = Math.max(hi, v);
  }
  const middle = (lo + hi) / 2;
  const low = values.filter((v) => v < middle);
  const high = values.filter((v) => v >= middle);
  if (low.length < 3 || high.length < 3) return 0;
  const lowMedian = median(low);
  const highMedian = median(high);
  const spread = median(low.map((v) => Math.abs(v - lowMedian))) + median(high.map((v) => Math.abs(v - highMedian)));
  return spread > 0 ? (highMedian - lowMedian) / spread : Infinity;
}

/**
 * The share of the brightnesses within NEAR_EXTREMES_BAND of either level, the
 * levels being the 5th and 95th percentiles: an LED piles its samples onto
 * its two levels however many frames catch an edge, where noise spreads them
 * out. 1 when every value is the same.
 */
export function nearExtremes(values: readonly number[]): number {
  if (values.length === 0) return 0;
  const sorted = values.slice().sort((a, b) => a - b);
  const last = sorted.length - 1;
  const lo = sorted[Math.floor(0.05 * last)];
  const hi = sorted[Math.floor(0.95 * last)];
  if (hi <= lo) return 1;
  const band = NEAR_EXTREMES_BAND * (hi - lo);
  let near = 0;
  for (let i = 0; i < sorted.length; i += 1) if (Math.abs(sorted[i] - lo) <= band || Math.abs(sorted[i] - hi) <= band) near += 1;
  return near / sorted.length;
}

function median(values: number[]): number {
  const sorted = values.slice().sort((a, b) => a - b);
  const mid = sorted.length >> 1;
  return sorted.length % 2 === 1 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
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
