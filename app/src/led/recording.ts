// A scan's luminance samples kept as JSON, so the decoder can be tuned
// against what phones actually see (TODO/ACTIVE.md, milestone 4). The scan
// screen records; the recordings screen saves, lists and exports; a file
// dropped into src/led/fixtures/ with `expected` filled in becomes a test.
//
// Plain TypeScript with nothing but erasable syntax, so scripts/ can run it
// under Node's type stripping.
import { ID_HEX_LENGTH } from '@/protocol/constants';
import { LedDecoder, type DecoderState, type Sample } from './decoder';

export const RECORDING_KIND = 'rpiledconfig.led-recording';
export const RECORDING_VERSION = 1;

/** Three minutes at 60 fps: longer than any scan worth keeping. */
export const MAX_RECORDED_SAMPLES = 3 * 60 * 60;

export interface CameraDetails {
  width: number;
  height: number;
  pixelFormat: string;
  /** The sampled square as a fraction of the frame's shorter side. */
  regionFraction: number;
  /** Every n-th pixel in each direction. */
  pixelStep: number;
}

export interface Recording {
  kind: typeof RECORDING_KIND;
  version: typeof RECORDING_VERSION;
  /** Present only on recordings made by a script rather than a phone. */
  synthetic?: { generator: string; description: string };
  /** ISO 8601. */
  createdAt: string;
  appVersion: string;
  /** `ios`, `android`, or `synthetic`. */
  platform: string;
  /** The frame rate asked of the camera. */
  fps: number;
  /** The frame rate the samples actually came at, from their median interval. */
  measuredFps: number;
  camera?: CameraDetails;
  /** What the app's decoder made of the scan at the time. */
  decoded: { idHex: string; framesCombined: number } | null;
  /**
   * The truth, for a fixture: the ID the Pi was blinking (`rpiledconfig
   * --show-id`), or null when the recording should yield no ID at all, and
   * optionally how soon after the first sample the read must come.
   */
  expected?: { idHex: string | null; withinMs?: number };
  note?: string;
  /** Milliseconds from the first sample. */
  t: number[];
  /** Mean luminance of the region, 0..255, one per entry of `t`. */
  y: number[];
}

const round2 = (value: number) => Math.round(value * 100) / 100;

/** Collects samples as they arrive, dropping the oldest past `limit`. */
export class SampleRecorder {
  private samples: Sample[] = [];
  private readonly limit: number;
  camera?: CameraDetails;

  constructor(limit = MAX_RECORDED_SAMPLES) {
    this.limit = limit;
  }

  push(sample: Sample): void {
    this.samples.push(sample);
    if (this.samples.length > this.limit) this.samples.splice(0, this.samples.length - this.limit);
  }

  get count(): number {
    return this.samples.length;
  }

  /** Seconds between the first and last sample. */
  get seconds(): number {
    return this.samples.length < 2 ? 0 : (this.samples[this.samples.length - 1].t - this.samples[0].t) / 1000;
  }

  clear(): void {
    this.samples = [];
  }

  snapshot(): Sample[] {
    return this.samples.slice();
  }
}

export interface RecordingMeta {
  appVersion: string;
  platform: string;
  fps: number;
  camera?: CameraDetails;
  decoded?: { idHex: string; framesCombined: number } | null;
  expected?: Recording['expected'];
  synthetic?: Recording['synthetic'];
  note?: string;
  createdAt?: Date;
}

/** The median sample rate, which a dropped frame or two does not move. */
export function measuredFps(samples: readonly Sample[]): number {
  if (samples.length < 2) return 0;
  const intervals = samples
    .slice(1)
    .map((sample, i) => sample.t - samples[i].t)
    .filter((dt) => dt > 0)
    .sort((a, b) => a - b);
  if (intervals.length === 0) return 0;
  const median = intervals[Math.floor(intervals.length / 2)];
  return round2(1000 / median);
}

export function makeRecording(samples: readonly Sample[], meta: RecordingMeta): Recording {
  const origin = samples.length > 0 ? samples[0].t : 0;
  const recording: Recording = {
    kind: RECORDING_KIND,
    version: RECORDING_VERSION,
    createdAt: (meta.createdAt ?? new Date()).toISOString(),
    appVersion: meta.appVersion,
    platform: meta.platform,
    fps: meta.fps,
    measuredFps: measuredFps(samples),
    decoded: meta.decoded ?? null,
    t: samples.map((sample) => round2(sample.t - origin)),
    y: samples.map((sample) => round2(sample.y)),
  };
  if (meta.synthetic) recording.synthetic = meta.synthetic;
  if (meta.camera) recording.camera = meta.camera;
  if (meta.expected) recording.expected = meta.expected;
  if (meta.note) recording.note = meta.note;
  return recording;
}

