// Byte helpers the protocol code shares: hex, UTF-8, concatenation and
// big-endian integers. Written out rather than through Buffer, which the
// bundle does not otherwise carry.

export function hexToBytes(hex: string): Uint8Array {
  const clean = hex.trim();
  if (clean.length % 2 !== 0 || /[^0-9a-fA-F]/.test(clean)) throw new Error(`not hex: ${hex}`);
  const out = new Uint8Array(clean.length / 2);
  for (let i = 0; i < out.length; i += 1) out[i] = parseInt(clean.slice(i * 2, i * 2 + 2), 16);
  return out;
}

export function bytesToHex(bytes: Uint8Array): string {
  let out = '';
  for (const byte of bytes) out += byte.toString(16).padStart(2, '0');
  return out;
}

export function concat(...parts: Uint8Array[]): Uint8Array {
  const out = new Uint8Array(parts.reduce((n, part) => n + part.length, 0));
  let at = 0;
  for (const part of parts) {
    out.set(part, at);
    at += part.length;
  }
  return out;
}

const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });

export function utf8Encode(text: string): Uint8Array {
  return encoder.encode(text);
}

/** Throws on malformed UTF-8: a message that is not text is not a message. */
export function utf8Decode(bytes: Uint8Array): string {
  return decoder.decode(bytes);
}

/** `value` as `length` big-endian bytes; the counter is 8 bytes and the direction 4. */
export function uintToBytes(value: number, length: number): Uint8Array {
  if (!Number.isSafeInteger(value) || value < 0) throw new Error(`not an unsigned integer: ${value}`);
  const out = new Uint8Array(length);
  let rest = value;
  for (let i = length - 1; i >= 0; i -= 1) {
    out[i] = rest % 256;
    rest = Math.floor(rest / 256);
  }
  if (rest !== 0) throw new Error(`${value} does not fit in ${length} bytes`);
  return out;
}

export function bytesToUint(bytes: Uint8Array): number {
  let value = 0;
  for (const byte of bytes) value = value * 256 + byte;
  if (!Number.isSafeInteger(value)) throw new Error('integer too large');
  return value;
}

export function bytesEqual(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i += 1) if (a[i] !== b[i]) return false;
  return true;
}
