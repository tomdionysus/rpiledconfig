// The LED frame encoder: what the daemon blinks for a machine ID. The app
// never blinks a real LED; this synthesizes samples for the decoder's tests.
import { FRAME_T, ID_BYTES, IDLE_T, SYNC_OFF_T, SYNC_ON_T, T_MS } from '@/protocol/constants';
import { ledCodeword } from './codeword';

/** One run of the LED held in one state, in units of T. */
export interface Run {
  on: boolean;
  t: number;
}

/** The bits after the sync: the start bit, then the 24-byte codeword, each byte most significant bit first. */
export function frameBits(idBytes: Uint8Array): number[] {
  const bits: number[] = [1];
  for (const byte of ledCodeword(idBytes)) {
    for (let bit = 7; bit >= 0; bit -= 1) bits.push((byte >> bit) & 1);
  }
  return bits;
}

/**
 * The LED level for every T of one frame, sync first. Manchester: a `1` is on
 * then off, a `0` is off then on.
 */
export function frameLevels(idBytes: Uint8Array): boolean[] {
  const levels: boolean[] = [];
  for (let i = 0; i < SYNC_ON_T; i += 1) levels.push(true);
  for (let i = 0; i < SYNC_OFF_T; i += 1) levels.push(false);
  for (const bit of frameBits(idBytes)) levels.push(bit === 1, bit !== 1);
  for (let i = 0; i < IDLE_T; i += 1) levels.push(false);
  if (levels.length !== FRAME_T) throw new Error('frame length does not match the protocol');
  return levels;
}

/** The frame as alternating runs, adjacent equal levels merged, starting with the sync's on. */
export function frameRuns(idBytes: Uint8Array): Run[] {
  const runs: Run[] = [];
  for (const on of frameLevels(idBytes)) {
    const last = runs[runs.length - 1];
    if (last && last.on === on) last.t += 1;
    else runs.push({ on, t: 1 });
  }
  return runs;
}

/** Whether the LED is on at `tMs` after the start of frame 0, frames repeating back to back. */
export function levelAt(levels: readonly boolean[], tMs: number): boolean {
  const period = levels.length * T_MS;
  const within = ((tMs % period) + period) % period;
  return levels[Math.floor(within / T_MS)];
}

export function idToBytes(id: bigint): Uint8Array {
  const bytes = new Uint8Array(ID_BYTES);
  for (let i = ID_BYTES - 1; i >= 0; i -= 1) {
    bytes[i] = Number(id & 0xffn);
    id >>= 8n;
  }
  return bytes;
}

export function bytesToId(bytes: Uint8Array): bigint {
  let id = 0n;
  for (const byte of bytes) id = (id << 8n) | BigInt(byte);
  return id;
}
