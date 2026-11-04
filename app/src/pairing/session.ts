// The app's side of docs/protocol.md after the LED: find the Pi whose
// fingerprint matches the ID, check Info, run SPAKE2, derive the session key,
// and then carry requests over the encrypted channel in fragments. Transport-agnostic: the same code runs over react-native-ble-plx
// and over the fake daemon in tests.
import type { BleConnection, BleTransport, Peripheral } from '@/ble/transport';
import { appAuth, verifyConfirmation, type AppAuth } from '@/protocol/auth';
import { utf8Decode } from '@/protocol/bytes';
import { appChannel, ChannelError, type Opener, type Sealer } from '@/protocol/channel';
import {
  AUTH_ACCEPTED,
  AUTH_REFUSED,
  BLE_NAME_PREFIX,
  CHAR_AUTH,
  CHAR_AUTH_RESULT,
  CHAR_CHALLENGE,
  CHAR_INFO,
  CHAR_REQUEST,
  CHAR_RESPONSE,
  AUTH_RESULT_ACCEPTED_LENGTH,
  MIN_AUTH_MTU,
  MIN_PROTOCOL_VERSION,
  PROTOCOL_VERSION,
} from '@/protocol/constants';
import { FragmentError, fragment, fragmentSizeForMtu, Reassembler } from '@/protocol/fragments';
import { fingerprint, passwordScalar, sessionKey } from '@/protocol/keys';
import { Spake2Error } from '@/protocol/spake2';
import {
  decodeResponse,
  encodeRequest,
  MessageError,
  type EasyConnectResponse,
  type ErrorResponse,
  type Request,
  type Response,
  type StatusResponse,
  type WifiScanResponse,
} from '@/protocol/messages';
import { realClock, type Clock } from '@/util/clock';

export type PairingPhase = 'scanning' | 'connecting' | 'checking' | 'authenticating' | 'ready';

export interface DaemonInfo {
  protocol: number;
  version: string;
  fingerprint: string;
}

export type PairingErrorKind = 'bluetooth' | 'not_found' | 'fingerprint_mismatch' | 'protocol' | 'refused' | 'disconnected' | 'timeout';

/** A raw platform error's text, kept for diagnostics and never shown as the message. */
export function detailOf(cause: unknown): string | undefined {
  if (cause === undefined) return undefined;
  return cause instanceof Error ? cause.message : String(cause);
}

export class PairingError extends Error {
  readonly detail?: string;

  constructor(
    readonly kind: PairingErrorKind,
    message: string,
    cause?: unknown,
  ) {
    super(message);
    this.detail = detailOf(cause);
  }
}

/**
 * The session's link is gone or cannot be trusted: a disconnect, a GATT
 * read or write that failed (a daemon restarted under a link bluetoothd
 * keeps up gives no disconnect, only failing writes), a Pi that stopped
 * answering, or an answer that did not verify. The message is for a person;
 * the platform's own text is in `detail`.
 */
export class LinkLostError extends Error {
  readonly detail?: string;

  constructor(message: string, cause?: unknown) {
    super(message);
    this.detail = detailOf(cause);
  }
}

const LINK_LOST = 'The Bluetooth link to the Pi was lost.';
const LINK_SILENT = 'The Pi stopped answering over Bluetooth.';
const LINK_UNVERIFIED = "The Pi's answer could not be verified, so the link was dropped.";
const LINK_CLOSED = 'The link to the Pi is closed.';

export interface PairOptions {
  onPhase?(phase: PairingPhase, detail?: string): void;
  scanTimeoutMs?: number;
  authTimeoutMs?: number;
  clock?: Clock;
  /**
   * The platform id the Pi had last time. A peripheral with the right name
   * and this id is taken at once; one with the right name and another id is
   * taken only if this one has not shown up within a moment (Android may see
   * a new address, and the name alone is only 16 bits of fingerprint; the
   * authentication is what proves it is the same Pi).
   */
  preferId?: string;
}

const DEFAULT_SCAN_TIMEOUT_MS = 20_000;
const DEFAULT_AUTH_TIMEOUT_MS = 10_000;
const REQUEST_TIMEOUT_MS = 20_000;
/** How long a same-named peripheral with another id waits for the preferred one. */
const PREFERRED_ID_GRACE_MS = 1_500;

