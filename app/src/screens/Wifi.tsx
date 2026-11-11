// WiFi over the paired session: pick a network from the Pi's scan, or type its
// name, and give its passphrase. The join flow (pairing/join.ts) expects the BLE
// link to drop and reconnects.
// Wi-Fi Easy Connect (copying this phone's WiFi) is left out: on the A85 it
// took the phone off its network and never reached the Pi.
//
// The link can also go while nothing is happening (a daemon restarted under
// a link bluetoothd kept up gives no disconnect), so the screen asks for
// status every few seconds while idle, and says so in plain words when it
// fails.
import { useRouter } from '@/navigation';
import { useCallback, useEffect, useRef, useState } from 'react';
import { Alert, BackHandler, KeyboardAvoidingView, Pressable, StyleSheet, Text, TextInput, View } from 'react-native';
import { describeCopyOutcome, describeCopyStep, type CopyOutcome, type CopyStep } from '@/pairing/copyWifi';
import { piIdentity } from '@/pairing/identity';
import { collapseSteps, joinWifi, type Verdict } from '@/pairing/join';
import { LinkLostError, RequestError } from '@/pairing/session';
import { bytesToHex } from '@/protocol/bytes';
import { MessageError, type Network, type StatusResponse } from '@/protocol/messages';
import { useApp } from '@/state/store';
import { Button } from '@/ui/Button';
import { Screen } from '@/ui/Screen';
import { colors, monospace, radius, space, type as typography, TOUCH_TARGET } from '@/ui/theme';
import { DIAGNOSTICS } from '@/util/diagnostics';
import { phoneSsid } from '@/wifi/phoneWifi';

