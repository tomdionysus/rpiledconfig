// Joining a network, from `wifi.configure` to a verdict. The Pi's WiFi and
// Bluetooth share one radio, so the BLE link may drop as it joins
// (docs/protocol.md, Setup mode): a drop after configure is expected, and the
// answer is to find the same Pi again, authenticate again with the key already
// held, and ask for status until it says the Pi is on the network or the time
// is up. A link that survives goes through the same polling, so the user sees
// one flow either way.
//
// What status says: `connected`, `ssid` and `ip`, and `join`, what became of
// the latest configure (docs/protocol.md, The latest join). A wrong passphrase
// or a name nothing broadcasts is reported the moment the Pi's NetworkManager
// gives up, with its reason; only a join that neither lands nor fails runs to
// the deadline.
import type { JoinState, StatusResponse, WifiStatus } from '@/protocol/messages';
import { sleep, type Clock } from '@/util/clock';
import { PairingError, RequestError, type PairingPhase, type Session } from './session';

export const JOIN_TIMEOUT_MS = 45_000;
export const STATUS_POLL_MS = 2_000;
/** Between reconnect attempts, so a Pi still busy on the WiFi side is not hammered. */
const RECONNECT_PAUSE_MS = 1_000;
/** One reconnect attempt's scan; the overall deadline still applies. */
const RECONNECT_SCAN_MS = 15_000;

export type JoinStep =
  | { kind: 'sending' }
  /** The Pi saved the network and asked for it to be activated. */
  | { kind: 'saved' }
  /** The BLE link went. `answered` is false when it went before configure was answered. */
  | { kind: 'link_lost'; answered: boolean }
  | { kind: 'reconnecting'; attempt: number; phase: PairingPhase }
  | { kind: 'reconnected' }
  /** A status answer that is not yet the network asked for. `deadline` is on the clock the join runs on. */
  | { kind: 'waiting'; wifi: WifiStatus; elapsedMs: number; remainingMs: number; deadline: number };

export type JoinOutcome =
  /** On the network asked for; `session` is the live one (it may be new). */
  | { kind: 'joined'; session: Session; status: StatusResponse }
  /** The Pi answered configure with an error: nothing was changed. */
  | { kind: 'refused'; session: Session; error: RequestError }
  /** The Pi reported the join failed (`reason`), or the deadline passed with it reachable and not on the network. */
  | { kind: 'not_joined'; session: Session; status: StatusResponse; elapsedMs: number; reason?: JoinState }
  /** The deadline passed, or authentication failed, without getting back to the Pi. */
  | { kind: 'lost'; error: Error; lastStatus?: StatusResponse };

export interface JoinOptions {
  timeoutMs?: number;
  pollMs?: number;
  onStep?(step: JoinStep): void;
  /** Defaults to the session's. */
  clock?: Clock;
  /** A network typed by hand, which the Pi probes for by name. */
  hidden?: boolean;
}

/** What counts as joined: the SSID asked for, or, when the phone chose it, any network the Pi was not on before. */
export interface JoinTarget {
  ssid?: string;
  before?: WifiStatus;
}

export function isOn(status: StatusResponse, target: JoinTarget): boolean {
  if (!status.wifi.connected) return false;
  if (target.ssid !== undefined) return status.wifi.ssid === target.ssid;
  return !target.before?.connected || status.wifi.ssid !== target.before.ssid;
}

/**
 * Configure `ssid` and see it through. Never rejects: every way it can end
 * is a `JoinOutcome`. The session passed in may be closed by the time it
 * resolves; use the one in the outcome.
 */
export async function joinWifi(session: Session, ssid: string, psk: string | undefined, options: JoinOptions = {}): Promise<JoinOutcome> {
  const clock = options.clock ?? session.clock;
  const start = clock.now();
  const step = (s: JoinStep) => options.onStep?.(s);
  step({ kind: 'sending' });
  try {
    await session.wifiConfigure(ssid, psk, options.hidden ?? false);
    step({ kind: 'saved' });
  } catch (error) {
    if (error instanceof RequestError) return { kind: 'refused', session, error };
    // No answer. The radio may have gone over to WiFi before the answer went
    // out; whether the network was taken is for status to say.
    await session.close();
    step({ kind: 'link_lost', answered: false });
    return followStatus(session, { ssid }, { ...options, clock }, start, true);
  }
  return followStatus(session, { ssid }, { ...options, clock }, start, false);
}

