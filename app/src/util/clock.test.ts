import { describe, expect, it } from 'vitest';
import { ManualClock, sleep } from './clock';

describe('ManualClock', () => {
  it('fires timers in order of due time, then of setting', async () => {
    const clock = new ManualClock(1000);
    const fired: string[] = [];
    clock.after(30, () => fired.push('c'));
    clock.after(10, () => fired.push('a'));
    clock.after(10, () => fired.push('b'));
    const cancel = clock.after(20, () => fired.push('cancelled'));
    cancel();
    await clock.advance(25);
    expect(fired).toEqual(['a', 'b']);
    expect(clock.now()).toBe(1025);
    await clock.advance(5);
    expect(fired).toEqual(['a', 'b', 'c']);
    expect(clock.pending).toBe(0);
  });

  it('fires a timer set by a promise chain within the same advance', async () => {
    const clock = new ManualClock();
    const times: number[] = [];
    const chain = (async () => {
      await sleep(clock, 100);
      times.push(clock.now());
      await Promise.resolve();
      await sleep(clock, 100);
      times.push(clock.now());
    })();
    await clock.advance(250);
    await chain;
    expect(times).toEqual([100, 200]);
    expect(clock.now()).toBe(250);
  });

  it('runs a promise to completion in manual time, or says it is stuck', async () => {
    const clock = new ManualClock();
    await expect(clock.run(sleep(clock, 45_000).then(() => clock.now()))).resolves.toBe(45_000);
    await expect(clock.run(new Promise(() => {}), 5_000, 1_000)).rejects.toThrow(/still pending/);
  });
});
