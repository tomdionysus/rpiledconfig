// The encrypted channel after authentication: AES-256-GCM under S, one
// counter per direction, replay refused (docs/protocol.md, The encrypted
// channel). One object per direction, so the app has a sealer for its
// requests and an opener for the daemon's responses; the fake daemon has the
// reverse pair.
import { bytesToUint, concat, uintToBytes } from './bytes';
import {
  COUNTER_LENGTH,
  DIRECTION_APP_TO_DAEMON,
  DIRECTION_DAEMON_TO_APP,
  GCM_NONCE_LENGTH,
  GCM_TAG_LENGTH,
  KEY_LENGTH,
} from './constants';
import { aes256GcmOpen, aes256GcmSeal } from './crypto';

export type Direction = typeof DIRECTION_APP_TO_DAEMON | typeof DIRECTION_DAEMON_TO_APP;

/** Nonce: 4-byte direction || 8-byte counter, both big-endian. */
export function nonce(direction: Direction, counter: number): Uint8Array {
  const out = concat(uintToBytes(direction, GCM_NONCE_LENGTH - COUNTER_LENGTH), uintToBytes(counter, COUNTER_LENGTH));
  if (out.length !== GCM_NONCE_LENGTH) throw new Error('nonce length');
  return out;
}

/** Seals messages in one direction, numbering them from 0. */
export class Sealer {
  private counter = 0;

  constructor(
    private readonly key: Uint8Array,
    private readonly direction: Direction,
  ) {
    if (key.length !== KEY_LENGTH) throw new Error('session key must be 32 bytes');
  }

  /** The wire form: counter || ciphertext || tag. */
  seal(plaintext: Uint8Array): Uint8Array {
    const counter = this.counter;
    this.counter += 1;
    return concat(uintToBytes(counter, COUNTER_LENGTH), aes256GcmSeal(this.key, nonce(this.direction, counter), plaintext));
  }
}

export class ChannelError extends Error {
  constructor(
    readonly kind: 'malformed' | 'replay' | 'auth',
    message: string,
  ) {
    super(message);
  }
}

/** Opens messages in one direction; a counter at or below the last accepted is a replay. */
export class Opener {
  private last = -1;

  constructor(
    private readonly key: Uint8Array,
    private readonly direction: Direction,
  ) {
    if (key.length !== KEY_LENGTH) throw new Error('session key must be 32 bytes');
  }

  /** Throws `ChannelError`; the protocol drops the connection on `auth`. */
  open(wire: Uint8Array): Uint8Array {
    if (wire.length < COUNTER_LENGTH + GCM_TAG_LENGTH) throw new ChannelError('malformed', 'message too short');
    const counter = bytesToUint(wire.subarray(0, COUNTER_LENGTH));
    if (counter <= this.last) throw new ChannelError('replay', `counter ${counter} already seen`);
    let plaintext: Uint8Array;
    try {
      plaintext = aes256GcmOpen(this.key, nonce(this.direction, counter), wire.subarray(COUNTER_LENGTH));
    } catch {
      throw new ChannelError('auth', 'message failed authentication');
    }
    this.last = counter;
    return plaintext;
  }
}

/** The app's pair: it seals towards the daemon and opens what the daemon sends. */
export function appChannel(sessionKeyS: Uint8Array): { sealer: Sealer; opener: Opener } {
  return { sealer: new Sealer(sessionKeyS, DIRECTION_APP_TO_DAEMON), opener: new Opener(sessionKeyS, DIRECTION_DAEMON_TO_APP) };
}

/** The daemon's pair, for the fake daemon and the tests. */
export function daemonChannel(sessionKeyS: Uint8Array): { sealer: Sealer; opener: Opener } {
  return { sealer: new Sealer(sessionKeyS, DIRECTION_DAEMON_TO_APP), opener: new Opener(sessionKeyS, DIRECTION_APP_TO_DAEMON) };
}
