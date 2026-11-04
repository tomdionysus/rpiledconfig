// Standard screen chrome: safe areas, a compact header with an optional back
// control, and a scrolling or fixed body.
import { useRouter } from 'expo-router';
import React from 'react';
import { Pressable, ScrollView, StyleSheet, Text, View, type StyleProp, type ViewStyle } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { colors, radius, space, type as typography, TOUCH_TARGET } from './theme';

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

  const header =
    title || showBack ? (
      <View style={[styles.header, { paddingTop: insets.top + space.sm }]}>
        {showBack ? (
          <Pressable
            accessibilityRole="button"
            accessibilityLabel="Back"
            hitSlop={8}
            onPress={() => (router.canGoBack() ? router.back() : router.replace('/'))}
            style={({ pressed }) => [styles.backButton, pressed && styles.pressed]}>
            <Text style={styles.backGlyph}>{'<'}</Text>
          </Pressable>
        ) : null}
        <View style={styles.headerText}>
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
      </View>
    ) : (
      <View style={{ height: insets.top }} />
    );

  const body = scrollable ? (
    <ScrollView
      style={styles.flex}
      contentContainerStyle={[styles.content, { paddingBottom: insets.bottom + space.xl }, contentStyle]}
      keyboardShouldPersistTaps="handled">
      {children}
    </ScrollView>
  ) : (
    <View style={[styles.flex, styles.content, { paddingBottom: insets.bottom + space.xl }, contentStyle]}>{children}</View>
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
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.md,
    paddingHorizontal: space.xl,
    paddingBottom: space.md,
  },
  headerText: {
    flex: 1,
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
