// Step 1: the Pis in setup mode nearby, over Bluetooth. Choosing one asks it
// to blink its ID (docs/protocol.md, Blink); its LED is dark until then. The
// discovery and the Blink write are in pairing/discover.ts.
import { useRouter } from 'expo-router';
import { useCallback, useEffect, useRef, useState } from 'react';
import { ActivityIndicator, Pressable, StyleSheet, Text, View } from 'react-native';
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
  const { services, setTarget, setMachineId } = useApp();
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
        await setBlinking(services.transport, pi);
        setTarget(pi);
        setMachineId(undefined);
        router.push('/scan');
      } catch (caught) {
        setError(caught instanceof PairingError ? caught : new PairingError('bluetooth', 'Something went wrong with Bluetooth. Try again.', caught));
        setAttempt((n) => n + 1);
      } finally {
        setAsking(undefined);
      }
    },
    [router, services.transport, setMachineId, setTarget],
  );

  return (
    <Screen title="Find the Pi" eyebrow="Step 1 of 4" showBack>
      <Text style={styles.body}>
        Pis waiting to be set up, nearest first. Choose yours and its main LED starts blinking; the next step reads it with the camera.
      </Text>

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
          accessibilityLabel={`Pi with fingerprint ${pi.fingerprint}`}
          disabled={asking !== undefined}
          onPress={() => void choose(pi)}
          style={({ pressed }) => [styles.row, pressed && styles.pressed, asking !== undefined && asking !== pi.id && styles.dimmed]}>
          <View style={styles.rowText}>
            <Text style={styles.fingerprint}>{pi.fingerprint}</Text>
            <Text style={styles.rowDetail}>
              {pi.name}
              {pi.rssi !== undefined ? `, signal ${pi.rssi} dBm` : ''}
            </Text>
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

      <Button label="Type the ID instead" variant="quiet" onPress={() => router.push('/enter')} style={styles.typeIt} />
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
  fingerprint: {
    ...typography.title,
    fontFamily: monospace,
    color: colors.accent,
    letterSpacing: 2,
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
  typeIt: {
    marginTop: space.xl,
  },
});
