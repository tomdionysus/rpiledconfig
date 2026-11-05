// SPAKE2 of RFC 9382, ciphersuite SPAKE2-P256-SHA256-HKDF-HMAC, over
// @noble/curves. Both roles are here: the app is A, and the fake daemon plays
// B. Scalars are 32 bytes big-endian; points are SEC1 uncompressed, 65 bytes.
import { p256 } from '@noble/curves/nist.js';
import { bytesToHex, concat, hexToBytes, utf8Encode } from './bytes';
import { hkdfSha256, hmacSha256, randomBytes, sha256 } from './crypto';

type Point = ReturnType<typeof p256.Point.fromHex>;

export const SCALAR_LENGTH = 32;
export const SHARE_LENGTH = 65;
export const CONFIRMATION_LENGTH = 32;

/** RFC 9382, section 6, for P-256: compressed SEC1. */
const M = p256.Point.fromHex('02886e2f97ace46e55ba9dd7242579f2993b64e16ef3dcab95afd497333d8fa12f');
const N = p256.Point.fromHex('03d8bbd6c639c62937b04d997f38c3770719c629d7014d49a24b4f98baa1292b49');
const ORDER = p256.Point.Fn.ORDER;

/** A uses M, B uses N (RFC 9382, section 3.3). */
export type Role = 'a' | 'b';

export class Spake2Error extends Error {}

function toScalar(bytes: Uint8Array): bigint {
  if (bytes.length !== SCALAR_LENGTH) throw new Spake2Error('a scalar is not 32 bytes');
  const value = BigInt(`0x${bytesToHex(bytes)}`);
  if (value === 0n || value >= ORDER) throw new Spake2Error('a scalar out of range');
  return value;
}

function scalarBytes(value: bigint): Uint8Array {
  return hexToBytes(value.toString(16).padStart(2 * SCALAR_LENGTH, '0'));
}

/** len(s) as RFC 9382 has it: eight bytes, little-endian, then s. */
function withLength(s: Uint8Array): Uint8Array {
  const length = new Uint8Array(8);
  let n = s.length;
  for (let i = 0; i < 8; i += 1) {
    length[i] = n & 0xff;
    n = Math.floor(n / 256);
  }
  return concat(length, s);
}

/**
 * w = HKDF(ikm = key_bytes, salt = "RPiLEDConfig", info = "spake2-w v2", L = 40)
 * mod n, as 32 bytes (docs/protocol.md, Keys).
 */
export function passwordScalar(idBytes: Uint8Array): Uint8Array {
  const wide = hkdfSha256(idBytes, utf8Encode('RPiLEDConfig'), utf8Encode('spake2-w v2'), 40);
  return scalarBytes(BigInt(`0x${bytesToHex(wide)}`) % ORDER);
}

/** A scalar drawn uniformly from [1, n) by rejection sampling (RFC 9382, section 7). */
export function randomScalar(): Uint8Array {
  for (let attempt = 0; attempt < 64; attempt += 1) {
    const candidate = randomBytes(SCALAR_LENGTH);
    const value = BigInt(`0x${bytesToHex(candidate)}`);
    if (value !== 0n && value < ORDER) return candidate;
  }
  throw new Spake2Error('no random scalar');
}

/** pA = x*P + w*M for A, pB = y*P + w*N for B. */
export function share(role: Role, w: Uint8Array, scalar: Uint8Array): Uint8Array {
  const blind = role === 'a' ? M : N;
  return p256.Point.BASE.multiply(toScalar(scalar))
    .add(blind.multiply(toScalar(w)))
    .toBytes(false);
}

/** Only a 65-byte uncompressed point on P-256 that is not the identity. */
function decodeShare(bytes: Uint8Array): Point {
  if (bytes.length !== SHARE_LENGTH || bytes[0] !== 0x04) throw new Spake2Error("the peer's share is not a 65-byte uncompressed point");
  let point: Point;
  try {
    point = p256.Point.fromBytes(bytes);
    point.assertValidity();
  } catch {
    throw new Spake2Error("the peer's share is not a point on P-256");
  }
  if (point.is0()) throw new Spake2Error("the peer's share is the identity");
  return point;
}

export interface Spake2Keys {
  /** The shared point, 65 bytes; never a key on its own. */
  k: Uint8Array;
  tt: Uint8Array;
  /** The shared secret, 16 bytes. */
  ke: Uint8Array;
  kcA: Uint8Array;
  kcB: Uint8Array;
  /** HMAC-SHA256(KcA, TT) and HMAC-SHA256(KcB, TT). */
  cA: Uint8Array;
  cB: Uint8Array;
}

export interface Transcript {
  aIdentity?: Uint8Array;
  bIdentity?: Uint8Array;
  aad?: Uint8Array;
}

/**
 * One side's view of a finished exchange, from its own scalar and both shares.
 * Throws a Spake2Error when the peer's share is malformed, off the curve or
 * the identity, or when K is the identity (RFC 9382, section 7).
 */
export function finish(role: Role, w: Uint8Array, scalar: Uint8Array, ownShare: Uint8Array, peerShare: Uint8Array, transcript: Transcript = {}): Spake2Keys {
  const peer = decodeShare(peerShare);
  const peerBlind = role === 'a' ? N : M;
  const kPoint = peer.subtract(peerBlind.multiply(toScalar(w))).multiply(toScalar(scalar));
  if (kPoint.is0()) throw new Spake2Error('the shared point is the identity');
  const k = kPoint.toBytes(false);

  const pA = role === 'a' ? ownShare : peerShare;
  const pB = role === 'a' ? peerShare : ownShare;
  const tt = concat(
    withLength(transcript.aIdentity ?? new Uint8Array(0)),
    withLength(transcript.bIdentity ?? new Uint8Array(0)),
    withLength(pA),
    withLength(pB),
    withLength(k),
    withLength(w),
  );

  const hash = sha256(tt);
  const ke = hash.slice(0, 16);
  const ka = hash.slice(16);
  const kc = hkdfSha256(ka, new Uint8Array(0), concat(utf8Encode('ConfirmationKeys'), transcript.aad ?? new Uint8Array(0)), 32);
  const kcA = kc.slice(0, 16);
  const kcB = kc.slice(16);
  return { k, tt, ke, kcA, kcB, cA: hmacSha256(kcA, tt), cB: hmacSha256(kcB, tt) };
}
