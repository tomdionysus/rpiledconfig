// The app's own native modules (android/.../RpiPackage.kt) as the logic sees them: what is
// asked of each, and that a module missing or failing costs only what it would have given.
import { afterEach, describe, expect, it, vi } from 'vitest';
import { base64Encode } from '@/ble/base64';
import { DEFAULT_RATE } from '@/led/rates';
import { NativeModules, PermissionsAndroid, Platform } from '@/test/react-native';
import { phoneSsid } from '@/wifi/phoneWifi';
import { randomBytes } from './random';
import { loadRate, markPermissionsAsked, permissionsAsked, saveRate } from './settings';

function memorySettings() {
  const stored = new Map<string, string>();
  return {
    stored,
    get: (key: string) => stored.get(key) ?? null,
    set: (key: string, value: string) => void stored.set(key, value),
  };
}

const failing = {
  get(): string | null {
    throw new Error('SharedPreferences unavailable');
  },
  set(): void {
    throw new Error('SharedPreferences unavailable');
  },
};

afterEach(() => {
  for (const key of Object.keys(NativeModules)) delete NativeModules[key];
  Platform.OS = 'ios';
  Platform.Version = 17;
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  vi.resetModules();
});

describe('settings', () => {
  it('remembers the LED rate and that permissions were asked for', () => {
    const settings = memorySettings();
    NativeModules.RpiSettings = settings;
    expect(loadRate()).toBe(DEFAULT_RATE);
    expect(permissionsAsked()).toBe(false);
    saveRate('robust');
    markPermissionsAsked();
    expect(settings.stored).toEqual(
      new Map([
        ['ledRate', 'robust'],
        ['permissionsAsked', 'yes'],
      ]),
    );
    expect(loadRate()).toBe('robust');
    expect(permissionsAsked()).toBe(true);
  });

  it('reads a damaged rate as the default', () => {
    const settings = memorySettings();
    settings.stored.set('ledRate', 'warp');
    NativeModules.RpiSettings = settings;
    expect(loadRate()).toBe(DEFAULT_RATE);
  });

  it('remembers nothing, and does not fail, without the module or when it throws', () => {
    for (const module of [undefined, failing]) {
      if (module) NativeModules.RpiSettings = module;
      expect(loadRate()).toBe(DEFAULT_RATE);
      expect(permissionsAsked()).toBe(false);
      expect(() => saveRate('fast')).not.toThrow();
      expect(() => markPermissionsAsked()).not.toThrow();
    }
  });
});

describe('random bytes', () => {
  it("come from the app's module when it is there", () => {
    const randomBytesFrom = vi.fn((length: number) => base64Encode(Uint8Array.from({ length }, (_, i) => i + 1)));
    NativeModules.RpiRandom = { randomBytes: randomBytesFrom };
    expect(randomBytes(4)).toEqual(new Uint8Array([1, 2, 3, 4]));
    expect(randomBytesFrom).toHaveBeenCalledWith(4);
  });

  it('are refused when the module gives the wrong number', () => {
    NativeModules.RpiRandom = { randomBytes: () => base64Encode(new Uint8Array(31)) };
    expect(() => randomBytes(32)).toThrow(/wrong number of bytes/);
  });

  it('come from crypto.getRandomValues without the module, and from nowhere else', () => {
    expect(randomBytes(16)).toHaveLength(16);
    vi.stubGlobal('crypto', undefined);
    expect(() => randomBytes(16)).toThrow(/no source of random bytes/);
  });
});

describe('diagnostics', () => {
  /** The constant as a fresh import computes it, with `__DEV__` and the module given. */
  async function diagnostics(dev: boolean, module?: object) {
    vi.resetModules();
    vi.stubGlobal('__DEV__', dev);
    const fresh = await import('@/test/react-native');
    if (module) fresh.NativeModules.RpiRandom = module;
    return (await import('@/util/diagnostics')).DIAGNOSTICS;
  }

  it('are off in a release build', async () => {
    expect(await diagnostics(false)).toBe(false);
    expect(await diagnostics(false, { getConstants: () => ({ diagnostics: false }) })).toBe(false);
    expect(await diagnostics(false, { randomBytes: () => '' })).toBe(false);
  });

  it('are on in a development bundle', async () => {
    expect(await diagnostics(true)).toBe(true);
  });

  it('are on in a build made with them', async () => {
    expect(await diagnostics(false, { getConstants: () => ({ diagnostics: true }) })).toBe(true);
  });

  it('are off when the module cannot say', async () => {
    const throwing = {
      getConstants() {
        throw new Error('no constants');
      },
    };
    expect(await diagnostics(false, throwing)).toBe(false);
  });
});

describe("the phone's network", () => {
  it('is asked of Android with location granted', async () => {
    Platform.OS = 'android';
    const check = vi.spyOn(PermissionsAndroid, 'check');
    NativeModules.RpiWifiInfo = { ssid: async () => 'CorvusFibre' };
    expect(await phoneSsid()).toBe('CorvusFibre');
    expect(check).toHaveBeenCalledWith('android.permission.ACCESS_FINE_LOCATION');
  });

  it('is unknown without location, without the module, off Android, or when Android cannot say', async () => {
    NativeModules.RpiWifiInfo = { ssid: async () => 'CorvusFibre' };
    expect(await phoneSsid()).toBeUndefined();

    Platform.OS = 'android';
    vi.spyOn(PermissionsAndroid, 'check').mockResolvedValueOnce(false);
    expect(await phoneSsid()).toBeUndefined();

    NativeModules.RpiWifiInfo = { ssid: async () => null };
    expect(await phoneSsid()).toBeUndefined();

    NativeModules.RpiWifiInfo = {
      ssid: async () => {
        throw new Error('WifiManager');
      },
    };
    expect(await phoneSsid()).toBeUndefined();

    delete NativeModules.RpiWifiInfo;
    expect(await phoneSsid()).toBeUndefined();
  });
});
