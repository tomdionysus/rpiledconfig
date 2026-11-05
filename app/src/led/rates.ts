// The LED's rates (docs/protocol.md, Symbols). The phone chooses one and asks the Pi for it, so
// the two cannot disagree. Kept apart from the screens so it can be tested.
import { BLINK_FAST, BLINK_NORMAL, BLINK_ROBUST } from '@/protocol/constants';

export type LedRate = 'fast' | 'normal' | 'robust';

export const DEFAULT_RATE: LedRate = 'normal';

/** Fastest first, as the control shows them. */
export const RATES: readonly LedRate[] = ['fast', 'normal', 'robust'];

const FRAME_SYMBOLS = 170;

export const RATE_INFO: Record<LedRate, { label: string; blink: number; symbolMs: number }> = {
  fast: { label: 'Fast', blink: BLINK_FAST, symbolMs: 33 },
  normal: { label: 'Normal', blink: BLINK_NORMAL, symbolMs: 51 },
  robust: { label: 'Robust', blink: BLINK_ROBUST, symbolMs: 77 },
};

/** How long one frame takes on the LED, in milliseconds. */
export function frameMs(rate: LedRate): number {
  return FRAME_SYMBOLS * RATE_INFO[rate].symbolMs;
}

/** The next rate slower than this one, or undefined at the slowest. */
export function slowerRate(rate: LedRate): LedRate | undefined {
  return RATES[RATES.indexOf(rate) + 1];
}

/** A rate from storage: anything else, an older or a damaged value, is the default. */
export function parseRate(value: string | null | undefined): LedRate {
  return value === 'fast' || value === 'normal' || value === 'robust' ? value : DEFAULT_RATE;
}
