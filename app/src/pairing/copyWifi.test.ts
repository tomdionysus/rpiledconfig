// Copying the phone's WiFi by Easy Connect, against the fake daemon, with
// Android's configurator played by the test.
import { describe, expect, it } from 'vitest';
import { FAKE_DPP_URI, FAKE_IP, FakeDaemon, type FakeDaemonOptions } from '@/ble/fake';
import { hexToBytes } from '@/protocol/bytes';
import { ManualClock } from '@/util/clock';
import { copyPhoneWifi, describeCopyOutcome, describeCopyStep, type CopyStep, type EasyConnectLaunch } from './copyWifi';
import { describeOutcome } from './join';
import { LinkLostError, pair, RequestError, type Session } from './session';

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

/** A session that answers as the test says, for the ways a real one fails. */
function scripted(overrides: Partial<Record<'status' | 'easyConnect' | 'easyConnectStop', () => Promise<unknown>>>, closed = false) {
  const clock = new ManualClock();
  const calls: string[] = [];
  const session = {
    clock,
    isClosed: closed,
    status: overrides.status ?? (async () => ({ ok: true, wifi: { connected: false } })),
    easyConnect: overrides.easyConnect ?? (async () => FAKE_DPP_URI),
    async easyConnectStop() {
      calls.push('stop');
      return overrides.easyConnectStop?.();
    },
  } as unknown as Session;
  return { session, calls, clock };
}

describe('copyPhoneWifi, when things go wrong', () => {
  it('ends at once when the link is gone before it starts', async () => {
    const lost = new LinkLostError('The Bluetooth link to the Pi was lost.');
    const { session } = scripted({ status: () => Promise.reject(lost) });
    const outcome = await copyPhoneWifi(session, async () => 'done');
    expect(outcome).toEqual({ kind: 'lost', error: lost });
  });

  it('goes on without knowing the network before when the Pi will not say', async () => {
    const { session } = scripted({
      status: () => Promise.reject(new RequestError('wifi_unavailable', 'busy')),
      easyConnect: () => Promise.reject(new RequestError('easyconnect_unavailable', 'wpa_supplicant has no DPP')),
    });
    const outcome = await copyPhoneWifi(session, async () => 'done');
    expect(outcome).toEqual({ kind: 'unavailable', session, reason: 'wpa_supplicant has no DPP' });
  });

  it('offers the other ways to a daemon that does not know Easy Connect', async () => {
    const { session } = scripted({ easyConnect: () => Promise.reject(new RequestError('unknown_op', 'unknown op')) });
    expect((await copyPhoneWifi(session, async () => 'done')).kind).toBe('unavailable');
  });

  it('passes on any other refusal', async () => {
    const busy = new RequestError('busy', 'Another phone is configuring this Pi.');
    const { session } = scripted({ easyConnect: () => Promise.reject(busy) });
    expect(await copyPhoneWifi(session, async () => 'done')).toEqual({ kind: 'refused', session, error: busy });
  });

  it('calls a failure to ask for the code a lost link', async () => {
    const { session } = scripted({ easyConnect: () => Promise.reject(new LinkLostError('The Pi stopped answering over Bluetooth.')) });
    const outcome = await copyPhoneWifi(session, async () => 'done');
    expect(outcome.kind).toBe('lost');
    if (outcome.kind === 'lost') expect(outcome.error.message).toBe('The Pi stopped answering over Bluetooth.');

    // Native code can reject with a string; it still ends as a lost link with that text.
    // eslint-disable-next-line @typescript-eslint/prefer-promise-reject-errors
    const odd = scripted({ easyConnect: () => Promise.reject('a string') });
    const second = await copyPhoneWifi(odd.session, async () => 'done');
    if (second.kind === 'lost') expect(second.error.message).toBe('a string');
    expect(second.kind).toBe('lost');
  });

  it('says nothing was sent when the user backs out and the Pi cannot be asked', async () => {
    let asked = 0;
    const { session, calls } = scripted({
      status: async () => {
        asked += 1;
        if (asked > 1) throw new LinkLostError('The Bluetooth link to the Pi was lost.');
        return { ok: true, wifi: { connected: false } };
      },
      easyConnectStop: () => Promise.reject(new Error('write failed')),
    });
    expect(await copyPhoneWifi(session, async () => 'cancelled')).toEqual({ kind: 'cancelled', session });
    expect(calls).toEqual(['stop']);
  });

  it('asks nothing more of a session already closed', async () => {
    const { session, calls } = scripted({}, true);
    expect(await copyPhoneWifi(session, async () => 'cancelled')).toEqual({ kind: 'cancelled', session });
    expect(calls).toEqual([]);
  });

  it('still counts the network the Pi was on as not new when the user backs out', async () => {
    const on = { ok: true, wifi: { connected: true, ssid: 'Neighbours 5G', ip: '10.0.0.7' } };
    const { session } = scripted({ status: async () => on });
    expect((await copyPhoneWifi(session, async () => 'cancelled')).kind).toBe('cancelled');
  });
});

describe('what the screen says while copying', () => {
  const session = {} as Session;

  it('names each step', () => {
    expect(describeCopyStep({ kind: 'asking' }, 'Blackraven')).toBe('Asking the Pi for its Easy Connect code.');
    expect(describeCopyStep({ kind: 'sending' }, 'Blackraven')).toBe('Sending Blackraven to the Pi.');
  });

  it('offers typing when Easy Connect is not there, and again after a cancel', () => {
    expect(describeCopyOutcome({ kind: 'unavailable', session, reason: 'This phone cannot send its WiFi by Easy Connect.' }, 'x')).toEqual({
      tone: 'error',
      title: 'Easy Connect is not available',
      body: 'This phone cannot send its WiFi by Easy Connect. Type the network instead.',
    });
    expect(describeCopyOutcome({ kind: 'cancelled', session }, 'x').title).toBe('Nothing was sent');
    expect(describeCopyOutcome({ kind: 'lost', error: new Error('gone.') }, 'Blackraven').title).toBe('Lost touch with the Pi');
  });
});
