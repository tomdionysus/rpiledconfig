// A daemon in TypeScript: the BLE service of docs/protocol.md, behind the
// BleTransport interface, with a fake WiFi backend. It is what the pairing
// tests run against, and
// it is the reference the daemon's behaviour is compared with.
//
// Time goes through a Clock, so the protocol's limits (30 s to authenticate,
// a 60 s advertising pause) and the radio's habits (a dropped link while the
// Pi joins a network, a join that takes a while or never happens) can be
// stepped through in a test.
import { acceptAuth } from '@/protocol/auth';
import { concat, utf8Encode } from '@/protocol/bytes';
import { ChannelError, daemonChannel, type Opener, type Sealer } from '@/protocol/channel';
import {
  AUTH_ACCEPTED,
  AUTH_REFUSED,
  AUTH_TIMEOUT_MS,
  BLE_NAME_PREFIX,
  CHAR_AUTH,
  CHAR_AUTH_RESULT,
  CHAR_BLINK,
  CHAR_CHALLENGE,
  CHAR_INFO,
  CHAR_REQUEST,
  CHAR_RESPONSE,
  BLINK_PROTOCOL_VERSION,
  BLINK_START,
  BLINK_STOP,
  BLINK_TIMEOUT_MS,
  PROTOCOL_VERSION,
  REFUSALS_PER_CONNECTION,
} from '@/protocol/constants';
import { FragmentError, fragment, fragmentSizeForMtu, Reassembler } from '@/protocol/fragments';
import { fingerprint, passwordScalar, sessionKey } from '@/protocol/keys';
import { randomScalar, share } from '@/protocol/spake2';
import { decodeRequest, encodeResponse, type JoinState, type Network, type Request, type Response, type WifiStatus } from '@/protocol/messages';
import { realClock, type Clock } from '@/util/clock';
import type { BleConnection, BleTransport, Peripheral } from './transport';

/** What the Pi's shared radio does to the BLE link when it joins a network. */
export interface LinkDrop {
  /**
   * `after_response`: the `ok` for `wifi.configure` goes out, then the link
   * drops. `before_response`: the link drops first and the `ok` is lost,
   * although the network was saved and activation asked for.
   */
  when: 'after_response' | 'before_response';
  /** How long the Pi stays off the air (not advertising) after the drop. */
  offAirMs: number;
}

export interface FakeDaemonOptions {
  machineId: Uint8Array;
  hostname?: string;
  version?: string;
  networks?: Network[];
  wifi?: WifiStatus;
  /** The ATT MTU of every connection; default 185, what the A85 settled on. */
  mtu?: number;
  /** Milliseconds a notification takes to arrive; tests leave it at 0. */
  latencyMs?: number;
  /** When set, every authentication is refused regardless of cA. */
  refuseAll?: boolean;
  /** When set, `wifi.configure` fails the way NetworkManager would. */
  wifiFailure?: string;
  clock?: Clock;
  /** How long an unauthenticated connection is kept; the protocol says 30 s. */
  authTimeoutMs?: number;
  /** The passphrase each network wants. A network not listed takes any (a secured one any non-empty). */
  passphrases?: Record<string, string>;
  /**
   * How long after `wifi.configure` the outcome is known: joined, or with a
   * wrong passphrase back to whatever it was on before. `never` leaves it
   * disconnected and trying. Default 0, at once.
   */
  joinAfterMs?: number | 'never';
  /** Drop the link after `wifi.configure`, as the Pi's radio may. */
  linkDrop?: LinkDrop;
  /** Whether this Pi can do Wi-Fi Easy Connect; `unavailable` answers `easyconnect_unavailable`. Default available. */
  easyConnect?: 'available' | 'unavailable';
  /** Whether `uninstall` works; `unavailable` answers `uninstall_unavailable`. Default available. */
  uninstall?: 'available' | 'unavailable';
  /** The protocol Info says; 2 is a daemon without Blink that blinks all through setup mode. Default 3. */
  protocol?: number;
}

/** How long the Pi listens for Android's configurator, as the protocol says. */
export const EASY_CONNECT_LISTEN_MS = 120_000;
/** The fake's bootstrap URI: channels 81/1, 81/6, 81/11, a MAC, version 2, a public key. */
export const FAKE_DPP_URI = 'DPP:C:81/1,81/6,81/11;M:dca632a1b2c3;V:2;K:MDkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDIgACUHhFXH4Yl3EQhMJmcpIXdeUvAtZYn6hb6TJsfu2ybks=;;';

