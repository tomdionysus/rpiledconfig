// GF(2^8) with the primitive polynomial 0x11D and a = 2, for the LED
// frame's Reed-Solomon code. Polynomials are arrays of coefficients, highest
// power first, as the codeword is sent (message first).
import { GF_GENERATOR, GF_PRIMITIVE } from '@/protocol/constants';

const EXP = new Uint8Array(512);
const LOG = new Uint8Array(256);
{
  let x = 1;
  for (let i = 0; i < 255; i += 1) {
    EXP[i] = x;
    LOG[x] = i;
    // Multiply by a = 2: shift, and reduce by the primitive polynomial.
    x <<= 1;
    if (x & 0x100) x ^= GF_PRIMITIVE;
  }
  // Doubled, so a product's log sum needs no reduction.
  for (let i = 255; i < 512; i += 1) EXP[i] = EXP[i - 255];
  if (GF_GENERATOR !== 2) throw new Error('the tables assume a = 2');
}

export function gfMul(x: number, y: number): number {
  if (x === 0 || y === 0) return 0;
  return EXP[LOG[x] + LOG[y]];
}

export function gfDiv(x: number, y: number): number {
  if (y === 0) throw new Error('division by zero in GF(2^8)');
  if (x === 0) return 0;
  return EXP[(LOG[x] + 255 - LOG[y]) % 255];
}

/** x to the power p; p may be negative. */
export function gfPow(x: number, p: number): number {
  if (x === 0) return p === 0 ? 1 : 0;
  return EXP[(((LOG[x] * p) % 255) + 255) % 255];
}

export function gfInverse(x: number): number {
  return gfDiv(1, x);
}

/** a^p, the generator raised to p. */
export function alphaPow(p: number): number {
  return EXP[((p % 255) + 255) % 255];
}

export type Poly = number[];

export function polyScale(p: Poly, x: number): Poly {
  return p.map((c) => gfMul(c, x));
}

/** Sum (and difference: the same in characteristic 2), aligned at the lowest power. */
export function polyAdd(p: Poly, q: Poly): Poly {
  const r = new Array<number>(Math.max(p.length, q.length)).fill(0);
  for (let i = 0; i < p.length; i += 1) r[i + r.length - p.length] = p[i];
  for (let i = 0; i < q.length; i += 1) r[i + r.length - q.length] ^= q[i];
  return r;
}

export function polyMul(p: Poly, q: Poly): Poly {
  const r = new Array<number>(p.length + q.length - 1).fill(0);
  for (let j = 0; j < q.length; j += 1) {
    for (let i = 0; i < p.length; i += 1) r[i + j] ^= gfMul(p[i], q[j]);
  }
  return r;
}

/** Horner's rule. */
export function polyEval(p: Poly, x: number): number {
  let y = p[0] ?? 0;
  for (let i = 1; i < p.length; i += 1) y = gfMul(y, x) ^ p[i];
  return y;
}

/** Quotient and remainder of `dividend` by a monic-or-not `divisor` (synthetic division). */
export function polyDivmod(dividend: Poly, divisor: Poly): { quotient: Poly; remainder: Poly } {
  const out = dividend.slice();
  const lead = divisor[0];
  for (let i = 0; i <= dividend.length - divisor.length; i += 1) {
    out[i] = gfDiv(out[i], lead);
    const coef = out[i];
    if (coef === 0) continue;
    for (let j = 1; j < divisor.length; j += 1) {
      if (divisor[j] !== 0) out[i + j] ^= gfMul(divisor[j], coef);
    }
  }
  const split = out.length - (divisor.length - 1);
  return { quotient: out.slice(0, split), remainder: out.slice(split) };
}
