// The primitives the protocol needs, over the noble libraries (pure
// TypeScript, so the same code runs in vitest and on the phone). Randomness
// comes from expo-crypto, which is native; tests stub it.
import { gcm } from '@noble/ciphers/aes.js';
import { hkdf } from '@noble/hashes/hkdf.js';
import { hmac } from '@noble/hashes/hmac.js';
import { sha256 as nobleSha256 } from '@noble/hashes/sha2.js';
import { getRandomValues } from 'expo-crypto';

export function sha256(data: Uint8Array): Uint8Array {
  return nobleSha256(data);
}

export function hmacSha256(key: Uint8Array, data: Uint8Array): Uint8Array {
  return hmac(nobleSha256, key, data);
}

export function hkdfSha256(ikm: Uint8Array, salt: Uint8Array, info: Uint8Array, length: number): Uint8Array {
  return hkdf(nobleSha256, ikm, salt, info, length);
}

/** AES-256-GCM, no associated data: returns ciphertext || 16-byte tag. */
export function aes256GcmSeal(key: Uint8Array, nonce: Uint8Array, plaintext: Uint8Array): Uint8Array {
  return gcm(key, nonce).encrypt(plaintext);
}

/** Throws when the tag does not verify. */
export function aes256GcmOpen(key: Uint8Array, nonce: Uint8Array, sealed: Uint8Array): Uint8Array {
  return gcm(key, nonce).decrypt(sealed);
}

export function randomBytes(length: number): Uint8Array {
  return getRandomValues(new Uint8Array(length));
}

/** Compares in time independent of where the first difference is. */
export function constantTimeEqual(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i += 1) diff |= a[i] ^ b[i];
  return diff === 0;
}
