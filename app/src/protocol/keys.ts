// Key derivation from docs/protocol.md: the SPAKE2 password scalar w from
// the machine ID, the public fingerprint, and the session key S from Ke.
import { bytesToHex, utf8Encode } from './bytes';
import { FINGERPRINT_BYTES, KEY_LENGTH, SESSION_KEY_INFO } from './constants';
import { hkdfSha256, sha256 } from './crypto';

export { passwordScalar } from './spake2';

/** Four lowercase hex characters: the first two bytes of SHA-256(id_bytes). */
export function fingerprint(idBytes: Uint8Array): string {
  return bytesToHex(sha256(idBytes).subarray(0, FINGERPRINT_BYTES));
}

/** S = HKDF(ikm = Ke, salt = empty, info = "session-key v2", L = 32). */
export function sessionKey(ke: Uint8Array): Uint8Array {
  return hkdfSha256(ke, new Uint8Array(0), utf8Encode(SESSION_KEY_INFO), KEY_LENGTH);
}