const DEFAULT_NETWORKS: Network[] = [
  { ssid: 'Blackraven', signal: 82, secure: true },
  { ssid: 'Neighbours 5G', signal: 61, secure: true },
  { ssid: 'Cafe Guest', signal: 44, secure: false },
  { ssid: 'Printer-Setup', signal: 20, secure: true },
];

export const FAKE_IP = '192.168.1.42';

/** Five refusals in a row pause advertising, as the protocol says. */
const REFUSALS_BEFORE_PAUSE = 5;
export const ADVERTISING_PAUSE_MS = 60_000;

/**
 * The `wifi.scan` answer from what the radio heard: hidden networks left out,
 * one entry per SSID (the strongest), signal clamped to 0..100, strongest
 * first, ties by name.
 */
export function scanList(heard: readonly Network[]): Network[] {
  const best = new Map<string, Network>();
  for (const network of heard) {
    if (network.ssid.length === 0) continue;
    const entry = { ...network, signal: Math.max(0, Math.min(100, Math.round(network.signal))) };
    const seen = best.get(entry.ssid);
    if (!seen || entry.signal > seen.signal) best.set(entry.ssid, entry);
  }
  return [...best.values()].sort((a, b) => b.signal - a.signal || (a.ssid < b.ssid ? -1 : a.ssid > b.ssid ? 1 : 0));
}

type Listener = (value: Uint8Array) => void;

interface Scanner {
  prefix: string;
  onFound(peripheral: Peripheral): void;
}

export class FakeDaemon implements BleTransport {
  readonly id = 'fake-pi';
  readonly fingerprint: string;
  readonly name: string;
  hostname: string;
  version: string;
  networks: Network[];
  wifi: WifiStatus;
  refuseAll: boolean;
  wifiFailure?: string;
  passphrases?: Record<string, string>;
  joinAfterMs: number | 'never';
  linkDrop?: LinkDrop;
  easyConnectMode: 'available' | 'unavailable';
  uninstallMode: 'available' | 'unavailable';
  readonly protocol: number;
  /** Whether the LED is blinking the ID: asked for by Blink (always, at protocol 2, in setup mode). */
  blinking = false;
  /** Set by `uninstall`: the package is gone, and nothing answers any more. */
  uninstalled = false;
  private cancelBlink?: () => void;
  /** Whether the Pi is listening for an Easy Connect configurator now. */
  easyConnectListening = false;
  private cancelListen?: () => void;
  readonly clock: Clock;
  readonly authTimeoutMs: number;
  /** Setup mode: advertising and answering. `finish` ends it. */
  setupMode = true;
  /** Refused attempts in a row across connections; an accepted one, or the end of a pause, resets it. */
  refusalsInARow = 0;
  advertisingPausedUntil = 0;
  /** While the radio is busy joining a network, nothing is advertised. */
  offAirUntil = 0;
  /** Every connect call, including refused second centrals. */
  connections = 0;
  /** A daemon stuck somewhere: requests are taken and never answered. */
  silent = false;
  /** Connections dropped by the daemon, with the reason, for tests to read. */
  readonly drops: string[] = [];
  private readonly key: Uint8Array;
  private readonly mtu: number;
  private readonly latencyMs: number;
  private current?: FakeConnection;
  private readonly scanners = new Set<Scanner>();
  private cancelJoin?: () => void;

  constructor(options: FakeDaemonOptions) {
    this.key = passwordScalar(options.machineId);
    this.fingerprint = fingerprint(options.machineId);
    this.name = `${BLE_NAME_PREFIX}${this.fingerprint}`;
    this.hostname = options.hostname ?? 'raspberrypi';
    this.version = options.version ?? '0.1.0';
    this.networks = options.networks ?? DEFAULT_NETWORKS;
    this.wifi = options.wifi ?? { connected: false };
    this.mtu = options.mtu ?? 185;
    this.latencyMs = options.latencyMs ?? 0;
    this.refuseAll = options.refuseAll ?? false;
    this.wifiFailure = options.wifiFailure;
    this.passphrases = options.passphrases;
    this.joinAfterMs = options.joinAfterMs ?? 0;
    this.linkDrop = options.linkDrop;
    this.easyConnectMode = options.easyConnect ?? 'available';
    this.uninstallMode = options.uninstall ?? 'available';
    this.protocol = options.protocol ?? PROTOCOL_VERSION;
    this.blinking = this.protocol < BLINK_PROTOCOL_VERSION;
    this.clock = options.clock ?? realClock;
    this.authTimeoutMs = options.authTimeoutMs ?? AUTH_TIMEOUT_MS;
  }

