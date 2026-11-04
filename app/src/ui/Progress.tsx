// A labelled progress bar, for the decoder's bits and the pairing steps.
import { StyleSheet, Text, View } from 'react-native';
import { colors, radius, space, type as typography } from './theme';

interface ProgressProps {
  /** 0..1 */
  value: number;
  label: string;
  /** Text at the right end, usually the count behind `value`. */
  detail?: string;
  tone?: 'accent' | 'warn';
}

export function Progress({ value, label, detail, tone = 'accent' }: ProgressProps) {
  const width = `${Math.round(Math.max(0, Math.min(1, value)) * 100)}%` as const;
  return (
    <View style={styles.root} accessibilityRole="progressbar" accessibilityValue={{ min: 0, max: 100, now: Math.round(value * 100) }}>
      <View style={styles.labels}>
        <Text style={styles.label}>{label}</Text>
        {detail ? <Text style={styles.detail}>{detail}</Text> : null}
      </View>
      <View style={styles.track}>
        <View style={[styles.fill, { width }, tone === 'warn' && styles.warn]} />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    gap: space.xs,
  },
  labels: {
    flexDirection: 'row',
    justifyContent: 'space-between',
  },
  label: {
    ...typography.label,
    color: colors.text,
  },
  detail: {
    ...typography.caption,
    color: colors.textFaint,
  },
  track: {
    height: 6,
    borderRadius: radius.pill,
    backgroundColor: colors.track,
    overflow: 'hidden',
  },
  fill: {
    height: '100%',
    borderRadius: radius.pill,
    backgroundColor: colors.accent,
  },
  warn: {
    backgroundColor: colors.warn,
  },
});
