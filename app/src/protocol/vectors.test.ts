import { describe, expect, it } from 'vitest';
import { acceptAuth, appAuth } from './auth';
import { bytesToHex, hexToBytes, utf8Decode, utf8Encode } from './bytes';
import { appChannel, daemonChannel } from './channel';
import { fragment } from './fragments';
import { passwordScalar, sessionKey } from './keys';
import { encodeRequest } from './messages';
import { share } from './spake2';
import { PAIRING_KEY, VECTORS, X, Y } from './vectors';

/** Everything derived from the fixed inputs, by the code under test. */
function compute() {
  const idBytes = hexToBytes(PAIRING_KEY);
  const w = passwordScalar(idBytes);
  const pB = share('b', w, hexToBytes(Y));
  const app = appAuth(w, pB, hexToBytes(X));
  const daemon = acceptAuth(w, hexToBytes(Y), pB, app.write);
  if (!daemon) throw new Error('the vectors authentication was refused');
  const S = sessionKey(daemon.ke);
  const wire = appChannel(S).sealer.seal(encodeRequest({ op: 'status' }));
  return {
    pairing_key: PAIRING_KEY,
    key_bytes: bytesToHex(idBytes),
    w: bytesToHex(w),
    pA: bytesToHex(app.write.subarray(0, 65)),
    pB: bytesToHex(pB),
    K: bytesToHex(daemon.k),
    Ke: bytesToHex(daemon.ke),
    cA: bytesToHex(daemon.cA),
    cB: bytesToHex(daemon.cB),
    S: bytesToHex(S),
    status_request_wire: bytesToHex(wire),
    status_request_fragments_mtu20: fragment(wire, 20).map(bytesToHex),
  };
}

describe('protocol vectors', () => {
  const computed = compute();

  // VECTORS=1 npx vitest run src/protocol/vectors.test.ts prints them in the
  // daemon's --print-vectors form, "label: value", for a line-by-line diff.
  if (process.env.VECTORS) {
    it('prints the vectors', () => {
      for (const [label, value] of Object.entries(computed)) {
        process.stdout.write(`${label}: ${Array.isArray(value) ? value.join(',') : value}\n`);
      }
    });
  }

  it('reproduces every stored vector', () => {
    for (const [label, stored] of Object.entries(VECTORS)) {
      expect(computed[label as keyof typeof VECTORS], label).toEqual(stored);
    }
  });

  it('uses the plaintext the document shows, byte for byte', () => {
    expect(utf8Decode(encodeRequest({ op: 'status' }))).toBe('{"op":"status"}');
  });

  it('round-trips the status request through the daemon side', () => {
    const S = sessionKey(hexToBytes(VECTORS.Ke));
    const plaintext = daemonChannel(S).opener.open(hexToBytes(VECTORS.status_request_wire));
    expect(utf8Decode(plaintext)).toBe('{"op":"status"}');
  });

  it('has the sizes the protocol states', () => {
    expect(hexToBytes(VECTORS.w)).toHaveLength(32);
    expect(hexToBytes(VECTORS.pA)).toHaveLength(65);
    expect(hexToBytes(VECTORS.K)).toHaveLength(65);
    expect(hexToBytes(VECTORS.Ke)).toHaveLength(16);
    expect(hexToBytes(VECTORS.S)).toHaveLength(32);
    expect(hexToBytes(VECTORS.cA)).toHaveLength(32);
    // counter 8 + ciphertext 15 + tag 16
    expect(hexToBytes(VECTORS.status_request_wire)).toHaveLength(8 + utf8Encode('{"op":"status"}').length + 16);
    // 39 bytes of wire in 19-byte slices behind a flags byte: 19 + 19 + 1.
    expect(VECTORS.status_request_fragments_mtu20).toHaveLength(3);
    for (const piece of VECTORS.status_request_fragments_mtu20) expect(hexToBytes(piece).length).toBeLessThanOrEqual(20);
  });
});