  get advertising(): boolean {
    const now = this.clock.now();
    return this.setupMode && !this.uninstalled && now >= this.advertisingPausedUntil && now >= this.offAirUntil;
  }

  /** Whether a central is connected now. */
  get connected(): boolean {
    return this.current?.isOpen ?? false;
  }

  async prepare(): Promise<void> {}

  scan(namePrefix: string, onFound: (peripheral: Peripheral) => void, _onError?: (error: Error) => void): () => void {
    const scanner: Scanner = { prefix: namePrefix, onFound };
    this.scanners.add(scanner);
    this.later(() => this.announceTo(scanner));
    return () => {
      this.scanners.delete(scanner);
    };
  }

  async connect(id: string): Promise<BleConnection> {
    if (id !== this.id || !this.advertising) throw new Error('Device not found');
    this.connections += 1;
    if (this.current?.isOpen) {
      // One central at a time: the second is disconnected without a challenge.
      this.drops.push('second central');
      return new FakeConnection(this, this.key, this.mtu, (fn) => this.later(fn), true);
    }
    const connection = new FakeConnection(this, this.key, this.mtu, (fn) => this.later(fn), false);
    this.current = connection;
    return connection;
  }

  /** The daemon's answer to one decrypted request. */
  handle(request: Request): Response {
    switch (request.op) {
      case 'status':
        return { ok: true, hostname: this.hostname, wifi: { ...this.wifi } };
      case 'wifi.scan':
        return { ok: true, networks: scanList(this.networks) };
      case 'wifi.configure':
        return this.configure(request.ssid, request.psk);
      case 'wifi.forget':
        this.cancelJoin?.();
        this.wifi = { connected: false };
        return { ok: true };
      case 'wifi.easyconnect':
        if (this.easyConnectMode === 'unavailable') {
          return { ok: false, error: 'easyconnect_unavailable', message: 'wpa_supplicant on this Pi cannot do Easy Connect.' };
        }
        this.stopListening();
        this.easyConnectListening = true;
        this.cancelListen = this.clock.after(EASY_CONNECT_LISTEN_MS, () => this.stopListening());
        return { ok: true, uri: FAKE_DPP_URI };
      case 'wifi.easyconnect.stop':
        this.stopListening();
        return { ok: true };
      case 'finish':
        this.setupMode = false;
        if (this.protocol < BLINK_PROTOCOL_VERSION) this.blinking = false;
        return { ok: true };
      case 'uninstall':
        if (this.uninstallMode === 'unavailable') {
          return { ok: false, error: 'uninstall_unavailable', message: 'rpiledconfig-uninstall.service does not exist: this daemon was not installed from the package' };
        }
        this.uninstalled = true;
        this.stopBlinking();
        return { ok: true };
    }
  }

  /** A write of Blink (docs/protocol.md): 0x01 blinks, or restarts the clock; 0x00 stops; anything else is ignored. */
  blink(value: Uint8Array): void {
    if (value.length !== 1) return;
    if (value[0] === BLINK_STOP) this.stopBlinking();
    if (value[0] !== BLINK_START) return;
    this.blinking = true;
    this.cancelBlink?.();
    this.cancelBlink = this.clock.after(BLINK_TIMEOUT_MS, () => this.stopBlinking());
  }

  stopBlinking(): void {
    if (this.protocol < BLINK_PROTOCOL_VERSION) return;
    this.cancelBlink?.();
    this.cancelBlink = undefined;
    this.blinking = false;
  }

  /**
   * Android's configurator, after the user confirmed: it sends the network
   * to a listening Pi, which applies it as `wifi.configure` would, and the
   * shared radio may drop the link as it joins. False if the Pi was not
   * listening.
   */
  easyConnectDeliver(ssid: string, psk?: string): boolean {
    if (!this.easyConnectListening) return false;
    this.stopListening();
    const response = this.configure(ssid, psk);
    if (response.ok && this.linkDrop) this.current?.radioDrop(this.linkDrop.offAirMs);
    return response.ok;
  }

