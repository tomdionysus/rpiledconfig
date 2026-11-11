import { p256 } from '@noble/curves/nist.js';
import { afterEach, describe, expect, it } from 'vitest';
import { base64Encode } from '@/ble/base64';
import { NativeModules } from '@/test/react-native';
import { acceptAuth, appAuth, verifyConfirmation } from './auth';
import { bytesToHex, hexToBytes, utf8Encode } from './bytes';
import { AUTH_WRITE_LENGTH } from './constants';
import { constantTimeEqual, randomBytes } from './crypto';
import { passwordScalar, sessionKey } from './keys';
import { finish, randomScalar, share, Spake2Error } from './spake2';
import { VECTORS, X, Y } from './vectors';

const ID = hexToBytes(VECTORS.key_bytes);

// RFC 9382, Appendix B, P256-SHA256-HKDF-HMAC: the first vector (both
// identities) and the last (neither). No AAD.
const RFC_VECTORS = [
  {
    a: 'server',
    b: 'client',
    w: '2ee57912099d31560b3a44b1184b9b4866e904c49d12ac5042c97dca461b1a5f',
    x: '43dd0fd7215bdcb482879fca3220c6a968e66d70b1356cac18bb26c84a78d729',
    y: 'dcb60106f276b02606d8ef0a328c02e4b629f84f89786af5befb0bc75b6e66be',
    pA: '04a56fa807caaa53a4d28dbb9853b9815c61a411118a6fe516a8798434751470f9010153ac33d0d5f2047ffdb1a3e42c9b4e6be662766e1eeb4116988ede5f912c',
    pB: '0406557e482bd03097ad0cbaa5df82115460d951e3451962f1eaf4367a420676d09857ccbc522686c83d1852abfa8ed6e4a1155cf8f1543ceca528afb591a1e0b7',
    k: '0412af7e89717850671913e6b469ace67bd90a4df8ce45c2af19010175e37eed69f75897996d539356e2fa6a406d528501f907e04d97515fbe83db277b715d3325',
    ke: '0e0672dc86f8e45565d338b0540abe69',
    kcA: '00c12546835755c86d8c0db7851ae86f',
    kcB: 'a9fa3406c3b781b93d804485430ca27a',
    cA: '58ad4aa88e0b60d5061eb6b5dd93e80d9c4f00d127c65b3b35b1b5281fee38f0',
    cB: 'd3e2e547f1ae04f2dbdbf0fc4b79f8ecff2dff314b5d32fe9fcef2fb26dc459b',
  },
  {
    a: '',
    b: '',
    w: '7bf46c454b4c1b25799527d896508afd5fc62ef4ec59db1efb49113063d70cca',
    x: '8cef65df64bb2d0f83540c53632de911b5b24b3eab6cc74a97609fd659e95473',
    y: 'd7a66f64074a84652d8d623a92e20c9675c61cb5b4f6a0063e4648a2fdc02d53',
    pA: '04a65b367a3f613cf9f0654b1b28a1e3a8a40387956c8ba6063e8658563890f46ca1ef6a676598889fc28de2950ab8120b79a5ef1ea4c9f44bc98f585634b46d66',
    pB: '04589f13218822710d98d8b2123a079041052d9941b9cf88c6617ddb2fcc0494662eea8ba6b64692dc318250030c6af045cb738bc81ba35b043c3dcb46adf6f58d',
    k: '041a3c03d51b452537ca2a1fea6110353c6d5ed483c4f0f86f4492ca3f378d40a994b4477f93c64d928edbbcd3e85a7c709b7ea73ee97986ce3d1438e135543772',
    ke: 'fc6374762ba5cf11f4b2caa08b2cd1b9',
    kcA: '5dbd2f477166b7fb6d61febbd77a5563',
    kcB: '7689b4654407a5faeffdc8f18359d8a3',
    cA: 'dfb4db8d48ae5a675963ea5e6c19d98d4ea028d8e898dad96ea19a80ade95dca',
    cB: 'd0f0609d1613138d354f7e95f19fb556bf52d751947241e8c7118df5ef0ae175',
  },
];

