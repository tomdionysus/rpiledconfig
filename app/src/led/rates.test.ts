import { describe, expect, it } from 'vitest';
import { DEFAULT_RATE, frameMs, parseRate, RATE_INFO, slowerRate } from './rates';

describe('the LED rates', () => {
  it('are the protocol table', () => {
    expect(RATE_INFO.normal).toMatchObject({ blink: 0x01, symbolMs: 51 });
    expect(RATE_INFO.fast).toMatchObject({ blink: 0x02, symbolMs: 33 });
    expect(RATE_INFO.robust).toMatchObject({ blink: 0x03, symbolMs: 77 });
    expect(frameMs('normal')).toBe(8_670);
    expect(frameMs('fast')).toBe(5_610);
    expect(frameMs('robust')).toBe(13_090);
  });

  it('go slower one step at a time, and stop at robust', () => {
    expect(slowerRate('fast')).toBe('normal');
    expect(slowerRate('normal')).toBe('robust');
    expect(slowerRate('robust')).toBeUndefined();
  });

  it('read back from storage, with anything unknown the default', () => {
    expect(parseRate('robust')).toBe('robust');
    expect(parseRate(null)).toBe(DEFAULT_RATE);
    expect(parseRate('ludicrous')).toBe(DEFAULT_RATE);
  });
});
