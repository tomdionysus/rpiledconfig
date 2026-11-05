// Key derivation from docs/protocol.md: the SPAKE2 password scalar w from
// the pairing key, and the session key S from Ke.
import { utf8Encode } from './bytes';
import { KEY_LENGTH, SESSION_KEY_INFO } from './constants';
import { hkdfSha256 } from './crypto';

export { passwordScalar } from './spake2';

/** S = HKDF(ikm = Ke, salt = empty, info = "session-key v2", L = 32). */
export function sessionKey(ke: Uint8Array): Uint8Array {
  return hkdfSha256(ke, new Uint8Array(0), utf8Encode(SESSION_KEY_INFO), KEY_LENGTH);
}
