// Pairing over Bluetooth: scan for the Pi named for the ID's fingerprint,
// connect, check Info, authenticate both ways. The session logic is in
// pairing/session.ts; this screen only shows where it is.
import { useRouter } from 'expo-router';
import { useCallback, useEffect, useState } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import { pair, PairingError, type PairingPhase } from '@/pairing/session';
import { bytesToHex } from '@/protocol/bytes';
import { BLE_NAME_PREFIX } from '@/protocol/constants';
import { fingerprint } from '@/protocol/keys';
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
  checking: 'Checking its fingerprint',
  authenticating: 'Proving we both know the ID',
  ready: 'Paired',
};

export default function PairScreen() {
  const router = useRouter();
  const { machineId, target, services, setSession } = useApp();
  const [phase, setPhase] = useState<PairingPhase>('scanning');
  const [detail, setDetail] = useState<string | undefined>(undefined);
  const [error, setError] = useState<PairingError | undefined>(undefined);
  const [attempt, setAttempt] = useState(0);

  const start = useCallback(async () => {
    if (!machineId) return;
    setError(undefined);
    setPhase('scanning');
    try {
      // The Pi chosen and asked to blink, by its platform id, when the LED read was its.
      const preferId = target && target.fingerprint === fingerprint(machineId) ? target.id : undefined;
      const session = await pair(services.transport, machineId, {
        preferId,
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
  }, [machineId, router, services.transport, setSession, target]);

  useEffect(() => {
    void start();
  }, [start, attempt]);

  if (!machineId) {
    return (
      <Screen title="Pair" showBack>
        <Text style={styles.body}>No machine ID yet. Read the LED first.</Text>
        <Button label="Read the LED" onPress={() => router.replace('/scan')} />
      </Screen>
    );
  }

  const fp = fingerprint(machineId);
  const step = PHASES.indexOf(phase);

  return (
    <Screen title="Pair" eyebrow="Step 3 of 4" showBack={!!error}>
      <View style={styles.idCard}>
        <Text style={styles.idLabel}>MACHINE ID</Text>
        <Text style={styles.id}>{bytesToHex(machineId)}</Text>
        <Text style={styles.idDetail}>
          Fingerprint {fp}. The Pi advertises as {BLE_NAME_PREFIX}
          {fp}; the ID itself never crosses the air.
        </Text>
      </View>

      <Progress value={(step + (error ? 0 : 0.5)) / PHASES.length} label={PHASE_LABELS[phase]} detail={detail} tone={error ? 'warn' : 'accent'} />

      <View style={styles.steps}>
        {PHASES.map((name, index) => (
          <Text key={name} style={[styles.step, index < step && styles.stepDone, index === step && !error && styles.stepCurrent]}>
            {index < step ? 'done  ' : index === step ? (error ? 'failed  ' : 'now  ') : '      '}
            {PHASE_LABELS[name]}
          </Text>
        ))}
      </View>

      {error ? (
        <View style={styles.errorBox}>
          <Text style={styles.errorTitle}>{errorTitle(error)}</Text>
          <Text style={styles.errorBody}>{error.message}</Text>
          {DIAGNOSTICS && error.detail ? <Text style={styles.errorDetail}>{error.detail}</Text> : null}
          <Button label="Try again" onPress={() => setAttempt((n) => n + 1)} />
          <Button label={error.kind === 'fingerprint_mismatch' || error.kind === 'refused' ? 'Read the LED again' : 'Back'} variant="quiet" onPress={() => router.replace('/')} />
        </View>
      ) : null}
    </Screen>
  );
}

function errorTitle(error: PairingError): string {
  switch (error.kind) {
    case 'not_found':
      return 'No Pi found';
    case 'fingerprint_mismatch':
      return 'Not the same Pi';
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
  steps: {
    marginTop: space.xl,
    gap: space.sm,
  },
  step: {
    ...typography.body,
    fontFamily: monospace,
    color: colors.textFaint,
  },
  stepDone: {
    color: colors.textDim,
  },
  stepCurrent: {
    color: colors.text,
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
