// Copying the phone's WiFi by Easy Connect, against the fake daemon, with
// Android's configurator played by the test.
import { describe, expect, it } from 'vitest';
import { FAKE_DPP_URI, FAKE_IP, FakeDaemon, type FakeDaemonOptions } from '@/ble/fake';
import { hexToBytes } from '@/protocol/bytes';
import { ManualClock } from '@/util/clock';
import { copyPhoneWifi, describeCopyStep, type CopyStep, type EasyConnectLaunch } from './copyWifi';
import { describeOutcome } from './join';
import { pair } from './session';

const ID = hexToBytes('012345');

async function setUp(options: Omit<FakeDaemonOptions, 'pairingKey' | 'clock'> = {}) {
  const clock = new ManualClock();
  const daemon = new FakeDaemon({ pairingKey: ID, clock, ...options });
  const session = await clock.run(pair(daemon, daemon.name, ID, { clock }));
  const steps: CopyStep[] = [];
  const copy = (launch: EasyConnectLaunch) => clock.run(copyPhoneWifi(session, launch, { onStep: (step) => steps.push(step) }), 120_000, 1_000);
  return { clock, daemon, session, steps, copy };
}

describe('copyPhoneWifi', () => {
  it('hands the URI to Android, which sends the network; status then shows the Pi on it', async () => {
    const { daemon, steps, copy } = await setUp({ joinAfterMs: 5_000 });
    const uris: string[] = [];
    const outcome = await copy(async (uri) => {
      uris.push(uri);
      expect(daemon.easyConnectListening).toBe(true);
      expect(daemon.easyConnectDeliver('Blackraven', 'phone-secret')).toBe(true);
      return 'done';
    });
    expect(uris).toEqual([FAKE_DPP_URI]);
    expect(outcome.kind).toBe('joined');
    if (outcome.kind !== 'joined') return;
    expect(outcome.status.wifi).toEqual({ connected: true, ssid: 'Blackraven', ip: FAKE_IP, join: { ssid: 'Blackraven', state: 'joined' } });
    expect(steps.map((step) => step.kind).slice(0, 2)).toEqual(['asking', 'android']);
    expect(describeCopyStep(steps[1], 'Blackraven')).toMatch(/Android's own screen/);
    expect(describeOutcome(outcome, 'the network this phone shared').title).toBe('Joined Blackraven');
    expect(daemon.easyConnectListening).toBe(false);
  });

  it('gets back to the Pi when the join drops the link', async () => {
    const { daemon, session, copy } = await setUp({ joinAfterMs: 6_000, linkDrop: { when: 'after_response', offAirMs: 3_000 } });
    const outcome = await copy(async () => {
      daemon.easyConnectDeliver('Blackraven', 'phone-secret');
      return 'done';
    });
    expect(outcome.kind).toBe('joined');
    if (outcome.kind === 'joined') expect(outcome.session).not.toBe(session);
  });

  it('stops the Pi listening when the user backs out of Android', async () => {
    const { daemon, copy } = await setUp();
    const outcome = await copy(async () => 'cancelled');
    expect(outcome.kind).toBe('cancelled');
    expect(daemon.easyConnectListening).toBe(false);
    expect(daemon.wifi.connected).toBe(false);
  });

  it('counts a network Android sent before the user backed out', async () => {
    const { daemon, copy } = await setUp();
    const outcome = await copy(async () => {
      daemon.easyConnectDeliver('Cafe Guest');
      return 'cancelled';
    });
    expect(outcome.kind).toBe('joined');
  });

  it('offers the other ways when the Pi cannot do Easy Connect', async () => {
    const { copy } = await setUp({ easyConnect: 'unavailable' });
    let launched = false;
    const outcome = await copy(async () => {
      launched = true;
      return 'done';
    });
    expect(outcome.kind).toBe('unavailable');
    expect(launched).toBe(false);
  });

  it('offers the other ways when this phone has no Easy Connect screen', async () => {
    const { daemon, copy } = await setUp();
    const outcome = await copy(async () => {
      throw new Error('No Activity found to handle Intent');
    });
    expect(outcome.kind).toBe('unavailable');
    expect(daemon.easyConnectListening).toBe(false);
  });

  it('does not count the network the Pi was already on', async () => {
    const { copy } = await setUp({ wifi: { connected: true, ssid: 'Neighbours 5G', ip: '10.0.0.7' } });
    // Android sends nothing; the Pi stays on its old network.
    const outcome = await copy(async () => 'done');
    expect(outcome.kind).toBe('not_joined');
  });

  it('stops listening when the connection that asked closes, and after 120 seconds', async () => {
    const { clock, daemon, session } = await setUp();
    await clock.run(session.easyConnect());
    expect(daemon.easyConnectListening).toBe(true);
    await clock.advance(120_000);
    expect(daemon.easyConnectListening).toBe(false);
    await clock.run(session.easyConnect());
    await session.close();
    expect(daemon.easyConnectListening).toBe(false);
  });
});
