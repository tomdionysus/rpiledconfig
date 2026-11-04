// Reed-Solomon RS(14, 7) over GF(2^8) for the LED frame (docs/protocol.md,
// The codeword): generator roots a^0..a^6, systematic, message first.
// Decoding corrects errors and erasures together, 2 x errors + erasures <=
// 7: syndromes, Forney syndromes to take the erasures out, Berlekamp-Massey
// for the error locator, Chien search for the positions, and Forney's
// formula for the values. Written from the textbook algorithm (the layout
// follows Wikiversity's "Reed-Solomon codes for coders"), no library.
//
// A word beyond the code's reach either fails here or decodes to some other
// codeword; the magic, version and CRC checked afterwards (codeword.ts) are
// what catch the second case.
import { LED_CODEWORD_BYTES, LED_PARITY_BYTES } from '@/protocol/constants';
import { alphaPow, gfDiv, gfInverse, gfMul, polyDivmod, polyEval, polyMul, type Poly } from './gf256';

const NSYM = LED_PARITY_BYTES;

/** g(x) = (x - a^0)(x - a^1)...(x - a^(nsym-1)), highest power first. */
export function generatorPoly(nsym = NSYM): Poly {
  let g: Poly = [1];
  for (let i = 0; i < nsym; i += 1) g = polyMul(g, [1, alphaPow(i)]);
  return g;
}

const GENERATOR = generatorPoly();

/** The message followed by its parity: the remainder of m(x) x^nsym divided by g(x). */
export function rsEncode(message: Uint8Array): Uint8Array {
  if (message.length + NSYM !== LED_CODEWORD_BYTES) throw new Error(`message must be ${LED_CODEWORD_BYTES - NSYM} bytes`);
  const { remainder } = polyDivmod([...message, ...new Array<number>(NSYM).fill(0)], GENERATOR);
  const out = new Uint8Array(LED_CODEWORD_BYTES);
  out.set(message);
  out.set(remainder, message.length);
  return out;
}

/** S_j = r(a^j), j = 0..nsym-1. */
export function syndromes(word: readonly number[]): number[] {
  const out: number[] = [];
  for (let j = 0; j < NSYM; j += 1) out.push(polyEval(word as Poly, alphaPow(j)));
  return out;
}

export interface RsResult {
  codeword: Uint8Array;
  /** Bytes that were wrong and were not marked erased. */
  errors: number;
  /** Erased bytes, filled in. */
  erasures: number;
}

/**
 * Corrects `received` (24 bytes) given the indices of erased bytes. Returns
 * undefined when the pattern is beyond the code: too many erasures, a
 * locator whose roots do not all lie in the word, or a result that is still
 * not a codeword.
 */
export function rsDecode(received: Uint8Array, erasures: readonly number[] = []): RsResult | undefined {
  const n = received.length;
  if (n !== LED_CODEWORD_BYTES) throw new Error(`a codeword is ${LED_CODEWORD_BYTES} bytes`);
  const erasePos = [...new Set(erasures)].filter((p) => p >= 0 && p < n).sort((a, b) => a - b);
  if (erasePos.length > NSYM) return undefined;

  const word = Array.from(received);
  for (const p of erasePos) word[p] = 0;
  const synd = syndromes(word);
  if (synd.every((s) => s === 0)) return { codeword: Uint8Array.from(word), errors: 0, erasures: erasePos.length };

  // Errors: the syndromes with the erasures' contribution removed, then
  // Berlekamp-Massey over what is left.
  const fsynd = forneySyndromes(synd, erasePos, n);
  const errLoc = errorLocator(fsynd, NSYM - erasePos.length);
  if (!errLoc) return undefined;
  const errPos = chienSearch(errLoc, n);
  if (!errPos) return undefined;
  if (errPos.some((p) => erasePos.includes(p))) return undefined;

  const all = [...erasePos, ...errPos];
  if (2 * errPos.length + erasePos.length > NSYM) return undefined;
  const corrected = correctErrata(word, synd, all);
  if (!corrected) return undefined;
  if (syndromes(corrected).some((s) => s !== 0)) return undefined;
  return { codeword: Uint8Array.from(corrected), errors: errPos.length, erasures: erasePos.length };
}

/** Position p in the word is the coefficient of x^(n-1-p); its locator is a^(n-1-p). */
function locatorOf(position: number, n: number): number {
  return alphaPow(n - 1 - position);
}