/**
 * Ask for status until the Pi is on `target` or the time is up, getting
 * back to the Pi whenever the link drops. `start` is when the join began;
 * the timeout runs from it. Never rejects.
 */
export async function followStatus(
  session: Session,
  target: JoinTarget,
  options: JoinOptions = {},
  start?: number,
  lossReported = false,
): Promise<JoinOutcome> {
  const clock = options.clock ?? session.clock;
  const timeoutMs = options.timeoutMs ?? JOIN_TIMEOUT_MS;
  const pollMs = options.pollMs ?? STATUS_POLL_MS;
  const step = (s: JoinStep) => options.onStep?.(s);
  const begun = start ?? clock.now();
  const deadline = begun + timeoutMs;
  let current = session;
  let last: StatusResponse | undefined;
  let reported = lossReported;

  while (clock.now() < deadline) {
    if (current.isClosed) {
      // An answer that did not verify is not the radio dropping, and is not retried past.
      if (current.closedBy?.unverified) return { kind: 'lost', error: current.closedBy, lastStatus: last };
      if (!reported) step({ kind: 'link_lost', answered: true });
      try {
        current = await reconnect(current, deadline, timeoutMs, clock, step);
      } catch (error) {
        return { kind: 'lost', error: error instanceof Error ? error : new Error(String(error)), lastStatus: last };
      }
      reported = false;
      step({ kind: 'reconnected' });
    }

    try {
      last = await current.status();
      // What the Pi says of this join comes first: it can be on a network of that name by another
      // connection while this attempt has failed, or not yet been decided.
      const join = last.wifi.join;
      const ours = join && (target.ssid === undefined || join.ssid === target.ssid) ? join : undefined;
      if (ours?.state === 'failed') {
        return { kind: 'not_joined', session: current, status: last, elapsedMs: clock.now() - begun, reason: ours };
      }
      if (isOn(last, target) && (!ours || ours.state === 'joined')) return { kind: 'joined', session: current, status: last };
      const now = clock.now();
      step({ kind: 'waiting', wifi: last.wifi, elapsedMs: now - begun, remainingMs: Math.max(0, deadline - now), deadline });
    } catch (error) {
      // `wifi_unavailable` while NetworkManager is busy is worth asking
      // again; anything else has closed the session, and the next pass
      // reconnects.
      if (!(error instanceof RequestError)) await current.close();
    }

    const remaining = deadline - clock.now();
    if (remaining > 0 && !current.isClosed) await sleep(clock, Math.min(pollMs, remaining));
  }

  // Out of time with the Pi on the network and no failure from it: it is on the network.
  if (!current.isClosed && last && isOn(last, target) && last.wifi.join?.state !== 'failed') return { kind: 'joined', session: current, status: last };
  if (current.isClosed || !last) {
    return { kind: 'lost', error: new PairingError('timeout', `The Pi did not answer within ${Math.round(timeoutMs / 1000)} seconds.`), lastStatus: last };
  }
  return { kind: 'not_joined', session: current, status: last, elapsedMs: clock.now() - begun };
}

async function reconnect(previous: Session, deadline: number, timeoutMs: number, clock: Clock, step: (s: JoinStep) => void): Promise<Session> {
  for (let attempt = 1; ; attempt += 1) {
    const remaining = deadline - clock.now();
    if (remaining <= 0) {
      throw new PairingError('not_found', `The Pi did not come back on Bluetooth within ${Math.round(timeoutMs / 1000)} seconds.`);
    }
    try {
      return await previous.reconnect({
        scanTimeoutMs: Math.min(remaining, RECONNECT_SCAN_MS),
        onPhase: (phase) => step({ kind: 'reconnecting', attempt, phase }),
      });
    } catch (error) {
      // A refusal will not get better by trying
      // again, and refusals count towards the Pi's limits.
      if (error instanceof PairingError && (error.kind === 'refused' || error.kind === 'protocol')) throw error;
      const left = deadline - clock.now();
      if (left > 0) await sleep(clock, Math.min(RECONNECT_PAUSE_MS, left));
    }
  }
}

