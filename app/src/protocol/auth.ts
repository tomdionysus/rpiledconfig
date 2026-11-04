// Authentication, docs/protocol.md: SPAKE2 with the app as A and the daemon
// as B, empty identities, and AUTH_AAD. Nothing that crosses the air can be
// used to test a guess of the machine ID offline.
import { concat, utf8Encode } from './bytes';
import { AUTH_AAD, AUTH_WRITE_LENGTH } from './constants';
import { constantTimeEqual } from './crypto';
import { CONFIRMATION_LENGTH, finish, randomScalar, share, SHARE_LENGTH, Spake2Error, type Spake2Keys } from './spake2';

const TRANSCRIPT = { aad: utf8Encode(AUTH_AAD) };

export interface AppAuth {
  /** The Auth write: pA || cA. */
  write: Uint8Array;
  keys: Spake2Keys;
}

/**
 * The app's side for the daemon's pB: a new x every time, as RFC 9382 says.
 * Throws a Spake2Error when pB is not a valid point.
 */
export function appAuth(w: Uint8Array, pB: Uint8Array, x: Uint8Array = randomScalar()): AppAuth {
  const pA = share('a', w, x);
  const keys = finish('a', w, x, pA, pB, TRANSCRIPT);
  return { write: concat(pA, keys.cA), keys };
}

/** Whether AuthResult's cB is the one these keys expect, in constant time. */
export function verifyConfirmation(keys: Spake2Keys, cB: Uint8Array): boolean {
  return cB.length === CONFIRMATION_LENGTH && constantTimeEqual(keys.cB, cB);
}

/**
 * The daemon's side of an Auth write, for the fake daemon: the keys when the
 * write is well formed, pA is valid and cA verifies; undefined otherwise.
 */
export function acceptAuth(w: Uint8Array, y: Uint8Array, pB: Uint8Array, write: Uint8Array): Spake2Keys | undefined {
  if (write.length !== AUTH_WRITE_LENGTH) return undefined;
  let keys: Spake2Keys;
  try {
    keys = finish('b', w, y, pB, write.subarray(0, SHARE_LENGTH), TRANSCRIPT);
  } catch (error) {
    if (error instanceof Spake2Error) return undefined;
    throw error;
  }
  return constantTimeEqual(keys.cA, write.subarray(SHARE_LENGTH)) ? keys : undefined;
}
