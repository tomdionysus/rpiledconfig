// The app's one button, in three weights, and a fourth for what cannot be undone.
import { ActivityIndicator, Pressable, StyleSheet, Text, type StyleProp, type ViewStyle } from 'react-native';
import { colors, radius, space, type as typography, TOUCH_TARGET } from './theme';

interface ButtonProps {
  label: string;
  onPress(): void;
  variant?: 'primary' | 'secondary' | 'quiet' | 'danger';
  disabled?: boolean;
  busy?: boolean;
  style?: StyleProp<ViewStyle>;
}

export function Button({ label, onPress, variant = 'primary', disabled, busy, style }: ButtonProps) {
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityState={{ disabled: disabled || busy }}
      disabled={disabled || busy}
      onPress={onPress}
      style={({ pressed }) => [
        styles.base,
        variant === 'primary' && styles.primary,
        variant === 'secondary' && styles.secondary,
        variant === 'quiet' && styles.quiet,
        variant === 'danger' && styles.danger,
        (disabled || busy) && styles.disabled,
        pressed && styles.pressed,
        style,
      ]}>
      {busy ? <ActivityIndicator color={variant === 'primary' ? colors.onAccent : colors.text} /> : null}
      <Text style={[styles.label, variant === 'primary' && styles.primaryLabel, variant === 'quiet' && styles.quietLabel, variant === 'danger' && styles.dangerLabel]}>{label}</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  base: {
    minHeight: TOUCH_TARGET + 4,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: space.sm,
    paddingHorizontal: space.xl,
    borderRadius: radius.pill,
    borderWidth: StyleSheet.hairlineWidth,
  },
  primary: {
    backgroundColor: colors.accent,
    borderColor: colors.accent,
  },
  secondary: {
    backgroundColor: colors.surface2,
    borderColor: colors.borderStrong,
  },
  quiet: {
    backgroundColor: 'transparent',
    borderColor: 'transparent',
    paddingHorizontal: space.md,
  },
  danger: {
    backgroundColor: 'transparent',
    borderColor: colors.danger,
  },
  disabled: {
    opacity: 0.45,
  },
  pressed: {
    opacity: 0.7,
  },
  label: {
    ...typography.label,
    color: colors.text,
  },
  primaryLabel: {
    color: colors.onAccent,
  },
  quietLabel: {
    color: colors.textDim,
  },
  dangerLabel: {
    color: colors.danger,
  },
});
