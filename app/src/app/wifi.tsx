// WiFi over the paired session. First the quick way: copy this phone's WiFi
// to the Pi with Wi-Fi Easy Connect (pairing/copyWifi.ts), Android's own
// screen sending the network so the password never passes through the app.
// Where that cannot work (iOS, Android before 10, a Pi without it) the other
// two ways: scan the network's QR code, or pick it from the Pi's scan and
// type the passphrase. Every way ends in the same join flow
// (pairing/join.ts), which expects the BLE link to drop and reconnects.
//
// The link can also go while nothing is happening (a daemon restarted under
// a link bluetoothd kept up gives no disconnect), so the screen asks for
// status every few seconds while idle, and says so in plain words when it
// fails.
import { useRouter } from 'expo-router';
import { useCallback, useEffect, useRef, useState } from 'react';
import { Pressable, StyleSheet, Text, TextInput, View } from 'react-native';
import { copyPhoneWifi, describeCopyOutcome, describeCopyStep, type CopyOutcome, type CopyStep } from '@/pairing/copyWifi';
import { collapseSteps, joinWifi, type Verdict } from '@/pairing/join';
import { LinkLostError, RequestError } from '@/pairing/session';
import { MessageError, type Network, type StatusResponse } from '@/protocol/messages';
import { FAKE_PASSPHRASE } from '@/state/services';
import { useApp } from '@/state/store';
import { Button } from '@/ui/Button';
import { Screen } from '@/ui/Screen';
import { colors, monospace, radius, space, type as typography, TOUCH_TARGET } from '@/ui/theme';
import { DIAGNOSTICS } from '@/util/diagnostics';
import { easyConnectSupported, launchEasyConnect } from '@/wifi/easyConnect';
import { phoneSsid } from '@/wifi/phoneWifi';
import { WifiQrScanner } from '@/wifi/WifiQrScanner';
import type { WifiCredentials } from '@/wifi/wifiQr';

type Busy = 'status' | 'scan' | 'forget' | 'finish' | undefined;

/** One way of getting the Pi onto a network, from the first step to its verdict. */
interface Attempt {
  /** The network's name as the screen says it. */
  label: string;
  steps: CopyStep[];
  outcome?: CopyOutcome;
  verdict?: Verdict;
}

/** How often to ask for status while nothing else is going on. */
const KEEPALIVE_MS = 10_000;

function describe(error: unknown): string {
  if (error instanceof RequestError) return `${error.message} (${error.code})`;
  if (error instanceof LinkLostError) return error.message;
  if (error instanceof MessageError) return 'The Pi answered with something this app does not understand.';
  return 'Something went wrong talking to the Pi.';
}

