import { describe, expect, it } from 'vitest';
import { hexToBytes } from '@/protocol/bytes';
import {
  makeRecording,
  measuredFps,
  parseRecording,
  RecordingError,
  recordingFileName,
  replay,
  SampleRecorder,
  serialiseRecording,
} from './recording';
import { synthesize } from './synth';

const ID_HEX = '0123456789abcdef';

describe('recordings', () => {
  const samples = synthesize(hexToBytes(ID_HEX), { fps: 30, seconds: 22, noise: 6, jitterMs: 3, seed: 7 }).map((s) => ({ ...s, t: s.t + 123_456.789 }));

  it('round-trips through JSON and replays to the same read', () => {
    const recording = makeRecording(samples, {
      appVersion: '0.1.0',
      platform: 'android',
      fps: 30,
      decoded: { idHex: ID_HEX, framesCombined: 1 },
      camera: { width: 1280, height: 720, pixelFormat: 'yuv', regionFraction: 0.15, pixelStep: 2 },
      createdAt: new Date('2026-11-03T14:05:09.123Z'),
    });
    expect(recording.t[0]).toBe(0);
    expect(recording.t.length).toBe(samples.length);
    expect(recording.measuredFps).toBeCloseTo(30, 0);

    const text = serialiseRecording(recording);
    const parsed = parseRecording(JSON.parse(text));
    expect(parsed).toEqual(recording);
    const { ids, readAtMs } = replay(parsed);
    expect(ids).toEqual([ID_HEX]);
    expect(readAtMs).toBeLessThan(20_000);
    expect(recordingFileName(parsed)).toBe('led-android-30fps-20261103-140509.json');
  });

  it('says what is wrong with a file that is not a recording', () => {
    const good = JSON.parse(serialiseRecording(makeRecording(samples.slice(0, 10), { appVersion: '0.1.0', platform: 'ios', fps: 30 })));
    expect(() => parseRecording([])).toThrow(RecordingError);
    expect(() => parseRecording({ ...good, kind: 'other' })).toThrow(/kind/);
    expect(() => parseRecording({ ...good, y: good.y.slice(1) })).toThrow(/same length/);
    expect(() => parseRecording({ ...good, t: [...good.t].reverse() })).toThrow(/backwards/);
    expect(() => parseRecording({ ...good, expected: { idHex: 'ABC' } })).toThrow(/16 lowercase hex/);
    expect(parseRecording({ ...good, expected: { idHex: null } }).expected).toEqual({ idHex: null });
  });

  it('measures the median frame rate, which a dropped frame does not move', () => {
    const steady = Array.from({ length: 100 }, (_, i) => ({ t: i * (1000 / 24), y: 0 }));
    const dropped = steady.filter((_, i) => i % 10 !== 5);
    expect(measuredFps(steady)).toBeCloseTo(24, 1);
    expect(measuredFps(dropped)).toBeCloseTo(24, 1);
    expect(measuredFps([])).toBe(0);
  });

  it('keeps only the newest samples past its limit', () => {
    const recorder = new SampleRecorder(5);
    for (let i = 0; i < 8; i += 1) recorder.push({ t: i * 100, y: i });
    expect(recorder.count).toBe(5);
    expect(recorder.snapshot().map((s) => s.y)).toEqual([3, 4, 5, 6, 7]);
    expect(recorder.seconds).toBeCloseTo(0.4);
    recorder.clear();
    expect(recorder.count).toBe(0);
  });
});
