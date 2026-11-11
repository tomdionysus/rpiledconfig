// The WiFi join from configure to verdict, against the fake daemon in manual
// time: a link that survives, a link that drops after or before the answer,
// a slow join, a wrong passphrase, a Pi that never comes back.
import { describe, expect, it } from 'vitest';
import { FAKE_IP, FakeDaemon, type FakeDaemonOptions } from '@/ble/fake';
import type { BleTransport } from '@/ble/transport';
import { hexToBytes } from '@/protocol/bytes';
import { ManualClock } from '@/util/clock';
import { MessageError, type StatusResponse } from '@/protocol/messages';
import { collapseSteps, describeOutcome, describeStep, followStatus, isOn, joinWifi, JOIN_TIMEOUT_MS, type JoinOutcome, type JoinStep } from './join';
import { LinkLostError, pair, PairingError, RequestError, type Session } from './session';

const ID = hexToBytes('012345');
const PSK = 'correct horse battery staple';

async function setUp(options: Omit<FakeDaemonOptions, 'pairingKey' | 'clock'> = {}) {
  const clock = new ManualClock();
  const daemon = new FakeDaemon({ pairingKey: ID, clock, passphrases: { Blackraven: PSK }, ...options });
  const session = await clock.run(pair(daemon, daemon.name, ID, { clock }));
  const steps: JoinStep[] = [];
  const join = (ssid: string, psk?: string) => clock.run(joinWifi(session, ssid, psk, { onStep: (step) => steps.push(step) }), 120_000, 1_000);
  return { clock, daemon, session, steps, join };
}

const kinds = (steps: JoinStep[]) => steps.map((step) => step.kind).filter((kind, i, all) => kind !== all[i - 1]);

