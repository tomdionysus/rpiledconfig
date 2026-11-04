import { describe, expect, it } from 'vitest';
import { FakeDaemon } from '@/ble/fake';
import { hexToBytes } from '@/protocol/bytes';
import { fingerprint } from '@/protocol/keys';
import { ManualClock } from '@/util/clock';
import { fingerprintOfName, setBlinking, watchForPis, type FoundPi } from './discover';
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

describe('fingerprintOfName', () => {
  it('takes the four hex characters after the prefix and nothing else', () => {
    expect(fingerprintOfName('RPiLEDConfig-55c5')).toBe('55c5');
    expect(fingerprintOfName('RPiLEDConfig-55C5')).toBeUndefined();
    expect(fingerprintOfName('RPiLEDConfig-55c')).toBeUndefined();
    expect(fingerprintOfName('Headphones')).toBeUndefined();
  });
});

describe('watchForPis and setBlinking', () => {
  it('lists the Pi by its fingerprint', async () => {
    const daemon = new FakeDaemon({ machineId: ID });
    const pi = await found(daemon);
    expect(pi.fingerprint).toBe(fingerprint(ID));
    expect(pi.id).toBe(daemon.id);
  });

  it('makes the LED blink, and leaves the link down', async () => {
    const daemon = new FakeDaemon({ machineId: ID });
    expect(daemon.blinking).toBe(false);

    const info = await setBlinking(daemon, await found(daemon));
    expect(info.protocol).toBe(3);
    expect(daemon.blinking).toBe(true);
    expect(daemon.connected).toBe(false);
  });

  it('the blinking stops on an accepted authentication', async () => {
    const daemon = new FakeDaemon({ machineId: ID });
    await setBlinking(daemon, await found(daemon));
    const session = await pair(daemon, ID);
    expect(daemon.blinking).toBe(false);
    await session.close();
  });

  it('the blinking stops two minutes after the last ask, and on 0x00', async () => {
    const clock = new ManualClock();
    const daemon = new FakeDaemon({ machineId: ID, clock });
    const pi = await found(daemon);

    await setBlinking(daemon, pi);
    await clock.advance(119_999);
    expect(daemon.blinking).toBe(true);
    await clock.advance(1);
    expect(daemon.blinking).toBe(false);

    await setBlinking(daemon, pi);
    await setBlinking(daemon, pi, false);
    expect(daemon.blinking).toBe(false);
  });

  it('writes nothing to a protocol 2 daemon, which blinks anyway', async () => {
    const daemon = new FakeDaemon({ machineId: ID, protocol: 2 });
    expect(daemon.blinking).toBe(true);
    const info = await setBlinking(daemon, await found(daemon));
    expect(info.protocol).toBe(2);
  });

  it('says so when the Pi cannot be reached', async () => {
    const daemon = new FakeDaemon({ machineId: ID });
    const pi = await found(daemon);
    daemon.setupMode = false;
    const error = await setBlinking(daemon, pi).catch((caught: unknown) => caught);
    expect(error).toBeInstanceOf(PairingError);
    expect((error as PairingError).kind).toBe('bluetooth');
  });
});