  private stopListening(): void {
    this.cancelListen?.();
    this.cancelListen = undefined;
    this.easyConnectListening = false;
  }

  /** A refusal counted; after five in a row advertising pauses for a minute. */
  refused(): void {
    this.endPauseIfOver();
    this.refusalsInARow += 1;
    if (this.refusalsInARow >= REFUSALS_BEFORE_PAUSE && this.advertisingPausedUntil === 0) {
      this.advertisingPausedUntil = this.clock.now() + ADVERTISING_PAUSE_MS;
      this.clock.after(ADVERTISING_PAUSE_MS, () => {
        this.endPauseIfOver();
        this.announce();
      });
    }
  }

  accepted(): void {
    this.refusalsInARow = 0;
    this.stopBlinking();
  }

  /**
   * The daemon process restarts while bluetoothd keeps the LE link up: the
   * phone gets no disconnect, only reads and writes that fail, as a real
   * BlueZ does when the GATT application goes away under a live link.
   */
  restartService(): void {
    this.current?.orphan();
    this.current = undefined;
  }

  /** Called by a connection when it closes, from either end. */
  closed(connection: FakeConnection, reason: string | undefined): void {
    if (reason) this.drops.push(reason);
    if (this.current === connection) {
      this.current = undefined;
      // Listening stops when the connection that asked for it closes.
      this.stopListening();
    }
  }

  /** The radio goes to the WiFi side: the link is gone and advertising stops for a while. */
  goOffAir(offAirMs: number): void {
    this.offAirUntil = this.clock.now() + offAirMs;
    this.clock.after(offAirMs, () => this.announce());
  }

  later(fn: () => void): void {
    if (this.latencyMs > 0) this.clock.after(this.latencyMs, fn);
    else queueMicrotask(fn);
  }

  private configure(ssid: string, psk: string | undefined): Response {
    if (this.wifiFailure) return { ok: false, error: 'wifi_failed', message: this.wifiFailure };
    const network = this.networks.find((entry) => entry.ssid === ssid);
    if (network?.secure && !psk) return { ok: false, error: 'wifi_failed', message: 'A passphrase is required for this network.' };

    this.cancelJoin?.();
    this.cancelJoin = undefined;
    const before = this.wifi.connected && this.wifi.ssid !== ssid ? { ...this.wifi } : { connected: false };
    const wanted = this.passphrases?.[ssid];
    // As NetworkManager reports it (docs/protocol.md, The latest join): a name nothing
    // broadcasts is not found; a passphrase the network refuses is wrong.
    const failure: JoinState | undefined = !network
      ? { ssid, state: 'failed', reason: 'not_found', message: 'NetworkManager: SSID not found' }
      : wanted !== undefined && wanted !== (psk ?? '')
        ? { ssid, state: 'failed', reason: 'wrong_passphrase', message: 'NetworkManager: no secrets' }
        : undefined;
    const outcome = (): WifiStatus => (failure ? { ...before, join: failure } : { connected: true, ssid, ip: FAKE_IP, join: { ssid, state: 'joined' } });
    const joining: JoinState = { ssid, state: 'joining' };

    if (this.joinAfterMs === 'never') {
      this.wifi = { connected: false, join: joining };
    } else if (this.joinAfterMs <= 0) {
      this.wifi = outcome();
    } else {
      // Activating: the old connection is down while the new one is tried.
      this.wifi = { connected: false, join: joining };
      this.cancelJoin = this.clock.after(this.joinAfterMs, () => {
        this.cancelJoin = undefined;
        this.wifi = outcome();
      });
    }
    return { ok: true };
  }

  private endPauseIfOver(): void {
    if (this.advertisingPausedUntil !== 0 && this.clock.now() >= this.advertisingPausedUntil) {
      this.advertisingPausedUntil = 0;
      this.refusalsInARow = 0;
    }
  }

  private announce(): void {
    for (const scanner of this.scanners) this.later(() => this.announceTo(scanner));
  }

  private announceTo(scanner: Scanner): void {
    if (!this.scanners.has(scanner) || !this.advertising || !this.name.startsWith(scanner.prefix)) return;
    scanner.onFound({ id: this.id, name: this.name, rssi: -55 });
  }
}

