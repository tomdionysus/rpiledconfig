// Random bytes from the system's secure source. On the phone, the app's own RpiRandom module
// (android/.../RpiPackage.kt), synchronous because SPAKE2's scalars are drawn inside
// synchronous code; under test, node's crypto. Hermes has no crypto.getRandomValues of its own.
import { NativeModules } from 'react-native';
import { base64Decode } from '@/ble/base64';

interface RpiRandom {
  randomBytes(length: number): string;
}

export function randomBytes(length: number): Uint8Array {
  const module = (NativeModules as { RpiRandom?: RpiRandom }).RpiRandom;
  if (module) {
    const bytes = base64Decode(module.randomBytes(length));
    if (bytes.length !== length) throw new Error('RpiRandom gave the wrong number of bytes');
    return bytes;
  }
  const web = (globalThis as { crypto?: { getRandomValues?(a: Uint8Array): Uint8Array } }).crypto;
  if (web?.getRandomValues) return web.getRandomValues(new Uint8Array(length));
  throw new Error('no source of random bytes');
}
