// Before the LED: find the Pis in setup mode over BLE, and ask the chosen one
// to blink its ID (docs/protocol.md, The idea and Blink). The LED is dark
// until a phone asks, so this is the first step of setting a Pi up.
import type { BleConnection, BleTransport, Peripheral } from '@/ble/transport';
import { BLE_NAME_PREFIX, BLINK_PROTOCOL_VERSION, BLINK_START, BLINK_STOP, CHAR_BLINK, CHAR_INFO } from '@/protocol/constants';
import { checkProtocol, PairingError, parseInfo, type DaemonInfo } from './session';

/** A Pi seen advertising, with the fingerprint its name carries. */
export interface FoundPi extends Peripheral {
  fingerprint: string;
}

/** The fingerprint in `RPiLEDConfig-<4 hex>`, or undefined for any other name. */
export function fingerprintOfName(name: string): string | undefined {
  if (!name.startsWith(BLE_NAME_PREFIX)) return undefined;
  const fp = name.slice(BLE_NAME_PREFIX.length);
  return /^[0-9a-f]{4}$/.test(fp) ? fp : undefined;
}

/**
 * Keeps a list of the Pis advertising, strongest signal first, calling
 * `onChange` whenever it changes, until the returned function is called.
 * A Pi seen again replaces its entry (its signal may have changed).
 */
export function watchForPis(
  transport: BleTransport,
  onChange: (pis: FoundPi[]) => void,
  onError: (error: PairingError) => void,
): () => void {
  const found = new Map<string, FoundPi>();
  return transport.scan(
    BLE_NAME_PREFIX,
    (peripheral) => {
      const fp = fingerprintOfName(peripheral.name);
      if (!fp) return;
      found.set(peripheral.id, { ...peripheral, fingerprint: fp });
      onChange([...found.values()].sort((a, b) => (b.rssi ?? -127) - (a.rssi ?? -127) || (a.fingerprint < b.fingerprint ? -1 : 1)));
    },
    (error) => onError(new PairingError('bluetooth', 'Bluetooth scanning failed. Check that Bluetooth is on and try again.', error)),
  );
}

/**
 * Connects to the Pi, checks it is RPiLEDConfig, and writes `value` to
 * Blink; then disconnects, as the LED goes on blinking without the link.
 * A protocol 2 daemon has no Blink and is blinking already, so nothing is
 * written to it. Resolves to the Pi's Info.
 */
export async function setBlinking(transport: BleTransport, pi: FoundPi, on = true): Promise<DaemonInfo> {
  let connection: BleConnection;
  try {
    connection = await transport.connect(pi.id);
  } catch (error) {
    throw new PairingError('bluetooth', 'Could not connect to the Pi over Bluetooth. Move closer and try again.', error);
  }
  try {
    const info = parseInfo(await connection.read(CHAR_INFO));
    checkProtocol(info);
    if (info.fingerprint !== pi.fingerprint) {
      throw new PairingError('fingerprint_mismatch', `The Pi advertises ${pi.fingerprint} but says it is ${info.fingerprint}.`);
    }
    if (info.protocol >= BLINK_PROTOCOL_VERSION) await connection.write(CHAR_BLINK, new Uint8Array([on ? BLINK_START : BLINK_STOP]));
    return info;
  } catch (error) {
    if (error instanceof PairingError) throw error;
    throw new PairingError('disconnected', 'The Bluetooth link to the Pi was lost. Try again.', error);
  } finally {
    await connection.disconnect();
  }
}
