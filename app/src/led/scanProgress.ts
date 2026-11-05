// What the scan screen says, from the native reader's state frame by frame.
// The reader reports what it measures now (whether the LED passes its test in
// this second, the share of the code its current tile has seen); the screen
// needs what the user is in the middle of. Kept apart from the screen so it
// can be tested.
//
// - Nothing is claimed that was not measured: the
//   bar is the share of the code seen, and it falls only when the reader
//   starts again on another tile.
// - Once the LED has been seen, a moment out of view is not "no LED": the
//   reader keeps what it saw, so the screen says the LED was lost and how
//   much is held, and asks for it back. A gap shorter than LOST_AFTER_MS is
//   not shown at all.
// - The bar is how much of one frame the camera has watched, placed by the rate it was told. It
//   says nothing about whether what was watched is valid: at a wrong rate it fills all the same.
//   Only the CRC says that, so a full bar with no read is said to be exactly that, and only a
//   read is called read (the user: "Our First Duty Is To The Truth").
// - Three frames' time with no read is `stalled`, whatever the phase: a wrong rate or a
//   camera that cannot manage this one reads nothing for ever, and says so in no other way.

export type ScanPhase = 'looking' | 'reading' | 'lost' | 'decoding' | 'read';

/** The part of the reader's state this needs (modules/led-reader, LedReaderState). */
export interface ReaderSnapshot {
  /** Milliseconds. */
  t: number;
  ledSeen: boolean;
  coverage: number;
  idHex?: string;
  tileX?: number;
  tileY?: number;
}

export interface ScanView {
  phase: ScanPhase;
  /** 0..1: the share of one frame watched; 1 at a read, which the CRC has passed. */
  progress: number;
  label: string;
  /** Short, beside the bar: the share watched, while that is news. */
  detail: string;
  /** A sentence under the bar: what to do. */
  hint: string;
  /** Three frames' time and no read: the screen offers another try. */
  stalled: boolean;
}

/** How long the LED must be out of view before the screen says so. */
export const LOST_AFTER_MS = 1_000;
/** How many frames' time before a read is called stalled. */
export const STALLED_AFTER_FRAMES = 3;

export class ScanProgress {
  private best = 0;
  private seen = false;
  private tile?: string;
  private lastSeenAt = 0;
  private readId?: string;
  private firstAt?: number;

  /** `frameMs`: how long one frame takes at the rate being read. */
  constructor(private readonly frameMs: number) {}

  update(state: ReaderSnapshot): ScanView {
    if (state.idHex) this.readId = state.idHex;
    if (this.readId) return { phase: 'read', progress: 1, label: 'Read', detail: '', hint: '', stalled: false };
    this.firstAt ??= state.t;
    return { ...this.reading(state), stalled: state.t - this.firstAt >= STALLED_AFTER_FRAMES * this.frameMs };
  }

  private reading(state: ReaderSnapshot): Omit<ScanView, 'stalled'> {

    if (state.ledSeen) {
      const tile = state.tileX !== undefined && state.tileY !== undefined ? `${state.tileX},${state.tileY}` : undefined;
      // Another tile is another reader, starting from what that tile saw.
      if (tile !== undefined && this.tile !== undefined && tile !== this.tile) this.best = 0;
      if (tile !== undefined) this.tile = tile;
      this.seen = true;
      this.lastSeenAt = state.t;
      this.best = Math.max(this.best, Math.min(1, state.coverage));
    }

    if (!this.seen) {
      return { phase: 'looking', progress: 0, label: 'Looking for the blinking LED', detail: '', hint: 'Point the camera at the Pi; the LED is found anywhere in the picture.' };
    }

    const percent = Math.floor(this.best * 100);
    const lost = !state.ledSeen && state.t - this.lastSeenAt >= LOST_AFTER_MS;

    if (this.best >= 1) {
      return {
        phase: 'decoding',
        progress: 1,
        label: 'A whole frame watched; no valid code yet',
        detail: '',
        hint: lost ? 'The LED is out of view; point the camera back at it.' : 'Still trying to decode what was watched. Hold still.',
      };
    }
    if (lost) {
      return { phase: 'lost', progress: this.best, label: 'Lost sight of the LED', detail: `${percent}%`, hint: 'Point the camera back at the LED.' };
    }
    return { phase: 'reading', progress: this.best, label: 'Watching the LED', detail: `${percent}%`, hint: 'Hold still: the bar is how much of one frame the camera has watched.' };
  }
}
