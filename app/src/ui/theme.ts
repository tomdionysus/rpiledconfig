// The app's design tokens. Dark only; one accent hue, the green of the Pi's
// activity LED, which every accent colour below is derived from.
import { Platform } from 'react-native';

/** The one hue: a green close to the ACT LED. Change this to retheme. */
export const ACCENT = '#3ddc84';
const ACCENT_RGB = '61, 220, 132';

export const colors = {
  background: '#0d110e',
  backgroundLift: '#121713',
  surface: '#161c18',
  surface2: '#1f2721',
  surface3: '#29332b',

  text: '#e4e8e5',
  textDim: '#a9b1ab',
  textFaint: '#737b75',

  accent: ACCENT,
  accentEdge: `rgba(${ACCENT_RGB}, 0.55)`,
  accentSurface: `rgba(${ACCENT_RGB}, 0.12)`,
  accentSurfaceStrong: `rgba(${ACCENT_RGB}, 0.22)`,
  onAccent: '#06130b',

  border: 'rgba(228, 232, 229, 0.09)',
  borderStrong: 'rgba(228, 232, 229, 0.16)',
  track: 'rgba(228, 232, 229, 0.14)',
  danger: '#ff6b6b',
  warn: '#e8b33a',
  ok: ACCENT,
  scrim: 'rgba(6, 8, 7, 0.72)',
} as const;

export const space = {
  xs: 4,
  sm: 8,
  md: 12,
  lg: 16,
  xl: 24,
  xxl: 32,
} as const;

export const radius = {
  sm: 6,
  md: 10,
  lg: 14,
  xl: 22,
  pill: 999,
} as const;

export const type = {
  display: { fontSize: 26, fontWeight: '600' as const, letterSpacing: 0.2 },
  title: { fontSize: 20, fontWeight: '600' as const },
  heading: { fontSize: 16, fontWeight: '600' as const },
  body: { fontSize: 15, fontWeight: '400' as const },
  label: { fontSize: 13, fontWeight: '500' as const },
  caption: { fontSize: 12, fontWeight: '400' as const },
  micro: { fontSize: 11, fontWeight: '500' as const, letterSpacing: 0.8 },
} as const;

/** Minimum height of every interactive control, in points. */
export const TOUCH_TARGET = 44;

export const monospace = Platform.select({ ios: 'Menlo', android: 'monospace', default: 'monospace' });
