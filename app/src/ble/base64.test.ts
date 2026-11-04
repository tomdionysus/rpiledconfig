import { describe, expect, it } from 'vitest';
import { base64Decode, base64Encode } from './base64';

describe('base64', () => {
  it('matches the standard alphabet and padding', () => {
    expect(base64Encode(new Uint8Array([]))).toBe('');
    expect(base64Encode(new Uint8Array([0x66]))).toBe('Zg==');
    expect(base64Encode(new Uint8Array([0x66, 0x6f]))).toBe('Zm8=');
    expect(base64Encode(new Uint8Array([0x66, 0x6f, 0x6f]))).toBe('Zm9v');
    expect(base64Encode(new Uint8Array([0xff, 0xfe, 0xfd, 0x00]))).toBe('//79AA==');
  });

  it('round-trips every byte value', () => {
    const all = Uint8Array.from({ length: 256 }, (_, i) => i);
    expect(base64Decode(base64Encode(all))).toEqual(all);
    for (let length = 0; length < 8; length += 1) {
      const part = all.subarray(0, length);
      expect(base64Decode(base64Encode(part))).toEqual(part);
    }
  });

  it('ignores whitespace and padding on the way in', () => {
    expect(base64Decode('Zm9v\nYmFy')).toEqual(new Uint8Array([0x66, 0x6f, 0x6f, 0x62, 0x61, 0x72]));
    expect(base64Decode('Zg')).toEqual(new Uint8Array([0x66]));
  });
});
