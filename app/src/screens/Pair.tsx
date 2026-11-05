// Pairing over Bluetooth: scan for the Pi chosen on the find screen, by its name,
// connect, check Info, authenticate both ways. The session logic is in
// pairing/session.ts; this screen only shows where it is.
import { useRouter } from '@/navigation';
import { useCallback, useEffect, useState } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import { pair, PairingError, type PairingPhase } from '@/pairing/session';
import { bytesToHex } from '@/protocol/bytes';
import { useApp } from '@/state/store';
import { Button } from '@/ui/Button';
import { Progress } from '@/ui/Progress';
import { Screen } from '@/ui/Screen';
import { colors, monospace, radius, space, type as typography } from '@/ui/theme';
import { DIAGNOSTICS } from '@/util/diagnostics';

const PHASES: PairingPhase[] = ['scanning', 'connecting', 'checking', 'authenticating', 'ready'];

const PHASE_LABELS: Record<PairingPhase, string> = {
  scanning: 'Looking for the Pi',
  connecting: 'Connecting',
  checking: 'Checking it is RPi LED Config',
  authenticating: 'Proving we both know the key',
  ready: 'Paired',
};

export default function PairScreen() {
  const router = useRouter();
  const { pairingKey, target, services, setSession } = useApp();
  const [phase, setPhase] = useState<PairingPhase>('scanning');
  const [detail, setDetail] = useState<string | undefined>(undefined);
  const [error, setError] = useState<PairingError | undefined>(undefined);
  const [attempt, setAttempt] = useState(0);

  const start = useCallback(async () => {
    if (!pairingKey || !target) return;
    setError(undefined);
    setPhase('scanning');
    try {
      // The Pi chosen and asked to blink; the authentication proves the key is its.
      const session = await pair(services.transport, target.name, pairingKey, {
        preferId: target.id,
        onPhase: (next, info) => {
          setPhase(next);
          setDetail(info);
        },
      });
      setSession(session);
      router.replace('/wifi');
    } catch (caught) {
      setError(caught instanceof PairingError ? caught : new PairingError('bluetooth', 'Something went wrong with Bluetooth. Try again.', caught));
    }
  }, [pairingKey, router, services.transport, setSession, target]);

  useEffect(() => {
    void start();
  }, [start, attempt]);

  if (!pairingKey || !target) {
    return (
      <Screen title="Pair" showBack>
        <Text style={styles.body}>No key yet. Choose a Pi and read its LED first.</Text>
        <Button label="Find the Pi" onPress={() => router.replace('/find')} />
      </Screen>
    );
  }

  const step = PHASES.indexOf(phase);

  return (
    <Screen title="Pair" eyebrow="Step 3 of 4" showBack={!!error}>
      <View style={styles.idCard}>
        <Text style={styles.idLabel}>{target.name}</Text>
        <Text style={styles.id}>{bytesToHex(pairingKey)}</Text>
        <Text style={styles.idDetail}>The one-time key read from its LED. It never goes over Bluetooth: both sides prove they know it.</Text>
      </View>

      <Progress value={(step + (error ? 0 : 0.5)) / PHASES.length} label={PHASE_LABELS[phase]} detail={detail} tone={error ? 'warn' : 'accent'} />

      {error ? (
        <View style={styles.errorBox}>
          <Text style={styles.errorTitle}>{errorTitle(error)}</Text>
          <Text style={styles.errorBody}>{error.message}</Text>
          {DIAGNOSTICS && error.detail ? <Text style={styles.errorDetail}>{error.detail}</Text> : null}
          <Button label="Try again" onPress={() => setAttempt((n) => n + 1)} />
          <Button label={error.kind === 'refused' ? 'Read the LED again' : 'Back'} variant="quiet" onPress={() => router.replace('/')} />
        </View>
      ) : null}
    </Screen>
  );
}

function errorTitle(error: PairingError): string {
  switch (error.kind) {
    case 'not_found':
      return 'No Pi found';
    case 'refused':
      return 'Authentication refused';
    case 'protocol':
      return 'Unexpected answer';
    case 'timeout':
      return 'No answer';
    case 'disconnected':
      return 'Connection lost';
    case 'bluetooth':
      return 'Bluetooth';
  }
}

const styles = StyleSheet.create({
  body: {
    ...typography.body,
    color: colors.textDim,
    marginBottom: space.xl,
  },
  idCard: {
    padding: space.lg,
    borderRadius: radius.lg,
    backgroundColor: colors.surface,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.border,
    gap: space.xs,
    marginBottom: space.xl,
  },
  idLabel: {
    ...typography.micro,
    color: colors.textFaint,
  },
  id: {
    ...typography.title,
    fontFamily: monospace,
    color: colors.accent,
    letterSpacing: 2,
  },
  idDetail: {
    ...typography.caption,
    color: colors.textDim,
    marginTop: space.xs,
  },
  errorBox: {
    marginTop: space.xxl,
    gap: space.md,
  },
  errorTitle: {
    ...typography.heading,
    color: colors.danger,
  },
  errorDetail: {
    ...typography.caption,
    fontFamily: monospace,
    color: colors.textFaint,
  },
  errorBody: {
    ...typography.body,
    color: colors.textDim,
    lineHeight: 21,
  },
});