describe('joinWifi', () => {
  it('joins over a link that survives, without reconnecting', async () => {
    const { daemon, session, steps, join } = await setUp();
    const outcome = await join('Blackraven', PSK);
    expect(outcome.kind).toBe('joined');
    if (outcome.kind !== 'joined') return;
    expect(outcome.session).toBe(session);
    expect(outcome.status.wifi).toEqual({ connected: true, ssid: 'Blackraven', ip: FAKE_IP, join: { ssid: 'Blackraven', state: 'joined' } });
    expect(kinds(steps)).toEqual(['sending', 'saved']);
    expect(daemon.connections).toBe(1);
    expect(describeOutcome(outcome, 'Blackraven').body).toMatch(new RegExp(`as ${FAKE_IP}`));
    await session.close();
  });

  it('does not call a failed join joined because the Pi is on a network of that name by another connection', async () => {
    const { clock, daemon, session } = await setUp();
    daemon.wifi = { connected: true, ssid: 'Blackraven', ip: FAKE_IP, join: { ssid: 'Blackraven', state: 'failed', reason: 'wrong_passphrase' } };
    const outcome = await clock.run(followStatus(session, { ssid: 'Blackraven' }, { clock }), 120_000, 1_000);
    expect(outcome.kind).toBe('not_joined');
    await session.close();
  });

  it('does not call a join joined while the Pi still says it is joining', async () => {
    const { clock, daemon, session } = await setUp();
    daemon.wifi = { connected: true, ssid: 'Blackraven', ip: FAKE_IP, join: { ssid: 'Blackraven', state: 'joining' } };
    const steps: JoinStep[] = [];
    const running = followStatus(session, { ssid: 'Blackraven' }, { clock, onStep: (step) => steps.push(step) });
    clock.after(5_000, () => {
      daemon.wifi = { connected: true, ssid: 'Blackraven', ip: FAKE_IP, join: { ssid: 'Blackraven', state: 'joined' } };
    });
    const outcome = await clock.run(running, 120_000, 1_000);
    expect(outcome.kind).toBe('joined');
    expect(steps.some((step) => step.kind === 'waiting')).toBe(true);
    await session.close();
  });

  it('polls a slow join on a surviving link until it lands', async () => {
    const { clock, steps, join } = await setUp({ joinAfterMs: 9_000 });
    const start = clock.now();
    const outcome = await join('Blackraven', PSK);
    expect(outcome.kind).toBe('joined');
    expect(clock.now() - start).toBeGreaterThanOrEqual(9_000);
    expect(clock.now() - start).toBeLessThan(9_000 + 2_000 + 1_000);
    expect(kinds(steps)).toEqual(['sending', 'saved', 'waiting']);
    const waiting = steps.filter((step) => step.kind === 'waiting');
    expect(waiting.length).toBeGreaterThanOrEqual(4);
    expect(describeStep(waiting[0], 'Blackraven')).toMatch(/Waiting for the Pi to join Blackraven: \d+ s left/);
  });

  it('treats a drop after the answer as expected: reconnects to the same Pi, authenticates again and sees it joined', async () => {
    const { daemon, session, steps, join } = await setUp({ joinAfterMs: 6_000, linkDrop: { when: 'after_response', offAirMs: 4_000 } });
    const outcome = await join('Blackraven', PSK);
    // The screen shows one line per step, the polls and reconnect phases folded.
    expect(collapseSteps(steps).map((step) => describeStep(step, 'Blackraven'))).toEqual([
      'Sending Blackraven to the Pi.',
      'Saved. The Pi is joining Blackraven.',
      'Bluetooth dropped, as it often does while the Pi switches its radio to WiFi. Finding it again.',
      'Reconnecting, attempt 1: authenticated.',
      'Back in touch and authenticated again.',
      expect.stringMatching(/^Waiting for the Pi to join Blackraven: \d+ s left\.$/),
    ]);
    expect(outcome.kind).toBe('joined');
    if (outcome.kind !== 'joined') return;
    expect(outcome.session).not.toBe(session);
    expect(session.isClosed).toBe(true);
    expect(outcome.session.peripheral.id).toBe(session.peripheral.id);
    expect(outcome.status.wifi.ssid).toBe('Blackraven');
    expect(kinds(steps)).toEqual(['sending', 'saved', 'link_lost', 'reconnecting', 'reconnected', 'waiting']);
    expect(steps.find((step) => step.kind === 'link_lost')).toEqual({ kind: 'link_lost', answered: true });
    expect(daemon.connections).toBe(2);
    expect(daemon.refusalsInARow).toBe(0);
    // The new session is a working one.
    expect((await outcome.session.status()).wifi.ip).toBe(FAKE_IP);
    await outcome.session.close();
  });

  it('recovers when the link drops before the answer, and lets status decide', async () => {
    const { steps, join } = await setUp({ linkDrop: { when: 'before_response', offAirMs: 3_000 } });
    const outcome = await join('Blackraven', PSK);
    expect(outcome.kind).toBe('joined');
    expect(kinds(steps)).toEqual(['sending', 'link_lost', 'reconnecting', 'reconnected']);
    expect(steps.find((step) => step.kind === 'link_lost')).toEqual({ kind: 'link_lost', answered: false });
    expect(describeStep({ kind: 'link_lost', answered: false }, 'Blackraven')).toMatch(/before the Pi answered/);
  });

  it('ends a wrong passphrase as soon as the Pi reports it, with the reason', async () => {
    const { clock, steps, join } = await setUp({ joinAfterMs: 20_000, linkDrop: { when: 'after_response', offAirMs: 4_000 } });
    const start = clock.now();
    const outcome = await join('Blackraven', 'wrong passphrase');
    expect(outcome.kind).toBe('not_joined');
    if (outcome.kind !== 'not_joined') return;
    // At the next status after the Pi gave up (20 s), not at the 45 s deadline.
    expect(clock.now() - start).toBeGreaterThanOrEqual(20_000);
    expect(clock.now() - start).toBeLessThan(JOIN_TIMEOUT_MS);
    expect(outcome.status.wifi.connected).toBe(false);
    expect(outcome.reason?.reason).toBe('wrong_passphrase');
    expect(kinds(steps)).toContain('reconnected');
    const verdict = describeOutcome(outcome, 'Blackraven');
    expect(verdict.tone).toBe('error');
    expect(verdict.title).toBe('Wrong passphrase for Blackraven');
    expect(verdict.body).toMatch(/refused the passphrase/);
    // The session is live, so the user can correct the passphrase and try again.
    expect(outcome.session.isClosed).toBe(false);
    const retry = await clock.run(joinWifi(outcome.session, 'Blackraven', PSK), 120_000, 1_000);
    expect(retry.kind).toBe('joined');
  });

  it('reports a name no network has as not found, at once', async () => {
    const { clock, join } = await setUp({ joinAfterMs: 3_000 });
    const start = clock.now();
    const outcome = await join('Blackraven5G', 'any passphrase');
    expect(outcome.kind).toBe('not_joined');
    if (outcome.kind !== 'not_joined') return;
    expect(clock.now() - start).toBeLessThan(10_000);
    expect(outcome.reason?.reason).toBe('not_found');
    const verdict = describeOutcome(outcome, 'Blackraven5G');
    expect(verdict.title).toBe('No network called Blackraven5G');
    expect(verdict.body).toMatch(/match exactly/);
    expect(outcome.session.isClosed).toBe(false);
  });

  it('runs to the deadline only when the Pi never says', async () => {
    const { clock, join } = await setUp({ joinAfterMs: 'never' });
    const start = clock.now();
    const outcome = await join('Blackraven', PSK);
    expect(outcome.kind).toBe('not_joined');
    if (outcome.kind !== 'not_joined') return;
    expect(outcome.reason).toBeUndefined();
    expect(clock.now() - start).toBeGreaterThanOrEqual(JOIN_TIMEOUT_MS);
    expect(describeOutcome(outcome, 'Blackraven').body).toMatch(/still not on Blackraven/);
  });

  it('says so when the Pi falls back to the network it was on', async () => {
    const { join } = await setUp({
      joinAfterMs: 10_000,
      wifi: { connected: true, ssid: 'Neighbours 5G', ip: '10.0.0.7' },
    });
    const outcome = await join('Blackraven', 'wrong passphrase');
    expect(outcome.kind).toBe('not_joined');
    const verdict = describeOutcome(outcome, 'Blackraven');
    expect(verdict.body).toMatch(/back on Neighbours 5G/);
  });

  it('ends a join that never completes at the deadline', async () => {
    const { join } = await setUp({ joinAfterMs: 'never' });
    const outcome = await join('Cafe Guest');
    expect(outcome.kind).toBe('not_joined');
  });

  it('reports a Pi that never comes back on Bluetooth as lost, at the deadline', async () => {
    const { clock, daemon, join } = await setUp({ linkDrop: { when: 'after_response', offAirMs: 10 * 60_000 } });
    const start = clock.now();
    const outcome = await join('Blackraven', PSK);
    expect(outcome.kind).toBe('lost');
    expect(clock.now() - start).toBeGreaterThanOrEqual(JOIN_TIMEOUT_MS);
    expect(clock.now() - start).toBeLessThan(JOIN_TIMEOUT_MS + 2_000);
    expect(daemon.connections).toBe(1);
    expect(describeOutcome(outcome, 'Blackraven').body).toMatch(/Pair again/);
  });

  it('passes a refused configure straight through', async () => {
    const { session, steps, join } = await setUp({ wifiFailure: 'NetworkManager refused: no such device' });
    const outcome = await join('Blackraven', PSK);
    expect(outcome.kind).toBe('refused');
    expect(describeOutcome(outcome, 'Blackraven').body).toMatch(/no such device \(wifi_failed\)/);
    expect(kinds(steps)).toEqual(['sending']);
    expect(session.isClosed).toBe(false);
  });

  it('stops at a refused re-authentication rather than retrying into the Pi limits', async () => {
    const { daemon, join } = await setUp({ linkDrop: { when: 'after_response', offAirMs: 2_000 } });
    // Whatever answers after the drop does not hold the key.
    const goOffAir = daemon.goOffAir.bind(daemon);
    daemon.goOffAir = (ms) => {
      daemon.refuseAll = true;
      goOffAir(ms);
    };
    const outcome = await join('Blackraven', PSK);
    expect(outcome.kind).toBe('lost');
    if (outcome.kind !== 'lost') return;
    expect(outcome.error.message).toMatch(/refused/);
    expect(daemon.refusalsInARow).toBe(1);
  });
});

