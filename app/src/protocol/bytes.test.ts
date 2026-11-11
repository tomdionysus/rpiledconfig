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

  it('tells arrays of one length apart by content', () => {
    expect(bytesEqual(new Uint8Array([1, 2]), new Uint8Array([1, 3]))).toBe(false);
  });

  it('refuses an integer past what a number holds exactly', () => {
    expect(bytesToUint(hexToBytes('001fffffffffffff'))).toBe(Number.MAX_SAFE_INTEGER);
    expect(() => bytesToUint(hexToBytes('0020000000000000'))).toThrow();
  });

  // RFC 3629: one to four bytes by code point, and every other byte sequence refused.
  it('encodes and decodes each length of UTF-8', () => {
    const cases: Array<[string, number[]]> = [
      ['é', [0xc3, 0xa9]],
      ['€', [0xe2, 0x82, 0xac]],
      ['\u{1f600}', [0xf0, 0x9f, 0x98, 0x80]],
      ['\u{10ffff}', [0xf4, 0x8f, 0xbf, 0xbf]],
      ['Café €\u{1f600}', [0x43, 0x61, 0x66, 0xc3, 0xa9, 0x20, 0xe2, 0x82, 0xac, 0xf0, 0x9f, 0x98, 0x80]],
    ];
    for (const [text, bytes] of cases) {
      expect(utf8Encode(text)).toEqual(new Uint8Array(bytes));
      expect(utf8Decode(new Uint8Array(bytes))).toBe(text);
    }
  });

  it('refuses malformed UTF-8', () => {
    const malformed = [
      [0x80], // a continuation byte first
      [0xc0, 0xaf], // overlong lead bytes are never valid
      [0xf5, 0x80, 0x80, 0x80], // past U+10FFFF by its lead byte
      [0xe2, 0x82], // cut short
      [0xf0, 0x9f, 0x98], // cut short
      [0xe0, 0x80, 0xaf], // overlong three-byte form
      [0xf0, 0x80, 0x80, 0xaf], // overlong four-byte form
      [0xed, 0xa0, 0x80], // a surrogate
      [0xf4, 0x90, 0x80, 0x80], // past U+10FFFF
      [0xe2, 0x28, 0xac], // a bad continuation byte inside
    ];
    for (const bytes of malformed) expect(() => utf8Decode(new Uint8Array(bytes)), bytes.join(' ')).toThrow(/malformed UTF-8/);
  });
});