export default function WifiScreen() {
  const router = useRouter();
  const { session, setSession, services } = useApp();
  const [status, setStatus] = useState<StatusResponse | undefined>(undefined);
  const [networks, setNetworks] = useState<Network[] | undefined>(undefined);
  const [selected, setSelected] = useState<Network | undefined>(undefined);
  const [psk, setPsk] = useState('');
  const [busy, setBusy] = useState<Busy>(undefined);
  const [message, setMessage] = useState<string | undefined>(undefined);
  const [attempt, setAttempt] = useState<Attempt | undefined>(undefined);
  const [closed, setClosed] = useState<LinkLostError | undefined>(undefined);
  const [phoneNetwork, setPhoneNetwork] = useState<string | undefined>(undefined);
  /** The other ways are on show: Easy Connect cannot work here, or did not. */
  const [otherWays, setOtherWays] = useState(!easyConnectSupported());
  const [scanningQr, setScanningQr] = useState(false);
  // While a join runs, the link dropping is part of the plan, not news.
  const joining = useRef(false);

  const run = useCallback(
    async (what: Exclude<Busy, undefined>, action: () => Promise<void>) => {
      setBusy(what);
      setMessage(undefined);
      try {
        await action();
      } catch (error) {
        // A lost link has its own notice; anything else is said here.
        if (!(error instanceof LinkLostError)) setMessage(describe(error));
      } finally {
        setBusy(undefined);
      }
    },
    [],
  );

  const refresh = useCallback(async () => {
    if (!session) return;
    await run('status', async () => {
      setStatus(await session.status());
    });
    if (session.isClosed) return;
    await run('scan', async () => {
      setNetworks((await session.wifiScan()).networks);
    });
  }, [run, session]);

  useEffect(() => {
    if (!session) return;
    if (session.isClosed) setClosed(new LinkLostError('The Bluetooth link to the Pi was lost.'));
    return session.onClosed((reason) => {
      if (!joining.current && reason) setClosed(reason);
    });
  }, [session]);

  // Once, on arrival; a session replaced by a reconnect does not rescan.
  const refreshed = useRef(false);
  useEffect(() => {
    if (refreshed.current || !session) return;
    refreshed.current = true;
    void refresh();
    void phoneSsid().then(setPhoneNetwork);
  }, [refresh, session]);

  // While idle, keep asking: a link that died quietly is found within seconds.
  const inProgress = attempt !== undefined && attempt.outcome === undefined;
  useEffect(() => {
    if (!session || closed || inProgress || busy || scanningQr) return;
    const timer = setTimeout(() => {
      if (joining.current || session.isClosed) return;
      session.status().then(setStatus, () => {
        // LinkLostError reaches the screen through onClosed.
      });
    }, KEEPALIVE_MS);
    return () => clearTimeout(timer);
  }, [session, closed, inProgress, busy, scanningQr, status]);

  if (!session) {
    return (
      <Screen title="WiFi" showBack>
        <Text style={styles.body}>Not paired. Start again from the beginning.</Text>
        <Button label="Home" onPress={() => router.replace('/')} />
      </Screen>
    );
  }

  const leave = () => {
    void session.close();
    setSession(undefined);
    router.replace('/');
  };

  const pairAgain = () => {
    void session.close();
    setSession(undefined);
    router.replace('/pair');
  };

  /** Runs one way of joining and shows its steps and verdict; the outcome's session becomes the screen's. */
  const follow = async (label: string, how: (onStep: (step: CopyStep) => void) => Promise<CopyOutcome>) => {
    joining.current = true;
    setMessage(undefined);
    setClosed(undefined);
    setAttempt({ label, steps: [] });
    const outcome = await how((step) => setAttempt((current) => (current && current.outcome === undefined ? { ...current, steps: [...current.steps, step] } : current)));
    joining.current = false;
    if (outcome.kind === 'lost') {
      if (outcome.lastStatus) setStatus(outcome.lastStatus);
    } else {
      if (outcome.session !== session) setSession(outcome.session);
      if (outcome.kind === 'joined' || outcome.kind === 'not_joined') setStatus(outcome.status);
    }
    if (outcome.kind === 'unavailable' || outcome.kind === 'cancelled') setOtherWays(true);
    setAttempt({ label, steps: [], outcome, verdict: describeCopyOutcome(outcome, label) });
  };

  const copyPhone = () => {
    if (!easyConnectSupported()) {
      setOtherWays(true);
      return;
    }
    const label = phoneNetwork ?? 'the network this phone shared';
    // With the fake Pi, Android's screen is played by the fake, which shares Blackraven.
    const fake = services.fake;
    const launch = fake
      ? async () => {
          fake.easyConnectDeliver('Blackraven', FAKE_PASSPHRASE);
          return 'done' as const;
        }
      : launchEasyConnect;
    void follow(label, (onStep) => copyPhoneWifi(session, launch, { onStep, ssidHint: phoneNetwork }));
  };

  const configure = (ssid: string, passphrase: string | undefined) => {
    void follow(ssid, (onStep) => joinWifi(session, ssid, passphrase, { onStep }));
  };

  const fromQr = (credentials: WifiCredentials) => {
    setScanningQr(false);
    configure(credentials.ssid, credentials.psk);
  };

  const typeIt = () => {
    const match = networks?.find((network) => network.ssid === phoneNetwork);
    if (match) setSelected(match);
  };

  const lost = attempt?.outcome?.kind === 'lost';
  const joined = attempt?.outcome?.kind === 'joined';
  const offline = !!closed || lost || inProgress;

  if (scanningQr) return <WifiQrScanner onRead={fromQr} onCancel={() => setScanningQr(false)} />;

  return (
    <Screen title="WiFi" eyebrow="Step 3 of 3">
      <View style={styles.card}>
        <Text style={styles.cardLabel}>PI</Text>
        <Text style={styles.cardTitle}>{status?.hostname ?? session.info.fingerprint}</Text>
        <Text style={styles.cardDetail}>
          {status
            ? status.wifi.connected
              ? `Connected to ${status.wifi.ssid ?? 'a network'}${status.wifi.ip ? ` as ${status.wifi.ip}` : ''}.`
              : 'Not connected to WiFi.'
            : 'Asking for its status.'}
          {` Daemon ${session.info.version}, fingerprint ${session.info.fingerprint}.`}
        </Text>
      </View>

      {closed && !inProgress && !lost ? (
        <View style={[styles.notice, styles.noticeError]}>
          <Text style={styles.errorTitle}>Lost touch with the Pi</Text>
          <Text style={styles.body}>{closed.message} Pair again to carry on; the Pi stays in setup mode until it is told to finish.</Text>
          {DIAGNOSTICS && closed.detail ? <Text style={styles.detail}>{closed.detail}</Text> : null}
          <Button label="Pair again" onPress={pairAgain} />
          <Button label="Start again" variant="quiet" onPress={leave} />
        </View>
      ) : null}

      {inProgress ? (
        <View style={styles.notice} accessibilityLiveRegion="polite">
          <Text style={styles.sectionTitle}>Joining {attempt.label}</Text>
          {collapseSteps(attempt.steps).map((step, index, all) => (
            <Text key={`${index}-${step.kind}`} style={[styles.step, index === all.length - 1 && styles.stepCurrent]}>
              {index === all.length - 1 ? 'now   ' : 'done  '}
              {describeCopyStep(step, attempt.label)}
            </Text>
          ))}
        </View>
      ) : null}

      {attempt?.verdict ? (
        <View style={[styles.notice, attempt.verdict.tone === 'error' && styles.noticeError]}>
          <Text style={attempt.verdict.tone === 'ok' ? styles.ok : styles.errorTitle}>{attempt.verdict.title}</Text>
          <Text style={styles.body}>{attempt.verdict.body}</Text>
          {joined ? (
            <>
              <Text style={styles.body}>Finish tells the Pi to leave setup mode: the LED stops blinking and Bluetooth goes quiet.</Text>
              <Button
                label="Finish"
                busy={busy === 'finish'}
                onPress={() =>
                  void run('finish', async () => {
                    await session.finish();
                    setSession(undefined);
                    router.replace('/');
                  })
                }
              />
              <Button label="Check status again" variant="quiet" busy={busy === 'status'} onPress={() => void run('status', async () => setStatus(await session.status()))} />
            </>
          ) : lost ? (
            <Button label="Pair again" onPress={pairAgain} />
          ) : null}
        </View>
      ) : null}

      {!joined ? (
        <View style={styles.copy}>
          <Text style={styles.sectionTitle}>Copy this phone's WiFi</Text>
          <Text style={styles.body}>
            {easyConnectSupported()
              ? `Android sends the Pi the network you pick${phoneNetwork ? `, ${phoneNetwork} first` : ''}, passphrase and all; this app never sees the passphrase.`
              : 'This phone cannot send its WiFi to the Pi by itself. Scan the network\'s QR code, or pick it below and type the passphrase.'}
          </Text>
          {easyConnectSupported() ? <Button label="Copy this phone's WiFi" busy={inProgress} disabled={offline} onPress={copyPhone} /> : null}
          {otherWays ? (
            <View style={styles.otherWays}>
              <Button label="Scan the network's QR code" variant="secondary" disabled={offline} onPress={() => setScanningQr(true)} />
              <Text style={styles.detail}>On a phone that is on the network: WiFi settings, the network, Share. Its QR code carries the passphrase.</Text>
              <Button label="Type it instead" variant="quiet" disabled={offline} onPress={typeIt} />
            </View>
          ) : (
            <Button label="Other ways" variant="quiet" disabled={offline} onPress={() => setOtherWays(true)} />
          )}
        </View>
      ) : null}

      <View style={styles.sectionHeader}>
        <Text style={styles.sectionTitle}>Networks the Pi can see</Text>
        <Button label="Rescan" variant="quiet" busy={busy === 'scan'} disabled={offline} onPress={() => void refresh()} />
      </View>
      {networks === undefined ? (
        <Text style={styles.body}>{busy === 'scan' ? 'Scanning.' : 'No scan yet.'}</Text>
      ) : networks.length === 0 ? (
        <Text style={styles.body}>The Pi sees no networks. Try again in a moment.</Text>
      ) : (
        networks.map((network) => (
          <Pressable
            key={network.ssid}
            accessibilityRole="button"
            accessibilityState={{ selected: selected?.ssid === network.ssid, disabled: inProgress }}
            disabled={inProgress}
            onPress={() => {
              setSelected(network);
              setMessage(undefined);
              if (attempt?.outcome) setAttempt(undefined);
            }}
            style={({ pressed }) => [styles.row, selected?.ssid === network.ssid && styles.rowSelected, pressed && styles.pressed]}>
            <View style={styles.rowText}>
              <Text style={styles.rowTitle}>
                {network.ssid}
                {network.ssid === phoneNetwork ? '  (this phone)' : ''}
              </Text>
              <Text style={styles.rowDetail}>{network.secure ? 'Secured' : 'Open'}</Text>
            </View>
            <Text style={styles.signal}>{network.signal}%</Text>
          </Pressable>
        ))
      )}

      {selected && !joined ? (
        <View style={styles.form}>
          <Text style={styles.sectionTitle}>Join {selected.ssid}</Text>
          {selected.secure ? (
            <TextInput
              value={psk}
              onChangeText={setPsk}
              placeholder="Passphrase"
              placeholderTextColor={colors.textFaint}
              secureTextEntry
              autoCapitalize="none"
              autoCorrect={false}
              editable={!inProgress}
              style={styles.input}
            />
          ) : (
            <Text style={styles.body}>This network is open; no passphrase.</Text>
          )}
          <Button
            label={attempt?.outcome && attempt.label === selected.ssid ? 'Try again' : 'Configure'}
            busy={inProgress}
            disabled={offline || (selected.secure && psk.length === 0)}
            onPress={() => configure(selected.ssid, selected.secure ? psk : undefined)}
          />
        </View>
      ) : null}

      {message ? <Text style={styles.error}>{message}</Text> : null}

      <View style={styles.footer}>
        {status?.wifi.connected && !joined ? (
          <Button
            label="Forget the Pi's WiFi"
            variant="secondary"
            busy={busy === 'forget'}
            disabled={offline}
            onPress={() =>
              void run('forget', async () => {
                await session.wifiForget();
                setAttempt(undefined);
                setStatus(await session.status());
              })
            }
          />
        ) : null}
        <Button label="Disconnect without finishing" variant="quiet" disabled={inProgress} onPress={leave} />
      </View>
    </Screen>
  );
}

