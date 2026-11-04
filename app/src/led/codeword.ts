// The LED frame's codeword (docs/protocol.md, The codeword): a 7-byte
// message (magic 0x52, frame version 0x04, id_bytes, CRC-16 over those five
// bytes) and 7 Reed-Solomon parity bytes. `readCodeword` is the last step
// of a read: correct, then accept only a word whose magic, version and CRC
// are right, which is what rejects a miscorrection and any other blinking
// light.
import { ID_BYTES, LED_CODEWORD_BYTES, LED_FRAME_VERSION, LED_MAGIC, LED_MESSAGE_BYTES } from '@/protocol/constants';
import { crc16 } from './crc16';
import { rsDecode, rsEncode } from './rs';

/** The CRC-16 follows magic, version and id_bytes, and covers them. */
const CRC_AT = 2 + ID_BYTES;

export function ledMessage(idBytes: Uint8Array): Uint8Array {
  if (idBytes.length !== ID_BYTES) throw new Error(`id must be ${ID_BYTES} bytes`);
  const message = new Uint8Array(LED_MESSAGE_BYTES);
  message[0] = LED_MAGIC;
  message[1] = LED_FRAME_VERSION;
  message.set(idBytes, 2);
  const crc = crc16(message.subarray(0, CRC_AT));
  message[CRC_AT] = crc >> 8;
  message[CRC_AT + 1] = crc & 0xff;
  return message;
}

export function ledCodeword(idBytes: Uint8Array): Uint8Array {
  return rsEncode(ledMessage(idBytes));
}

export type CodewordRejection = 'uncorrectable' | 'magic' | 'version' | 'crc';

export type CodewordRead =
  | { ok: true; idBytes: Uint8Array; errors: number; erasures: number }
  | { ok: false; reason: CodewordRejection; erasures: number };

/** Checks a corrected message's magic, version and CRC. */
export function checkMessage(message: Uint8Array): CodewordRejection | undefined {
  if (message[0] !== LED_MAGIC) return 'magic';
  if (message[1] !== LED_FRAME_VERSION) return 'version';
  const crc = crc16(message.subarray(0, CRC_AT));
  if (message[CRC_AT] !== crc >> 8 || message[CRC_AT + 1] !== (crc & 0xff)) return 'crc';
  return undefined;
}

/** Corrects `received` with `erasures` marked, then checks it. */
export function readCodeword(received: Uint8Array, erasures: readonly number[] = []): CodewordRead {
  if (received.length !== LED_CODEWORD_BYTES) throw new Error(`a codeword is ${LED_CODEWORD_BYTES} bytes`);
  const decoded = rsDecode(received, erasures);
  if (!decoded) return { ok: false, reason: 'uncorrectable', erasures: erasures.length };
  const message = decoded.codeword.subarray(0, LED_MESSAGE_BYTES);
  const rejection = checkMessage(message);
  if (rejection) return { ok: false, reason: rejection, erasures: decoded.erasures };
  return { ok: true, idBytes: message.slice(2, 2 + ID_BYTES), errors: decoded.errors, erasures: decoded.erasures };
}
