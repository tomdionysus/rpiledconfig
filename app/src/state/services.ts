// Which Bluetooth the screens talk to: the phone's, or the fake daemon.
// `EXPO_PUBLIC_RPILEDCONFIG_FAKE=1 npx expo start` starts with the fake, and
// a development build can switch on the home screen.
import { blePlxTransport } from '@/ble/blePlx';
import { FAKE_MACHINE_ID_HEX, FakeDaemon } from '@/ble/fake';
import type { BleTransport } from '@/ble/transport';
import { hexToBytes } from '@/protocol/bytes';

export interface Services {
  transport: BleTransport;
  /** Set when `transport` is the fake daemon, which the screens may say so. */
  fake?: FakeDaemon;
}

/** Whether the build was started with the fake switched on. */
export function fakeByDefault(): boolean {
  return process.env.EXPO_PUBLIC_RPILEDCONFIG_FAKE === '1';
}

/** The fake Pi's passphrase for Blackraven; any other ends as a failed join after the timeout. */
export const FAKE_PASSPHRASE = 'raspberry';

export function createServices(fake: boolean): Services {
  if (!fake) return { transport: blePlxTransport };
  // As a Pi 4 or 5 behaves on a join: the shared radio drops the BLE link for
  // a few seconds, and the network is up a few seconds after that.
  const daemon = new FakeDaemon({
    machineId: hexToBytes(FAKE_MACHINE_ID_HEX),
    hostname: 'fake-pi',
    latencyMs: 120,
    passphrases: { Blackraven: FAKE_PASSPHRASE },
    joinAfterMs: 7_000,
    linkDrop: { when: 'after_response', offAirMs: 4_000 },
  });
  return { transport: daemon, fake: daemon };
}
