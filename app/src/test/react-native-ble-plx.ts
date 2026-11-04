// react-native-ble-plx under test: nothing that reaches it is exercised by
// the logic tests (the fake transport stands in), so this is only enough for
// the module to import.
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

export class BleManager {
  constructor() {
    throw new Error('BleManager is native; tests use the fake transport');
  }
}

export type Device = unknown;
export type Subscription = { remove(): void };
export type BleError = Error;
