// Step 1: the Pis in setup mode nearby, over Bluetooth. Choosing one asks it
// to blink its key (docs/protocol.md, Blink); its LED is dark until then. The
// discovery and the Blink write are in pairing/discover.ts.
import { useLocalSearchParams, useRouter } from '@/navigation';
import { useCallback, useEffect, useRef, useState } from 'react';
import { ActivityIndicator, Pressable, StyleSheet, Text, View } from 'react-native';
import { frameMs, RATE_INFO, RATES } from '@/led/rates';
import { setBlinking, watchForPis, type FoundPi } from '@/pairing/discover';
import { PairingError } from '@/pairing/session';
import { useApp } from '@/state/store';
import { Button } from '@/ui/Button';
import { Screen } from '@/ui/Screen';
import { colors, monospace, radius, space, type as typography, TOUCH_TARGET } from '@/ui/theme';
import { DIAGNOSTICS } from '@/util/diagnostics';

/** How long before an empty list says what to check. */
const HINT_AFTER_MS = 12_000;

export default function FindScreen() {
  const router = useRouter();
  // A diagnostics build's frame rate, carried to the scan screen.
  const experiment = useLocalSearchParams<{ fps?: string }>();
  const { services, setTarget, setPairingKey, rate, setRate } = useApp();
  const [pis, setPis] = useState<FoundPi[]>([]);
  const [error, setError] = useState<PairingError | undefined>(undefined);
  const [asking, setAsking] = useState<string | undefined>(undefined);
  const [hint, setHint] = useState(false);
  const [attempt, setAttempt] = useState(0);
  const stopScan = useRef<(() => void) | undefined>(undefined);

  useEffect(() => {
    let cancelled = false;
    setError(undefined);
    setHint(false);
    const timer = setTimeout(() => setHint(true), HINT_AFTER_MS);
    void services.transport.prepare().then(
      () => {
        if (cancelled) return;
        stopScan.current = watchForPis(services.transport, setPis, setError);
      },
      (caught: unknown) => {
        if (cancelled) return;
        setError(
          new PairingError(
            'bluetooth',
            caught instanceof Error && caught.name === 'BluetoothUnavailable' ? caught.message : 'Bluetooth is not available. Check that it is on and that the app may use it.',
            caught,
          ),
        );
      },
    );
    return () => {
      cancelled = true;
      clearTimeout(timer);
      stopScan.current?.();
      stopScan.current = undefined;
    };
  }, [services, attempt]);

  const choose = useCallback(
    async (pi: FoundPi) => {
      setAsking(pi.id);
      setError(undefined);
      stopScan.current?.();
      stopScan.current = undefined;
      try {
        await setBlinking(services.transport, pi, rate);
        setTarget(pi);
        setPairingKey(undefined);
        router.push({ pathname: '/scan', params: experiment });
      } catch (caught) {
        setError(caught instanceof PairingError ? caught : new PairingError('bluetooth', 'Something went wrong with Bluetooth. Try again.', caught));
        setAttempt((n) => n + 1);
      } finally {
        setAsking(undefined);
      }
    },
    [rate, router, services.transport, setPairingKey, setTarget],
  );

  return (
    <Screen title="Find the Pi" eyebrow="Step 1 of 4" showBack>
      <Text style={styles.body}>
        Pis waiting to be set up, strongest signal first. Choose yours and its main LED starts blinking; the next step reads it with the camera.
      </Text>

      <Text style={styles.speedLabel}>LED speed</Text>
      <View style={styles.speeds} accessibilityRole="radiogroup">
        {RATES.map((option) => (
          <Pressable
            key={option}
            accessibilityRole="radio"
            accessibilityLabel={`LED speed ${RATE_INFO[option].label}`}
            accessibilityState={{ checked: option === rate }}
            disabled={asking !== undefined}
            onPress={() => setRate(option)}
            style={[styles.speed, option === rate && styles.speedChosen]}>
            <Text style={[styles.speedName, option === rate && styles.speedNameChosen]}>{RATE_INFO[option].label}</Text>
          </Pressable>
        ))}
      </View>
      <Text style={styles.speedDetail}>Slower gives the camera longer on each blink. One frame takes {(frameMs(rate) / 1000).toFixed(1)} s at this speed.</Text>

      {pis.length === 0 && !error ? (
        <View style={styles.searching}>
          <ActivityIndicator color={colors.accent} />
          <Text style={styles.searchingText}>Looking over Bluetooth</Text>
        </View>
      ) : null}

      {pis.map((pi) => (
        <Pressable
          key={pi.id}
          accessibilityRole="button"
          accessibilityLabel={`Pi named ${pi.name}`}
          disabled={asking !== undefined}
          onPress={() => void choose(pi)}
          style={({ pressed }) => [styles.row, pressed && styles.pressed, asking !== undefined && asking !== pi.id && styles.dimmed]}>
          <View style={styles.rowText}>
            <Text style={styles.name}>{pi.name}</Text>
            {pi.rssi !== undefined ? <Text style={styles.rowDetail}>Signal {pi.rssi} dBm</Text> : null}
          </View>
          {asking === pi.id ? <ActivityIndicator color={colors.accent} /> : <Text style={styles.chevron}>{'>'}</Text>}
        </Pressable>
      ))}

      {hint && pis.length === 0 && !error ? (
        <Text style={styles.hint}>
          None yet. A Pi shows here while it is in setup mode: powered, within a few metres, and with no WiFi connection (or with setup.always on).
        </Text>
      ) : null}

      {error ? (
        <View style={styles.errorBox}>
          <Text style={styles.errorTitle}>{error.kind === 'bluetooth' ? 'Bluetooth' : 'That Pi did not answer'}</Text>
          <Text style={styles.errorBody}>{error.message}</Text>
          {DIAGNOSTICS && error.detail ? <Text style={styles.errorDetail}>{error.detail}</Text> : null}
          <Button label="Look again" variant="secondary" onPress={() => setAttempt((n) => n + 1)} />
        </View>
      ) : null}

    </Screen>
  );
}

