// Fragmenting a message's wire form into BLE writes and putting it back
// together (docs/protocol.md, Fragments). The flags byte leads each
// fragment; only bit 0 (last) is defined.
import { ATT_OVERHEAD, FLAG_LAST, MAX_MESSAGE_LENGTH, MIN_FRAGMENT_SIZE } from './constants';

/** The largest fragment a link with this ATT MTU carries. */
export function fragmentSizeForMtu(mtu: number): number {
  return Math.max(MIN_FRAGMENT_SIZE, mtu - ATT_OVERHEAD);
}

/**
 * Splits `message` into fragments of at most `fragmentSize` bytes, flags byte
 * included. An empty message is one fragment of just its flags.
 */
export function fragment(message: Uint8Array, fragmentSize: number): Uint8Array[] {
  if (fragmentSize < 2) throw new Error('a fragment needs room for flags and data');
  if (message.length > MAX_MESSAGE_LENGTH) throw new Error('message too long');
  const payload = fragmentSize - 1;
  const out: Uint8Array[] = [];
  let at = 0;
  do {
    const end = Math.min(message.length, at + payload);
    const piece = new Uint8Array(1 + (end - at));
    piece[0] = end === message.length ? FLAG_LAST : 0;
    piece.set(message.subarray(at, end), 1);
    out.push(piece);
    at = end;
  } while (at < message.length);
  return out;
}

export class FragmentError extends Error {}

/** Collects one direction's fragments into messages; refuses unknown flags and oversize messages. */
export class Reassembler {
  private parts: Uint8Array[] = [];
  private length = 0;

  /** The complete message when `piece` was its last fragment, else undefined. */
  push(piece: Uint8Array): Uint8Array | undefined {
    if (piece.length < 1) throw new FragmentError('empty fragment');
    const flags = piece[0];
    if ((flags & ~FLAG_LAST) !== 0) throw new FragmentError(`unknown fragment flags 0x${flags.toString(16)}`);
    const data = piece.subarray(1);
    if (this.length + data.length > MAX_MESSAGE_LENGTH) {
      this.reset();
      throw new FragmentError('message too long');
    }
    this.parts.push(data);
    this.length += data.length;
    if ((flags & FLAG_LAST) === 0) return undefined;
    const message = new Uint8Array(this.length);
    let at = 0;
    for (const part of this.parts) {
      message.set(part, at);
      at += part.length;
    }
    this.reset();
    return message;
  }

  reset(): void {
    this.parts = [];
    this.length = 0;
  }
}
