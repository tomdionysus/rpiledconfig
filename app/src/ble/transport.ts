// What the pairing logic needs from Bluetooth, and nothing more, so the
// session can be driven by react-native-ble-plx on a phone and by the fake
// daemon in tests and development.

export interface Peripheral {
  /** The platform's identifier for the device, passed back to `connect`. */
  id: string;
  /** The advertised local name, `RPiLEDConfig-<fingerprint>` for a Pi. */
  name: string;
  rssi?: number;
}

export interface BleConnection {
  /** The negotiated ATT MTU; a write carries MTU - 3 bytes. */
  readonly mtu: number;
  read(characteristic: string): Promise<Uint8Array>;
  /** A write with response: it resolves once the daemon has taken the value. */
  write(characteristic: string, value: Uint8Array): Promise<void>;
  /** Notifications; returns the function that unsubscribes. */
  monitor(characteristic: string, onValue: (value: Uint8Array) => void, onError?: (error: Error) => void): () => void;
  /** Called once if the link drops for any reason other than `disconnect`. */
  onDisconnected(listener: (reason: Error | undefined) => void): () => void;
  disconnect(): Promise<void>;
}

export interface BleTransport {
  /** Permissions and the adapter; rejects with a reason a person can read. */
  prepare(): Promise<void>;
  /**
   * Reports every peripheral whose name starts with `namePrefix` until the
   * returned function is called: first any seen in the last minute (by an
   * earlier scan or a prescan), then new ones.
   */
  scan(namePrefix: string, onFound: (peripheral: Peripheral) => void, onError: (error: Error) => void): () => void;
  /**
   * Looks for Pis in the background while the LED is being read, so the
   * pairing step finds its Pi at once instead of starting a scan after the
   * read. Returns the function that stops it. Optional: a transport without
   * it simply scans later.
   */
  prescan?(): () => void;
  connect(id: string): Promise<BleConnection>;
}
