import { describe, expect, it } from 'vitest';
import { FakeDaemon } from '@/ble/fake';
import type { BleTransport } from '@/ble/transport';
import { hexToBytes, utf8Encode } from '@/protocol/bytes';
import { appChannel } from '@/protocol/channel';
import { AUTH_ACCEPTED, CHAR_AUTH_RESULT, CHAR_CHALLENGE, CHAR_INFO, CHAR_RESPONSE, CHAR_REQUEST } from '@/protocol/constants';
import { fragment } from '@/protocol/fragments';
import { sessionKey } from '@/protocol/keys';
import { encodeRequest } from '@/protocol/messages';
import { ManualClock } from '@/util/clock';
import { LinkLostError, pair, PairingError, parseInfo, RequestError, type PairingPhase } from './session';

const ID = hexToBytes('012345');
const OTHER_ID = hexToBytes('fedcba');

function tick(ms = 0): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function failure(promise: Promise<unknown>): Promise<PairingError> {
  try {
    await promise;
  } catch (error) {
    if (error instanceof PairingError) return error;
    throw error;
  }
  throw new Error('expected the pairing to fail');
}

describe('pair', () => {
  it('finds the Pi by its name, authenticates both ways and answers status', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID, hostname: 'pi5' });
    const phases: PairingPhase[] = [];
    const session = await pair(daemon, daemon.name, ID, { onPhase: (phase) => phases.push(phase) });
    expect(phases).toEqual(['scanning', 'connecting', 'checking', 'authenticating', 'ready']);
    expect(session.peripheral.name).toBe(daemon.name);
    expect(session.info.protocol).toBe(4);

    const status = await session.status();
    expect(status).toEqual({ ok: true, hostname: 'pi5', wifi: { connected: false } });
    expect(daemon.refusalsInARow).toBe(0);
    await session.close();
  });

  it('finds nothing under another name', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const error = await failure(pair(daemon, 'RPiLEDConfig-ffff', ID, { scanTimeoutMs: 50 }));
    expect(error.kind).toBe('not_found');
  });

  it('finds a Pi whatever prefix its name has', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID, name: 'Kitchen Pi-3a9c' });
    const session = await pair(daemon, 'Kitchen Pi-3a9c', ID);
    expect(session.peripheral.name).toBe('Kitchen Pi-3a9c');
    await session.close();
  });

  it("is refused with a key from another Pi's LED", async () => {
    const daemon = new FakeDaemon({ pairingKey: OTHER_ID });
    const error = await failure(pair(daemon, daemon.name, ID));
    expect(error.kind).toBe('refused');
    expect(error.message).toMatch(/another Pi/);
  });

  it('reports a refused authentication and counts it on the daemon', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID, refuseAll: true });
    const error = await failure(pair(daemon, daemon.name, ID));
    expect(error.kind).toBe('refused');
    expect(daemon.refusalsInARow).toBe(1);
  });

  it.each([
    ['too short', (v: Uint8Array) => v.subarray(0, v.length - 1)],
    ['too long', (v: Uint8Array) => Uint8Array.from([...v, 0])],
    ['not marked accepted', (v: Uint8Array) => Uint8Array.from([0x7f, ...v.subarray(1)])],
  ])('refuses an authentication result that is %s', async (_name, change) => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const malformed: BleTransport = {
      prepare: () => daemon.prepare(),
      scan: (onFound, onError) => daemon.scan(onFound, onError),
      async connect(id) {
        const real = await daemon.connect(id);
        const altered = (characteristic: string, value: Uint8Array) => (characteristic === CHAR_AUTH_RESULT && value.length > 1 ? change(value) : value);
        return {
          ...real,
          get mtu() {
            return real.mtu;
          },
          read: async (characteristic) => altered(characteristic, await real.read(characteristic)),
          monitor: (characteristic, onValue, onError) => real.monitor(characteristic, (value) => onValue(altered(characteristic, value)), onError),
          write: (characteristic, value) => real.write(characteristic, value),
          onDisconnected: (listener) => real.onDisconnected(listener),
          disconnect: () => real.disconnect(),
        };
      },
    };
    const error = await failure(pair(malformed, daemon.name, ID));
    expect(error.kind).toBe('protocol');
  });

  it('rejects a Pi whose cB does not verify', async () => {
    // A link that accepts but hands back a wrong proof: the app must not
    // take "accepted" on trust.
    const daemon = new FakeDaemon({ pairingKey: ID });
    const tampering: BleTransport = {
      prepare: () => daemon.prepare(),
      scan: (onFound, onError) => daemon.scan(onFound, onError),
      async connect(id) {
        const real = await daemon.connect(id);
        const corrupt = (characteristic: string, value: Uint8Array) => {
          if (characteristic === CHAR_AUTH_RESULT && value.length > 1) value[value.length - 1] ^= 0x01;
          return value;
        };
        return {
          ...real,
          get mtu() {
            return real.mtu;
          },
          read: async (characteristic) => corrupt(characteristic, await real.read(characteristic)),
          monitor: (characteristic, onValue, onError) => real.monitor(characteristic, (value) => onValue(corrupt(characteristic, value)), onError),
          write: (characteristic, value) => real.write(characteristic, value),
          onDisconnected: (listener) => real.onDisconnected(listener),
          disconnect: () => real.disconnect(),
        };
      },
    };
    const error = await failure(pair(tampering, daemon.name, ID));
    expect(error.kind).toBe('refused');
    expect(error.message).toMatch(/proof/);
  });

  it('pauses advertising after five refusals in a row', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID, refuseAll: true });
    for (let attempt = 0; attempt < 5; attempt += 1) {
      const error = await failure(pair(daemon, daemon.name, ID));
      expect(error.kind).toBe('refused');
    }
    expect(daemon.advertising).toBe(false);
    const error = await failure(pair(daemon, daemon.name, ID, { scanTimeoutMs: 50 }));
    expect(error.kind).toBe('not_found');
  });

  it('waits for the notification when AuthResult is not ready at the first read, and reads it then', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    let reads = 0;
    const slow: BleTransport = {
      prepare: () => daemon.prepare(),
      scan: (onFound, onError) => daemon.scan(onFound, onError),
      async connect(id) {
        const real = await daemon.connect(id);
        return {
          get mtu() {
            return real.mtu;
          },
          // The first read after the write comes before the daemon has an answer.
          read: async (characteristic) => (characteristic === CHAR_AUTH_RESULT && reads++ === 0 ? new Uint8Array(0) : real.read(characteristic)),
          monitor: (characteristic, onValue, onError) => real.monitor(characteristic, onValue, onError),
          write: (characteristic, value) => real.write(characteristic, value),
          onDisconnected: (listener) => real.onDisconnected(listener),
          disconnect: () => real.disconnect(),
        };
      },
    };
    const session = await pair(slow, daemon.name, ID);
    expect(reads).toBe(2);
    expect((await session.status()).ok).toBe(true);
    await session.close();
  });

  it('is turned away as a second central while another phone is connected', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const first = await pair(daemon, daemon.name, ID);
    const error = await failure(pair(daemon, daemon.name, ID));
    expect(error.kind).toBe('disconnected');
    expect(daemon.refusalsInARow).toBe(0);
    expect((await first.status()).ok).toBe(true);
    await first.close();
    const second = await pair(daemon, daemon.name, ID);
    await second.close();
  });

  it('treats failing writes under a live link as a lost link, in plain words', async () => {
    // The daemon restarted; bluetoothd kept the link, so there is no disconnect.
    const daemon = new FakeDaemon({ pairingKey: ID });
    const session = await pair(daemon, daemon.name, ID);
    const closed = new Promise<LinkLostError | undefined>((resolve) => session.onClosed(resolve));
    daemon.restartService();
    const error = await session.wifiScan().catch((caught: unknown) => caught);
    expect(error).toBeInstanceOf(LinkLostError);
    expect((error as LinkLostError).message).toBe('The Bluetooth link to the Pi was lost.');
    expect((error as LinkLostError).message).not.toMatch(/Characteristic|2a7f|device/);
    expect((error as LinkLostError).detail).toMatch(/write failed/);
    expect((await closed)?.message).toBe('The Bluetooth link to the Pi was lost.');
    expect(session.isClosed).toBe(true);
    // Pairing again reaches the restarted daemon.
    const again = await pair(daemon, daemon.name, ID);
    expect((await again.status()).ok).toBe(true);
    await again.close();
  });

  it('treats a Pi that stops answering as a lost link', async () => {
    const clock = new ManualClock();
    const daemon = new FakeDaemon({ pairingKey: ID, clock });
    const session = await clock.run(pair(daemon, daemon.name, ID, { clock }));
    // Requests are taken but never answered.
    daemon.silent = true;
    const error = await clock.run(session.status().catch((caught: unknown) => caught));
    expect(error).toBeInstanceOf(LinkLostError);
    expect((error as Error).message).toBe('The Pi stopped answering over Bluetooth.');
    expect(session.isClosed).toBe(true);
  });

  it('closes at once on a message that does not verify, and says it is not a radio drop', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    let deliver: (value: Uint8Array) => void = () => {};
    const injecting: BleTransport = {
      prepare: () => daemon.prepare(),
      scan: (onFound, onError) => daemon.scan(onFound, onError),
      async connect(id) {
        const real = await daemon.connect(id);
        return {
          ...real,
          get mtu() {
            return real.mtu;
          },
          read: (characteristic) => real.read(characteristic),
          monitor: (characteristic, onValue, onError) => {
            if (characteristic === CHAR_RESPONSE) deliver = onValue;
            return real.monitor(characteristic, onValue, onError);
          },
          write: (characteristic, value) => real.write(characteristic, value),
          onDisconnected: (listener) => real.onDisconnected(listener),
          disconnect: () => real.disconnect(),
        };
      },
    };
    const session = await pair(injecting, daemon.name, ID);
    const closed = new Promise<LinkLostError | undefined>((resolve) => session.onClosed(resolve));
    // A last fragment holding a counter and a tag under no key the app has.
    deliver(new Uint8Array([0x01, ...Array<number>(8).fill(0), ...Array<number>(32).fill(0xaa)]));
    const reason = await closed;
    expect(reason?.unverified).toBe(true);
    expect(session.isClosed).toBe(true);
    expect(session.closedBy?.message).toMatch(/could not be verified/);
  });

  it('drops the connection on a replayed message', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const session = await pair(daemon, daemon.name, ID);
    await session.status();

    // A second sealer under the same key starts its counter at 0 again: the
    // wire it makes is a replay of the first request.
    const closed = new Promise<LinkLostError | undefined>((resolve) => session.onClosed(resolve));
    for (const piece of replayOfFirstRequest(session.connection.mtu)) await session.connection.write(CHAR_REQUEST, piece);
    const reason = await closed;
    // In plain words for the screen, with the daemon's reason kept behind it.
    expect(reason?.message).toBe('The Bluetooth link to the Pi was lost.');
    expect(reason?.detail).toMatch(/counter 0 already seen/);
    await expect(session.status()).rejects.toThrow(/closed/);
  });

  it('configures WiFi end to end and finishes', async () => {
    // The smallest link that can authenticate: 97-byte writes.
    const daemon = new FakeDaemon({ pairingKey: ID, mtu: 100 });
    const session = await pair(daemon, daemon.name, ID);

    const scan = await session.wifiScan();
    expect(scan.networks.map((n) => n.ssid)).toEqual(['Blackraven', 'Neighbours 5G', 'Cafe Guest', 'Printer-Setup']);
    expect(scan.networks[0].signal).toBeGreaterThanOrEqual(scan.networks[1].signal);

    await expect(session.wifiConfigure('Blackraven')).rejects.toBeInstanceOf(RequestError);
    await session.wifiConfigure('Blackraven', 'correct horse battery staple');
    expect(daemon.wifi).toEqual({ connected: true, ssid: 'Blackraven', ip: '192.168.1.42', join: { ssid: 'Blackraven', state: 'joined' } });

    const status = await session.status();
    expect(status.wifi).toEqual({ connected: true, ssid: 'Blackraven', ip: '192.168.1.42', join: { ssid: 'Blackraven', state: 'joined' } });

    await session.wifiForget();
    expect(daemon.wifi).toEqual({ connected: false });

    await session.wifiConfigure('Cafe Guest');
    expect(daemon.wifi.ssid).toBe('Cafe Guest');

    await session.finish();
    expect(daemon.setupMode).toBe(false);
    expect(daemon.advertising).toBe(false);
    await tick();
  });

  it('says so, without writing Auth, when the link is too small to authenticate', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID, mtu: 23 });
    const error = await failure(pair(daemon, daemon.name, ID));
    expect(error.kind).toBe('bluetooth');
    expect(error.message).toMatch(/20 bytes per write, and pairing needs 97/);
    expect(daemon.refusalsInARow).toBe(0);
  });

  it('serialises requests over a large MTU as well', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID, mtu: 185, latencyMs: 1 });
    const session = await pair(daemon, daemon.name, ID);
    const [a, b, c] = await Promise.all([session.status(), session.wifiScan(), session.status()]);
    expect(a.ok && b.ok && c.ok).toBe(true);
    await session.close();
  });

  it('reports how many networks a forget deleted, including none', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const session = await pair(daemon, daemon.name, ID);
    expect(await session.wifiForget()).toBe(0);
    await session.close();
  });

  it('surfaces the daemon error codes', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID, wifiFailure: 'NetworkManager refused: secrets were required' });
    const session = await pair(daemon, daemon.name, ID);
    try {
      await session.wifiConfigure('Blackraven', 'pw');
      throw new Error('expected failure');
    } catch (error) {
      expect(error).toBeInstanceOf(RequestError);
      expect((error as RequestError).code).toBe('wifi_failed');
      expect((error as RequestError).message).toMatch(/secrets/);
    }
    await session.close();
  });
});

