// What the scan screen says, from the native reader's state frame by frame.
// The reader reports what it measures now (whether the LED passes its test in
// this second, the share of the code its current tile has seen); the screen
// needs what the user is in the middle of. Kept apart from the screen so it
// can be tested.
//
// - Nothing is claimed that was not measured (TODO/ACTIVE.md, Decisions): the
//   bar is the share of the code seen, and it falls only when the reader
//   starts again on another tile.
// - Once the LED has been seen, a moment out of view is not "no LED": the
//   reader keeps what it saw, so the screen says the LED was lost and how
//   much is held, and asks for it back. A gap shorter than LOST_AFTER_MS is
//   not shown at all.
// - All of the code seen and not yet read is "decoding", whether or not the
//   LED is in view this instant.

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
  /** 0..1: the share of the code seen. */
  progress: number;
  label: string;
  /** Short, beside the bar: the share seen. */
  detail: string;
  /** A sentence under the bar: what to do. */
  hint: string;
}

/** How long the LED must be out of view before the screen says so. */
export const LOST_AFTER_MS = 1_000;

export class ScanProgress {
  private best = 0;
  private seen = false;
  private tile?: string;
  private lastSeenAt = 0;
  private readId?: string;

  update(state: ReaderSnapshot): ScanView {
    if (state.idHex) this.readId = state.idHex;
    if (this.readId) return { phase: 'read', progress: 1, label: 'Read', detail: '', hint: '' };

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
        label: 'All of the code seen; decoding',
        detail: '100%',
        hint: lost ? 'The LED is out of view; point the camera back at it.' : 'Hold still.',
      };
    }
    if (lost) {
      return { phase: 'lost', progress: this.best, label: 'Lost sight of the LED', detail: `${percent}%`, hint: 'What was seen is kept; point the camera back at the LED.' };
    }
    return { phase: 'reading', progress: this.best, label: 'Reading the LED', detail: `${percent}%`, hint: 'Hold still: the bar is how much of the code has been seen.' };
  }
}
