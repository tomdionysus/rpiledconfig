import { describe, expect, it } from 'vitest';
import { FLAG_LAST, MAX_MESSAGE_LENGTH } from './constants';
import { fragment, FragmentError, fragmentSizeForMtu, Reassembler } from './fragments';

function bytes(length: number): Uint8Array {
  return Uint8Array.from({ length }, (_, i) => i & 0xff);
}

describe('fragments', () => {
  it('derives the fragment size from the MTU and never goes below 20', () => {
    expect(fragmentSizeForMtu(23)).toBe(20);
    expect(fragmentSizeForMtu(185)).toBe(182);
    expect(fragmentSizeForMtu(517)).toBe(514);
    expect(fragmentSizeForMtu(10)).toBe(20);
  });

  it('puts a flags byte first and sets bit 0 only on the last', () => {
    const pieces = fragment(bytes(40), 20);
    expect(pieces.map((p) => p.length)).toEqual([20, 20, 3]);
    expect(pieces.map((p) => p[0])).toEqual([0, 0, FLAG_LAST]);
    expect(pieces[2]).toEqual(new Uint8Array([FLAG_LAST, 38, 39]));
  });

  it('sends an exact fit as one fragment and an empty message as flags alone', () => {
    expect(fragment(bytes(19), 20)).toEqual([Uint8Array.from([FLAG_LAST, ...bytes(19)])]);
    expect(fragment(new Uint8Array(), 20)).toEqual([new Uint8Array([FLAG_LAST])]);
  });

  it('wants room for the flags byte and at least one byte of data', () => {
    expect(() => fragment(bytes(3), 1)).toThrow(/room/);
    expect(fragment(bytes(2), 2)).toEqual([new Uint8Array([0, 0]), new Uint8Array([FLAG_LAST, 1])]);
  });

  it('reassembles in order across messages', () => {
    const reassembler = new Reassembler();
    const one = bytes(50);
    const two = bytes(7);
    const pieces = [...fragment(one, 20), ...fragment(two, 20)];
    const out: Uint8Array[] = [];
    for (const piece of pieces) {
      const message = reassembler.push(piece);
      if (message) out.push(message);
    }
    expect(out).toEqual([one, two]);
  });

  it('refuses unknown flag bits', () => {
    const reassembler = new Reassembler();
    expect(() => reassembler.push(new Uint8Array([0x02, 1, 2]))).toThrow(FragmentError);
    expect(() => reassembler.push(new Uint8Array([0x81, 1, 2]))).toThrow(/flags/);
    expect(() => reassembler.push(new Uint8Array([]))).toThrow(/empty/);
  });

  it('refuses a message past 65536 bytes', () => {
    expect(() => fragment(new Uint8Array(MAX_MESSAGE_LENGTH + 1), 20)).toThrow(/long/);
    const reassembler = new Reassembler();
    const big = new Uint8Array(1 + 65536);
    big[0] = FLAG_LAST;
    expect(reassembler.push(big)).toHaveLength(65536);
    const tooBig = new Uint8Array(1 + 65537);
    tooBig[0] = FLAG_LAST;
    expect(() => reassembler.push(tooBig)).toThrow(/long/);
    // And it starts clean afterwards.
    expect(reassembler.push(new Uint8Array([FLAG_LAST, 9]))).toEqual(new Uint8Array([9]));
  });
});