describe('Session.reconnect', () => {
  /** The fake, seen through a scan that also reports another same-named peripheral first. */
  function withLookalike(daemon: FakeDaemon, lookalikeId: string): BleTransport & { connectedTo: string[] } {
    const connectedTo: string[] = [];
    return {
      connectedTo,
      prepare: () => daemon.prepare(),
      scan(onFound, onError) {
        const stop = daemon.scan((peripheral) => {
          onFound({ ...peripheral, id: lookalikeId });
          if (lookalikeId !== 'moved') onFound(peripheral);
        }, onError);
        return stop;
      },
      connect(id) {
        // Both ids lead to the one fake; what is under test is which is chosen.
        connectedTo.push(id);
        return daemon.connect(daemon.id);
      },
    };
  }

  async function sessionOver(transport: BleTransport, clock: ManualClock): Promise<Session> {
    return clock.run(pair(transport, 'RPiLEDConfig-3a9c', ID, { clock }));
  }

  it('prefers the platform id it had over a same-named peripheral', async () => {
    const clock = new ManualClock();
    const daemon = new FakeDaemon({ pairingKey: ID, clock });
    const transport = withLookalike(daemon, 'lookalike');
    const session = await sessionOver(transport, clock);
    // With no preference the first same-named peripheral is taken.
    expect(transport.connectedTo).toEqual(['lookalike']);
    await session.close();

    const preferring = await clock.run(pair(transport, daemon.name, ID, { clock, preferId: daemon.id }));
    const again = await clock.run(preferring.reconnect());
    expect(transport.connectedTo).toEqual(['lookalike', daemon.id, daemon.id]);
    expect(again.peripheral.id).toBe(daemon.id);
    await again.close();
  });

  it('takes the same name at another id when the old id does not show up', async () => {
    const clock = new ManualClock();
    const daemon = new FakeDaemon({ pairingKey: ID, clock });
    const transport = withLookalike(daemon, 'moved');
    const session = await clock.run(pair(transport, daemon.name, ID, { clock, preferId: daemon.id }));
    expect(session.peripheral.id).toBe('moved');
    const again = await clock.run(session.reconnect({ onPhase: () => {} }));
    expect(again.peripheral.id).toBe('moved');
    expect(transport.connectedTo).toEqual(['moved', 'moved']);
    await again.close();
  });
});

