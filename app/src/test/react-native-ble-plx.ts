// react-native-ble-plx under test. The pairing logic is tested over the fake transport
// (src/ble/fake.ts); the transport over this library (src/ble/blePlx.ts) is tested against a
// manager the test puts in `testing.manager`, which `new BleManager()` hands back.
export enum State {
  Unknown = 'Unknown',
  Resetting = 'Resetting',
  Unsupported = 'Unsupported',
  Unauthorized = 'Unauthorized',
  PoweredOff = 'PoweredOff',
  PoweredOn = 'PoweredOn',
}

export enum ScanMode {
  Opportunistic = -1,
  LowPower = 0,
  Balanced = 1,
  LowLatency = 2,
}

export const testing: { manager?: object } = {};

export class BleManager {
  constructor() {
    if (!testing.manager) throw new Error('BleManager is native; a test sets testing.manager, or uses the fake transport');
    return testing.manager;
  }
}

export type Device = unknown;
export type Subscription = { remove(): void };
export type BleError = Error;
