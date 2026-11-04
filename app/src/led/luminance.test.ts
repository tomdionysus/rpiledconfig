import { describe, expect, it } from 'vitest';
import { brightLuminance, centredRegion, frameTimestampMs, meanLuminance } from './luminance';

/** A `width` x `height` Y plane with a padded row stride, `fill(x, y)` per pixel. */
function plane(width: number, height: number, bytesPerRow: number, fill: (x: number, y: number) => number): Uint8Array {
  const out = new Uint8Array(bytesPerRow * height);
  for (let y = 0; y < height; y += 1) for (let x = 0; x < width; x += 1) out[y * bytesPerRow + x] = fill(x, y);
  return out;
}

describe('brightLuminance', () => {
  // A 100 x 100 region of background 30 with a 5 x 5 LED of 220 off to one side: the
  // plain mean barely moves when the LED lights, the brightest 2% follow it.
  const frame = (led: boolean) => plane(100, 100, 100, (x, y) => (led && x >= 70 && x < 75 && y >= 20 && y < 25 ? 220 : 30));
  const all = { x: 0, y: 0, width: 100, height: 100 };

  it('follows a small LED anywhere in the region', () => {
    const meanSwing = meanLuminance(frame(true), 100, 100, 100, all) - meanLuminance(frame(false), 100, 100, 100, all);
    const brightSwing = brightLuminance(frame(true), 100, 100, 100, all) - brightLuminance(frame(false), 100, 100, 100, all);
    expect(meanSwing).toBeLessThan(1);
    // 25 LED pixels in the brightest 200: a swing of about 24, some 50 times the mean's.
    expect(brightSwing).toBeGreaterThan(20);
  });

  it('is the background when nothing is lit, and 0 for an empty region', () => {
    expect(brightLuminance(frame(false), 100, 100, 100, all)).toBe(30);
    expect(brightLuminance(new Uint8Array(0), 4, 4, 4, { x: 0, y: 0, width: 2, height: 2 })).toBe(0);
  });
});

describe('meanLuminance', () => {
  it('averages only the region, honouring the row stride', () => {
    // Bright square at 4..8 x 4..8, padding bytes are 255 to catch a stride mistake.
    const y = plane(10, 10, 16, (x, row) => (x >= 4 && x < 8 && row >= 4 && row < 8 ? 200 : 0));
    for (let row = 0; row < 10; row += 1) for (let x = 10; x < 16; x += 1) y[row * 16 + x] = 255;
    expect(meanLuminance(y, 10, 10, 16, { x: 4, y: 4, width: 4, height: 4 })).toBe(200);
    expect(meanLuminance(y, 10, 10, 16, { x: 0, y: 0, width: 4, height: 4 })).toBe(0);
    expect(meanLuminance(y, 10, 10, 16, { x: 2, y: 4, width: 4, height: 4 })).toBe(100);
  });

  it('clips the region to the frame and returns 0 for nothing', () => {
    const y = plane(4, 4, 4, () => 10);
    expect(meanLuminance(y, 4, 4, 4, { x: 2, y: 2, width: 10, height: 10 })).toBe(10);
    expect(meanLuminance(y, 4, 4, 4, { x: 6, y: 6, width: 2, height: 2 })).toBe(0);
    expect(meanLuminance(new Uint8Array(0), 4, 4, 4, { x: 0, y: 0, width: 2, height: 2 })).toBe(0);
  });

  it('samples every n-th pixel with step', () => {
    const y = plane(8, 8, 8, (x) => (x % 2 === 0 ? 100 : 0));
    expect(meanLuminance(y, 8, 8, 8, { x: 0, y: 0, width: 8, height: 8 }, 'y', 1)).toBe(50);
    expect(meanLuminance(y, 8, 8, 8, { x: 0, y: 0, width: 8, height: 8 }, 'y', 2)).toBe(100);
  });

  it('takes luma from BGRA when the frame is rgb', () => {
    const bgra = new Uint8Array(2 * 2 * 4);
    // Pure green pixels: luma weight 150/256 of 255.
    for (let i = 0; i < 4; i += 1) bgra.set([0, 255, 0, 255], i * 4);
    expect(meanLuminance(bgra, 2, 2, 8, { x: 0, y: 0, width: 2, height: 2 }, 'bgra')).toBe(Math.floor((255 * 150) / 256));
    // White is full scale.
    bgra.fill(255);
    expect(meanLuminance(bgra, 2, 2, 8, { x: 0, y: 0, width: 2, height: 2 }, 'bgra')).toBe(255);
  });

  it('centres a square on the shorter side', () => {
    expect(centredRegion(1920, 1080, 0.25)).toEqual({ x: 825, y: 405, width: 270, height: 270 });
    expect(centredRegion(100, 200, 0.5)).toEqual({ x: 25, y: 75, width: 50, height: 50 });
  });

  it('brings Android nanoseconds to the milliseconds iOS reports', () => {
    expect(frameTimestampMs(1_500_000_000, 'android')).toBe(1500);
    expect(frameTimestampMs(1500, 'ios')).toBe(1500);
  });
});
