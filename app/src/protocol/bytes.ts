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

// UTF-8 by hand: Hermes has TextEncoder but no TextDecoder, and a polyfill is a dependency.

export function utf8Encode(text: string): Uint8Array {
  const out: number[] = [];
  for (let i = 0; i < text.length; i += 1) {
    let code = text.charCodeAt(i);
    if (code >= 0xd800 && code <= 0xdbff && i + 1 < text.length) {
      const low = text.charCodeAt(i + 1);
      if (low >= 0xdc00 && low <= 0xdfff) {
        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
        i += 1;
      }
    }
    if (code < 0x80) out.push(code);
    else if (code < 0x800) out.push(0xc0 | (code >> 6), 0x80 | (code & 0x3f));
    else if (code < 0x10000) out.push(0xe0 | (code >> 12), 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f));
    else out.push(0xf0 | (code >> 18), 0x80 | ((code >> 12) & 0x3f), 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f));
  }
  return Uint8Array.from(out);
}

/** Throws on malformed UTF-8: a message that is not text is not a message. */
export function utf8Decode(bytes: Uint8Array): string {
  let out = '';
  for (let i = 0; i < bytes.length; ) {
    const b0 = bytes[i];
    let code: number;
    let length: number;
    if (b0 < 0x80) {
      code = b0;
      length = 1;
    } else if (b0 >= 0xc2 && b0 <= 0xdf) {
      code = b0 & 0x1f;
      length = 2;
    } else if (b0 >= 0xe0 && b0 <= 0xef) {
      code = b0 & 0x0f;
      length = 3;
    } else if (b0 >= 0xf0 && b0 <= 0xf4) {
      code = b0 & 0x07;
      length = 4;
    } else {
      throw new Error(`malformed UTF-8 at byte ${i}`);
    }
    if (i + length > bytes.length) throw new Error(`malformed UTF-8 at byte ${i}`);
    for (let k = 1; k < length; k += 1) {
      const b = bytes[i + k];
      if ((b & 0xc0) !== 0x80) throw new Error(`malformed UTF-8 at byte ${i + k}`);
      code = (code << 6) | (b & 0x3f);
    }
    // Overlong forms, surrogates and values past U+10FFFF are not UTF-8.
    if ((length === 3 && code < 0x800) || (length === 4 && code < 0x10000) || (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff) {
      throw new Error(`malformed UTF-8 at byte ${i}`);
    }
    out += String.fromCodePoint(code);
    i += length;
  }
  return out;
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
