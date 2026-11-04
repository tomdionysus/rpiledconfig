// Joining a network, from `wifi.configure` to a verdict. The Pi's WiFi and
// Bluetooth share one radio, so the BLE link may drop as it joins
// (docs/protocol.md, Setup mode): a drop after configure is expected, and the
// answer is to find the same Pi again, authenticate again with the ID already
// held, and ask for status until it says the Pi is on the network or the time
// is up. A link that survives goes through the same polling, so the user sees
// one flow either way.
//
// What status can and cannot say: it reports `connected`, `ssid` and `ip`, and
// nothing about why a join failed. So a wrong passphrase is told apart from a
// slow join only by time: at the deadline, not connected (or connected to some
// other network, the one NetworkManager fell back to) is a failure, and the
// most likely cause, a wrong passphrase, is what the user is told to check.
import type { StatusResponse, WifiStatus } from '@/protocol/messages';
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
  /** A status answer that is not yet the network asked for. */
  | { kind: 'waiting'; wifi: WifiStatus; elapsedMs: number; remainingMs: number };

export type JoinOutcome =
  /** On the network asked for; `session` is the live one (it may be new). */
  | { kind: 'joined'; session: Session; status: StatusResponse }
  /** The Pi answered configure with an error: nothing was changed. */
  | { kind: 'refused'; session: Session; error: RequestError }
  /** The deadline passed with the Pi reachable and not on the network asked for. */
  | { kind: 'not_joined'; session: Session; status: StatusResponse; elapsedMs: number }
  /** The deadline passed, or authentication failed, without getting back to the Pi. */
  | { kind: 'lost'; error: Error; lastStatus?: StatusResponse };

export interface JoinOptions {
  timeoutMs?: number;
  pollMs?: number;
  onStep?(step: JoinStep): void;
  /** Defaults to the session's. */
  clock?: Clock;
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
    await session.wifiConfigure(ssid, psk);
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
export async function followStatus(session: Session, target: JoinTarget, options: JoinOptions = {}, start?: number, lossReported = false): Promise<JoinOutcome> {
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
      if (isOn(last, target)) return { kind: 'joined', session: current, status: last };
      const now = clock.now();
      step({ kind: 'waiting', wifi: last.wifi, elapsedMs: now - begun, remainingMs: Math.max(0, deadline - now) });
    } catch (error) {
      // `wifi_unavailable` while NetworkManager is busy is worth asking
      // again; anything else has closed the session, and the next pass
      // reconnects.
      if (!(error instanceof RequestError)) await current.close();
    }

    const remaining = deadline - clock.now();
    if (remaining > 0 && !current.isClosed) await sleep(clock, Math.min(pollMs, remaining));
  }

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
      // A refusal or a different fingerprint will not get better by trying
      // again, and refusals count towards the Pi's limits.
      if (error instanceof PairingError && (error.kind === 'refused' || error.kind === 'fingerprint_mismatch' || error.kind === 'protocol')) throw error;
      const left = deadline - clock.now();
      if (left > 0) await sleep(clock, Math.min(RECONNECT_PAUSE_MS, left));
    }
  }
}

/** What the screen says about one step. */
export function describeStep(step: JoinStep, ssid: string): string {
  switch (step.kind) {
    case 'sending':
      return `Sending ${ssid} to the Pi.`;
    case 'saved':
      return `Saved. The Pi is joining ${ssid}.`;
    case 'link_lost':
      return step.answered
        ? 'Bluetooth dropped while the Pi switched its radio to WiFi. That is expected; finding it again.'
        : 'Bluetooth dropped before the Pi answered. It may have taken the network; finding it again to ask.';
    case 'reconnecting':
      return `Reconnecting, attempt ${step.attempt}: ${RECONNECT_PHASES[step.phase]}.`;
    case 'reconnected':
      return 'Back in touch and authenticated again.';
    case 'waiting':
      return `Waiting for the Pi to join ${ssid}: ${Math.round(step.remainingMs / 1000)} s left.`;
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
  checking: 'checking its fingerprint',
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
        body: `${outcome.error.message} It may still have joined ${ssid}: it stays in setup mode until told to finish, so pair again to ask it.`,
      };
  }
}
