// The WiFi join from configure to verdict, against the fake daemon in manual
// time: a link that survives, a link that drops after or before the answer,
// a slow join, a wrong passphrase, a Pi that never comes back.
import { describe, expect, it } from 'vitest';
import { FAKE_IP, FakeDaemon, type FakeDaemonOptions } from '@/ble/fake';
import type { BleTransport } from '@/ble/transport';
import { hexToBytes } from '@/protocol/bytes';
import { ManualClock } from '@/util/clock';
import { collapseSteps, describeOutcome, describeStep, joinWifi, JOIN_TIMEOUT_MS, type JoinStep } from './join';
import { pair, type Session } from './session';

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
      'Bluetooth dropped while the Pi switched its radio to WiFi. That is expected; finding it again.',
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
    expect(describeOutcome(outcome, 'Blackraven').body).toMatch(/pair again/);
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
        const stop = daemon.scan(
          (peripheral) => {
            onFound({ ...peripheral, id: lookalikeId });
            if (lookalikeId !== 'moved') onFound(peripheral);
          },
          onError,
        );
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
