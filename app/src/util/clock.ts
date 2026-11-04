// Time as the pairing client and the fake daemon see it. Every wait and
// deadline in those goes through a Clock, so a test can run a 45-second WiFi
// join or the daemon's 30-second authentication limit in a few milliseconds
// with a ManualClock, and the app runs on the real one.

export interface Clock {
  /** Milliseconds, any origin, monotonic enough for deadlines. */
  now(): number;
  /** Calls `fn` once after `ms`; returns the function that cancels it. */
  after(ms: number, fn: () => void): () => void;
}

export const realClock: Clock = {
  now: () => Date.now(),
  after(ms, fn) {
    const handle = setTimeout(fn, ms);
    return () => clearTimeout(handle);
  },
};

export function sleep(clock: Clock, ms: number): Promise<void> {
  return new Promise((resolve) => {
    clock.after(Math.max(0, ms), resolve);
  });
}

interface Timer {
  at: number;
  seq: number;
  fn: () => void;
}

/** One real macrotask: every promise continuation queued so far has run by the time it resolves. */
function settle(): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, 0));
}

/**
 * A clock that moves only when told. `advance` fires the due timers in order
 * and lets the promise chains they start run between them, so a timer set by
 * one of those chains is fired in the same advance if it falls due.
 */
export class ManualClock implements Clock {
  private t: number;
  private seq = 0;
  private timers: Timer[] = [];

  constructor(start = 0) {
    this.t = start;
  }

  now(): number {
    return this.t;
  }

  after(ms: number, fn: () => void): () => void {
    const timer = { at: this.t + Math.max(0, ms), seq: this.seq++, fn };
    this.timers.push(timer);
    return () => {
      this.timers = this.timers.filter((entry) => entry !== timer);
    };
  }

  /** Timers not yet fired. */
  get pending(): number {
    return this.timers.length;
  }

  async advance(ms: number): Promise<void> {
    const target = this.t + ms;
    await settle();
    for (;;) {
      const next = this.timers.reduce<Timer | undefined>((best, timer) => (!best || timer.at < best.at || (timer.at === best.at && timer.seq < best.seq) ? timer : best), undefined);
      if (!next || next.at > target) break;
      this.timers = this.timers.filter((timer) => timer !== next);
      this.t = next.at;
      next.fn();
      await settle();
    }
    this.t = target;
    await settle();
  }

  /** Advances in `stepMs` steps until `promise` settles or `limitMs` has passed, and returns it. */
  async run<T>(promise: Promise<T>, limitMs = 300_000, stepMs = 100): Promise<T> {
    let done = false;
    promise.then(
      () => (done = true),
      () => (done = true),
    );
    for (let elapsed = 0; !done && elapsed < limitMs; elapsed += stepMs) await this.advance(stepMs);
    if (!done) throw new Error(`still pending after ${limitMs} ms of manual time`);
    return promise;
  }
}