/** A session whose status answers are the test's, for what the fake daemon does not do on its own. */
function scripted(clock: ManualClock, status: () => Promise<StatusResponse>, closedBy?: LinkLostError) {
  let closed = closedBy !== undefined;
  const session = {
    clock,
    closedBy,
    get isClosed() {
      return closed;
    },
    status,
    async close() {
      closed = true;
    },
    reconnect: () => Promise.reject(new PairingError('not_found', 'The Pi did not come back.')),
  };
  return session as unknown as Session;
}

const ON_BLACKRAVEN: StatusResponse = { ok: true, wifi: { connected: true, ssid: 'Blackraven', ip: FAKE_IP } };

describe('followStatus, edge by edge', () => {
  it('does not retry past an answer that did not verify', async () => {
    const clock = new ManualClock();
    const unverified = new LinkLostError("The Pi's answer could not be verified, so the link was dropped.", 'tag', true);
    const outcome = await clock.run(
      followStatus(
        scripted(clock, () => Promise.resolve(ON_BLACKRAVEN), unverified),
        { ssid: 'Blackraven' },
      ),
    );
    expect(outcome).toEqual({ kind: 'lost', error: unverified, lastStatus: undefined });
  });

  it('asks again while the Pi says its WiFi is busy', async () => {
    const clock = new ManualClock();
    let asked = 0;
    const session = scripted(clock, async () => {
      asked += 1;
      if (asked < 3) throw new RequestError('wifi_unavailable', 'NetworkManager is not answering');
      return ON_BLACKRAVEN;
    });
    const outcome = await clock.run(followStatus(session, { ssid: 'Blackraven' }), 60_000, 500);
    expect(outcome.kind).toBe('joined');
    expect(asked).toBe(3);
    expect(session.isClosed).toBe(false);
  });

  it('closes the session on an answer that is not one, and reports the Pi lost when it does not come back', async () => {
    const clock = new ManualClock();
    const session = scripted(clock, () => Promise.reject(new MessageError('scan response has no networks')));
    const outcome = await clock.run(followStatus(session, { ssid: 'Blackraven' }), 60_000, 500);
    expect(session.isClosed).toBe(true);
    expect(outcome.kind).toBe('lost');
  });

  it('counts the Pi on the network at the deadline when it never said the join failed', async () => {
    const clock = new ManualClock();
    const joining: StatusResponse = { ok: true, wifi: { ...ON_BLACKRAVEN.wifi, join: { ssid: 'Blackraven', state: 'joining' } } };
    const outcome = await clock.run(
      followStatus(
        scripted(clock, async () => joining),
        { ssid: 'Blackraven' },
        { timeoutMs: 5_000 },
      ),
      60_000,
      500,
    );
    expect(outcome.kind).toBe('joined');
  });

  it('reports the Pi lost at the deadline when it never answered', async () => {
    const clock = new ManualClock();
    const busy = scripted(clock, () => Promise.reject(new RequestError('wifi_unavailable', 'busy')));
    const outcome = await clock.run(followStatus(busy, { ssid: 'Blackraven' }, { timeoutMs: 5_000 }), 60_000, 500);
    expect(outcome.kind).toBe('lost');
    if (outcome.kind === 'lost') expect(outcome.error.message).toBe('The Pi did not answer within 5 seconds.');
  });

  it('with no SSID asked for, counts any network the Pi was not on before', () => {
    const before = { connected: true, ssid: 'Neighbours 5G' };
    expect(isOn(ON_BLACKRAVEN, { before })).toBe(true);
    expect(isOn({ ok: true, wifi: { connected: true, ssid: 'Neighbours 5G' } }, { before })).toBe(false);
    expect(isOn(ON_BLACKRAVEN, {})).toBe(true);
    expect(isOn({ ok: true, wifi: { connected: false } }, {})).toBe(false);
  });
});

