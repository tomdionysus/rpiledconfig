// The LED codeword from docs/protocol.md's vectors, which were computed
// independently of both sides (Python, from the definitions).
import { describe, expect, it } from 'vitest';
import { bytesToHex, hexToBytes, utf8Encode } from '@/protocol/bytes';
import { checkMessage, ledCodeword, ledMessage, readCodeword } from './codeword';
import { crc16 } from './crc16';
import { gfDiv, gfInverse, gfMul, gfPow } from './gf256';
import { generatorPoly, rsDecode, rsEncode, syndromes } from './rs';
import { lcg } from './synth';

const ID = hexToBytes('0123456789abcdef');
const MESSAGE = '52050123456789abcdef07a6';
const CODEWORD = '52050123456789abcdef07a62dbecfdf83914ddbbd24721a';
const GENERATOR = '0144774376dc1f07545c7fd561';

function damaged(codeword: Uint8Array, positions: number[], random: () => number): Uint8Array {
  const out = codeword.slice();
  for (const p of positions) {
    // A different value, never the same byte.
    out[p] ^= 1 + Math.floor(random() * 255);
  }
  return out;
}

/** `count` distinct positions in 0..23. */
function positions(count: number, random: () => number): number[] {
  const all = Array.from({ length: 24 }, (_, i) => i);
  for (let i = all.length - 1; i > 0; i -= 1) {
    const j = Math.floor(random() * (i + 1));
    [all[i], all[j]] = [all[j], all[i]];
  }
  return all.slice(0, count);
}

describe('CRC-16/CCITT-FALSE', () => {
  it('has the check value 0x29B1', () => {
    expect(crc16(utf8Encode('123456789'))).toBe(0x29b1);
    expect(crc16(new Uint8Array())).toBe(0xffff);
  });
});

describe('GF(2^8), primitive polynomial 0x11D', () => {
  it('multiplies, divides and inverts consistently', () => {
    expect(gfMul(0x80, 2)).toBe(0x1d);
    expect(gfPow(2, 8)).toBe(0x1d);
    expect(gfPow(2, 255)).toBe(1);
    for (let x = 1; x < 256; x += 1) {
      expect(gfMul(x, gfInverse(x))).toBe(1);
      expect(gfDiv(gfMul(x, 0x53), 0x53)).toBe(x);
    }
  });
});

describe('RS(24, 12)', () => {
  it('has the generator polynomial of the vectors', () => {
    expect(bytesToHex(Uint8Array.from(generatorPoly()))).toBe(GENERATOR);
  });

  it('encodes the vectors message to the vectors codeword', () => {
    expect(bytesToHex(ledMessage(ID))).toBe(MESSAGE);
    expect(bytesToHex(ledCodeword(ID))).toBe(CODEWORD);
    expect(bytesToHex(rsEncode(hexToBytes(MESSAGE)))).toBe(CODEWORD);
    expect(syndromes(Array.from(hexToBytes(CODEWORD))).every((s) => s === 0)).toBe(true);
  });

  const codeword = hexToBytes(CODEWORD);

  it('corrects 6 wrong bytes anywhere', () => {
    const random = lcg(5);
    for (let trial = 0; trial < 200; trial += 1) {
      const wrong = positions(6, random);
      const result = rsDecode(damaged(codeword, wrong, random));
      expect(result, `trial ${trial}, positions ${wrong}`).toBeDefined();
      expect(bytesToHex(result!.codeword)).toBe(CODEWORD);
      expect(result!.errors).toBe(6);
    }
  });

  it('fills 12 erased bytes', () => {
    const random = lcg(10);
    for (let trial = 0; trial < 200; trial += 1) {
      const erased = positions(12, random);
      const result = rsDecode(damaged(codeword, erased, random), erased);
      expect(bytesToHex(result!.codeword), `trial ${trial}`).toBe(CODEWORD);
      expect(result!.erasures).toBe(12);
      expect(result!.errors).toBe(0);
    }
  });

  it('corrects 4 wrong and 4 erased together, and every mix within 2e + f <= 12', () => {
    const random = lcg(44);
    for (let errors = 0; errors <= 6; errors += 1) {
      for (let erased = 0; 2 * errors + erased <= 12; erased += 1) {
        for (let trial = 0; trial < 30; trial += 1) {
          const picked = positions(errors + erased, random);
          const erasedAt = picked.slice(0, erased);
          // Some erased bytes keep their right value: erasure is about
          // confidence, not about the byte being wrong.
          const word = damaged(codeword, picked.filter((p, i) => i >= erased || random() < 0.7), random);
          const result = rsDecode(word, erasedAt);
          expect(result && bytesToHex(result.codeword), `${errors} errors, ${erased} erasures, trial ${trial}`).toBe(CODEWORD);
          expect(result!.errors).toBe(errors);
        }
      }
    }
  });

  it('never returns a wrong ID beyond capacity: it fails, or the magic, version or CRC catch it', () => {
    const random = lcg(99);
    let failed = 0;
    let caught = 0;
    for (let trial = 0; trial < 3000; trial += 1) {
      const errors = 7 + Math.floor(random() * 8);
      const read = readCodeword(damaged(codeword, positions(errors, random), random));
      if (read.ok) {
        expect(bytesToHex(read.idBytes)).toBe('0123456789abcdef');
      } else if (read.reason === 'uncorrectable') {
        failed += 1;
      } else {
        caught += 1;
      }
    }
    expect(failed + caught).toBe(3000);
  });

  it('catches a miscorrection: a word nearer another codeword decodes to it, and the CRC refuses it', () => {
    // Another ID behind the same CRC bytes: a valid codeword whose message
    // fails the CRC. Damage it lightly and RS returns it, as it would return
    // any codeword nearer the received word than the true one.
    const message = hexToBytes(MESSAGE);
    message[4] ^= 0x40;
    const other = rsEncode(message);
    const random = lcg(5);
    const read = readCodeword(damaged(other, positions(2, random), random));
    expect(read.ok).toBe(false);
    expect(!read.ok && read.reason).toBe('crc');
  });

  it('refuses more than 12 erasures', () => {
    expect(rsDecode(codeword, positions(13, lcg(11)))).toBeUndefined();
  });
});

describe('readCodeword', () => {
  it('reads the ID from the clean codeword and through damage', () => {
    const clean = readCodeword(hexToBytes(CODEWORD));
    expect(clean.ok && bytesToHex(clean.idBytes)).toBe('0123456789abcdef');
    const random = lcg(3);
    const read = readCodeword(damaged(hexToBytes(CODEWORD), [0, 23], random), [9]);
    expect(read.ok && read.errors).toBe(2);
  });

  it('rejects a word with the magic, the version or the CRC wrong', () => {
    const variants: [string, (m: Uint8Array) => void][] = [
      ['magic', (m) => (m[0] = 0x53)],
      ['version', (m) => (m[1] = 0x01)],
      ['crc', (m) => (m[11] ^= 0x01)],
    ];
    for (const [reason, spoil] of variants) {
      const message = hexToBytes(MESSAGE);
      spoil(message);
      expect(checkMessage(message)).toBe(reason);
      // A proper codeword for the spoilt message: RS is content with it,
      // and the checks after it are what refuse it.
      const read = readCodeword(rsEncode(message));
      expect(read.ok).toBe(false);
      expect(!read.ok && read.reason).toBe(reason);
    }
  });
});