/** Wait for the peripheral advertising as `name`, or give up. */
function findPeripheral(transport: BleTransport, name: string, timeoutMs: number, clock: Clock, preferId?: string): Promise<Peripheral> {
  return new Promise((resolve, reject) => {
    let done = false;
    let fallback: Peripheral | undefined;
    let cancelGrace: (() => void) | undefined;
    const finish = (fn: () => void) => {
      if (done) return;
      done = true;
      cancelTimeout();
      cancelGrace?.();
      stop();
      fn();
    };
    const cancelTimeout = clock.after(timeoutMs, () =>
      finish(() =>
        fallback ? resolve(fallback) : reject(new PairingError('not_found', `No Pi advertising as ${name} was found. Is it in setup mode and within range?`)),
      ),
    );
    const stop = transport.scan(
      name,
      (peripheral) => {
        if (peripheral.name !== name) return;
        if (!preferId || peripheral.id === preferId) {
          finish(() => resolve(peripheral));
          return;
        }
        if (!fallback) {
          fallback = peripheral;
          cancelGrace = clock.after(PREFERRED_ID_GRACE_MS, () => finish(() => resolve(peripheral)));
        }
      },
      (error) => finish(() => reject(new PairingError('bluetooth', 'Bluetooth scanning failed. Check that Bluetooth is on and try again.', error))),
    );
  });
}

export function parseInfo(bytes: Uint8Array): DaemonInfo {
  let parsed: unknown;
  try {
    parsed = JSON.parse(utf8Decode(bytes));
  } catch {
    throw new PairingError('protocol', 'The Pi answered with something that is not RPiLEDConfig.');
  }
  const record = parsed as Record<string, unknown> | null;
  if (!record || typeof record.protocol !== 'number' || typeof record.version !== 'string' || typeof record.fingerprint !== 'string') {
    throw new PairingError('protocol', 'The Pi answered with something that is not RPiLEDConfig.');
  }
  return { protocol: record.protocol, version: record.version, fingerprint: record.fingerprint };
}

/** A daemon this app can talk to: protocol 2 (no Blink) or 3. */
export function checkProtocol(info: DaemonInfo): void {
  if (info.protocol < MIN_PROTOCOL_VERSION || info.protocol > PROTOCOL_VERSION) {
    throw new PairingError('protocol', `The Pi speaks protocol ${info.protocol}; this app speaks ${MIN_PROTOCOL_VERSION} to ${PROTOCOL_VERSION}.`);
  }
}

function withTimeout<T>(promise: Promise<T>, ms: number, error: () => Error, clock: Clock): Promise<T> {
  return new Promise((resolve, reject) => {
    const cancel = clock.after(ms, () => reject(error()));
    promise.then(
      (value) => {
        cancel();
        resolve(value);
      },
      (reason) => {
        cancel();
        reject(reason);
      },
    );
  });
}

/** What a session needs to find, reach and prove itself to the same Pi again. */
interface SessionOrigin {
  transport: BleTransport;
  idBytes: Uint8Array;
  peripheral: Peripheral;
  clock: Clock;
}

/**
 * Pair with the Pi whose LED gave `idBytes`. Resolves to a session over the
 * encrypted channel, or rejects with a `PairingError` saying which step
 * failed; the connection is closed on any failure.
 */
