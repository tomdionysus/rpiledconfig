// Standard screen chrome: safe areas, a compact header with an optional back
// control, and a scrolling or fixed body.
import { useRouter } from '@/navigation';
import React from 'react';
import { Image, Pressable, ScrollView, StyleSheet, Text, View, type ImageSourcePropType, type StyleProp, type ViewStyle } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { colors, radius, space, type as typography, TOUCH_TARGET } from './theme';

// Metro bundles an image only through require().
// eslint-disable-next-line @typescript-eslint/no-require-imports, @typescript-eslint/no-unsafe-assignment
const TITLE_ICON: ImageSourcePropType = require('../../assets/title-icon.png');

interface ScreenProps {
  title?: string;
  /** Small line above the title: the step this screen is. */
  eyebrow?: string;
  showBack?: boolean;
  scrollable?: boolean;
  contentStyle?: StyleProp<ViewStyle>;
  children: React.ReactNode;
}

export function Screen({ title, eyebrow, showBack = false, scrollable = true, contentStyle, children }: ScreenProps) {
  const insets = useSafeAreaInsets();
  const router = useRouter();

  // The title bar, on every screen: back where there is somewhere to go, the
  // app's icon and name. The screen's own title sits under it.
  const header = (
    <View style={[styles.bar, { paddingTop: insets.top + space.sm }]}>
      {showBack ? (
        <Pressable
          accessibilityRole="button"
          accessibilityLabel="Back"
          hitSlop={8}
          onPress={() => (router.canGoBack() ? router.back() : router.replace('/'))}
          style={({ pressed }) => [styles.backButton, pressed && styles.pressed]}
        >
          <Text style={styles.backGlyph}>{'<'}</Text>
        </Pressable>
      ) : null}
      <Image source={TITLE_ICON} style={styles.icon} accessibilityIgnoresInvertColors />
      <Text style={styles.appName}>RPi LED Config</Text>
    </View>
  );

  const heading =
    title || eyebrow ? (
      <View style={styles.heading}>
        {eyebrow ? (
          <Text numberOfLines={1} style={styles.eyebrow}>
            {eyebrow.toUpperCase()}
          </Text>
        ) : null}
        {title ? (
          <Text numberOfLines={1} style={styles.title}>
            {title}
          </Text>
        ) : null}
      </View>
    ) : null;

  const body = scrollable ? (
    <ScrollView
      style={styles.flex}
      contentContainerStyle={[styles.content, { paddingBottom: insets.bottom + space.xl }, contentStyle]}
      keyboardShouldPersistTaps="handled"
    >
      {heading}
      {children}
    </ScrollView>
  ) : (
    <View style={[styles.flex, styles.content, { paddingBottom: insets.bottom + space.xl }, contentStyle]}>
      {heading}
      {children}
    </View>
  );

  return (
    <View style={styles.root}>
      {header}
      {body}
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    backgroundColor: colors.background,
  },
  flex: {
    flex: 1,
  },
  content: {
    paddingHorizontal: space.xl,
  },
  bar: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.sm,
    paddingHorizontal: space.xl,
    paddingBottom: space.sm,
    borderBottomWidth: StyleSheet.hairlineWidth,
    borderBottomColor: colors.border,
  },
  icon: {
    width: 28,
    height: 28,
    borderRadius: radius.sm,
  },
  appName: {
    ...typography.heading,
    color: colors.text,
  },
  heading: {
    paddingTop: space.lg,
    paddingBottom: space.lg,
  },
  eyebrow: {
    ...typography.micro,
    color: colors.textFaint,
  },
  title: {
    ...typography.display,
    color: colors.text,
  },
  backButton: {
    width: TOUCH_TARGET,
    height: TOUCH_TARGET,
    marginLeft: -space.md,
    borderRadius: radius.pill,
    alignItems: 'center',
    justifyContent: 'center',
  },
  backGlyph: {
    ...typography.title,
    color: colors.text,
  },
  pressed: {
    opacity: 0.6,
  },
});
