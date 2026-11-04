import { describe, expect, it } from 'vitest';
import { hexToBytes } from '@/protocol/bytes';
import { FRAME_BITS, FRAME_T, T_MS } from '@/protocol/constants';
import { LedDecoder, type DecoderState, type Sample } from './decoder';
import { lcg, synthesize } from './synth';

const ID = hexToBytes('0123456789abcdef');
const ID_HEX = '0123456789abcdef';
const FRAME_MS = FRAME_T * T_MS;

function run(samples: Sample[]): { final: DecoderState; firstResultAt?: number; states: DecoderState[]; ids: Set<string> } {
  const decoder = new LedDecoder();
  let final = decoder.state();
  let firstResultAt: number | undefined;
  const states: DecoderState[] = [];
  const ids = new Set<string>();
  for (const sample of samples) {
    final = decoder.push(sample);
    states.push(final);
    if (final.result) ids.add(final.result.idHex);
    if (firstResultAt === undefined && final.result) firstResultAt = sample.t;
  }
  return { final, firstResultAt, states, ids };
}

describe('LedDecoder progress: only what was measured', () => {
  it('reports no LED and no progress for a black camera with noise and exposure drift', () => {
    const random = lcg(7);
    const samples: Sample[] = [];
    for (let i = 0; i < 30 * 20; i += 1) {
      const t = (i * 1000) / 30;
      samples.push({ t, y: 6 + 10 * Math.sin(t / 3000) + (random() - 0.5) * 16 });
    }
    const { states } = run(samples);
    expect(states.every((state) => !state.ledSeen)).toBe(true);
    expect(states.every((state) => state.coverage === 0)).toBe(true);
  });

  it('does not take a 1 Hz blink for the protocol', () => {
    const samples: Sample[] = [];
    for (let i = 0; i < 30 * 20; i += 1) {
      const t = (i * 1000) / 30;
      samples.push({ t, y: Math.floor(t / 500) % 2 === 0 ? 200 : 20 });
    }
    const { states } = run(samples);
    expect(states.every((state) => !state.ledSeen && state.coverage === 0)).toBe(true);
  });

  it('is 0 in the dark, only grows while the LED is seen, and is full within a frame of it appearing', () => {
    const darkMs = 3_000;
    const samples = synthesize(ID, { fps: 30, seconds: 50, darkMs, noise: 6, startMs: 4_000 });
    const decoder = new LedDecoder();
    let previous = 0;
    let fullAt: number | undefined;
    for (const sample of samples) {
      const state = decoder.push(sample);
      if (sample.t < darkMs) expect(state.coverage, `at ${sample.t} ms, LED dark`).toBe(0);
      expect(state.coverage, `at ${sample.t} ms`).toBeGreaterThanOrEqual(previous);
      previous = state.coverage;
      if (fullAt === undefined && state.coverage >= 1) fullAt = sample.t;
    }
    expect(fullAt).toBeDefined();
    expect(fullAt!).toBeLessThan(darkMs + FRAME_MS + 1_000);
  });

  it('goes back to 0 when the LED is lost', () => {
    const samples = synthesize(ID, { fps: 30, seconds: 20, noise: 6, startMs: 4_000, covers: [{ fromMs: 6_000, durationMs: 3_000 }] });
    const decoder = new LedDecoder();
    let during = -1;
    for (const sample of samples) {
      const state = decoder.push(sample);
      if (sample.t > 7_500 && sample.t < 9_000) during = Math.max(during, state.coverage);
    }
    expect(during).toBe(0);
  });
});

