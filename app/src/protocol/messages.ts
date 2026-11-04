// The requests and responses inside the encrypted channel: small flat JSON
// (docs/protocol.md, Messages). Encoding keeps the key order the document
// shows; decoding validates the shape, since the daemon's version may differ.
import { utf8Decode, utf8Encode } from './bytes';

export type Request =
  | { op: 'status' }
  | { op: 'wifi.scan' }
  | { op: 'wifi.configure'; ssid: string; psk?: string; hidden?: boolean }
  | { op: 'wifi.forget' }
  | { op: 'wifi.easyconnect' }
  | { op: 'wifi.easyconnect.stop' }
  | { op: 'finish' }
  | { op: 'uninstall' };

export type Op = Request['op'];

/** What became of the latest `wifi.configure` (docs/protocol.md, The latest join). */
export interface JoinState {
  ssid: string;
  state: 'joining' | 'joined' | 'failed';
  /** With `failed` only. */
  reason?: 'wrong_passphrase' | 'not_found' | 'other';
  message?: string;
}

export interface WifiStatus {
  connected: boolean;
  ssid?: string;
  ip?: string;
  join?: JoinState;
}

export interface StatusResponse {
  ok: true;
  hostname: string;
  wifi: WifiStatus;
}

export interface Network {
  ssid: string;
  /** 0..100 */
  signal: number;
  secure: boolean;
}

export interface WifiScanResponse {
  ok: true;
  networks: Network[];
}

/** The Pi's Wi-Fi Easy Connect bootstrap code, for Android's configurator. */
export interface EasyConnectResponse {
  ok: true;
  uri: string;
}

export interface OkResponse {
  ok: true;
}

export type ErrorCode = 'bad_request' | 'unknown_op' | 'wifi_unavailable' | 'wifi_failed' | 'busy' | 'easyconnect_unavailable' | 'uninstall_unavailable';

export interface ErrorResponse {
  ok: false;
  error: ErrorCode | string;
  message: string;
}

export type Response = StatusResponse | WifiScanResponse | EasyConnectResponse | OkResponse | ErrorResponse;

/** UTF-8 JSON; `psk` is left out for an open network, `hidden` unless it is true. */
export function encodeRequest(request: Request): Uint8Array {
  let json: string;
  switch (request.op) {
    case 'wifi.configure':
      json = JSON.stringify({ op: request.op, ssid: request.ssid, ...(request.psk ? { psk: request.psk } : {}), ...(request.hidden ? { hidden: true } : {}) });
      break;
    default:
      json = JSON.stringify({ op: request.op });
  }
  return utf8Encode(json);
}

export class MessageError extends Error {}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** Parses a response for `op`, throwing `MessageError` when it is not the shape the protocol gives. */
export function decodeResponse(bytes: Uint8Array, op: Op): Response {
  let parsed: unknown;
  try {
    parsed = JSON.parse(utf8Decode(bytes));
  } catch {
    throw new MessageError('response is not JSON');
  }
  if (!isRecord(parsed) || typeof parsed.ok !== 'boolean') throw new MessageError('response has no ok');

  if (parsed.ok === false) {
    if (typeof parsed.error !== 'string') throw new MessageError('error response has no error code');
    return { ok: false, error: parsed.error, message: typeof parsed.message === 'string' ? parsed.message : parsed.error };
  }

  switch (op) {
    case 'status': {
      const wifi = parsed.wifi;
      if (typeof parsed.hostname !== 'string' || !isRecord(wifi) || typeof wifi.connected !== 'boolean') {
        throw new MessageError('status response malformed');
      }
      const status: WifiStatus = { connected: wifi.connected };
      if (typeof wifi.ssid === 'string') status.ssid = wifi.ssid;
      if (typeof wifi.ip === 'string') status.ip = wifi.ip;
      const join = wifi.join;
      if (isRecord(join) && typeof join.ssid === 'string' && (join.state === 'joining' || join.state === 'joined' || join.state === 'failed')) {
        status.join = { ssid: join.ssid, state: join.state };
        if (join.state === 'failed') {
          status.join.reason = join.reason === 'wrong_passphrase' || join.reason === 'not_found' ? join.reason : 'other';
          if (typeof join.message === 'string') status.join.message = join.message;
        }
      }
      return { ok: true, hostname: parsed.hostname, wifi: status };
    }
    case 'wifi.scan': {
      if (!Array.isArray(parsed.networks)) throw new MessageError('scan response has no networks');
      const networks: Network[] = parsed.networks.map((entry: unknown) => {
        if (!isRecord(entry) || typeof entry.ssid !== 'string' || typeof entry.signal !== 'number' || typeof entry.secure !== 'boolean') {
          throw new MessageError('network entry malformed');
        }
        return { ssid: entry.ssid, signal: Math.max(0, Math.min(100, entry.signal)), secure: entry.secure };
      });
      return { ok: true, networks };
    }
    case 'wifi.easyconnect': {
      if (typeof parsed.uri !== 'string' || !parsed.uri.startsWith('DPP:')) throw new MessageError('easyconnect response has no DPP uri');
      return { ok: true, uri: parsed.uri };
    }
    default:
      return { ok: true };
  }
}

/** The daemon side of the same, for the fake daemon: a request or why it is bad. */
export function decodeRequest(bytes: Uint8Array): { request: Request } | { error: 'bad_request' | 'unknown_op'; message: string } {
  let parsed: unknown;
  try {
    parsed = JSON.parse(utf8Decode(bytes));
  } catch {
    return { error: 'bad_request', message: 'request is not JSON' };
  }
  if (!isRecord(parsed) || typeof parsed.op !== 'string') return { error: 'bad_request', message: 'request has no op' };
  switch (parsed.op) {
    case 'status':
    case 'wifi.scan':
    case 'wifi.forget':
    case 'wifi.easyconnect':
    case 'wifi.easyconnect.stop':
    case 'finish':
    case 'uninstall':
      return { request: { op: parsed.op } };
    case 'wifi.configure': {
      if (typeof parsed.ssid !== 'string' || parsed.ssid.length === 0) return { error: 'bad_request', message: 'ssid missing' };
      if (parsed.psk !== undefined && typeof parsed.psk !== 'string') return { error: 'bad_request', message: 'psk must be a string' };
      const request: Request = { op: 'wifi.configure', ssid: parsed.ssid };
      if (parsed.psk) request.psk = parsed.psk;
      if (parsed.hidden !== undefined && typeof parsed.hidden !== 'boolean') return { error: 'bad_request', message: 'hidden must be true or false' };
      if (parsed.hidden === true) request.hidden = true;
      return { request };
    }
    default:
      return { error: 'unknown_op', message: `unknown op ${parsed.op}` };
  }
}

export function encodeResponse(response: Response): Uint8Array {
  return utf8Encode(JSON.stringify(response));
}