describe('SPAKE2', () => {
  it("reproduces RFC 9382's vectors from both sides", () => {
    for (const v of RFC_VECTORS) {
      const w = hexToBytes(v.w);
      const pA = share('a', w, hexToBytes(v.x));
      const pB = share('b', w, hexToBytes(v.y));
      expect(bytesToHex(pA), v.a).toBe(v.pA);
      expect(bytesToHex(pB), v.a).toBe(v.pB);
      const transcript = { aIdentity: utf8Encode(v.a), bIdentity: utf8Encode(v.b) };
      for (const keys of [finish('a', w, hexToBytes(v.x), pA, pB, transcript), finish('b', w, hexToBytes(v.y), pB, pA, transcript)]) {
        expect(bytesToHex(keys.k)).toBe(v.k);
        expect(bytesToHex(keys.ke)).toBe(v.ke);
        expect(bytesToHex(keys.kcA)).toBe(v.kcA);
        expect(bytesToHex(keys.kcB)).toBe(v.kcB);
        expect(bytesToHex(keys.cA)).toBe(v.cA);
        expect(bytesToHex(keys.cB)).toBe(v.cB);
      }
    }
  });

  it('refuses a share that is not a valid 65-byte point (RFC 9382, section 7)', () => {
    const w = passwordScalar(ID);
    const y = randomScalar();
    const pB = share('b', w, y);
    const pA = share('a', w, randomScalar());
    const offCurve = Uint8Array.from(pA);
    offCurve[64] ^= 1;
    const identity = new Uint8Array(65);
    identity[0] = 4;
    const compressed = Uint8Array.from([0x02 | (pA[64] & 1), ...pA.subarray(1, 33)]);
    for (const bad of [offCurve, identity, compressed, pA.subarray(0, 64), new Uint8Array(0)]) {
      expect(() => finish('b', w, y, pB, bad)).toThrow(Spake2Error);
    }
  });

  // RFC 9382, section 7: a share of exactly w*N (or w*M) makes K the identity, and the
  // exchange is aborted rather than keyed from it.
  it('refuses a share that makes the shared point the identity', () => {
    const w = BigInt(`0x${bytesToHex(passwordScalar(ID))}`);
    const wN = p256.Point.fromHex('03d8bbd6c639c62937b04d997f38c3770719c629d7014d49a24b4f98baa1292b49').multiply(w).toBytes(false);
    const x = randomScalar();
    expect(() => finish('a', passwordScalar(ID), x, share('a', passwordScalar(ID), x), wN)).toThrow(/shared point is the identity/);
    // And the daemon's side turns the same away without answering.
    const wM = p256.Point.fromHex('02886e2f97ace46e55ba9dd7242579f2993b64e16ef3dcab95afd497333d8fa12f').multiply(w).toBytes(false);
    const y = randomScalar();
    const pB = share('b', passwordScalar(ID), y);
    expect(acceptAuth(passwordScalar(ID), y, pB, Uint8Array.from([...wM, ...new Uint8Array(32)]))).toBeUndefined();
  });

  it('refuses a zero, out-of-range or short scalar', () => {
    const w = passwordScalar(ID);
    expect(() => share('a', w, new Uint8Array(32))).toThrow(Spake2Error);
    expect(() => share('a', w, new Uint8Array(32).fill(0xff))).toThrow(Spake2Error);
    expect(() => share('a', w, new Uint8Array(31).fill(1))).toThrow(Spake2Error);
  });
});