const styles = StyleSheet.create({
  body: {
    ...typography.body,
    color: colors.textDim,
    lineHeight: 21,
    marginBottom: space.xl,
  },
  speedLabel: {
    ...typography.label,
    color: colors.textDim,
    marginBottom: space.sm,
  },
  speeds: {
    flexDirection: 'row',
    gap: space.sm,
  },
  speed: {
    flex: 1,
    minHeight: TOUCH_TARGET,
    alignItems: 'center',
    justifyContent: 'center',
    borderRadius: radius.md,
    backgroundColor: colors.surface,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.border,
  },
  speedChosen: {
    backgroundColor: colors.accentSurface,
    borderColor: colors.accentEdge,
  },
  speedName: {
    ...typography.label,
    color: colors.textDim,
  },
  speedNameChosen: {
    color: colors.accent,
  },
  speedDetail: {
    ...typography.caption,
    color: colors.textFaint,
    marginTop: space.sm,
    marginBottom: space.xl,
  },
  searching: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.md,
    paddingVertical: space.lg,
  },
  searchingText: {
    ...typography.body,
    color: colors.textDim,
  },
  row: {
    minHeight: TOUCH_TARGET + space.xl,
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.md,
    paddingHorizontal: space.lg,
    paddingVertical: space.md,
    marginBottom: space.sm,
    borderRadius: radius.lg,
    backgroundColor: colors.surface,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.border,
  },
  pressed: {
    backgroundColor: colors.surface2,
  },
  dimmed: {
    opacity: 0.5,
  },
  rowText: {
    flex: 1,
    gap: space.xs,
  },
  name: {
    ...typography.heading,
    fontFamily: monospace,
    color: colors.accent,
  },
  rowDetail: {
    ...typography.caption,
    color: colors.textDim,
  },
  chevron: {
    ...typography.title,
    color: colors.textFaint,
  },
  hint: {
    ...typography.body,
    color: colors.textDim,
    lineHeight: 21,
    marginTop: space.lg,
  },
  errorBox: {
    marginTop: space.xl,
    gap: space.md,
  },
  errorTitle: {
    ...typography.heading,
    color: colors.danger,
  },
  errorBody: {
    ...typography.body,
    color: colors.textDim,
    lineHeight: 21,
  },
  errorDetail: {
    ...typography.caption,
    fontFamily: monospace,
    color: colors.textFaint,
  },
});