describe('what the screen says', () => {
  it('counts the wait down between polls', () => {
    const waiting: JoinStep = { kind: 'waiting', wifi: { connected: false }, elapsedMs: 5_000, remainingMs: 40_000, deadline: 45_000 };
    expect(describeStep(waiting, 'Blackraven')).toBe('Waiting for the Pi to join Blackraven: 40 s left.');
    expect(describeStep(waiting, 'Blackraven', 30_500)).toBe('Waiting for the Pi to join Blackraven: 15 s left.');
    expect(describeStep(waiting, 'Blackraven', 50_000)).toBe('Waiting for the Pi to join Blackraven: 0 s left.');
    expect(describeStep({ kind: 'reconnecting', attempt: 2, phase: 'scanning' }, 'Blackraven')).toBe('Reconnecting, attempt 2: looking for the Pi.');
  });

  it('names the network the Pi is on, with its address when it has one', () => {
    const session = {} as Session;
    expect(describeOutcome({ kind: 'joined', session, status: ON_BLACKRAVEN }, 'Blackraven').body).toBe(
      `The Pi is on Blackraven as ${FAKE_IP}. Finish to take it out of setup mode.`,
    );
    expect(describeOutcome({ kind: 'joined', session, status: { ok: true, wifi: { connected: true } } }, 'Blackraven')).toEqual({
      tone: 'ok',
      title: 'Joined Blackraven',
      body: 'The Pi is on Blackraven. Finish to take it out of setup mode.',
    });
  });

  it("gives the Pi's own words for a failure it could not name", () => {
    const session = {} as Session;
    const failed = (message?: string): JoinOutcome => ({
      kind: 'not_joined',
      session,
      status: { ok: true, wifi: { connected: true, ssid: 'Neighbours 5G' } },
      elapsedMs: 12_400,
      reason: { ssid: 'Blackraven', state: 'failed', reason: 'other', ...(message ? { message } : {}) },
    });
    expect(describeOutcome(failed('IP configuration could not be reserved'), 'Blackraven').body).toBe(
      'The Pi gave up after 12 seconds: IP configuration could not be reserved. The Pi is back on Neighbours 5G.',
    );
    expect(describeOutcome(failed(), 'Blackraven').body).toBe('The Pi gave up after 12 seconds: no reason given. The Pi is back on Neighbours 5G.');
  });
});
