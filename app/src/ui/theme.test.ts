// The design tokens' own rules: every accent colour is the one hue, and text is readable on
// every surface by WCAG 2.2's contrast ratio (1.4.3: 4.5:1 for text, 3:1 for large text and
// for the parts of controls, 1.4.11).
import { describe, expect, it } from 'vitest';
import { ACCENT, colors, monospace, TOUCH_TARGET } from './theme';

function rgb(hex: string): [number, number, number] {
  const value = /^#([0-9a-f]{6})$/i.exec(hex);
  if (!value) throw new Error(`not #rrggbb: ${hex}`);
  const n = parseInt(value[1], 16);
  return [(n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff];
}

function luminance(hex: string): number {
  const [r, g, b] = rgb(hex).map((channel) => {
    const c = channel / 255;
    return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  });
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

function contrast(a: string, b: string): number {
  const [light, dark] = [luminance(a), luminance(b)].sort((x, y) => y - x);
  return (light + 0.05) / (dark + 0.05);
}

const SURFACES = ['background', 'backgroundLift', 'surface', 'surface2', 'surface3'] as const;

describe('theme', () => {
  it('measures contrast as WCAG does', () => {
    expect(contrast('#ffffff', '#000000')).toBeCloseTo(21, 5);
    expect(contrast('#777777', '#ffffff')).toBeCloseTo(4.48, 2);
  });

  it('gives text, dim text, the accent and the warnings 4.5:1 on every surface', () => {
    for (const surface of SURFACES) {
      for (const ink of ['text', 'textDim', 'accent', 'danger', 'warn'] as const) {
        expect(contrast(colors[ink], colors[surface]), `${ink} on ${surface}`).toBeGreaterThanOrEqual(4.5);
      }
    }
    expect(contrast(colors.onAccent, colors.accent)).toBeGreaterThanOrEqual(4.5);
  });

  // textFaint is used for captions and placeholders, and reaches 4.5:1 on none of the
  // surfaces (4.37 on background down to 3.01 on surface3); held here at 3:1 so it does not
  // get fainter.
  it('gives faint text at least 3:1 on every surface', () => {
    for (const surface of SURFACES) {
      expect(contrast(colors.textFaint, colors[surface]), `textFaint on ${surface}`).toBeGreaterThanOrEqual(3);
    }
  });

  it('derives every accent colour from the one hue', () => {
    const hue = rgb(ACCENT).join(', ');
    expect(colors.accent).toBe(ACCENT);
    expect(colors.ok).toBe(ACCENT);
    for (const token of ['accentEdge', 'accentSurface', 'accentSurfaceStrong'] as const) {
      expect(colors[token]).toMatch(new RegExp(`^rgba\\(${hue}, 0\\.\\d+\\)$`));
    }
  });

  it('keeps controls large enough to touch, and has a monospace face', () => {
    expect(TOUCH_TARGET).toBeGreaterThanOrEqual(44);
    expect(monospace).toBe('Menlo');
  });
});
