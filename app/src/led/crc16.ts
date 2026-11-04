// CRC-16/CCITT-FALSE over the LED message's first ten bytes: polynomial
// 0x1021, initial value 0xFFFF, no reflection, no final XOR
// (docs/protocol.md, The codeword). Check value for "123456789": 0x29B1.
import { CRC16_INIT, CRC16_POLY } from '@/protocol/constants';

export function crc16(bytes: Uint8Array): number {
  let crc = CRC16_INIT;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = crc & 0x8000 ? ((crc << 1) ^ CRC16_POLY) & 0xffff : (crc << 1) & 0xffff;
    }
  }
  return crc;
}
