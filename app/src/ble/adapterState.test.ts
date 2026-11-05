import { describe, expect, it } from 'vitest';
import { adapterMessage, isFinalAdapterState } from './adapterState';

describe('adapterMessage', () => {
  it('only says Bluetooth is off when it is off', () => {
    expect(adapterMessage('PoweredOff')).toMatch(/is off/);
    for (const state of ['Unauthorized', 'Unsupported', 'Resetting', 'Unknown']) expect(adapterMessage(state)).not.toMatch(/is off/);
  });

  it('does not wait for what cannot change', () => {
    expect(isFinalAdapterState('Unauthorized')).toBe(true);
    expect(isFinalAdapterState('Unsupported')).toBe(true);
    expect(isFinalAdapterState('PoweredOff')).toBe(false);
  });
});
