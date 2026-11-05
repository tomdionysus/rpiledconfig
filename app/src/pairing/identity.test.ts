import { describe, expect, it } from 'vitest';
import type { StatusResponse } from '@/protocol/messages';
import { piIdentity } from './identity';

const KEY = '0123456789abcdef';
const NAME = 'RPiLEDConfig-3a9c';
const status = (hostname?: string): StatusResponse => ({ ok: true, ...(hostname ? { hostname } : {}), wifi: { connected: false } });

describe('piIdentity', () => {
  it('uses the hostname when the Pi reports one, with no notice and no key', () => {
    expect(piIdentity(status('kitchen'), KEY, NAME)).toEqual({ title: 'kitchen' });
  });

  it('shows the one-time key with a notice when the Pi does not report one', () => {
    const identity = piIdentity(status(), KEY, NAME);
    expect(identity.title).toBe(KEY);
    expect(identity.notice).toMatch(/not to report its hostname/);
    expect(identity.notice).toMatch(/one-time key/);
  });

  it('says what it shows instead if it has no key to show', () => {
    const identity = piIdentity(status(), undefined, NAME);
    expect(identity.title).toBe(NAME);
    expect(identity.notice).toMatch(/Bluetooth name/);
  });

  it('claims nothing about the hostname before the Pi has been asked', () => {
    expect(piIdentity(undefined, KEY, NAME)).toEqual({ title: NAME });
  });
});