export async function pair(transport: BleTransport, idBytes: Uint8Array, options: PairOptions = {}): Promise<Session> {
  const expected = fingerprint(idBytes);
  const name = `${BLE_NAME_PREFIX}${expected}`;
  const phase = (p: PairingPhase, detail?: string) => options.onPhase?.(p, detail);
  const clock = options.clock ?? realClock;

  phase('scanning', name);
  try {
    await transport.prepare();
  } catch (error) {
    throw new PairingError('bluetooth', error instanceof Error && error.name === 'BluetoothUnavailable' ? error.message : 'Bluetooth is not available. Check that it is on and that the app may use it.', error);
  }
  const peripheral = await findPeripheral(transport, name, options.scanTimeoutMs ?? DEFAULT_SCAN_TIMEOUT_MS, clock, options.preferId);

  phase('connecting', peripheral.name);
  let connection: BleConnection;
  try {
    connection = await transport.connect(peripheral.id);
  } catch (error) {
    throw new PairingError('bluetooth', 'Could not connect to the Pi over Bluetooth. Move closer and try again.', error);
  }

  try {
    phase('checking');
    const info = parseInfo(await connection.read(CHAR_INFO));
    checkProtocol(info);
    if (info.fingerprint !== expected) {
      throw new PairingError(
        'fingerprint_mismatch',
        `The Pi's fingerprint is ${info.fingerprint}, but the LED you read gives ${expected}. It is not the same Pi.`,
      );
    }

    phase('authenticating');
    // Writes are never long (docs/protocol.md), and pA || cA is 97 bytes.
    if (connection.mtu < MIN_AUTH_MTU) {
      throw new PairingError(
        'bluetooth',
        `This phone's Bluetooth link to the Pi carries ${connection.mtu - 3} bytes per write, and pairing needs ${MIN_AUTH_MTU - 3}.`,
      );
    }
    const w = passwordScalar(idBytes);
    const pB = await connection.read(CHAR_CHALLENGE);
    let auth: AppAuth;
    try {
      auth = appAuth(w, pB);
    } catch (error) {
      if (error instanceof Spake2Error) throw new PairingError('protocol', "The Pi's challenge was not a valid SPAKE2 share.", error);
      throw error;
    }

    // AuthResult's notification only says that it changed: 0x01 || cB is
    // 33 bytes and a notification at the default MTU carries 20, so its
    // payload is never used. The value is read: at once after the write
    // (empty means no answer yet), and again when the notification comes.
    let stopAuthMonitor = () => {};
    const changed = new Promise<void>((resolve, reject) => {
      stopAuthMonitor = connection.monitor(CHAR_AUTH_RESULT, () => resolve(), reject);
    });
    changed.catch(() => {});
    await connection.write(CHAR_AUTH, auth.write);
    const answer = async (): Promise<Uint8Array> => {
      const first = await connection.read(CHAR_AUTH_RESULT);
      if (first.length > 0) return first;
      await changed;
      return connection.read(CHAR_AUTH_RESULT);
    };
    const result = await withTimeout(
      answer(),
      options.authTimeoutMs ?? DEFAULT_AUTH_TIMEOUT_MS,
      () => new PairingError('timeout', 'The Pi did not answer the authentication.'),
      clock,
    );
    stopAuthMonitor();
    if (result.length === 1 && result[0] === AUTH_REFUSED) {
      throw new PairingError('refused', 'The Pi refused the authentication: the ID read from the LED does not match its own.');
    }
    if (result.length !== AUTH_RESULT_ACCEPTED_LENGTH || result[0] !== AUTH_ACCEPTED) {
      throw new PairingError('protocol', 'The authentication result was malformed.');
    }
    if (!verifyConfirmation(auth.keys, result.subarray(1))) {
      throw new PairingError('refused', "The Pi's proof did not verify: it does not hold the key for this ID.");
    }

    const s = sessionKey(auth.keys.ke);
    const session = new Session(connection, s, info, { transport, idBytes, peripheral, clock });
    phase('ready', info.version);
    return session;
  } catch (error) {
    await connection.disconnect();
    if (error instanceof PairingError) throw error;
    throw new PairingError('disconnected', 'The Bluetooth link to the Pi was lost while pairing. Try again.', error);
  }
}

/** The result of a request the daemon answered with `ok: false`. */
export class RequestError extends Error {
  constructor(
    readonly code: string,
    message: string,
  ) {
    super(message);
  }

  static from(response: ErrorResponse): RequestError {
    return new RequestError(response.error, response.message);
  }
}

/** An authenticated connection: requests go out sealed and fragmented, responses come back the same way. */
export class Session {
  private readonly sealer: Sealer;
  private readonly opener: Opener;
  private readonly reassembler = new Reassembler();
  private pending?: { op: Request['op']; resolve(response: Response): void; reject(error: Error): void };
  private chain: Promise<unknown> = Promise.resolve();
  private readonly unsubscribe: () => void;
  private closed = false;
  private readonly closeListeners = new Set<(reason: LinkLostError | undefined) => void>();

  constructor(
    readonly connection: BleConnection,
    sessionKeyS: Uint8Array,
    readonly info: DaemonInfo,
    private readonly origin: SessionOrigin,
  ) {
    const channel = appChannel(sessionKeyS);
    this.sealer = channel.sealer;
    this.opener = channel.opener;
    const stopMonitor = connection.monitor(
      CHAR_RESPONSE,
      (piece) => this.onFragment(piece),
      (error) => this.fail(new LinkLostError(LINK_LOST, error)),
    );
    const stopDisconnect = connection.onDisconnected((reason) => this.fail(new LinkLostError(LINK_LOST, reason)));
    this.unsubscribe = () => {
      stopMonitor();
      stopDisconnect();
    };
  }

  /** The peripheral this session runs over, as the scan found it. */
  get peripheral(): Peripheral {
    return this.origin.peripheral;
  }

  get clock(): Clock {
    return this.origin.clock;
  }

  get isClosed(): boolean {
    return this.closed;
  }

  /** Called once when the session ends: with a `LinkLostError` if the link failed, undefined if it was closed on purpose. */
  onClosed(listener: (reason: LinkLostError | undefined) => void): () => void {
    this.closeListeners.add(listener);
    return () => this.closeListeners.delete(listener);
  }

  /**
   * A new session with the same Pi: scan for the same name (preferring the
   * same platform id), connect, check Info and authenticate again with the
   * ID this one was made from. This session is closed first if it is not
   * already. Rejects with a `PairingError` as `pair` does.
   */
  async reconnect(options: Omit<PairOptions, 'clock' | 'preferId'> = {}): Promise<Session> {
    await this.close();
    return pair(this.origin.transport, this.origin.idBytes, { ...options, clock: this.origin.clock, preferId: this.origin.peripheral.id });
  }