const styles = StyleSheet.create({
  body: {
    ...typography.body,
    color: colors.textDim,
    lineHeight: 21,
  },
  card: {
    padding: space.lg,
    borderRadius: radius.lg,
    backgroundColor: colors.surface,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.border,
    gap: space.xs,
    marginBottom: space.xl,
  },
  cardLabel: {
    ...typography.micro,
    color: colors.textFaint,
  },
  cardTitle: {
    ...typography.title,
    color: colors.text,
  },
  cardDetail: {
    ...typography.caption,
    color: colors.textDim,
  },
  notice: {
    gap: space.md,
    padding: space.lg,
    borderRadius: radius.lg,
    backgroundColor: colors.accentSurface,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.accentEdge,
    marginBottom: space.xl,
  },
  noticeError: {
    backgroundColor: colors.surface,
    borderColor: colors.danger,
  },
  step: {
    ...typography.caption,
    fontFamily: monospace,
    color: colors.textDim,
  },
  stepCurrent: {
    color: colors.text,
  },
  sectionHeader: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  sectionTitle: {
    ...typography.heading,
    color: colors.text,
  },
  row: {
    minHeight: TOUCH_TARGET + 8,
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.md,
    paddingVertical: space.sm,
    paddingHorizontal: space.md,
    marginHorizontal: -space.md,
    borderRadius: radius.md,
  },
  rowSelected: {
    backgroundColor: colors.accentSurface,
  },
  pressed: {
    backgroundColor: colors.surface,
  },
  rowText: {
    flex: 1,
  },
  rowTitle: {
    ...typography.body,
    color: colors.text,
  },
  rowDetail: {
    ...typography.caption,
    color: colors.textFaint,
  },
  signal: {
    ...typography.label,
    color: colors.textDim,
  },
  form: {
    marginTop: space.xl,
    gap: space.md,
  },
  input: {
    ...typography.body,
    color: colors.text,
    backgroundColor: colors.surface,
    borderRadius: radius.md,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.borderStrong,
    paddingHorizontal: space.lg,
    paddingVertical: space.md,
    minHeight: 50,
  },
  error: {
    ...typography.body,
    color: colors.danger,
    marginTop: space.lg,
  },
  errorTitle: {
    ...typography.heading,
    color: colors.danger,
  },
  ok: {
    ...typography.heading,
    color: colors.ok,
  },
  copy: {
    gap: space.md,
    marginBottom: space.xl,
  },
  otherWays: {
    gap: space.sm,
  },
  detail: {
    ...typography.caption,
    color: colors.textFaint,
  },
  footer: {
    marginTop: space.xxl,
    gap: space.sm,
  },
});
