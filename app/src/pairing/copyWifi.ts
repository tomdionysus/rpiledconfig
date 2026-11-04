// Copying the phone's WiFi to the Pi with Wi-Fi Easy Connect
// (docs/protocol.md, Copying the phone's WiFi). The Pi hands over its DPP
// bootstrap URI on the encrypted channel; Android's own configurator screen
// takes it, the user picks the network and confirms, and Android sends it
// straight to the Pi. The app never sees the password. Afterwards status is
// followed exactly as after wifi.configure (join.ts).
//
// The Android screen is reached through `launch`, so this runs against the
// fake daemon in tests and expo-intent-launcher on a phone
// (src/wifi/easyConnect.ts).
import type { WifiStatus } from '@/protocol/messages';
import { describeOutcome, describeStep, followStatus, type JoinOptions, type JoinOutcome, type JoinStep, type Verdict } from './join';
import { LinkLostError, RequestError, type Session } from './session';

/** Opens Android's configurator with the URI; `done` when it reports success, `cancelled` otherwise. */
export type EasyConnectLaunch = (uri: string) => Promise<'done' | 'cancelled'>;

export type CopyStep = { kind: 'asking' } | { kind: 'android' } | JoinStep;

export type CopyOutcome =
  | JoinOutcome
  /** The Pi (or this phone) cannot do Easy Connect: offer the QR code and typing. */
  | { kind: 'unavailable'; session: Session; reason: string }
  /** The user backed out of Android's screen, and the Pi did not get a network. */
  | { kind: 'cancelled'; session: Session };

export interface CopyOptions extends JoinOptions {
  onStep?(step: CopyStep): void;
  /** The phone's current SSID, if the platform told us: only for the words on screen. */
  ssidHint?: string;
}

/** Errors meaning "not this way": the daemon's own, and an older daemon that does not know the op. */
const UNAVAILABLE_CODES = new Set(['easyconnect_unavailable', 'unknown_op', 'wifi_unavailable']);

export async function copyPhoneWifi(session: Session, launch: EasyConnectLaunch, options: CopyOptions = {}): Promise<CopyOutcome> {
  const clock = options.clock ?? session.clock;
  const step = (s: CopyStep) => options.onStep?.(s);

  // What the Pi was on before, so that only a change counts as joined.
  let before: WifiStatus | undefined;
  try {
    before = (await session.status()).wifi;
  } catch (error) {
    if (error instanceof LinkLostError) return { kind: 'lost', error };
  }

  step({ kind: 'asking' });
  let uri: string;
  try {
    uri = await session.easyConnect();
  } catch (error) {
    if (error instanceof RequestError) {
      if (UNAVAILABLE_CODES.has(error.code)) return { kind: 'unavailable', session, reason: error.message };
      return { kind: 'refused', session, error };
    }
    return { kind: 'lost', error: error instanceof Error ? error : new Error(String(error)) };
  }

  step({ kind: 'android' });
  let result: 'done' | 'cancelled';
  try {
    result = await launch(uri);
  } catch {
    await stop(session);
    return { kind: 'unavailable', session, reason: 'This phone cannot send its WiFi by Easy Connect.' };
  }
  const start = clock.now();
  const target = { before };

  if (result === 'cancelled') {
    // Android may have sent it before the user left its screen: one look.
    if (!session.isClosed) {
      try {
        const status = await session.status();
        if (status.wifi.connected && (!before?.connected || status.wifi.ssid !== before.ssid)) return { kind: 'joined', session, status };
      } catch {
        // Whatever it was, the user backed out; say so.
      }
    }
    await stop(session);
    return { kind: 'cancelled', session };
  }

  return followStatus(session, target, { ...options, clock, onStep: (s) => step(s) }, start, false);
}

async function stop(session: Session): Promise<void> {
  if (session.isClosed) return;
  try {
    await session.easyConnectStop();
  } catch {
    // Listening also ends after 120 s and when the connection closes.
  }
}

export function describeCopyStep(step: CopyStep, label: string, now?: number): string {
  switch (step.kind) {
    case 'asking':
      return 'Asking the Pi for its Easy Connect code.';
    case 'android':
      return "Android's own screen is open: pick the network to share, usually the one this phone is on, and confirm.";
    default:
      return describeStep(step, label, now);
  }
}

export function describeCopyOutcome(outcome: CopyOutcome, label: string): Verdict {
  switch (outcome.kind) {
    case 'unavailable':
      return { tone: 'error', title: 'Easy Connect is not available', body: `${outcome.reason} Scan the network's QR code or type it instead.` };
    case 'cancelled':
      return { tone: 'error', title: 'Nothing was sent', body: "Android's screen was closed before it sent a network. Try again, or scan or type the network instead." };
    default:
      return describeOutcome(outcome, label);
  }
}