/**
 * The daemon derived S from the SPAKE2 run of the real handshake; this test
 * cannot see them, but it does not need to: any wire the daemon has already
 * accepted at counter 0 is refused again by its counter alone, before the
 * tag is checked. So a fresh sealer under any key sends counter 0.
 */
function replayOfFirstRequest(mtu: number): Uint8Array[] {
  const anyKey = sessionKey(new Uint8Array(16));
  const sealed = appChannel(anyKey).sealer.seal(encodeRequest({ op: 'status' }));
  return fragment(sealed, mtu - 3);
}

describe('uninstall', () => {
  it('is answered, closes the session, and the Pi stops advertising', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const session = await pair(daemon, daemon.name, ID);
    await session.uninstall();
    expect(session.isClosed).toBe(true);
    expect(daemon.uninstalled).toBe(true);
    expect(daemon.advertising).toBe(false);
  });

  it('reports uninstall_unavailable and keeps the session', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID, uninstall: 'unavailable' });
    const session = await pair(daemon, daemon.name, ID);
    const error = await session.uninstall().then(
      () => undefined,
      (caught: unknown) => caught,
    );
    expect(error).toBeInstanceOf(RequestError);
    expect((error as RequestError).code).toBe('uninstall_unavailable');
    expect(session.isClosed).toBe(false);
    expect((await session.status()).ok).toBe(true);
    await session.close();
  });
});