/** What the screen says about one step; `now`, on the join's clock, keeps a countdown moving between polls. */
export function describeStep(step: JoinStep, ssid: string, now?: number): string {
  switch (step.kind) {
    case 'sending':
      return `Sending ${ssid} to the Pi.`;
    case 'saved':
      return `Saved. The Pi is joining ${ssid}.`;
    case 'link_lost':
      return step.answered
        ? 'Bluetooth dropped, as it often does while the Pi switches its radio to WiFi. Finding it again.'
        : 'Bluetooth dropped before the Pi answered. It may have taken the network; finding it again to ask.';
    case 'reconnecting':
      return `Reconnecting, attempt ${step.attempt}: ${RECONNECT_PHASES[step.phase]}.`;
    case 'reconnected':
      return 'Back in touch and authenticated again.';
    case 'waiting': {
      const left = now === undefined ? step.remainingMs : Math.max(0, step.deadline - now);
      return `Waiting for the Pi to join ${ssid}: ${Math.ceil(left / 1000)} s left.`;
    }
  }
}

/**
 * The steps as a list to show: a run of status polls, or of one reconnect
 * attempt's phases, is one line that updates rather than a line each.
 */
export function collapseSteps<T extends { kind: string }>(steps: readonly T[]): T[] {
  const out: T[] = [];
  for (const step of steps) {
    const last = out[out.length - 1];
    const sameLine = last && last.kind === step.kind && (step.kind === 'waiting' || step.kind === 'reconnecting');
    if (sameLine) out[out.length - 1] = step;
    else out.push(step);
  }
  return out;
}

const RECONNECT_PHASES: Record<PairingPhase, string> = {
  scanning: 'looking for the Pi',
  connecting: 'connecting',
  checking: 'checking it',
  authenticating: 'authenticating',
  ready: 'authenticated',
};

export interface Verdict {
  tone: 'ok' | 'error';
  title: string;
  body: string;
}

/** What the screen says about how it ended. */
export function describeOutcome(outcome: JoinOutcome, ssid: string): Verdict {
  switch (outcome.kind) {
    case 'joined':
      return {
        tone: 'ok',
        title: `Joined ${outcome.status.wifi.ssid ?? ssid}`,
        body: `The Pi is on ${outcome.status.wifi.ssid ?? ssid}${outcome.status.wifi.ip ? ` as ${outcome.status.wifi.ip}` : ''}. Finish to take it out of setup mode.`,
      };
    case 'refused':
      return { tone: 'error', title: `The Pi would not take ${ssid}`, body: `${outcome.error.message} (${outcome.error.code})` };
    case 'not_joined': {
      const seconds = Math.round(outcome.elapsedMs / 1000);
      const other = outcome.status.wifi.connected ? outcome.status.wifi.ssid : undefined;
      const back = other ? ` The Pi is back on ${other}.` : '';
      if (outcome.reason?.reason === 'wrong_passphrase') {
        return {
          tone: 'error',
          title: `Wrong passphrase for ${ssid}`,
          body: `${ssid} refused the passphrase after ${seconds} seconds.${back} Check it and try again.`,
        };
      }
      if (outcome.reason?.reason === 'not_found') {
        return {
          tone: 'error',
          title: `No network called ${ssid}`,
          body: `The Pi looked for ${seconds} seconds and found no network called ${ssid}.${back} Check the name: it has to match exactly, capitals included.`,
        };
      }
      if (outcome.reason) {
        return {
          tone: 'error',
          title: `Did not join ${ssid}`,
          body: `The Pi gave up after ${seconds} seconds: ${outcome.reason.message ?? 'no reason given'}.${back}`,
        };
      }
      return {
        tone: 'error',
        title: `Did not join ${ssid}`,
        body: other
          ? `After ${seconds} seconds the Pi is back on ${other}, not ${ssid}. The passphrase for ${ssid} is most likely wrong. Check it and try again.`
          : `After ${seconds} seconds the Pi is still not on ${ssid}. The passphrase is most likely wrong, or the network is out of the Pi's range. Check the passphrase and try again.`,
      };
    }
    case 'lost':
      return {
        tone: 'error',
        title: 'Lost touch with the Pi',
        body: `${outcome.error.message} It may still have joined ${ssid}. Pair again to ask it; if it is not listed, it has left setup mode.`,
      };
  }
}