function forneySyndromes(synd: number[], erasePos: number[], n: number): number[] {
  // Synd as a polynomial with S_0 as the lowest power, multiplied by each
  // erasure's (1 + X x) and kept to its length: the classic in-place form.
  const fsynd = synd.slice();
  for (const p of erasePos) {
    const x = locatorOf(p, n);
    for (let j = fsynd.length - 1; j > 0; j -= 1) fsynd[j] = gfMul(fsynd[j - 1], x) ^ fsynd[j];
  }
  // The first `erasures` entries are now mixed with the erasures' terms; the
  // rest carry only the errors.
  return fsynd.slice(erasePos.length);
}

/**
 * Berlekamp-Massey over `synd` (lowest index first), giving the error
 * locator Lambda(x) = 1 + L_1 x + ... as an array lowest power first, or
 * undefined if it implies more errors than `capacity` syndromes can find.
 */
function errorLocator(synd: number[], capacity: number): number[] | undefined {
  let lambda: number[] = [1];
  let prev: number[] = [1];
  let l = 0;
  let m = 1;
  let b = 1;
  for (let k = 0; k < synd.length; k += 1) {
    let delta = synd[k];
    for (let i = 1; i <= l; i += 1) delta ^= gfMul(lambda[i] ?? 0, synd[k - i]);
    if (delta === 0) {
      m += 1;
      continue;
    }
    const scale = gfDiv(delta, b);
    const next = lambda.slice();
    for (let i = 0; i < prev.length; i += 1) {
      const at = i + m;
      while (next.length <= at) next.push(0);
      next[at] ^= gfMul(scale, prev[i]);
    }
    if (2 * l <= k) {
      prev = lambda;
      l = k + 1 - l;
      b = delta;
      m = 1;
    } else {
      m += 1;
    }
    lambda = next;
  }
  while (lambda.length > 1 && lambda[lambda.length - 1] === 0) lambda.pop();
  const errors = lambda.length - 1;
  if (errors !== l || 2 * errors > capacity) return undefined;
  return lambda;
}

/** The positions whose inverse locator is a root of Lambda, or undefined if fewer roots than its degree. */
function chienSearch(lambdaLowFirst: number[], n: number): number[] | undefined {
  const errors = lambdaLowFirst.length - 1;
  if (errors === 0) return [];
  const highFirst = lambdaLowFirst.slice().reverse();
  const found: number[] = [];
  for (let p = 0; p < n; p += 1) {
    if (polyEval(highFirst, gfInverse(locatorOf(p, n))) === 0) found.push(p);
  }
  return found.length === errors ? found : undefined;
}

/** Forney's formula for every errata position at once, given the full syndromes. */
function correctErrata(word: number[], synd: number[], positions: number[]): number[] | undefined {
  const n = word.length;
  const X = positions.map((p) => locatorOf(p, n));
  // Errata locator, lowest power first: product of (1 + X_i x).
  let loc: number[] = [1];
  for (const xi of X) {
    const next = new Array<number>(loc.length + 1).fill(0);
    for (let i = 0; i < loc.length; i += 1) {
      next[i] ^= loc[i];
      next[i + 1] ^= gfMul(loc[i], xi);
    }
    loc = next;
  }
  // Evaluator Omega(x) = S(x) Lambda(x) mod x^nsym, lowest power first.
  const omega = new Array<number>(NSYM).fill(0);
  for (let i = 0; i < NSYM; i += 1) {
    for (let j = 0; j < loc.length && i + j < NSYM; j += 1) omega[i + j] ^= gfMul(synd[i], loc[j]);
  }
  const out = word.slice();
  for (let i = 0; i < X.length; i += 1) {
    const xiInv = gfInverse(X[i]);
    // Lambda'(X_i^-1) by the product over the other locators.
    let denom = 1;
    for (let j = 0; j < X.length; j += 1) if (j !== i) denom = gfMul(denom, 1 ^ gfMul(xiInv, X[j]));
    if (denom === 0) return undefined;
    const num = polyEval(omega.slice().reverse(), xiInv);
    // With the first root at a^0, Omega(X_i^-1) = e_i * prod_{j != i}(1 + X_j X_i^-1),
    // so the magnitude is the quotient (Forney's X_i^(1-c) and Lambda's
    // X_i cancel).
    out[positions[i]] ^= gfDiv(num, denom);
  }
  return out;
}