class FakeConnection implements BleConnection {
  readonly mtu: number;
  /** This attempt's y and the pB made from it, which Challenge returns. */
  private y: Uint8Array = new Uint8Array(0);
  private pB: Uint8Array = new Uint8Array(0);
  /** Empty until the first attempt on this connection, as the protocol says. */
  private authResult: Uint8Array = new Uint8Array(0);
  private authenticated = false;
  private refusals = 0;
  private sealer?: Sealer;
  private opener?: Opener;
  private readonly reassembler = new Reassembler();
  private readonly listeners = new Map<string, Set<Listener>>();
  private readonly disconnectListeners = new Set<(reason: Error | undefined) => void>();
  private open: boolean;
  /** The service went away under the link: everything fails, nothing disconnects. */
  private orphaned = false;
  private droppedFor?: Error;
  private cancelAuthTimer: () => void;

  constructor(
    private readonly daemon: FakeDaemon,
    private readonly key: Uint8Array,
    mtu: number,
    private readonly later: (fn: () => void) => void,
    secondCentral: boolean,
  ) {
    this.mtu = mtu;
    this.renewChallenge();
    if (secondCentral) {
      this.open = false;
      this.droppedFor = new Error('The Pi is busy with another phone.');
      this.cancelAuthTimer = () => {};
      return;
    }
    this.open = true;
    this.cancelAuthTimer = this.startAuthTimer();
  }

  /** A new y for every pB: a y used twice gives the password away. */
  private renewChallenge(): void {
    this.y = randomScalar();
    this.pB = share('b', this.key, this.y);
  }

  private startAuthTimer(): () => void {
    return this.daemon.clock.after(this.daemon.authTimeoutMs, () => {
      if (!this.authenticated) this.drop(new Error('not authenticated in time'));
    });
  }

  /**
   * A Challenge read on an authenticated connection: an app that started
   * again on a link the phone kept open. Forget S, draw a new y and pB, and wait
   * for Auth as on a new connection (docs/protocol.md, Authentication).
   */
  private startOver(): void {
    this.authenticated = false;
    this.sealer = undefined;
    this.opener = undefined;
    this.reassembler.reset();
    this.authResult = new Uint8Array(0);
    this.refusals = 0;
    this.renewChallenge();
    this.cancelAuthTimer();
    this.cancelAuthTimer = this.startAuthTimer();
  }

  get isOpen(): boolean {
    return this.open && !this.orphaned;
  }

  orphan(): void {
    this.orphaned = true;
    this.cancelAuthTimer();
  }

  async read(characteristic: string): Promise<Uint8Array> {
    this.assertOpen();
    switch (characteristic) {
      case CHAR_INFO:
        return utf8Encode(JSON.stringify({ protocol: this.daemon.protocol, version: this.daemon.version, fingerprint: this.daemon.fingerprint }));
      case CHAR_CHALLENGE:
        if (this.authenticated) this.startOver();
        return Uint8Array.from(this.pB);
      case CHAR_AUTH_RESULT:
        return Uint8Array.from(this.authResult);
      default:
        throw new Error(`characteristic ${characteristic} is not readable`);
    }
  }

  async write(characteristic: string, value: Uint8Array): Promise<void> {
    this.assertOpen();
    switch (characteristic) {
      case CHAR_AUTH:
        this.auth(value);
        return;
      case CHAR_REQUEST:
        this.request(value);
        return;
      case CHAR_BLINK:
        if (this.daemon.protocol < BLINK_PROTOCOL_VERSION) throw new Error(`characteristic ${characteristic} is not writable`);
        this.daemon.blink(value);
        return;
      default:
        throw new Error(`characteristic ${characteristic} is not writable`);
    }
  }

  monitor(characteristic: string, onValue: Listener): () => void {
    if (characteristic !== CHAR_AUTH_RESULT && characteristic !== CHAR_RESPONSE) throw new Error(`characteristic ${characteristic} does not notify`);
    let set = this.listeners.get(characteristic);
    if (!set) {
      set = new Set();
      this.listeners.set(characteristic, set);
    }
    set.add(onValue);
    return () => set.delete(onValue);
  }

  onDisconnected(listener: (reason: Error | undefined) => void): () => void {
    if (!this.open && this.droppedFor) {
      // Already gone (a refused second central): say so once.
      const reason = this.droppedFor;
      this.later(() => listener(reason));
      return () => {};
    }
    this.disconnectListeners.add(listener);
    return () => this.disconnectListeners.delete(listener);
  }

