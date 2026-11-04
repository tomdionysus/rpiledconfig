import { describe, expect, it } from 'vitest';
import { bytesEqual, bytesToHex, bytesToUint, concat, hexToBytes, uintToBytes, utf8Decode, utf8Encode } from './bytes';

describe('bytes', () => {
  it('converts hex both ways and refuses what is not hex', () => {
    expect(bytesToHex(hexToBytes('00ff10'))).toBe('00ff10');
    expect(hexToBytes('ABCD')).toEqual(new Uint8Array([0xab, 0xcd]));
    expect(() => hexToBytes('abc')).toThrow();
    expect(() => hexToBytes('zz')).toThrow();
  });

  it('writes big-endian integers of a given width', () => {
    expect(bytesToHex(uintToBytes(1, 4))).toBe('00000001');
    expect(bytesToHex(uintToBytes(0x0102030405, 8))).toBe('0000000102030405');
    expect(bytesToUint(hexToBytes('0000000102030405'))).toBe(0x0102030405);
    expect(() => uintToBytes(256, 1)).toThrow();
    expect(() => uintToBytes(-1, 4)).toThrow();
  });

  it('concatenates and compares', () => {
    expect(concat(new Uint8Array([1]), new Uint8Array(), new Uint8Array([2, 3]))).toEqual(new Uint8Array([1, 2, 3]));
    expect(bytesEqual(new Uint8Array([1, 2]), new Uint8Array([1, 2]))).toBe(true);
    expect(bytesEqual(new Uint8Array([1, 2]), new Uint8Array([1, 2, 3]))).toBe(false);
  });

  it('handles UTF-8 strictly', () => {
    expect(utf8Decode(utf8Encode('RPiLEDConfig'))).toBe('RPiLEDConfig');
    expect(utf8Encode('é')).toEqual(new Uint8Array([0x65, 0xcc, 0x81]));
    expect(() => utf8Decode(new Uint8Array([0xc3, 0x28]))).toThrow();
  });
});
