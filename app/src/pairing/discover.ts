// Before the LED: find the Pis in setup mode over BLE, and ask the chosen one
// to blink its key (docs/protocol.md, The idea and Blink). The LED is dark
// until a phone asks, so this is the first step of setting a Pi up.
import type { BleConnection, BleTransport, Peripheral } from '@/ble/transport';
import { DEFAULT_RATE, RATE_INFO, type LedRate } from '@/led/rates';
import { BLINK_STOP, CHAR_BLINK, CHAR_INFO } from '@/protocol/constants';
import { realClock, sleep, type Clock } from '@/util/clock';
import { checkProtocol, PairingError, parseInfo, type DaemonInfo } from './session';

/** A Pi seen advertising the service. */
export type FoundPi = Peripheral;

const LED_CHECKS = 5;
const LED_CHECK_MS = 200;
const LED_FAILED = 'The Pi says it could not drive its LED, so there is nothing for the camera to read. Its journal says why.';

/** A Pi not heard for this long has gone: removed, switched off, or taken by another phone. */
export const PI_STALE_MS = 8_000;
const SWEEP_MS = 1_000;

/**
 * Keeps a list of the Pis advertising now, strongest signal first, calling
 * `onChange` whenever it changes, until the returned function is called. A Pi
 * seen again replaces its entry (its signal may have changed); one not heard
 * for PI_STALE_MS leaves the list.
 */
export function watchForPis(
  transport: BleTransport,
  onChange: (pis: FoundPi[]) => void,
  onError: (error: PairingError) => void,
  clock: Clock = realClock,
): () => void {
  const found = new Map<string, { pi: FoundPi; heard: number }>();
  const report = () =>
    onChange([...found.values()].map((entry) => entry.pi).sort((a, b) => (b.rssi ?? -127) - (a.rssi ?? -127) || (a.name < b.name ? -1 : 1)));

  let cancelSweep = () => {};
  const sweep = () => {
    const now = clock.now();
    let changed = false;
    for (const [id, entry] of found) {
      if (now - entry.heard >= PI_STALE_MS) {
        found.delete(id);
        changed = true;
      }
    }
    if (changed) report();
    cancelSweep = clock.after(SWEEP_MS, sweep);
  };
  cancelSweep = clock.after(SWEEP_MS, sweep);

  const stopScan = transport.scan(
    (peripheral) => {
      found.set(peripheral.id, { pi: peripheral, heard: clock.now() });
      report();
    },
    (error) => onError(new PairingError('bluetooth', 'Bluetooth scanning failed. Check that Bluetooth is on and try again.', error)),
  );
  return () => {
    cancelSweep();
    stopScan();
  };
}

/**
 * Connects to the Pi, checks it is RPiLEDConfig, and writes the rate to
 * Blink; then disconnects, as the LED goes on blinking without the link.
 * Resolves to the Pi's Info.
 */
export async function setBlinking(transport: BleTransport, pi: FoundPi, rate: LedRate | 'stop' = DEFAULT_RATE, clock: Clock = realClock): Promise<DaemonInfo> {
  let connection: BleConnection;
  try {
    connection = await transport.connect(pi.id);
  } catch (error) {
    throw new PairingError('bluetooth', 'Could not connect to the Pi over Bluetooth. Move closer and try again.', error);
  }
  try {
    const info = parseInfo(await connection.read(CHAR_INFO));
    checkProtocol(info);
    await connection.write(CHAR_BLINK, new Uint8Array([rate === 'stop' ? BLINK_STOP : RATE_INFO[rate].blink]));
    if (rate === 'stop') return info;

    // The write is acknowledged before it is acted on, and the LED can refuse: only the Pi's own
    // word says it is blinking (docs/protocol.md, Blink).
    for (let check = 0; check < LED_CHECKS; check += 1) {
      const after = parseInfo(await connection.read(CHAR_INFO));
      if (after.led === 'blinking') return after;
      if (after.led === 'failed') throw new PairingError('led', LED_FAILED);
      await sleep(clock, LED_CHECK_MS);
    }
    throw new PairingError('led', LED_FAILED);
  } catch (error) {
    if (error instanceof PairingError) throw error;
    throw new PairingError('disconnected', 'The Bluetooth link to the Pi was lost. Try again.', error);
  } finally {
    await connection.disconnect();
  }
}
