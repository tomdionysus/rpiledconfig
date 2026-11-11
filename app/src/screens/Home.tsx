// Home: what the app does, in two lines, and the button that starts.
import { useRouter } from '@/navigation';
import { Image, StyleSheet, Text, View, type ImageSourcePropType } from 'react-native';
import { Button } from '@/ui/Button';
import { Screen } from '@/ui/Screen';
import { SourceLink } from '@/ui/SourceLink';
import { colors, radius, space, type as typography } from '@/ui/theme';

// Metro bundles an image only through require().
// eslint-disable-next-line @typescript-eslint/no-require-imports, @typescript-eslint/no-unsafe-assignment
const HERO_ICON: ImageSourcePropType = require('../../assets/hero-icon.png');

const STEPS = [
  ['Find', 'Choose the Pi over Bluetooth; its LED starts blinking.'],
  ['Read', 'Point the camera at the LED for a few seconds.'],
  ['Connect', 'Pick the network the Pi should join and give its passphrase.'],
] as const;

export default function HomeScreen() {
  const router = useRouter();

  return (
    <Screen>
      <View style={styles.hero}>
        <Image source={HERO_ICON} style={styles.icon} accessibilityIgnoresInvertColors />
        <Text style={styles.lede}>Set up a Raspberry Pi's WiFi with only what someone in the room has: sight of its LED and Bluetooth range.</Text>
      </View>

      <View style={styles.steps}>
        {STEPS.map(([name, text], index) => (
          <View key={name} style={styles.step}>
            <Text style={styles.stepNumber}>{index + 1}</Text>
            <View style={styles.stepText}>
              <Text style={styles.stepName}>{name}</Text>
              <Text style={styles.stepBody}>{text}</Text>
            </View>
          </View>
        ))}
      </View>

      <Button label="Find the Pi" onPress={() => router.push('/find')} style={styles.start} />
      <SourceLink />
    </Screen>
  );
}

const styles = StyleSheet.create({
  hero: {
    alignItems: 'center',
    paddingTop: space.lg,
    paddingBottom: space.xxl,
    gap: space.md,
  },
  icon: {
    width: 96,
    height: 96,
    borderRadius: radius.xl,
  },
  lede: {
    ...typography.body,
    color: colors.textDim,
    textAlign: 'center',
    lineHeight: 21,
  },
  steps: {
    gap: space.lg,
  },
  step: {
    flexDirection: 'row',
    gap: space.lg,
  },
  stepNumber: {
    ...typography.title,
    color: colors.accent,
    width: 24,
    textAlign: 'center',
  },
  stepText: {
    flex: 1,
    gap: space.xs,
  },
  stepName: {
    ...typography.heading,
    color: colors.text,
  },
  stepBody: {
    ...typography.body,
    color: colors.textDim,
    lineHeight: 21,
  },
  start: {
    marginTop: space.xxl,
    marginBottom: space.sm,
  },
});
