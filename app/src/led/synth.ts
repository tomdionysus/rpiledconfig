// What a camera would sample from a Pi's LED, made from the encoder: the
// decoder's tests and the synthetic recordings in fixtures/ come from here.
// It is a model, and a kind one; recordings from real phones are the test that
// matters (TODO/ACTIVE.md, milestone 4).
//
// Plain TypeScript with nothing but erasable syntax, so scripts/ can run it
// under Node's type stripping.
import { T_MS } from '@/protocol/constants';
import type { Sample } from './decoder';
import { frameLevels, levelAt } from './frame';

export interface SynthOptions {
  fps: number;
  /** Seconds of signal. */
  seconds: number;
  /** The LED's clock error: 0.05 runs it 5% slow. */
  clockError?: number;
  /** Peak-to-peak noise on each sample, in luminance units. */
  noise?: number;
  /** Luminance of off and on. */
  off?: number;
  on?: number;
  /** A slow sine added to every sample: amplitude and period in seconds. */
  drift?: { amplitude: number; periodS: number };
  /** Where in the frame sampling begins, in ms. */
  startMs?: number;
  /** Flip these bit cells of frame 0 (0 = the start bit) by inverting their halves. */
  corruptBits?: number[];
  /** Start sampling with this many ms of darkness. */
  darkMs?: number;
  /** Peak-to-peak jitter on each frame's timestamp, in ms, as a camera's frame pacing has. */
  jitterMs?: number;
  /** Windows of sampling time, in ms, when a hand covers the LED: it reads as off. */
  covers?: { fromMs: number; durationMs: number }[];
  /**
   * Exposure, in ms: each sample is the LED averaged over the exposure ending
   * at its time, so an edge inside it gives an in-between level. 0 samples
   * the instant.
   */
  exposureMs?: number;
  /**
   * Saturation of a bright LED: the exposed fraction of "on" is multiplied by
   * this and clipped at 1, so a partly lit exposure reads nearly fully on.
   * This is the bloom a real camera shows: on runs come out long and off runs
   * short by up to an exposure. 1 is linear.
   */
  saturation?: number;
  /**
   * Peak-to-peak error, in ms, between a sample's timestamp and when its
   * image was really taken: the timestamp is honest about pacing, but the
   * picture can be from a little earlier or later, as on the A85.
   */
  contentJitterMs?: number;
  /** Seed for the noise and jitter, so a run is reproducible. */
  seed?: number;
}

/** A deterministic generator, so a failing run is reproducible. */
export function lcg(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state / 0x100000000;
  };
}

export function synthesize(id: Uint8Array, options: SynthOptions): Sample[] {
  const {
    fps,
    seconds,
    clockError = 0,
    noise = 0,
    off = 30,
    on = 180,
    drift,
    startMs = 0,
    corruptBits = [],
    darkMs = 0,
    jitterMs = 0,
    seed = 12345,
    covers = [],
    exposureMs = 0,
    saturation = 1,
    contentJitterMs = 0,
  } = options;
  const random = lcg(seed);
  const levels = frameLevels(id);
  const period = levels.length * T_MS;
  const samples: Sample[] = [];
  const interval = 1000 / fps;

  /** Whether the LED is lit at sampling time `at`. */
  const litAt = (at: number): boolean => {
    if (at < darkMs) return false;
    if (covers.some((cover) => at >= cover.fromMs && at < cover.fromMs + cover.durationMs)) return false;
    // The LED's own clock runs (1 + clockError) times slower.
    const ledTime = (at - darkMs + startMs) / (1 + clockError);
    let level = levelAt(levels, ledTime);
    if (ledTime < period && corruptBits.length > 0) {
      const cell = Math.floor((ledTime - 8 * T_MS) / (2 * T_MS));
      if (corruptBits.includes(cell)) level = !level;
    }
    return level;
  };

  for (let nominal = 0; nominal < seconds * 1000; nominal += interval) {
    // The exposure happens when it happens; the timestamp says so.
    const t = jitterMs > 0 ? Math.max(0, nominal + (random() - 0.5) * jitterMs) : nominal;
    // ...though the picture may be from a little earlier or later than that.
    const taken = contentJitterMs > 0 ? t + (random() - 0.5) * contentJitterMs : t;
    let lit: number;
    if (exposureMs > 0) {
      const steps = 8;
      let count = 0;
      for (let i = 0; i < steps; i += 1) if (litAt(taken - exposureMs + ((i + 0.5) * exposureMs) / steps)) count += 1;
      lit = Math.min(1, (count / steps) * saturation);
    } else {
      lit = litAt(taken) ? 1 : 0;
    }
    let y = off + (on - off) * lit;
    if (noise > 0) y += (random() - 0.5) * noise;
    if (drift) y += drift.amplitude * Math.sin((2 * Math.PI * t) / (drift.periodS * 1000));
    samples.push({ t, y: Math.max(0, Math.min(255, y)) });
  }
  return samples;
}