describe('randomScalar', () => {
  afterEach(() => {
    delete NativeModules.RpiRandom;
  });

  /** The phone's random source, giving `draws` in turn and then the last again. */
  function source(...draws: Uint8Array[]) {
    NativeModules.RpiRandom = { randomBytes: () => base64Encode(draws.length > 1 ? draws.shift()! : draws[0]) };
  }

  it('draws again past zero and past the order, as rejection sampling does', () => {
    const valid = new Uint8Array(32).fill(0x11);
    source(new Uint8Array(32), new Uint8Array(32).fill(0xff), valid);
    expect(randomScalar()).toEqual(valid);
  });

  it('gives up on a source that never gives a usable scalar', () => {
    source(new Uint8Array(32));
    expect(() => randomScalar()).toThrow(Spake2Error);
  });
});

describe('keys', () => {
  it('derives w and S as the vectors say', () => {
    expect(bytesToHex(passwordScalar(ID))).toBe(VECTORS.w);
    expect(bytesToHex(sessionKey(hexToBytes(VECTORS.Ke)))).toBe(VECTORS.S);
  });
});

describe('auth', () => {
  const w = passwordScalar(ID);

  it('is the vectors authentication, byte for byte', () => {
    const pB = share('b', w, hexToBytes(Y));
    expect(bytesToHex(pB)).toBe(VECTORS.pB);
    const app = appAuth(w, pB, hexToBytes(X));
    expect(app.write).toHaveLength(AUTH_WRITE_LENGTH);
    expect(bytesToHex(app.write)).toBe(VECTORS.pA + VECTORS.cA);
    const daemon = acceptAuth(w, hexToBytes(Y), pB, app.write);
    expect(daemon && bytesToHex(daemon.cB)).toBe(VECTORS.cB);
    expect(daemon && verifyConfirmation(app.keys, daemon.cB)).toBe(true);
  });

  it('is refused for the wrong ID, an altered write, another pB and a wrong length', () => {
    const y = randomScalar();
    const pB = share('b', w, y);
    const right = appAuth(w, pB);
    expect(acceptAuth(w, y, pB, right.write)).toBeDefined();

    expect(acceptAuth(w, y, pB, appAuth(passwordScalar(hexToBytes('012346')), pB).write)).toBeUndefined();
    const altered = Uint8Array.from(right.write);
    altered[96] ^= 1;
    expect(acceptAuth(w, y, pB, altered)).toBeUndefined();
    // A recorded write is no good against another pB: nothing to replay.
    const y2 = randomScalar();
    expect(acceptAuth(w, y2, share('b', w, y2), right.write)).toBeUndefined();
    expect(acceptAuth(w, y, pB, right.write.subarray(1))).toBeUndefined();
    expect(acceptAuth(w, y, pB, new Uint8Array(48))).toBeUndefined();
  });

  it('verifies only the 32-byte cB that matches', () => {
    const y = randomScalar();
    const pB = share('b', w, y);
    const app = appAuth(w, pB);
    const daemon = acceptAuth(w, y, pB, app.write)!;
    expect(verifyConfirmation(app.keys, daemon.cB)).toBe(true);
    const wrong = Uint8Array.from(daemon.cB);
    wrong[31] ^= 1;
    expect(verifyConfirmation(app.keys, wrong)).toBe(false);
    expect(verifyConfirmation(app.keys, daemon.cB.subarray(0, 31))).toBe(false);
  });
});

describe('crypto helpers', () => {
  it('compares bytes in constant time by value', () => {
    expect(constantTimeEqual(new Uint8Array([1, 2]), new Uint8Array([1, 2]))).toBe(true);
    expect(constantTimeEqual(new Uint8Array([1, 2]), new Uint8Array([1, 3]))).toBe(false);
    expect(constantTimeEqual(new Uint8Array([1, 2]), new Uint8Array([1]))).toBe(false);
  });

  it('draws fresh random bytes of the asked length', () => {
    const a = randomBytes(16);
    const b = randomBytes(16);
    expect(a).toHaveLength(16);
    expect(bytesToHex(a)).not.toBe(bytesToHex(b));
  });
});
