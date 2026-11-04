#!/usr/bin/env node
/**
 * Writes the synthetic recordings in src/led/fixtures/: LED sample streams
 * made by the app's own encoder (src/led/synth.ts), in the same format a
 * phone saves, so the fixture loader and test run before any phone has been
 * pointed at a Pi. Each file says it is synthetic in `synthetic` and in its
 * name. They prove the plumbing, not the decoder: real recordings do that.
 *
 *   npm run fixtures:synthetic
 *
 * Deterministic: the same encoder gives the same files.
 */
import { register } from 'node:module';
import { writeFileSync } from 'node:fs';

register('./ts-hooks.mjs', import.meta.url);

const { synthesize } = await import('../src/led/synth.ts');
const { makeRecording, replay, serialiseRecording } = await import('../src/led/recording.ts');
const { hexToBytes } = await import('../src/protocol/bytes.ts');

const pkg = JSON.parse((await import('node:fs')).readFileSync(new URL('../package.json', import.meta.url), 'utf8'));
const FIXTURES = new URL('../src/led/fixtures/', import.meta.url);
const GENERATOR = 'scripts/make-synthetic-recordings.mjs';
// A fixed date, so regenerating changes nothing unless the encoder does.
const CREATED = new Date('2026-11-03T12:00:00Z');

const cases = [
  {
    file: 'synthetic-v5-24fps-clock-slow-5pct.json',
    idHex: '0123456789abcdef',
    description: 'Frame version 5. 24 fps, the LED clock 5% slow, 8 units of noise, 4 ms of frame jitter, starting 3 s into a frame.',
    options: { fps: 24, seconds: 30, clockError: 0.05, noise: 8, jitterMs: 4, startMs: 3000, seed: 24 },
    withinMs: 22_500,
  },
  {
    file: 'synthetic-v5-30fps-brightness-drift.json',
    idHex: 'fedcba9876543210',
    description: 'Frame version 5. 30 fps, low contrast (60 to 110) with a 40-unit brightness drift over 9 s, 6 units of noise, 3 ms of jitter.',
    options: { fps: 30, seconds: 30, off: 60, on: 110, drift: { amplitude: 40, periodS: 9 }, noise: 6, jitterMs: 3, startMs: 5500, seed: 30 },
    withinMs: 21_500,
  },
  {
    file: 'synthetic-v5-60fps-noisy.json',
    idHex: '5a17c3e2b9d04f86',
    description: 'Frame version 5. 60 fps, 30 units of noise against a 130-unit swing, the LED clock 3% fast, 2 ms of jitter.',
    options: { fps: 60, seconds: 30, off: 35, on: 165, noise: 30, clockError: -0.03, jitterMs: 2, startMs: 1200, seed: 60 },
    withinMs: 21_500,
  },
  {
    file: 'synthetic-v5-30fps-hand-over-led.json',
    idHex: '0123456789abcdef',
    description: 'Frame version 5. 30 fps, 6 units of noise, a hand over the LED for 1.5 s mid-frame (4 to 5.5 s): must still read in one frame.',
    options: { fps: 30, seconds: 30, noise: 6, jitterMs: 3, covers: [{ fromMs: 4000, durationMs: 1500 }], seed: 15 },
    withinMs: 20_900,
  },
  {
    file: 'synthetic-v5-30fps-phone-camera.json',
    idHex: '64958d1c7ab3e205',
    description:
      'Frame version 5. Calibrated to a Blackview A85 on a Pi 5 LED at 30 fps and T = 50 ms: a 33 ms exposure with the LED saturating at a third of it (bloom: on runs long, off runs short, 15% of samples disagreeing with the LED at the best alignment, all at edges, against 11% measured), pictures up to 4 ms off their timestamps, plus a lit room (20 to 69, the LED 70% inside the square) with 12 units of noise. Must read within two frames.',
    options: { fps: 30, seconds: 50, startMs: 4000, exposureMs: 33, saturation: 3, contentJitterMs: 8, jitterMs: 3, off: 20, on: 69, noise: 12, seed: 85 },
    withinMs: 39_800,
  },
  {
    file: 'synthetic-v5-30fps-no-led.json',
    idHex: null,
    description: '30 fps of a steady scene with noise and slow drift and no LED in it: must yield no ID.',
    options: { fps: 30, seconds: 12, darkMs: 1e9, off: 90, noise: 6, drift: { amplitude: 3, periodS: 5 }, seed: 31 },
  },
];

for (const { file, idHex, description, options, withinMs } of cases) {
  const samples = synthesize(hexToBytes(idHex ?? '0000000000000000'), options);
  const draft = makeRecording(samples, { appVersion: pkg.version, platform: 'synthetic', fps: options.fps, createdAt: CREATED });
  const { final, readAtMs } = replay(draft);
  const recording = makeRecording(samples, {
    appVersion: pkg.version,
    platform: 'synthetic',
    fps: options.fps,
    createdAt: CREATED,
    decoded: final.result ? { idHex: final.result.idHex, framesCombined: final.result.framesCombined } : null,
    expected: idHex === null ? { idHex: null } : { idHex, withinMs },
    synthetic: { generator: GENERATOR, description: `SYNTHETIC, made by the encoder, not a camera. ${description}` },
  });
  writeFileSync(new URL(file, FIXTURES), serialiseRecording(recording));
  console.log(`${file}: ${samples.length} samples, ${recording.decoded ? `read ${recording.decoded.idHex} at ${(readAtMs / 1000).toFixed(1)} s from ${recording.decoded.framesCombined} stretches` : "read nothing"}`);
}