describe('a daemon of another protocol', () => {
  it.each([3, 5])('is refused at protocol %i', async (protocol) => {
    const daemon = new FakeDaemon({ pairingKey: ID, protocol });
    const error = await failure(pair(daemon, daemon.name, ID));
    expect(error.kind).toBe('protocol');
  });
});

/**
 * The fake daemon's transport with faults put in its way: `prepare`, `scan` and `connect`
 * replaced, a characteristic's reads answered by the test, its notifications muted, and the
 * response monitor's callbacks kept so the test can deliver to them.
 */
function faulty(daemon: FakeDaemon, faults: Partial<BleTransport> & { reads?: Record<string, () => Promise<Uint8Array>>; mute?: string[] } = {}) {
  const taps: { deliver: (value: Uint8Array) => void; error: (error: Error) => void } = { deliver: () => {}, error: () => {} };
  const disconnects: string[] = [];
  const transport: BleTransport = {
    prepare: faults.prepare ?? (() => daemon.prepare()),
    scan: faults.scan ?? ((onFound, onError) => daemon.scan(onFound, onError)),
    connect:
      faults.connect ??
      (async (id) => {
        const real = await daemon.connect(id);
        return {
          get mtu() {
            return real.mtu;
          },
          read: (characteristic) => faults.reads?.[characteristic]?.() ?? real.read(characteristic),
          monitor: (characteristic, onValue, onError) => {
            if (faults.mute?.includes(characteristic)) return () => {};
            if (characteristic === CHAR_RESPONSE) {
              taps.deliver = onValue;
              taps.error = (error) => onError?.(error);
            }
            return real.monitor(characteristic, onValue, onError);
          },
          write: (characteristic, value) => real.write(characteristic, value),
          onDisconnected: (listener) => real.onDisconnected(listener),
          disconnect: () => {
            disconnects.push(id);
            return real.disconnect();
          },
        };
      }),
  };
  return { transport, taps, disconnects };
}

