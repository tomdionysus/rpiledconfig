// Home: what the two factors are, the button that starts, and the fallback
// of typing the ID. In development, the switch to the fake daemon.
import { useRouter } from 'expo-router';
import { StyleSheet, Switch, Text, View } from 'react-native';
import { FAKE_MACHINE_ID_HEX } from '@/ble/fake';
import { hexToBytes } from '@/protocol/bytes';
import { FAKE_PASSPHRASE } from '@/state/services';
import { useApp } from '@/state/store';
import { Button } from '@/ui/Button';
import { Screen } from '@/ui/Screen';
import { colors, radius, space, type as typography } from '@/ui/theme';
import { DIAGNOSTICS } from '@/util/diagnostics';

export default function HomeScreen() {
  const router = useRouter();
  const { fake, setFake, setMachineId } = useApp();

  return (
    <Screen>
      <View style={styles.hero}>
        <View style={styles.led} />
        <Text style={styles.title}>RPiLEDConfig</Text>
        <Text style={styles.lede}>Set up a Raspberry Pi's WiFi with two things only someone in the room has.</Text>
      </View>

      <View style={styles.factor}>
        <Text style={styles.factorNumber}>1</Text>
        <View style={styles.factorText}>
          <Text style={styles.factorTitle}>Line of sight</Text>
          <Text style={styles.factorBody}>
            The Pi blinks its machine ID on its main LED. Point the camera at it for up to twenty seconds and the
            app reads it.
          </Text>
        </View>
      </View>
      <View style={styles.factor}>
        <Text style={styles.factorNumber}>2</Text>
        <View style={styles.factorText}>
          <Text style={styles.factorTitle}>Bluetooth range</Text>
          <Text style={styles.factorBody}>
            The app proves over Bluetooth that it knows the ID, without ever sending it, and then configures WiFi over an
            encrypted channel.
          </Text>
        </View>
      </View>

      <Button label="Read the LED" onPress={() => router.push('/scan')} style={styles.start} />
      <Button label="Type the ID instead" variant="quiet" onPress={() => router.push('/enter')} />

      {DIAGNOSTICS ? (
        <View style={styles.dev}>
          <Text style={styles.devTitle}>DEVELOPMENT</Text>
          <View style={styles.devRow}>
            <Text style={styles.devLabel}>Talk to the fake Pi instead of Bluetooth</Text>
            <Switch value={fake} onValueChange={setFake} trackColor={{ true: colors.accentEdge }} thumbColor={fake ? colors.accent : colors.textFaint} />
          </View>
          {fake ? <Text style={styles.devLabel}>The fake Pi joins Blackraven with the passphrase {FAKE_PASSPHRASE}; any other fails after 45 seconds.</Text> : null}
          {fake ? (
            <Button
              label="Pair with the fake Pi's ID"
              variant="secondary"
              onPress={() => {
                setMachineId(hexToBytes(FAKE_MACHINE_ID_HEX));
                router.push('/pair');
              }}
              style={styles.devButton}
            />
          ) : null}
          <Button label="Show a virtual LED for another phone" variant="secondary" onPress={() => router.push('/virtual-led')} style={styles.devButton} />
          <Button label="LED recordings" variant="secondary" onPress={() => router.push('/recordings')} style={styles.devButton} />
        </View>
      ) : null}
    </Screen>
  );
}

const styles = StyleSheet.create({
  hero: {
    alignItems: 'center',
    paddingTop: space.xxl,
    paddingBottom: space.xl,
    gap: space.md,
  },
  led: {
    width: 18,
    height: 18,
    borderRadius: radius.pill,
    backgroundColor: colors.accent,
    shadowColor: colors.accent,
    shadowOpacity: 0.9,
    shadowRadius: 12,
    shadowOffset: { width: 0, height: 0 },
  },
  title: {
    ...typography.display,
    fontSize: 30,
    color: colors.text,
  },
  lede: {
    ...typography.body,
    color: colors.textDim,
    textAlign: 'center',
    lineHeight: 21,
  },
  factor: {
    flexDirection: 'row',
    gap: space.lg,
    paddingVertical: space.md,
  },
  factorNumber: {
    ...typography.title,
    color: colors.accent,
    width: 24,
    textAlign: 'center',
  },
  factorText: {
    flex: 1,
    gap: space.xs,
  },
  factorTitle: {
    ...typography.heading,
    color: colors.text,
  },
  factorBody: {
    ...typography.body,
    color: colors.textDim,
    lineHeight: 21,
  },
  start: {
    marginTop: space.xl,
    marginBottom: space.sm,
  },
  dev: {
    marginTop: space.xxl,
    padding: space.lg,
    borderRadius: radius.lg,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.border,
    backgroundColor: colors.surface,
    gap: space.md,
  },
  devTitle: {
    ...typography.micro,
    color: colors.textFaint,
  },
  devRow: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: space.md,
  },
  devLabel: {
    ...typography.body,
    color: colors.textDim,
    flex: 1,
  },
  devButton: {
    alignSelf: 'stretch',
  },
});