type Busy = 'status' | 'scan' | 'forget' | 'finish' | 'uninstall' | undefined;

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
  const { session, setSession, pairingKey, setPairingKey } = useApp();
  const [status, setStatus] = useState<StatusResponse | undefined>(undefined);
  const [networks, setNetworks] = useState<Network[] | undefined>(undefined);
  const [choice, setChoice] = useState<JoinChoice | undefined>(undefined);
  const cancelChoice = useCallback(() => setChoice(undefined), []);
  const [busy, setBusy] = useState<Busy>(undefined);
  const [message, setMessage] = useState<string | undefined>(undefined);
  const [note, setNote] = useState<string | undefined>(undefined);
  const [attempt, setAttempt] = useState<Attempt | undefined>(undefined);
  const [closed, setClosed] = useState<LinkLostError | undefined>(undefined);
  const [phoneNetwork, setPhoneNetwork] = useState<string | undefined>(undefined);
  /** The Pi answered uninstall: its removal has started. */
  const [removed, setRemoved] = useState(false);
  // While a join runs, the link dropping is part of the plan, not news.
  const joining = useRef(false);
  // Once a second while a join runs, so its countdown moves between status polls.
  const [now, setNow] = useState(() => Date.now());

  const run = useCallback(async (what: Exclude<Busy, undefined>, action: () => Promise<void>) => {
    setBusy(what);
    setMessage(undefined);
    setNote(undefined);
    try {
      await action();
    } catch (error) {
      // A lost link has its own notice; anything else is said here.
      if (!(error instanceof LinkLostError)) setMessage(describe(error));
    } finally {
      setBusy(undefined);
    }
  }, []);

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
    if (!inProgress) return;
    const timer = setInterval(() => setNow(Date.now()), 1_000);
    return () => clearInterval(timer);
  }, [inProgress]);
  useEffect(() => {
    if (!session || closed || inProgress || busy) return;
    const timer = setTimeout(() => {
      if (joining.current || session.isClosed) return;
      session.status().then(setStatus, () => {
        // LinkLostError reaches the screen through onClosed.
      });
    }, KEEPALIVE_MS);
    return () => clearTimeout(timer);
  }, [session, closed, inProgress, busy, status]);

  if (removed) {
    return (
      <Screen title="Removal started">
        <Text style={styles.body}>
          The Pi accepted the request and has started removing RPi LED Config: the service stops, and its program and settings are deleted, while its WiFi
          stays. This app cannot see how that ends. If the Pi is listed again later, the removal did not finish.
        </Text>
        <Button label="Home" onPress={() => router.replace('/')} style={styles.removedHome} />
      </Screen>
    );
  }

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

  // docs/protocol.md, Uninstalling: destructive and for good, so it is confirmed first.
  const uninstall = () => {
    Alert.alert(
      'Remove RPi LED Config from this Pi?',
      'The service is stopped and deleted with its settings. The Pi keeps its WiFi, but cannot be set up from a phone again until the package is reinstalled.',
      [
        { text: 'Cancel', style: 'cancel' },
        {
          text: 'Remove',
          style: 'destructive',
          onPress: () =>
            void run('uninstall', async () => {
              await session.uninstall();
              setSession(undefined);
              setPairingKey(undefined);
              setRemoved(true);
            }),
        },
      ],
    );
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
    const outcome = await how((step) =>
      setAttempt((current) => (current && current.outcome === undefined ? { ...current, steps: [...current.steps, step] } : current)),
    );
    joining.current = false;
    if (outcome.kind === 'lost') {
      if (outcome.lastStatus) setStatus(outcome.lastStatus);
    } else {
      if (outcome.session !== session) setSession(outcome.session);
      if (outcome.kind === 'joined' || outcome.kind === 'not_joined') setStatus(outcome.status);
    }
    setAttempt({
      label,
      steps: [],
      outcome,
      verdict: describeCopyOutcome(outcome, label),
    });
  };

  const configure = (ssid: string, passphrase: string | undefined, hidden = false) => {
    if (DIAGNOSTICS) console.log(`[wifi] join ${ssid}${hidden ? ' (typed)' : ''}`);
    void follow(ssid, (onStep) => joinWifi(session, ssid, passphrase, { onStep, hidden }));
  };

  const choose = (next: JoinChoice) => {
    setMessage(undefined);
    if (attempt?.outcome) setAttempt(undefined);
    setChoice(next);
  };

  const identity = piIdentity(status, pairingKey ? bytesToHex(pairingKey) : undefined, session.peripheral.name);
  const lost = attempt?.outcome?.kind === 'lost';
  const joined = attempt?.outcome?.kind === 'joined';
  const offline = !!closed || lost || inProgress;
  const piNetwork = status?.wifi.connected ? status.wifi.ssid : undefined;

  return (
    <View style={styles.fill}>
      <Screen title="WiFi" eyebrow="Step 4 of 4">
        <View style={styles.card}>
          <Text style={[styles.cardTitle, identity.notice ? styles.cardKey : null]}>{identity.title}</Text>
          {identity.notice ? <Text style={styles.cardDetail}>{identity.notice}</Text> : null}
          <Text style={styles.cardStatus}>
            {status
              ? status.wifi.connected
                ? `On ${status.wifi.ssid ?? 'WiFi'}${status.wifi.ip ? `, ${status.wifi.ip}` : ''}`
                : 'Not on WiFi'
              : 'Asking for its status'}
          </Text>
          <Text style={styles.cardDetail}>
            {session.peripheral.name}, daemon {session.info.version}
          </Text>
        </View>

        {closed && !inProgress && !lost ? (
          <View style={[styles.notice, styles.noticeError]}>
            <Text style={styles.errorTitle}>Lost touch with the Pi</Text>
            <Text style={styles.body}>{closed.message}</Text>
            {DIAGNOSTICS && closed.detail ? <Text style={styles.detail}>{closed.detail}</Text> : null}
            <Button label="Pair again" onPress={pairAgain} />
          </View>
        ) : null}

        {inProgress ? (
          <View style={styles.notice} accessibilityLiveRegion="polite">
            <Text style={styles.sectionTitle}>Joining {attempt.label}</Text>
            {collapseSteps(attempt.steps).map((step, index, all) => (
              <Text key={`${index}-${step.kind}`} style={[styles.step, index === all.length - 1 && styles.stepCurrent]}>
                {index === all.length - 1 ? 'now   ' : 'done  '}
                {describeCopyStep(step, attempt.label, now)}
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
                <Button
                  label="Finish"
                  busy={busy === 'finish'}
                  onPress={() =>
                    void run('finish', async () => {
                      await session.finish();
                      setSession(undefined);
                      setPairingKey(undefined);
                      router.replace('/');
                    })
                  }
                />
                <Text style={styles.detail}>Or, if this Pi will not need setting up from a phone again, remove the service and keep the WiFi it is on.</Text>
                <Button
                  label="Remove RPi LED Config from this Pi"
                  variant="danger"
                  busy={busy === 'uninstall'}
                  disabled={busy !== undefined}
                  onPress={uninstall}
                />
              </>
            ) : lost ? (
              <Button label="Pair again" onPress={pairAgain} />
            ) : null}
          </View>
        ) : null}

        <View style={styles.sectionHeader}>
          <Text style={styles.sectionTitle}>Networks the Pi can see</Text>
          <Button label="Rescan" variant="quiet" busy={busy === 'scan'} disabled={offline} onPress={() => void refresh()} />
        </View>
        {networks === undefined ? (
          <Text style={styles.body}>{busy === 'scan' ? 'Scanning.' : 'No scan yet.'}</Text>
        ) : networks.length === 0 ? (
          <Text style={styles.body}>The Pi sees no networks. Rescan in a moment.</Text>
        ) : (
          <View style={styles.list}>
            {networks.map((network, index) => (
              <Pressable
                key={network.ssid}
                accessibilityRole="button"
                accessibilityLabel={`Join ${network.ssid}`}
                disabled={offline}
                onPress={() => choose({ kind: 'scanned', network })}
                style={({ pressed }) => [styles.row, index > 0 && styles.rowDivider, pressed && styles.pressed, offline && styles.dimmed]}
              >
                <View style={styles.rowText}>
                  <Text style={styles.rowTitle} numberOfLines={1}>
                    {network.ssid}
                    <Text style={styles.tags}>{networkTags(network.ssid, piNetwork, phoneNetwork)}</Text>
                  </Text>
                  <Text style={styles.rowDetail}>{network.secure ? 'Secured' : 'Open'}</Text>
                </View>
                <Text style={styles.signal}>{network.signal}%</Text>
              </Pressable>
            ))}
          </View>
        )}
        <Button label="Other network: type its name" variant="quiet" disabled={offline} onPress={() => choose({ kind: 'typed' })} />

        {message ? <Text style={styles.error}>{message}</Text> : null}
        {note ? <Text style={styles.noteText}>{note}</Text> : null}

        <View style={styles.footer}>
          {!joined ? (
            <Button
              label="Forget networks this app saved"
              variant="secondary"
              busy={busy === 'forget'}
              disabled={offline}
              onPress={() =>
                void run('forget', async () => {
                  const count = await session.wifiForget();
                  setAttempt(undefined);
                  setNote(
                    count === 0
                      ? 'This app has saved no network on this Pi, so nothing changed. A network set up any other way stays.'
                      : `Forgot ${count === 1 ? 'the network' : `${count} networks`} this app saved. A network set up any other way stays.`,
                  );
                  setStatus(await session.status());
                })
              }
            />
          ) : null}
          <Button label="Disconnect without finishing" variant="quiet" disabled={inProgress} onPress={leave} />
          {joined ? null : (
            <Button
              label="Remove RPi LED Config from this Pi"
              variant="danger"
              busy={busy === 'uninstall'}
              disabled={offline || busy !== undefined}
              onPress={uninstall}
            />
          )}
        </View>
      </Screen>
      <JoinSheet
        choice={choice}
        busy={inProgress}
        onCancel={cancelChoice}
        onJoin={(request) => {
          setChoice(undefined);
          configure(request.ssid, request.passphrase, request.hidden);
        }}
      />
    </View>
  );
}