export class RecordingError extends Error {}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

const HEX_ID = new RegExp(`^[0-9a-f]{${ID_HEX_LENGTH}}$`);

/** Checks the shape of a parsed recording; throws `RecordingError` saying what is wrong. */
export function parseRecording(value: unknown): Recording {
  if (!isRecord(value)) throw new RecordingError('not a JSON object');
  if (value.kind !== RECORDING_KIND) throw new RecordingError(`kind is not ${RECORDING_KIND}`);
  if (value.version !== RECORDING_VERSION) throw new RecordingError(`version ${String(value.version)} is not ${RECORDING_VERSION}`);
  for (const key of ['createdAt', 'appVersion', 'platform'] as const) {
    if (typeof value[key] !== 'string') throw new RecordingError(`${key} is missing`);
  }
  if (typeof value.fps !== 'number' || typeof value.measuredFps !== 'number') throw new RecordingError('fps or measuredFps is missing');
  const { t, y } = value;
  if (!Array.isArray(t) || !Array.isArray(y) || t.length !== y.length) throw new RecordingError('t and y must be arrays of the same length');
  if (!t.every((v) => typeof v === 'number' && Number.isFinite(v)) || !y.every((v) => typeof v === 'number' && Number.isFinite(v))) {
    throw new RecordingError('t and y must hold only numbers');
  }
  for (let i = 1; i < t.length; i += 1) {
    if ((t[i] as number) < (t[i - 1] as number)) throw new RecordingError(`t goes backwards at sample ${i}`);
  }
  const decoded = value.decoded;
  if (decoded !== null && !(isRecord(decoded) && typeof decoded.idHex === 'string' && typeof decoded.framesCombined === 'number')) {
    throw new RecordingError('decoded must be null or { idHex, framesCombined }');
  }
  const expected = value.expected;
  if (expected !== undefined) {
    if (!isRecord(expected) || !(expected.idHex === null || (typeof expected.idHex === 'string' && HEX_ID.test(expected.idHex)))) {
      throw new RecordingError(`expected.idHex must be ${ID_HEX_LENGTH} lowercase hex characters, or null`);
    }
    if (expected.withinMs !== undefined && (typeof expected.withinMs !== 'number' || expected.withinMs <= 0)) {
      throw new RecordingError('expected.withinMs must be a positive number');
    }
  }
  return value as unknown as Recording;
}

export function samplesOf(recording: Recording): Sample[] {
  return recording.t.map((t, i) => ({ t, y: recording.y[i] }));
}

export interface Replay {
  final: DecoderState;
  /** Every distinct ID read, in order of first appearance. */
  ids: string[];
  /** Milliseconds from the first sample to the first read. */
  readAtMs?: number;
}

/** Runs today's decoder over a recording. */
export function replay(recording: Recording): Replay {
  const decoder = new LedDecoder();
  let final = decoder.state();
  const ids: string[] = [];
  let lastRead = 0;
  let readAtMs: number | undefined;
  const samples = samplesOf(recording);
  for (const sample of samples) {
    final = decoder.push(sample);
    if (final.framesRead !== lastRead) {
      lastRead = final.framesRead;
      const id = final.result?.idHex;
      if (id && !ids.includes(id)) ids.push(id);
      if (readAtMs === undefined) readAtMs = sample.t - samples[0].t;
    }
  }
  return { final, ids, readAtMs };
}

/** `led-<platform>-<fps>fps-<yyyymmdd-hhmmss>.json`, which sorts by time within a platform. */
export function recordingFileName(recording: Pick<Recording, 'platform' | 'fps' | 'createdAt'>): string {
  const stamp = recording.createdAt.replace(/\.\d+Z$/, '').replace(/[-:]/g, '').replace('T', '-');
  return `led-${recording.platform}-${Math.round(recording.fps)}fps-${stamp}.json`;
}

export function serialiseRecording(recording: Recording): string {
  // One line per array keeps a file readable and diffs small.
  const { t, y, ...rest } = recording;
  const head = JSON.stringify(rest, null, 2).replace(/\n}$/, '');
  return `${head},\n  "t": ${JSON.stringify(t)},\n  "y": ${JSON.stringify(y)}\n}\n`;
}