describe('LedDecoder, frame version 5', () => {
  it('reads a clean stream at 30 fps in one frame', () => {
    const { final, firstResultAt, ids } = run(synthesize(ID, { fps: 30, seconds: 24 }));
    expect([...ids]).toEqual([ID_HEX]);
    expect(final.result?.errors).toBe(0);
    expect(final.framesFailed).toBe(0);
    // Before the frame ends: the last bytes' erasures are filled by the code.
    expect(firstResultAt).toBeLessThan(FRAME_MS);
  });

  it('reads in about one frame from whenever the LED is first seen, using the tail before the first sync', () => {
    for (const startMs of [2_000, 5_000, 9_000]) {
      const { firstResultAt, ids, final } = run(synthesize(ID, { fps: 30, seconds: 34, startMs, noise: 6 }));
      expect([...ids], `start ${startMs}`).toEqual([ID_HEX]);
      // One frame from the first sample, give or take the bytes the code fills.
      expect(firstResultAt, `start ${startMs}`).toBeLessThan(FRAME_MS + 1_000);
      expect(final.result?.framesCombined, `start ${startMs}`).toBe(2);
    }
  });

  // What the A85 showed: the unsure count rose while a frame was read, and a read waited for
  // a second frame. A tail fitted wrong is confident and wrong; summed with the frame being
  // read it cancelled the good bits. Here every tail cell is inverted.
  it('is not held back by a wrong tail: unsure only falls, and the first whole frame reads', () => {
    const startMs = 5_000;
    const tailCells = Array.from({ length: FRAME_BITS }, (_, k) => k).filter((k) => k >= Math.floor((startMs - 8 * T_MS) / (2 * T_MS)));
    const firstSyncAt = FRAME_MS - startMs;
    const { ids, firstResultAt, states } = run(synthesize(ID, { fps: 30, seconds: 50, startMs, noise: 6, corruptBits: tailCells }));
    expect([...ids]).toEqual([ID_HEX]);
    expect(firstResultAt).toBeLessThan(firstSyncAt + FRAME_MS);

    const reading = states.filter((state) => state.phase === 'reading' && !state.result).map((state) => state.erasures);
    for (let i = 1; i < reading.length; i += 1) expect(reading[i], `unsure at step ${i}`).toBeLessThanOrEqual(reading[i - 1]);
  });

  it('reads at 24 fps with 5% clock error either way, and at 60 fps', () => {
    for (const clockError of [0.05, -0.05]) {
      const { ids, firstResultAt } = run(synthesize(ID, { fps: 24, seconds: 34, clockError, noise: 8, jitterMs: 4, startMs: 3_000 }));
      expect([...ids], `24 fps, clock error ${clockError}`).toEqual([ID_HEX]);
      expect(firstResultAt, `24 fps, clock error ${clockError}`).toBeLessThan(FRAME_MS * 1.05 + 1_500);
    }
    const fast = run(synthesize(ID, { fps: 60, seconds: 24, noise: 20, jitterMs: 2 }));
    expect([...fast.ids]).toEqual([ID_HEX]);
  });

  it('reads through a hand over the LED for 1.5 s mid-frame, in one frame', () => {
    const { ids, firstResultAt, final } = run(synthesize(ID, { fps: 30, seconds: 24, noise: 6, covers: [{ fromMs: 4_000, durationMs: 1_500 }] }));
    expect([...ids]).toEqual([ID_HEX]);
    expect(firstResultAt).toBeLessThan(FRAME_MS + 1_000);
    // 1.5 s is 15 bits: two or three bytes the code had to fill or repair.
    expect((final.result?.erasures ?? 0) + (final.result?.errors ?? 0)).toBeGreaterThanOrEqual(2);
  });

  it('repairs several wrong bytes in one frame', () => {
    // A wrong bit in each of three bytes, confidently wrong (the halves swapped).
    const corruptBits = [1 + 8 * 1 + 3, 1 + 8 * 6, 1 + 8 * 12 + 6];
    const { ids, final } = run(synthesize(ID, { fps: 30, seconds: 24, corruptBits }));
    expect([...ids]).toEqual([ID_HEX]);
    // Read before the frame ends: the bytes still to come were erasures then.
    expect(final.result?.errors).toBeGreaterThanOrEqual(2);
    expect((final.result?.errors ?? 0) + (final.result?.erasures ?? 0)).toBeGreaterThanOrEqual(3);
  });

  it('gets past a frame too damaged to read, and never reads a wrong ID', () => {
    // Twenty bad bits across the first ten bytes of frame 0: beyond the code.
    const random = lcg(20);
    const corruptBits = Array.from({ length: 20 }, () => 1 + Math.floor(random() * 80));
    const { ids, final } = run(synthesize(ID, { fps: 30, seconds: 50, corruptBits, noise: 4 }));
    expect([...ids]).toEqual([ID_HEX]);
    // Frame 0 failed; the next frame, alone or added to it, is what read.
    expect(final.framesFailed).toBeGreaterThanOrEqual(1);
  });

  // Calibrated to a Blackview A85 on a Pi 5's LED at 30 fps and T = 50 ms
  // (correct polarity): a saturated LED blooming over a 33 ms exposure, which
  // left 11% of samples disagreeing with the LED, all at edges, and no bit
  // errors in a whole frame; this model disagrees on 15%, a little harsher. Added here: a lit and noisy room, the LED only 70%
  // inside the sampled square, and a little timing error on the pictures.
  it('reads what a real phone camera sees: bloom, edge blur, a bright noisy room, the LED off centre', () => {
    for (const seed of [1, 2, 3, 4, 5]) {
      const { ids, firstResultAt } = run(
        synthesize(ID, { fps: 30, seconds: 50, seed, startMs: 4_000, exposureMs: 33, saturation: 3, contentJitterMs: 8, jitterMs: 3, off: 20, on: 69, noise: 12 }),
      );
      expect([...ids], `seed ${seed}`).toEqual([ID_HEX]);
      expect(firstResultAt, `seed ${seed}`).toBeLessThan(2 * FRAME_MS);
    }
  });

  it('stays dark on a steady scene', () => {
    const { final } = run(synthesize(ID, { fps: 30, seconds: 5, darkMs: 1e9, noise: 2 }));
    expect(final.phase).toBe('dark');
    expect(final.syncFound).toBe(false);
    expect(final.result).toBeUndefined();
  });

  it('reads nothing from a light that is not this protocol', () => {
    // Random on and off every T, which makes sync-like runs and Manchester-like
    // data: whatever it decodes to, magic, version and CRC refuse it.
    const random = lcg(77);
    const levels = Array.from({ length: 4000 }, () => random() < 0.5);
    const samples: Sample[] = [];
    for (let t = 0; t < 250_000; t += 1000 / 30) samples.push({ t, y: levels[Math.floor(t / T_MS) % levels.length] ? 180 : 30 });
    const { ids } = run(samples);
    expect([...ids]).toEqual([]);
    // A plain 1 Hz blink (5T on, 5T off) has syncs and nothing else.
    const blink: Sample[] = [];
    for (let t = 0; t < 100_000; t += 1000 / 30) blink.push({ t, y: Math.floor(t / 500) % 2 === 0 ? 180 : 30 });
    expect([...run(blink).ids]).toEqual([]);
  });

});