  status(): Promise<StatusResponse> {
    return this.request({ op: 'status' }) as Promise<StatusResponse>;
  }

  wifiScan(): Promise<WifiScanResponse> {
    return this.request({ op: 'wifi.scan' }) as Promise<WifiScanResponse>;
  }

  async wifiConfigure(ssid: string, psk?: string): Promise<void> {
    await this.request({ op: 'wifi.configure', ssid, psk });
  }

  async wifiForget(): Promise<void> {
    await this.request({ op: 'wifi.forget' });
  }

  /** The Pi's DPP bootstrap URI; it listens for Android's configurator for up to 120 s. */
  async easyConnect(): Promise<string> {
    return ((await this.request({ op: 'wifi.easyconnect' })) as EasyConnectResponse).uri;
  }

  /** The Pi stops listening for Easy Connect and drops its bootstrap key. */
  async easyConnectStop(): Promise<void> {
    await this.request({ op: 'wifi.easyconnect.stop' });
  }

  /** The daemon leaves setup mode and disconnects; the session is closed after. */
  async finish(): Promise<void> {
    await this.request({ op: 'finish' });
    await this.close();
  }

  /**
   * Removes RPiLEDConfig from the Pi for good (docs/protocol.md,
   * Uninstalling): the daemon starts its removal, answers, and disconnects.
   * Rejects with `RequestError` (`uninstall_unavailable`, or `unknown_op`
   * from a protocol 2 daemon) when it cannot; the session stays open then.
   */
  async uninstall(): Promise<void> {
    await this.request({ op: 'uninstall' });
    await this.close();
  }

  /**
   * One request at a time. Rejects with `RequestError` on `ok: false`, with
   * `MessageError` on an answer of the wrong shape, and otherwise with
   * `LinkLostError`, having closed the session: a write that fails or a Pi
   * that does not answer means the link cannot be used any more.
   */
  request(request: Request): Promise<Response> {
    const run = async (): Promise<Response> => {
      if (this.closed) throw new LinkLostError(LINK_CLOSED);
      const response = await withTimeout(
        new Promise<Response>((resolve, reject) => {
          this.pending = { op: request.op, resolve, reject };
          const sealed = this.sealer.seal(encodeRequest(request));
          const pieces = fragment(sealed, fragmentSizeForMtu(this.connection.mtu));
          Promise.all(pieces.map((piece) => this.connection.write(CHAR_REQUEST, piece))).catch((error: unknown) =>
            this.fail(new LinkLostError(LINK_LOST, error)),
          );
        }),
        REQUEST_TIMEOUT_MS,
        () => {
          const silent = new LinkLostError(LINK_SILENT, `no answer to ${request.op}`);
          this.fail(silent);
          return silent;
        },
        this.origin.clock,
      );
      if (!response.ok) throw RequestError.from(response);
      return response;
    };
    const next = this.chain.then(run, run);
    this.chain = next.catch(() => {});
    return next;
  }

  async close(): Promise<void> {
    if (this.closed) return;
    this.closed = true;
    this.unsubscribe();
    await this.connection.disconnect();
    this.pending?.reject(new LinkLostError(LINK_CLOSED));
    this.pending = undefined;
    this.closeListeners.forEach((listener) => listener(undefined));
    this.closeListeners.clear();
  }

  private onFragment(piece: Uint8Array): void {
    let wire: Uint8Array | undefined;
    try {
      wire = this.reassembler.push(piece);
    } catch (error) {
      this.fail(new LinkLostError(LINK_UNVERIFIED, error instanceof FragmentError ? error : 'bad fragment'));
      return;
    }
    if (!wire) return;
    const pending = this.pending;
    this.pending = undefined;
    try {
      const plaintext = this.opener.open(wire);
      if (!pending) return;
      pending.resolve(decodeResponse(plaintext, pending.op));
    } catch (error) {
      if (error instanceof ChannelError) {
        // A failed tag or a replay means the other end is not who it was.
        this.fail(new LinkLostError(LINK_UNVERIFIED, error));
        return;
      }
      pending?.reject(error instanceof MessageError ? error : new Error('The Pi answered with something unreadable.'));
    }
  }

  private fail(reason: LinkLostError): void {
    if (this.closed) return;
    this.closed = true;
    this.unsubscribe();
    void this.connection.disconnect();
    this.pending?.reject(reason);
    this.pending = undefined;
    this.closeListeners.forEach((listener) => listener(reason));
    this.closeListeners.clear();
  }
}