describe('pairing that fails on the way', () => {
  it('says what Bluetooth said when it is unavailable, and something plain otherwise', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const off = Object.assign(new Error('Bluetooth is off. Turn it on and try again.'), { name: 'BluetoothUnavailable' });
    let error = await failure(pair(faulty(daemon, { prepare: () => Promise.reject(off) }).transport, daemon.name, ID));
    expect([error.kind, error.message]).toEqual(['bluetooth', 'Bluetooth is off. Turn it on and try again.']);

    error = await failure(pair(faulty(daemon, { prepare: () => Promise.reject(new Error('BleManager destroyed')) }).transport, daemon.name, ID));
    expect([error.kind, error.message, error.detail]).toEqual([
      'bluetooth',
      'Bluetooth is not available. Check that it is on and that the app may use it.',
      'BleManager destroyed',
    ]);
  });

  it('reports a scan that fails', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const scan: BleTransport['scan'] = (_onFound, onError) => {
      queueMicrotask(() => onError(new Error('scan throttled')));
      return () => {};
    };
    const error = await failure(pair(faulty(daemon, { scan }).transport, daemon.name, ID));
    expect([error.kind, error.detail]).toEqual(['bluetooth', 'scan throttled']);
    expect(error.message).toMatch(/scanning failed/);
  });

  it('reports a connection that cannot be made', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const connect = () => Promise.reject(new Error('GATT 133'));
    const error = await failure(pair(faulty(daemon, { connect }).transport, daemon.name, ID));
    expect([error.kind, error.detail]).toEqual(['bluetooth', 'GATT 133']);
    expect(error.message).toMatch(/Could not connect/);
  });

  it('takes a Pi under another id when the one it had does not show up', async () => {
    const clock = new ManualClock();
    const daemon = new FakeDaemon({ pairingKey: ID, clock });
    const session = await clock.run(pair(daemon, daemon.name, ID, { clock, preferId: 'gone' }), 5_000, 500);
    expect(session.peripheral.name).toBe(daemon.name);
    await session.close();
  });

  it('refuses Info that is not RPi LED Config, and hangs up', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    for (const info of ['not json', '{"protocol":"4","version":"0.5.1"}', 'null']) {
      const { transport, disconnects } = faulty(daemon, { reads: { [CHAR_INFO]: async () => utf8Encode(info) } });
      const error = await failure(pair(transport, daemon.name, ID));
      expect([error.kind, error.message], info).toEqual(['protocol', 'The Pi answered with something that is not RPi LED Config.']);
      expect(disconnects).toHaveLength(1);
    }
  });

  it('refuses a challenge that is not a SPAKE2 share without writing Auth', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const { transport, disconnects } = faulty(daemon, { reads: { [CHAR_CHALLENGE]: async () => new Uint8Array(65) } });
    const error = await failure(pair(transport, daemon.name, ID));
    expect([error.kind, error.message]).toEqual(['protocol', "The Pi's challenge was not a valid SPAKE2 share."]);
    expect(disconnects).toHaveLength(1);
    expect(daemon.refusalsInARow).toBe(0);
  });

  it('gives up on an authentication the Pi does not answer', async () => {
    const clock = new ManualClock();
    const daemon = new FakeDaemon({ pairingKey: ID, clock });
    const { transport } = faulty(daemon, { reads: { [CHAR_AUTH_RESULT]: async () => new Uint8Array() }, mute: [CHAR_AUTH_RESULT] });
    const error = await clock.run(failure(pair(transport, daemon.name, ID, { clock, authTimeoutMs: 3_000 })), 10_000, 500);
    expect([error.kind, error.message]).toEqual(['timeout', 'The Pi did not answer the authentication.']);
  });

  it('refuses an authentication result of the wrong shape', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    for (const result of [new Uint8Array([AUTH_ACCEPTED]), new Uint8Array([0x02, ...new Uint8Array(32)])]) {
      const error = await failure(pair(faulty(daemon, { reads: { [CHAR_AUTH_RESULT]: async () => result } }).transport, daemon.name, ID));
      expect([error.kind, error.message]).toEqual(['protocol', 'The authentication result was malformed.']);
    }
  });

  it('calls a read that fails while pairing a lost link', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const { transport } = faulty(daemon, { reads: { [CHAR_CHALLENGE]: () => Promise.reject(new Error('read failed')) } });
    const error = await failure(pair(transport, daemon.name, ID));
    expect([error.kind, error.detail]).toEqual(['disconnected', 'read failed']);
    expect(error.message).toMatch(/lost while pairing/);
  });
});

