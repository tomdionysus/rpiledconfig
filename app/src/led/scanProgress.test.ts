import { describe, expect, it } from 'vitest';
import { LOST_AFTER_MS, ScanProgress } from './scanProgress';

const at = (t: number, ledSeen: boolean, coverage: number, extra: Partial<{ idHex: string; tileX: number; tileY: number }> = {}) => ({
  t,
  ledSeen,
  coverage,
  tileX: 640,
  tileY: 280,
  ...extra,
});

describe('ScanProgress', () => {
  it('looks until the LED is seen, then reads with the share seen', () => {
    const p = new ScanProgress();
    expect(p.update(at(0, false, 0)).phase).toBe('looking');
    const view = p.update(at(100, true, 0.42));
    expect(view.phase).toBe('reading');
    expect(view.progress).toBe(0.42);
    expect(view.detail).toContain('42%');
  });

  it('never goes back to looking once the LED was seen, and keeps the share', () => {
    const p = new ScanProgress();
    p.update(at(0, true, 0.6));
    expect(p.update(at(500, false, 0)).phase).toBe('reading');
    const lost = p.update(at(LOST_AFTER_MS, false, 0));
    expect(lost.phase).toBe('lost');
    expect(lost.progress).toBe(0.6);
    expect(p.update(at(2_000, true, 0.7)).phase).toBe('reading');
  });

  it('stays decoding once all of the code was seen, even with the LED out of view', () => {
    const p = new ScanProgress();
    p.update(at(0, true, 1));
    const gone = p.update(at(5_000, false, 0));
    expect(gone.phase).toBe('decoding');
    expect(gone.progress).toBe(1);
    expect(gone.hint).toContain('out of view');
  });

  it('is read once an ID comes, and stays read', () => {
    const p = new ScanProgress();
    p.update(at(0, true, 1));
    expect(p.update(at(100, false, 0, { idHex: '0123456789abcdef' })).phase).toBe('read');
    expect(p.update(at(200, false, 0)).phase).toBe('read');
  });

  it('starts the share again when the reader moves to another tile', () => {
    const p = new ScanProgress();
    p.update(at(0, true, 0.8));
    expect(p.update(at(100, true, 0.1, { tileX: 0, tileY: 0 })).progress).toBe(0.1);
  });
});
