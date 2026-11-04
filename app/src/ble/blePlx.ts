// The BleTransport over react-native-ble-plx. Thin by design: permissions,
// the adapter state, one scan, one connection, and the base64 that the
// library speaks. Everything with a decision in it is in pairing/session.ts.
import { PermissionsAndroid, Platform } from 'react-native';
import { BleManager, ScanMode, State, type Device, type Subscription } from 'react-native-ble-plx';
import { BLE_NAME_PREFIX, SERVICE_UUID } from '@/protocol/constants';
import { base64Decode, base64Encode } from './base64';
import type { BleConnection, BleTransport, Peripheral } from './transport';

/**
 * 517, the most ATT allows and what BlueZ offers, so both ends settle on the
 * same number. Asking for 185 on a Blackview A85 left Android at 185 while
 * BlueZ still reported 517 to the daemon, whose larger notifications were
 * then dropped (the daemon now caps its fragments at 185 as well; iOS gives
 * what it gives).
 */
const REQUESTED_MTU = 517;
const CONNECT_TIMEOUT_MS = 12_000;
const POWER_ON_TIMEOUT_MS = 4_000;

/** An error whose message is ours and plain, as opposed to the library's; pairing shows these as they are. */
function unavailable(message: string): Error {
  const error = new Error(message);
  error.name = 'BluetoothUnavailable';
  return error;
}

let manager: BleManager | undefined;
function ble(): BleManager {
  if (!manager) manager = new BleManager();
  return manager;
}

async function requestPermissions(): Promise<void> {
  if (Platform.OS !== 'android') return;
  if (Number(Platform.Version) >= 31) {
    const results = await PermissionsAndroid.requestMultiple([
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
    ]);
    if (!Object.values(results).every((result) => result === PermissionsAndroid.RESULTS.GRANTED)) {
      throw unavailable('Bluetooth permission was not granted.');
    }
    return;
  }
  const result = await PermissionsAndroid.request(PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION);
  if (result !== PermissionsAndroid.RESULTS.GRANTED) throw unavailable('Location permission, which Bluetooth scanning needs on this Android, was not granted.');
}

/** Resolves once the adapter is powered on, or rejects with a readable reason. */
async function waitForPoweredOn(): Promise<void> {
  if ((await ble().state()) === State.PoweredOn) return;
  await new Promise<void>((resolve, reject) => {
    const timer = setTimeout(() => {
      subscription.remove();
      reject(unavailable('Bluetooth is off. Turn it on and try again.'));
    }, POWER_ON_TIMEOUT_MS);
    const subscription = ble().onStateChange((state) => {
      if (state === State.PoweredOn) {
        clearTimeout(timer);
        subscription.remove();
        resolve();
      }
    }, true);
  });
}

/** Pis seen recently, by device id, from any scan: what `scan` reports first. */
const RECENT_MS = 60_000;
const recent = new Map<string, { peripheral: Peripheral; at: number }>();

/**
 * One scan for the service, at full duty: LOW_POWER (the default) listens a
 * fraction of each few seconds, and finding the Pi after a read took up to 14 s
 * on a Blackview A85. Every Pi seen goes into `recent`.
 */
function startScan(onPi: (peripheral: Peripheral) => void, onError: (error: Error) => void): void {
  // By service as well as by name: the 128-bit UUID and the name may not
  // both fit in the advertisement, and which one lands in the scan
  // response depends on the stack.
  void ble().startDeviceScan([SERVICE_UUID], { allowDuplicates: false, scanMode: ScanMode.LowLatency }, (error, device) => {
    if (error) {
      onError(error);
      return;
    }
    if (!device) return;
    // The advertised name first: `name` is the GAP name Android caches once it has
    // connected, which is the Pi's hostname, and preferring it made every scan after
    // the first connection miss a Pi that was advertising all along.
    const name = [device.localName, device.name].find((candidate) => candidate?.startsWith(BLE_NAME_PREFIX));
    if (!name) return;
    const peripheral = { id: device.id, name, rssi: device.rssi ?? undefined };
    recent.set(device.id, { peripheral, at: Date.now() });
    onPi(peripheral);
  });
}

function connection(device: Device): BleConnection {
  const subscriptions: Subscription[] = [];
  let closing = false;
  // One write at a time: fragments of a message must arrive in order.
  let chain: Promise<void> = Promise.resolve();

  return {
    get mtu() {
      return device.mtu;
    },
    async read(characteristic) {
      const result = await device.readCharacteristicForService(SERVICE_UUID, characteristic);
      return result.value ? base64Decode(result.value) : new Uint8Array();
    },
    write(characteristic, value) {
      const next = chain.then(async () => {
        await device.writeCharacteristicWithResponseForService(SERVICE_UUID, characteristic, base64Encode(value));
      });
      chain = next.catch(() => {});
      return next;
    },
    monitor(characteristic, onValue, onError) {
      const subscription = device.monitorCharacteristicForService(SERVICE_UUID, characteristic, (error, ch) => {
        if (error) {
          if (!closing) onError?.(error);
          return;
        }
        if (ch?.value) onValue(base64Decode(ch.value));
      });
      subscriptions.push(subscription);
      return () => subscription.remove();
    },
    onDisconnected(listener) {
      const subscription = device.onDisconnected((error) => {
        if (!closing) listener(error ?? undefined);
      });
      subscriptions.push(subscription);
      return () => subscription.remove();
    },
    async disconnect() {
      closing = true;
      subscriptions.forEach((subscription) => subscription.remove());
      await ble()
        .cancelDeviceConnection(device.id)
        .catch(() => {});
    },
  };
}

export const blePlxTransport: BleTransport = {
  async prepare() {
    await requestPermissions();
    await waitForPoweredOn();
  },

  scan(namePrefix, onFound, onError) {
    let stopped = false;
    // What a prescan already saw: the Pi is usually found before the scan starts.
    const now = Date.now();
    const known = [...recent.values()].filter((entry) => now - entry.at <= RECENT_MS && entry.peripheral.name.startsWith(namePrefix));
    if (known.length > 0) {
      queueMicrotask(() => {
        if (!stopped) for (const entry of known) onFound(entry.peripheral);
      });
    }
    startScan(
      (peripheral) => {
        if (peripheral.name.startsWith(namePrefix)) onFound(peripheral);
      },
      onError,
    );
    return () => {
      stopped = true;
      void ble().stopDeviceScan();
    };
  },

  prescan() {
    void requestPermissions()
      .then(waitForPoweredOn)
      .then(() => startScan(() => {}, () => {}))
      .catch(() => {});
    return () => {
      void ble().stopDeviceScan();
    };
  },

  async connect(id) {
    const device = await ble().connectToDevice(id, { requestMTU: REQUESTED_MTU, timeout: CONNECT_TIMEOUT_MS });
    try {
      await device.discoverAllServicesAndCharacteristics();
    } catch (error) {
      await ble()
        .cancelDeviceConnection(id)
        .catch(() => {});
      throw error;
    }
    return connection(device);
  },
};