describe('a session that fails', () => {
  it('ends on a notification error, once, however many follow', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const { transport, taps } = faulty(daemon);
    const session = await pair(transport, daemon.name, ID);
    const reasons: Array<LinkLostError | undefined> = [];
    session.onClosed((reason) => reasons.push(reason));
    taps.error(new Error('notify failed'));
    taps.error(new Error('notify failed again'));
    expect(reasons.map((reason) => [reason?.message, reason?.detail])).toEqual([['The Bluetooth link to the Pi was lost.', 'notify failed']]);
    expect(session.closedBy?.unverified).toBe(false);
  });

  it('ends on a fragment that breaks the framing, as an answer that did not verify', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const { transport, taps } = faulty(daemon);
    const session = await pair(transport, daemon.name, ID);
    taps.deliver(new Uint8Array([0x02, 1, 2, 3]));
    expect(session.isClosed).toBe(true);
    expect(session.closedBy?.unverified).toBe(true);
    expect(session.closedBy?.message).toMatch(/could not be verified/);
  });

  it('tells its listeners when it is closed on purpose, and not those that left', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const session = await pair(daemon, daemon.name, ID);
    const heard: string[] = [];
    session.onClosed((reason) => heard.push(`stayed ${String(reason)}`));
    session.onClosed(() => heard.push('left'))();
    await session.close();
    await session.close();
    expect(heard).toEqual(['stayed undefined']);
    expect(session.closedBy).toBeUndefined();
  });

  it('turns away a request once closed, and the one it was waiting on', async () => {
    const clock = new ManualClock();
    const daemon = new FakeDaemon({ pairingKey: ID, clock });
    const session = await clock.run(pair(daemon, daemon.name, ID, { clock }));
    daemon.silent = true;
    const waiting = session.status().catch((caught: unknown) => caught);
    await tick();
    await session.close();
    expect(await waiting).toBeInstanceOf(LinkLostError);
    expect(((await waiting) as Error).message).toBe('The link to the Pi is closed.');
  });
});

describe('parseInfo', () => {
  it('keeps what the LED is doing only when it is one of the three states', () => {
    expect(parseInfo(utf8Encode('{"protocol":4,"version":"0.5.1","led":"blinking"}'))).toEqual({ protocol: 4, version: '0.5.1', led: 'blinking' });
    expect(parseInfo(utf8Encode('{"protocol":4,"version":"0.5.1","led":"on"}'))).toEqual({ protocol: 4, version: '0.5.1' });
  });
});