  async disconnect(): Promise<void> {
    if (!this.open) return;
    this.open = false;
    this.cancelAuthTimer();
    this.listeners.clear();
    this.disconnectListeners.clear();
    this.daemon.closed(this, undefined);
  }

  private auth(value: Uint8Array): void {
    const keys = acceptAuth(this.key, this.y, this.pB, value);
    if (!keys || this.daemon.refuseAll) {
      this.authResult = new Uint8Array([AUTH_REFUSED]);
      this.renewChallenge();
      this.refusals += 1;
      this.daemon.refused();
      this.notifyChanged(CHAR_AUTH_RESULT, this.authResult);
      if (this.refusals >= REFUSALS_PER_CONNECTION) this.drop(new Error('too many refused attempts'));
      return;
    }
    const s = sessionKey(keys.ke);
    const channel = daemonChannel(s);
    this.sealer = channel.sealer;
    this.opener = channel.opener;
    this.authResult = concat(new Uint8Array([AUTH_ACCEPTED]), keys.cB);
    this.authenticated = true;
    this.cancelAuthTimer();
    this.daemon.accepted();
    // Renewed after every Auth write, as the protocol says.
    this.renewChallenge();
    this.notifyChanged(CHAR_AUTH_RESULT, this.authResult);
  }

  private request(piece: Uint8Array): void {
    if (!this.authenticated || !this.opener || !this.sealer) {
      this.drop(new Error('request before authentication'));
      return;
    }
    let wire: Uint8Array | undefined;
    try {
      wire = this.reassembler.push(piece);
    } catch (error) {
      this.drop(error instanceof FragmentError ? error : new Error('bad fragment'));
      return;
    }
    if (!wire) return;
    let plaintext: Uint8Array;
    try {
      plaintext = this.opener.open(wire);
    } catch (error) {
      this.drop(error instanceof ChannelError ? error : new Error('bad message'));
      return;
    }
    if (this.daemon.silent) return;
    const decoded = decodeRequest(plaintext);
    const response: Response = 'request' in decoded ? this.daemon.handle(decoded.request) : { ok: false, error: decoded.error, message: decoded.message };
    const op = 'request' in decoded ? decoded.request.op : undefined;
    const drop = op === 'wifi.configure' && response.ok ? this.daemon.linkDrop : undefined;

    if (drop?.when === 'before_response') {
      this.radioDrop(drop.offAirMs);
      return;
    }
    const sealed = this.sealer.seal(encodeResponse(response));
    for (const part of fragment(sealed, fragmentSizeForMtu(this.mtu))) this.notify(CHAR_RESPONSE, part);
    if (drop) this.later(() => this.radioDrop(drop.offAirMs));
    else if (op === 'finish' || (op === 'uninstall' && response.ok)) this.later(() => this.drop(undefined));
  }

  radioDrop(offAirMs: number): void {
    this.drop(new Error('The link dropped while the radio joined the network.'));
    this.daemon.goOffAir(offAirMs);
  }

  /**
   * A notification of a value longer than one notification carries: BlueZ
   * truncates it to MTU - 3 bytes, so it only says the value changed
   * (docs/protocol.md, Authentication).
   */
  private notifyChanged(characteristic: string, value: Uint8Array): void {
    this.notify(characteristic, value.subarray(0, fragmentSizeForMtu(this.mtu)));
  }

  private notify(characteristic: string, value: Uint8Array): void {
    const copy = Uint8Array.from(value);
    this.later(() => {
      if (!this.open) return;
      for (const listener of this.listeners.get(characteristic) ?? []) listener(copy);
    });
  }

  private drop(reason: Error | undefined): void {
    if (!this.open) return;
    this.open = false;
    this.droppedFor = reason;
    this.cancelAuthTimer();
    const listeners = [...this.disconnectListeners];
    this.listeners.clear();
    this.disconnectListeners.clear();
    this.daemon.closed(this, reason?.message ?? 'finished');
    this.later(() => listeners.forEach((listener) => listener(reason)));
  }

  private assertOpen(): void {
    if (!this.open) throw new Error('Device disconnected');
    // ble-plx's wording for a GATT operation on a service that has gone.
    if (this.orphaned) throw new Error(`Characteristic write failed for device ${this.daemon.id}`);
  }
}

/** The ID the fake daemon answers to when nothing else is chosen: the vectors' one. */
export const FAKE_MACHINE_ID_HEX = '012345';
