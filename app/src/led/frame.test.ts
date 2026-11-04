import { describe, expect, it } from 'vitest';
import { FRAME_BITS, FRAME_T, T_MS } from '@/protocol/constants';
import { bytesToHex, hexToBytes } from '@/protocol/bytes';
import { bytesToId, frameBits, frameLevels, frameRuns, idToBytes, levelAt } from './frame';

const ID = hexToBytes('0123456789abcdef');
const CODEWORD = '52050123456789abcdef07a62dbecfdf83914ddbbd24721a';

const runText = (runs: { on: boolean; t: number }[]) => runs.map((run) => `${run.on ? '+' : '-'}${run.t}`).join(',');

describe('frame, version 5', () => {
  it('is 398 half-bits, 19.9 seconds', () => {
    expect(FRAME_T).toBe(398);
    expect(FRAME_T * T_MS).toBe(19_900);
    expect(FRAME_BITS).toBe(193);
    expect(frameLevels(ID)).toHaveLength(398);
  });

  it('starts with a 1 and carries the codeword, each byte most significant bit first', () => {
    const bits = frameBits(ID);
    expect(bits).toHaveLength(193);
    expect(bits[0]).toBe(1);
    const bytes = new Uint8Array(24);
    for (let i = 0; i < 192; i += 1) bytes[i >> 3] = (bytes[i >> 3] << 1) | bits[1 + i];
    expect(bytesToHex(bytes)).toBe(CODEWORD);
  });

  // The vector docs/protocol.md gives: 398 T in 294 runs, physical runs
  // (adjacent equal half-bits merged), starting with the sync.
  it('gives the runs of the vectors for 0x0123456789abcdef', () => {
    const runs = frameRuns(ID);
    expect(runs).toHaveLength(294);
    expect(runs.reduce((n, run) => n + run.t, 0)).toBe(398);
    expect(runText(runs.slice(0, 8))).toBe('+4,-4,+1,-2,+2,-2,+2,-2');
    expect(runText(runs.slice(-4))).toBe('+2,-2,+1,-4');
    expect(runs.every((run, i) => i === 0 || run.on !== runs[i - 1].on)).toBe(true);
    // Within data nothing is longer than 2T; only the sync and the idle (with
    // a last bit's off half merged into it) are.
    expect(runs.slice(2, -1).every((run) => run.t <= 2)).toBe(true);
  });

  it('repeats the frame back to back for the virtual LED', () => {
    const levels = frameLevels(ID);
    expect(levelAt(levels, 0)).toBe(true);
    expect(levelAt(levels, 4 * T_MS - 1)).toBe(true);
    expect(levelAt(levels, 4 * T_MS)).toBe(false);
    expect(levelAt(levels, FRAME_T * T_MS)).toBe(true);
    expect(levelAt(levels, FRAME_T * T_MS - 1)).toBe(false);
  });

  it('converts between the ID and its big-endian bytes', () => {
    expect(bytesToId(ID)).toBe(0x0123456789abcdefn);
    expect(idToBytes(0x0123456789abcdefn)).toEqual(ID);
  });
});
