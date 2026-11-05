import { describe, expect, it } from 'vitest';
import { FakeDaemon } from '@/ble/fake';
import { hexToBytes } from '@/protocol/bytes';
import { ManualClock } from '@/util/clock';
import type { BleTransport, Peripheral } from '@/ble/transport';
import { PI_STALE_MS, setBlinking, watchForPis, type FoundPi } from './discover';
import { pair, PairingError } from './session';

const ID = hexToBytes('0123456789abcdef');

function tick(): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, 0));
}

async function found(daemon: FakeDaemon): Promise<FoundPi> {
  let pis: FoundPi[] = [];
  const stop = watchForPis(
    daemon,
    (next) => (pis = next),
    () => {},
  );
  await tick();
  stop();
  expect(pis).toHaveLength(1);
  return pis[0];
}

describe('watchForPis, over time', () => {
  /** A transport whose only act is the reports a test makes. */
  function radio() {
    let onFound: (peripheral: Peripheral) => void = () => {};
    let stopped = false;
    const transport = {
      scan(found: (peripheral: Peripheral) => void) {
        onFound = found;
        return () => {
          stopped = true;
        };
      },
    } as unknown as BleTransport;
    return { transport, hear: (peripheral: Peripheral) => onFound(peripheral), isStopped: () => stopped };
  }
  const PI = { id: 'a', name: 'RPiLEDConfig-3a9c', rssi: -50 };
  const OTHER = { id: 'b', name: 'Kitchen Pi-8ab5', rssi: -60 };

  it('drops a Pi that has gone quiet, such as one just removed', async () => {
    const clock = new ManualClock();
    const { transport, hear } = radio();
    let pis: FoundPi[] = [];
    const stop = watchForPis(transport, (next) => (pis = next), () => {}, clock);

    hear(PI);
    expect(pis).toEqual([PI]);
    await clock.advance(PI_STALE_MS - 1_000);
    expect(pis).toEqual([PI]);
    await clock.advance(1_000);
    expect(pis).toEqual([]);
    stop();
  });

  it('keeps a Pi that is still being heard, and drops only the one that is not', async () => {
    const clock = new ManualClock();
    const { transport, hear } = radio();
    let pis: FoundPi[] = [];
    const stop = watchForPis(transport, (next) => (pis = next), () => {}, clock);

    hear(PI);
    hear(OTHER);
    for (let second = 0; second < 20; ++second) {
      await clock.advance(1_000);
      hear(PI);
    }
    expect(pis).toEqual([PI]);
    stop();
  });

  it('lists a Pi again when it comes back', async () => {
    const clock = new ManualClock();
    const { transport, hear } = radio();
    let pis: FoundPi[] = [];
    const stop = watchForPis(transport, (next) => (pis = next), () => {}, clock);

    hear(PI);
    await clock.advance(PI_STALE_MS);
    expect(pis).toEqual([]);
    hear(PI);
    expect(pis).toEqual([PI]);
    stop();
  });

  it('stops both the scan and the sweep', async () => {
    const clock = new ManualClock();
    const { transport, hear, isStopped } = radio();
    const calls: FoundPi[][] = [];
    const stop = watchForPis(transport, (next) => calls.push(next), () => {}, clock);

    hear(PI);
    stop();
    expect(isStopped()).toBe(true);
    await clock.advance(PI_STALE_MS * 2);
    expect(calls).toHaveLength(1);
  });
});

describe('watchForPis and setBlinking', () => {
  it('lists the Pi by its full name, whatever its prefix', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID, name: 'Kitchen Pi-3a9c' });
    const pi = await found(daemon);
    expect(pi.name).toBe('Kitchen Pi-3a9c');
    expect(pi.id).toBe(daemon.id);
  });

  it('makes the LED blink, and leaves the link down', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    expect(daemon.blinking).toBe(false);

    const info = await setBlinking(daemon, await found(daemon));
    expect(info.protocol).toBe(4);
    expect(daemon.blinking).toBe(true);
    expect(daemon.connected).toBe(false);
  });

  it.each([
    ['normal', 0x01],
    ['fast', 0x02],
    ['robust', 0x03],
  ] as const)('asks for the %s rate with its byte', async (rate, byte) => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    await setBlinking(daemon, await found(daemon), rate);
    expect(daemon.blinking).toBe(true);
    expect(daemon.blinkByte).toBe(byte);
  });

  it('reports an LED the Pi could not drive, rather than sending the user to read it', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID, ledFails: true });
    const error = await setBlinking(daemon, await found(daemon)).catch((caught: unknown) => caught);
    expect(error).toBeInstanceOf(PairingError);
    expect((error as PairingError).kind).toBe('led');
    expect(daemon.connected).toBe(false);
  });

  it('the blinking stops on an accepted authentication', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    await setBlinking(daemon, await found(daemon));
    const session = await pair(daemon, daemon.name, ID);
    expect(daemon.blinking).toBe(false);
    await session.close();
  });

  it('the blinking stops two minutes after the last ask, and on 0x00', async () => {
    const clock = new ManualClock();
    const daemon = new FakeDaemon({ pairingKey: ID, clock });
    const pi = await found(daemon);

    await setBlinking(daemon, pi);
    await clock.advance(119_999);
    expect(daemon.blinking).toBe(true);
    await clock.advance(1);
    expect(daemon.blinking).toBe(false);

    await setBlinking(daemon, pi);
    await setBlinking(daemon, pi, 'stop');
    expect(daemon.blinking).toBe(false);
  });

  it('says so when the Pi cannot be reached', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const pi = await found(daemon);
    daemon.setupMode = false;
    const error = await setBlinking(daemon, pi).catch((caught: unknown) => caught);
    expect(error).toBeInstanceOf(PairingError);
    expect((error as PairingError).kind).toBe('bluetooth');
  });
});
