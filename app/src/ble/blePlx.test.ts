// The transport over react-native-ble-plx, against a fake of the library's manager and device:
// what it asks Android for, how it waits for the adapter, what a scan reports, and that a
// connection keeps its writes in order and goes quiet once it is closed.
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { CHAR_INFO, SERVICE_UUID } from '@/protocol/constants';
import { base64Encode } from './base64';
import type { BleTransport, Peripheral } from './transport';

type Listener<T> = (value: T) => void;
type ScanCallback = (error: Error | null, device: { id: string; localName: string | null; rssi: number | null } | null) => void;
type MonitorCallback = (error: Error | null, characteristic: { value: string | null } | null) => void;

function deferred() {
  let resolve!: () => void;
  let reject!: (error: Error) => void;
  const promise = new Promise<void>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

class FakeDevice {
  mtu = 517;
  discoverError?: Error;
  values = new Map<string, string | null>();
  writes: Array<{ characteristic: string; value: string; done: ReturnType<typeof deferred> }> = [];
  monitors = new Map<string, MonitorCallback>();
  disconnectListeners = new Set<(error: Error | null) => void>();
  services: string[] = [];

  constructor(readonly id: string) {}

  async discoverAllServicesAndCharacteristics() {
    if (this.discoverError) throw this.discoverError;
    return this;
  }
  async readCharacteristicForService(service: string, characteristic: string) {
    this.services.push(service);
    return { value: this.values.get(characteristic) ?? null };
  }
  writeCharacteristicWithResponseForService(service: string, characteristic: string, value: string) {
    this.services.push(service);
    const done = deferred();
    this.writes.push({ characteristic, value, done });
    return done.promise;
  }
  monitorCharacteristicForService(service: string, characteristic: string, callback: MonitorCallback) {
    this.services.push(service);
    this.monitors.set(characteristic, callback);
    return { remove: () => this.monitors.delete(characteristic) };
  }
  onDisconnected(listener: (error: Error | null) => void) {
    this.disconnectListeners.add(listener);
    return { remove: () => this.disconnectListeners.delete(listener) };
  }
  dropLink(error: Error | null) {
    for (const listener of [...this.disconnectListeners]) listener(error);
  }
}

class FakeManager {
  adapter = 'PoweredOn';
  stateListeners = new Set<Listener<string>>();
  scan?: { uuids: string[]; options: object; callback: ScanCallback };
  stops = 0;
  devices = new Map<string, FakeDevice>();
  connects: Array<{ id: string; options: object }> = [];
  cancelled: string[] = [];
  cancelError?: Error;

  async state() {
    return this.adapter;
  }
  // The library reports the current state asynchronously, after it has been asked for.
  onStateChange(listener: Listener<string>, emitCurrent: boolean) {
    this.stateListeners.add(listener);
    if (emitCurrent) {
      void this.state().then((state) => {
        if (this.stateListeners.has(listener)) listener(state);
      });
    }
    return { remove: () => this.stateListeners.delete(listener) };
  }
  setState(state: string) {
    this.adapter = state;
    for (const listener of [...this.stateListeners]) listener(state);
  }
  startDeviceScan(uuids: string[], options: object, callback: ScanCallback) {
    this.scan = { uuids, options, callback };
    return Promise.resolve();
  }
  stopDeviceScan() {
    this.stops += 1;
    return Promise.resolve();
  }
  report(id: string, localName: string | null, rssi: number | null = -60) {
    this.scan?.callback(null, { id, localName, rssi });
  }
  async connectToDevice(id: string, options: object) {
    this.connects.push({ id, options });
    const device = this.devices.get(id);
    if (!device) throw new Error(`no device ${id}`);
    return device;
  }
  async cancelDeviceConnection(id: string) {
    this.cancelled.push(id);
    if (this.cancelError) throw this.cancelError;
  }
}

let manager: FakeManager;
let transport: BleTransport;
let rn: typeof import('@/test/react-native');

/** A fresh module each time: blePlx.ts keeps its manager and the Pis it saw at module level. */
async function load(os: 'android' | 'ios', version: number) {
  vi.resetModules();
  rn = await import('@/test/react-native');
  rn.Platform.OS = os;
  rn.Platform.Version = version;
  manager = new FakeManager();
  (await import('@/test/react-native-ble-plx')).testing.manager = manager;
  transport = (await import('./blePlx')).blePlxTransport;
}

async function settle() {
  for (let i = 0; i < 5; i += 1) await Promise.resolve();
}

afterEach(() => {
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe('prepare: permissions', () => {
  it('asks for nothing on iOS', async () => {
    await load('ios', 17);
    const requestMultiple = vi.spyOn(rn.PermissionsAndroid, 'requestMultiple');
    const request = vi.spyOn(rn.PermissionsAndroid, 'request');
    await transport.prepare();
    expect(requestMultiple).not.toHaveBeenCalled();
    expect(request).not.toHaveBeenCalled();
  });

  it("asks Android 12 and later for Bluetooth's own permissions", async () => {
    await load('android', 31);
    const requestMultiple = vi.spyOn(rn.PermissionsAndroid, 'requestMultiple');
    await transport.prepare();
    expect(requestMultiple).toHaveBeenCalledWith(['android.permission.BLUETOOTH_SCAN', 'android.permission.BLUETOOTH_CONNECT']);
  });

  it('refuses when either Bluetooth permission is declined', async () => {
    await load('android', 33);
    vi.spyOn(rn.PermissionsAndroid, 'requestMultiple').mockResolvedValue({
      'android.permission.BLUETOOTH_SCAN': 'granted',
      'android.permission.BLUETOOTH_CONNECT': 'denied',
    });
    await expect(transport.prepare()).rejects.toMatchObject({ name: 'BluetoothUnavailable', message: 'Bluetooth permission was not granted.' });
  });

  it('asks an older Android for location, which scanning needs there', async () => {
    await load('android', 30);
    const request = vi.spyOn(rn.PermissionsAndroid, 'request');
    await transport.prepare();
    expect(request).toHaveBeenCalledWith('android.permission.ACCESS_FINE_LOCATION');
    request.mockResolvedValue('never_ask_again');
    await expect(transport.prepare()).rejects.toMatchObject({ name: 'BluetoothUnavailable', message: /Location permission/ });
  });
});

describe('prepare: the adapter', () => {
  beforeEach(() => load('ios', 17));

  it('goes on at once when Bluetooth is on', async () => {
    await expect(transport.prepare()).resolves.toBeUndefined();
    expect(manager.stateListeners.size).toBe(0);
  });

  it('does not wait on a state that waiting will not change', async () => {
    manager.adapter = 'Unauthorized';
    await expect(transport.prepare()).rejects.toMatchObject({ name: 'BluetoothUnavailable', message: /not allowed to use Bluetooth/ });
    manager.adapter = 'Unsupported';
    await expect(transport.prepare()).rejects.toThrow(/does not support Bluetooth LE/);
  });

  it('waits for the adapter to come on', async () => {
    manager.adapter = 'PoweredOff';
    const prepared = transport.prepare();
    await settle();
    manager.setState('Resetting');
    manager.setState('PoweredOn');
    await expect(prepared).resolves.toBeUndefined();
    expect(manager.stateListeners.size).toBe(0);
  });

  it('gives up after 4 s with the state the adapter was last in', async () => {
    vi.useFakeTimers();
    manager.adapter = 'Unknown';
    const prepared = transport.prepare();
    const outcome = expect(prepared).rejects.toMatchObject({ name: 'BluetoothUnavailable', message: 'Bluetooth is off. Turn it on and try again.' });
    await settle();
    manager.setState('PoweredOff');
    await vi.advanceTimersByTimeAsync(3_999);
    expect(manager.stateListeners.size).toBe(1);
    await vi.advanceTimersByTimeAsync(1);
    await outcome;
    expect(manager.stateListeners.size).toBe(0);
  });
});

describe('scan', () => {
  beforeEach(() => load('android', 31));

  it('scans for the service at full duty, every report taken', () => {
    const stop = transport.scan(
      () => {},
      () => {},
    );
    expect(manager.scan?.uuids).toEqual([SERVICE_UUID]);
    expect(manager.scan?.options).toEqual({ allowDuplicates: true, scanMode: 2 });
    stop();
    expect(manager.stops).toBe(1);
  });

  it('reports a Pi by its advertised name, and nothing without one', () => {
    const found: Peripheral[] = [];
    transport.scan(
      (peripheral) => found.push(peripheral),
      () => {},
    );
    manager.report('AA', null);
    manager.scan?.callback(null, null);
    manager.report('AA', 'RPiLEDConfig-7fab', -48);
    manager.report('BB', 'RPiLEDConfig-0c21', null);
    expect(found).toEqual([
      { id: 'AA', name: 'RPiLEDConfig-7fab', rssi: -48 },
      { id: 'BB', name: 'RPiLEDConfig-0c21', rssi: undefined },
    ]);
  });

  it("passes the library's error on", () => {
    const errors: Error[] = [];
    const found: Peripheral[] = [];
    transport.scan(
      (peripheral) => found.push(peripheral),
      (error) => errors.push(error),
    );
    manager.scan?.callback(new Error('scan throttled'), null);
    expect(errors.map((error) => error.message)).toEqual(['scan throttled']);
    expect(found).toEqual([]);
  });

  it('reports first the Pis heard in the last 5 s, from any scan', async () => {
    vi.useFakeTimers();
    transport.scan(
      () => {},
      () => {},
    )();
    manager.report('OLD', 'RPiLEDConfig-0001');
    vi.advanceTimersByTime(3_000);
    manager.report('NEW', 'RPiLEDConfig-0002');
    vi.advanceTimersByTime(2_500);

    const found: string[] = [];
    transport.scan(
      (peripheral) => found.push(peripheral.id),
      () => {},
    );
    expect(found).toEqual([]);
    await settle();
    expect(found).toEqual(['NEW']);
  });

  it('reports nothing remembered once stopped', async () => {
    transport.scan(
      () => {},
      () => {},
    )();
    manager.report('AA', 'RPiLEDConfig-7fab');
    const found: string[] = [];
    const stop = transport.scan(
      (peripheral) => found.push(peripheral.id),
      () => {},
    );
    stop();
    await settle();
    expect(found).toEqual([]);
  });

  it('prescans once permitted and powered, so the next scan knows the Pi', async () => {
    const stop = transport.prescan!();
    await settle();
    expect(manager.scan?.uuids).toEqual([SERVICE_UUID]);
    manager.report('AA', 'RPiLEDConfig-7fab');
    manager.scan?.callback(new Error('ignored'), null);
    stop();
    expect(manager.stops).toBe(1);

    const found: string[] = [];
    transport.scan(
      (peripheral) => found.push(peripheral.id),
      () => {},
    );
    await settle();
    expect(found).toEqual(['AA']);
  });

  it('prescans not at all without permission, and quietly', async () => {
    vi.spyOn(rn.PermissionsAndroid, 'requestMultiple').mockResolvedValue({
      'android.permission.BLUETOOTH_SCAN': 'denied',
      'android.permission.BLUETOOTH_CONNECT': 'denied',
    });
    transport.prescan!();
    await settle();
    expect(manager.scan).toBeUndefined();
  });

  it('starts no scan once stopped, even if it was still waiting for the adapter', async () => {
    manager.adapter = 'PoweredOff';
    const stop = transport.prescan!();
    await settle();
    stop();
    manager.setState('PoweredOn');
    await settle();
    expect(manager.scan).toBeUndefined();
  });
});

describe('connect', () => {
  let device: FakeDevice;

  beforeEach(async () => {
    await load('android', 31);
    device = new FakeDevice('AA');
    manager.devices.set('AA', device);
  });

  it('asks for an MTU of 517 within 12 s and discovers the service', async () => {
    const connection = await transport.connect('AA');
    expect(manager.connects).toEqual([{ id: 'AA', options: { requestMTU: 517, timeout: 12_000 } }]);
    expect(connection.mtu).toBe(517);
    device.mtu = 185;
    expect(connection.mtu).toBe(185);
  });

  it('drops the link when discovery fails, and says why', async () => {
    device.discoverError = new Error('GATT 133');
    manager.cancelError = new Error('not connected');
    await expect(transport.connect('AA')).rejects.toThrow('GATT 133');
    expect(manager.cancelled).toEqual(['AA']);
  });

  it('reads base64 from the library as bytes, and nothing as empty', async () => {
    const connection = await transport.connect('AA');
    device.values.set(CHAR_INFO, base64Encode(new Uint8Array([1, 2, 255])));
    expect(await connection.read(CHAR_INFO)).toEqual(new Uint8Array([1, 2, 255]));
    device.values.set(CHAR_INFO, null);
    expect(await connection.read(CHAR_INFO)).toEqual(new Uint8Array());
    expect(device.services.every((service) => service === SERVICE_UUID)).toBe(true);
  });

  it('writes one at a time, in order, past a write that failed', async () => {
    const connection = await transport.connect('AA');
    const first = connection.write('c', new Uint8Array([1]));
    const second = connection.write('c', new Uint8Array([2]));
    const third = connection.write('c', new Uint8Array([3]));
    await settle();
    expect(device.writes.map((write) => write.value)).toEqual([base64Encode(new Uint8Array([1]))]);

    device.writes[0].done.reject(new Error('write failed'));
    await expect(first).rejects.toThrow('write failed');
    await settle();
    expect(device.writes).toHaveLength(2);
    device.writes[1].done.resolve();
    await expect(second).resolves.toBeUndefined();
    await settle();
    expect(device.writes.map((write) => write.value)).toEqual([1, 2, 3].map((n) => base64Encode(new Uint8Array([n]))));
    device.writes[2].done.resolve();
    await expect(third).resolves.toBeUndefined();
  });

  it('decodes notifications, and stops them when asked', async () => {
    const connection = await transport.connect('AA');
    const values: Uint8Array[] = [];
    const errors: Error[] = [];
    const stop = connection.monitor(
      'n',
      (value) => values.push(value),
      (error) => errors.push(error),
    );
    const notify = device.monitors.get('n')!;
    notify(null, { value: base64Encode(new Uint8Array([7, 8])) });
    notify(null, { value: null });
    notify(null, null);
    notify(new Error('notify failed'), null);
    expect(values).toEqual([new Uint8Array([7, 8])]);
    expect(errors.map((error) => error.message)).toEqual(['notify failed']);
    stop();
    expect(device.monitors.has('n')).toBe(false);
  });

  it('reports a dropped link with its reason, if it had one', async () => {
    const connection = await transport.connect('AA');
    const reasons: Array<Error | undefined> = [];
    const stop = connection.onDisconnected((reason) => reasons.push(reason));
    device.dropLink(new Error('link lost'));
    device.dropLink(null);
    expect(reasons.map((reason) => reason?.message)).toEqual(['link lost', undefined]);
    stop();
    expect(device.disconnectListeners.size).toBe(0);
  });

  it('goes quiet on disconnect: no errors, no drop reported, the link cancelled', async () => {
    const connection = await transport.connect('AA');
    const errors: Error[] = [];
    const reasons: Array<Error | undefined> = [];
    connection.monitor(
      'n',
      () => {},
      (error) => errors.push(error),
    );
    const notify = device.monitors.get('n')!;
    connection.onDisconnected((reason) => reasons.push(reason));
    const drop = [...device.disconnectListeners][0];
    manager.cancelError = new Error('already gone');

    await connection.disconnect();
    expect(manager.cancelled).toEqual(['AA']);
    expect(device.monitors.size).toBe(0);
    expect(device.disconnectListeners.size).toBe(0);
    // What the library delivers late, while it tears down, is not news.
    notify(new Error('cancelled'), null);
    drop(new Error('disconnected'));
    expect(errors).toEqual([]);
    expect(reasons).toEqual([]);
  });

  it('a monitor without an error handler ignores errors', async () => {
    const connection = await transport.connect('AA');
    connection.monitor('n', () => {});
    expect(() => device.monitors.get('n')!(new Error('notify failed'), null)).not.toThrow();
  });
});

describe('services', () => {
  it('are the BLE transport on the phone', async () => {
    await load('android', 31);
    const { services } = await import('@/state/services');
    expect(services.transport).toBe(transport);
  });
});