/** What the join dialog was opened for: a network from the Pi's scan, or one to type. */
type JoinChoice = { kind: 'scanned'; network: Network } | { kind: 'typed' };

/** What the dialog hands back. */
interface JoinRequest {
  ssid: string;
  passphrase: string | undefined;
  hidden: boolean;
}

/** How long after the dialog mounts the first field is focused: after the mount is complete. */
const FOCUS_DELAY_MS = 80;

/** An eye, drawn with views (no icon library, no emoji): open, or struck through. */
function EyeIcon({ open }: { open: boolean }) {
  return (
    <View style={styles.eye} pointerEvents="none">
      <View style={styles.eyeOutline}>
        <View style={styles.eyePupil} />
      </View>
      {open ? null : <View style={styles.eyeStrike} />}
    </View>
  );
}

/**
 * The join dialog: the passphrase for a network from the list, or the SSID
 * and passphrase of one typed by hand. Centred, and moved up by the keyboard.
 *
 * The first field is focused from code after the mount, not with autoFocus:
 * on the A85 (React Native 0.86, new architecture) a field opened with
 * autoFocus showed what was typed but its onChangeText never fired, so the
 * passphrase stayed empty and Join stayed disabled.
 */
function JoinSheet({
  choice,
  busy,
  onCancel,
  onJoin,
}: {
  choice: JoinChoice | undefined;
  busy: boolean;
  onCancel(): void;
  onJoin(request: JoinRequest): void;
}) {
  const [ssid, setSsid] = useState('');
  const [psk, setPsk] = useState('');
  const [secure, setSecure] = useState(true);
  const [shown, setShown] = useState(false);
  const first = useRef<TextInput>(null);

  // A fresh dialog each time it opens, and the first field focused once it is on screen.
  useEffect(() => {
    setSsid('');
    setPsk('');
    setShown(false);
    setSecure(choice?.kind === 'scanned' ? choice.network.secure : true);
    if (!choice) return;
    const timer = setTimeout(() => first.current?.focus(), FOCUS_DELAY_MS);
    return () => clearTimeout(timer);
  }, [choice]);

  // The system back closes the dialog rather than the screen.
  useEffect(() => {
    if (!choice) return;
    const subscription = BackHandler.addEventListener('hardwareBackPress', () => {
      onCancel();
      return true;
    });
    return () => subscription.remove();
  }, [choice, onCancel]);

  if (!choice) return null;
  const typed = choice.kind === 'typed';
  const name = typed ? ssid.trim() : choice.network.ssid;
  // Only what cannot be sent at all: no name, or a secured network with nothing typed. What
  // a passphrase must look like is the network's business, and the Pi reports a refusal.
  const ready = name.length > 0 && (!secure || psk.length > 0) && !busy;
  const join = () => {
    if (DIAGNOSTICS) console.log(`[join] press: name ${name.length} chars, passphrase ${psk.length} chars, secure ${secure}, ready ${ready}`);
    if (!ready) return;
    onJoin({ ssid: name, passphrase: secure ? psk : undefined, hidden: typed });
  };
  const onPsk = (text: string) => {
    if (DIAGNOSTICS) console.log(`[join] passphrase now ${text.length} chars`);
    setPsk(text);
  };

  return (
    <KeyboardAvoidingView behavior="padding" style={styles.scrim}>
      <View style={styles.sheet}>
        <Text style={styles.sheetTitle} numberOfLines={2}>
          {typed ? 'Join another network' : `Join ${name}`}
        </Text>
        {typed ? (
          <TextInput
            ref={first}
            value={ssid}
            onChangeText={setSsid}
            placeholder="Network name (SSID)"
            placeholderTextColor={colors.textFaint}
            autoCapitalize="none"
            autoCorrect={false}
            returnKeyType="next"
            style={styles.input}
          />
        ) : null}
        {typed ? (
          <Pressable accessibilityRole="switch" accessibilityState={{ checked: secure }} hitSlop={8} onPress={() => setSecure((on) => !on)}>
            <Text style={styles.show}>{secure ? 'Secured: tap if it is an open network' : 'Open: tap if it has a passphrase'}</Text>
          </Pressable>
        ) : null}
        {secure ? (
          <View style={styles.field}>
            <TextInput
              ref={typed ? undefined : first}
              value={psk}
              onChangeText={onPsk}
              placeholder="Passphrase"
              placeholderTextColor={colors.textFaint}
              secureTextEntry={!shown}
              autoCapitalize="none"
              autoCorrect={false}
              returnKeyType="done"
              onSubmitEditing={join}
              style={[styles.input, styles.inputWithIcon]}
            />
            <Pressable
              accessibilityRole="switch"
              accessibilityLabel={shown ? 'Hide passphrase' : 'Show passphrase'}
              accessibilityState={{ checked: shown }}
              hitSlop={8}
              onPress={() => setShown((on) => !on)}
              style={styles.eyeButton}
            >
              <EyeIcon open={shown} />
            </Pressable>
          </View>
        ) : (
          <Text style={styles.body}>An open network: no passphrase.</Text>
        )}
        <Button label="Join" disabled={!ready} busy={busy} onPress={join} />
        <Button label="Cancel" variant="quiet" onPress={onCancel} />
      </View>
    </KeyboardAvoidingView>
  );
}

