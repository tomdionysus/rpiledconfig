import { describe, expect, it } from 'vitest';
import { LOST_AFTER_MS, ScanProgress, STALLED_AFTER_FRAMES } from './scanProgress';

const FRAME_MS = 8_500;

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
    const p = new ScanProgress(FRAME_MS);
    expect(p.update(at(0, false, 0)).phase).toBe('looking');
    const view = p.update(at(100, true, 0.42));
    expect(view.phase).toBe('reading');
    expect(view.progress).toBe(0.42);
    expect(view.detail).toContain('42%');
  });

  it('never goes back to looking once the LED was seen, and keeps the share', () => {
    const p = new ScanProgress(FRAME_MS);
    p.update(at(0, true, 0.6));
    expect(p.update(at(500, false, 0)).phase).toBe('reading');
    const lost = p.update(at(LOST_AFTER_MS, false, 0));
    expect(lost.phase).toBe('lost');
    expect(lost.progress).toBe(0.6);
    expect(p.update(at(2_000, true, 0.7)).phase).toBe('reading');
  });

  it('stays decoding once all of the code was seen, even with the LED out of view', () => {
    const p = new ScanProgress(FRAME_MS);
    p.update(at(0, true, 1));
    const gone = p.update(at(5_000, false, 0));
    expect(gone.phase).toBe('decoding');
    expect(gone.progress).toBe(1);
    expect(gone.hint).toContain('out of view');
  });

  it('is read once an ID comes, and stays read', () => {
    const p = new ScanProgress(FRAME_MS);
    p.update(at(0, true, 1));
    expect(p.update(at(100, false, 0, { idHex: '0123456789abcdef' })).phase).toBe('read');
    expect(p.update(at(200, false, 0)).phase).toBe('read');
  });

  it('starts the share again when the reader moves to another tile', () => {
    const p = new ScanProgress(FRAME_MS);
    p.update(at(0, true, 0.8));
    expect(p.update(at(100, true, 0.1, { tileX: 0, tileY: 0 })).progress).toBe(0.1);
  });

  it('calls a read stalled after three frames, whatever it is showing, and not before', () => {
    const limit = STALLED_AFTER_FRAMES * FRAME_MS;
    const decoding = new ScanProgress(FRAME_MS);
    decoding.update(at(1_000, true, 1));
    expect(decoding.update(at(1_000 + limit - 1, true, 1))).toMatchObject({ phase: 'decoding', stalled: false });
    expect(decoding.update(at(1_000 + limit, true, 1))).toMatchObject({ phase: 'decoding', stalled: true });

    const looking = new ScanProgress(FRAME_MS);
    looking.update(at(0, false, 0));
    expect(looking.update(at(limit, false, 0))).toMatchObject({ phase: 'looking', stalled: true });
  });

  it('allows a slower rate longer', () => {
    const slow = new ScanProgress(12_750);
    slow.update(at(0, true, 0.5));
    expect(slow.update(at(3 * FRAME_MS, true, 0.5)).stalled).toBe(false);
  });

  it('shows the bar full at a read, however little of the code had been seen', () => {
    const p = new ScanProgress(FRAME_MS);
    expect(p.update(at(0, true, 0.64)).progress).toBe(0.64);
    expect(p.update(at(100, true, 0.64, { idHex: '0123456789abcdef' }))).toMatchObject({ phase: 'read', progress: 1 });
  });

  it('does not claim the code from a full bar: only a read is a read', () => {
    const p = new ScanProgress(FRAME_MS);
    const view = p.update(at(0, true, 1));
    expect(view.phase).toBe('decoding');
    expect(view.label).toBe('A whole frame watched; no valid code yet');
    expect(view.detail).toBe('');
    expect(`${view.label} ${view.hint}`).not.toMatch(/all of the code|code seen|100%/i);
  });

  it('reads the LED as one tile when the reader does not say which', () => {
    const p = new ScanProgress(FRAME_MS);
    p.update({ t: 0, ledSeen: true, coverage: 0.3 });
    const view = p.update({ t: 100, ledSeen: true, coverage: 0.2 });
    expect(view.phase).toBe('reading');
    expect(view.progress).toBe(0.3);
  });

  it('is never stalled once read', () => {
    const p = new ScanProgress(FRAME_MS);
    p.update(at(0, true, 1));
    p.update(at(100, true, 1, { idHex: '0123456789abcdef' }));
    expect(p.update(at(10 * FRAME_MS, true, 1)).stalled).toBe(false);
  });
});
