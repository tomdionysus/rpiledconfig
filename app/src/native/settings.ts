// What the app remembers between launches: the LED rate, and that permissions have been asked for. On the phone, the app's own RpiSettings
// module (android/.../RpiPackage.kt); synchronous, so the first screen already has it. Where
// the module is not there (under test), nothing is remembered.
import { NativeModules } from 'react-native';
import { DEFAULT_RATE, parseRate, type LedRate } from '@/led/rates';

interface RpiSettings {
  get(key: string): string | null;
  set(key: string, value: string): void;
}

const RATE_KEY = 'ledRate';
const PERMISSIONS_ASKED_KEY = 'permissionsAsked';

function settings(): RpiSettings | undefined {
  return (NativeModules as { RpiSettings?: RpiSettings }).RpiSettings;
}

export function loadRate(): LedRate {
  try {
    return parseRate(settings()?.get(RATE_KEY));
  } catch {
    return DEFAULT_RATE;
  }
}

/** Whether the first-launch screen has asked for permissions on this phone. */
export function permissionsAsked(): boolean {
  try {
    return settings()?.get(PERMISSIONS_ASKED_KEY) === 'yes';
  } catch {
    return false;
  }
}

export function markPermissionsAsked(): void {
  try {
    settings()?.set(PERMISSIONS_ASKED_KEY, 'yes');
  } catch {
    // Asked once more next launch, and nothing else depends on it.
  }
}

export function saveRate(rate: LedRate): void {
  try {
    settings()?.set(RATE_KEY, rate);
  } catch {
    // Not remembered, and nothing else depends on it.
  }
}