/** After an SSID in the list: whether the Pi is on it now, and whether this phone is. */
function networkTags(ssid: string, piNetwork: string | undefined, phoneNetwork: string | undefined): string {
  const tags = [ssid === piNetwork ? 'current' : undefined, ssid === phoneNetwork ? 'this phone' : undefined].filter((tag) => tag !== undefined);
  return tags.length > 0 ? `  (${tags.join(', ')})` : '';
}

const styles = StyleSheet.create({
  removedHome: {
    marginTop: space.xl,
  },
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
  cardTitle: {
    ...typography.title,
    color: colors.text,
  },
  cardKey: {
    fontFamily: monospace,
  },
  cardStatus: {
    ...typography.body,
    color: colors.text,
  },
  cardDetail: {
    ...typography.caption,
    color: colors.textFaint,
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
    marginBottom: space.xs,
  },
  sectionTitle: {
    ...typography.heading,
    color: colors.text,
  },
  list: {
    borderRadius: radius.lg,
    backgroundColor: colors.surface,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.border,
    overflow: 'hidden',
    marginBottom: space.sm,
  },
  row: {
    minHeight: TOUCH_TARGET + 12,
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.md,
    paddingVertical: space.sm,
    paddingHorizontal: space.lg,
  },
  rowDivider: {
    borderTopWidth: StyleSheet.hairlineWidth,
    borderTopColor: colors.border,
  },
  pressed: {
    backgroundColor: colors.surface2,
  },
  dimmed: {
    opacity: 0.5,
  },
  rowText: {
    flex: 1,
  },
  rowTitle: {
    ...typography.body,
    color: colors.text,
  },
  tags: {
    ...typography.caption,
    color: colors.accent,
  },
  rowDetail: {
    ...typography.caption,
    color: colors.textFaint,
  },
  signal: {
    ...typography.label,
    color: colors.textDim,
  },
  noteText: {
    ...typography.body,
    color: colors.textDim,
    marginTop: space.lg,
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
  detail: {
    ...typography.caption,
    color: colors.textFaint,
  },
  footer: {
    marginTop: space.xxl,
    paddingTop: space.xl,
    gap: space.sm,
    borderTopWidth: StyleSheet.hairlineWidth,
    borderTopColor: colors.border,
  },
  fill: {
    flex: 1,
  },
  scrim: {
    position: 'absolute',
    top: 0,
    left: 0,
    right: 0,
    bottom: 0,
    justifyContent: 'center',
    paddingHorizontal: space.xl,
    backgroundColor: colors.scrim,
  },
  sheet: {
    gap: space.md,
    padding: space.xl,
    borderRadius: radius.xl,
    backgroundColor: colors.backgroundLift,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.borderStrong,
  },
  sheetTitle: {
    ...typography.title,
    color: colors.text,
  },
  field: {
    justifyContent: 'center',
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
  inputWithIcon: {
    paddingRight: TOUCH_TARGET + space.sm,
  },
  eyeButton: {
    position: 'absolute',
    right: 0,
    top: 0,
    bottom: 0,
    width: TOUCH_TARGET + space.sm,
    alignItems: 'center',
    justifyContent: 'center',
  },
  eye: {
    width: 24,
    height: 16,
    alignItems: 'center',
    justifyContent: 'center',
  },
  eyeOutline: {
    width: 24,
    height: 14,
    borderRadius: 7,
    borderWidth: 1.5,
    borderColor: colors.textDim,
    alignItems: 'center',
    justifyContent: 'center',
  },
  eyePupil: {
    width: 6,
    height: 6,
    borderRadius: 3,
    backgroundColor: colors.textDim,
  },
  eyeStrike: {
    position: 'absolute',
    width: 26,
    height: 1.5,
    backgroundColor: colors.textDim,
    transform: [{ rotate: '-35deg' }],
  },
  show: {
    ...typography.label,
    color: colors.textDim,
  },
});
