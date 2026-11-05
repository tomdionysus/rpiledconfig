// The fake daemon held to docs/protocol.md where the pairing tests do not
// reach: the details a client never sees when it behaves.
import { describe, expect, it } from 'vitest';
import { hexToBytes } from '@/protocol/bytes';
import { appAuth } from '@/protocol/auth';
import { AUTH_TIMEOUT_MS, CHAR_AUTH, CHAR_AUTH_RESULT, CHAR_CHALLENGE, CHAR_INFO, CHAR_REQUEST, CHAR_RESPONSE } from '@/protocol/constants';
import { passwordScalar } from '@/protocol/keys';
import { ManualClock } from '@/util/clock';
import { ADVERTISING_PAUSE_MS, FAKE_IP, FakeDaemon, scanList } from './fake';
import type { Peripheral } from './transport';

const ID = hexToBytes('012345');

function tick(): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, 0));
}

describe('FakeDaemon', () => {
  it('reads AuthResult as empty before any attempt, and refused after a bad one', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const connection = await daemon.connect(daemon.id);
    expect((await connection.read(CHAR_AUTH_RESULT)).length).toBe(0);
    const challenge = await connection.read(CHAR_CHALLENGE);
    await connection.write(CHAR_AUTH, new Uint8Array(97));
    expect(Array.from(await connection.read(CHAR_AUTH_RESULT))).toEqual([0x00]);
    // The challenge is renewed after the refused write.
    expect(Array.from(await connection.read(CHAR_CHALLENGE))).not.toEqual(Array.from(challenge));
    await connection.disconnect();
  });

  it('counts a write that is not 97 bytes as a refusal and drops after three', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const connection = await daemon.connect(daemon.id);
    const dropped = new Promise<Error | undefined>((resolve) => connection.onDisconnected(resolve));
    for (const length of [0, 96, 98]) await connection.write(CHAR_AUTH, new Uint8Array(length));
    expect((await dropped)?.message).toMatch(/refused/);
    expect(daemon.refusalsInARow).toBe(3);
    await expect(connection.read(CHAR_INFO)).rejects.toThrow(/disconnected/);
  });

  it('notifies AuthResult truncated to one notification, so the value must be read', async () => {
    // A link at the default MTU, written to as no real one could be (97 bytes in one
    // write): what is checked is how the notification is cut.
    const daemon = new FakeDaemon({ pairingKey: ID, mtu: 23 });
    const connection = await daemon.connect(daemon.id);
    const notified = new Promise<Uint8Array>((resolve) => connection.monitor(CHAR_AUTH_RESULT, resolve));
    const pB = await connection.read(CHAR_CHALLENGE);
    await connection.write(CHAR_AUTH, appAuth(passwordScalar(ID), pB).write);
    expect((await notified).length).toBe(20);
    expect((await connection.read(CHAR_AUTH_RESULT)).length).toBe(33);
    await connection.disconnect();
  });

  it('starts over when Challenge is read on an authenticated connection', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const connection = await daemon.connect(daemon.id);
    const w = passwordScalar(ID);
    const authenticate = async () => {
      const pB = await connection.read(CHAR_CHALLENGE);
      await connection.write(CHAR_AUTH, appAuth(w, pB).write);
      return connection.read(CHAR_AUTH_RESULT);
    };
    expect((await authenticate())[0]).toBe(0x01);
    // An app starting again on the kept link: the Challenge read forgets S.
    const fresh = await connection.read(CHAR_CHALLENGE);
    expect(fresh.length).toBe(65);
    expect((await connection.read(CHAR_AUTH_RESULT)).length).toBe(0);
    // And it authenticates again on the same link.
    expect((await authenticate())[0]).toBe(0x01);
    expect(daemon.connected).toBe(true);
    // After another start-over, a Request before Auth drops the link.
    await connection.read(CHAR_CHALLENGE);
    const dropped = new Promise<Error | undefined>((resolve) => connection.onDisconnected(resolve));
    connection.monitor(CHAR_RESPONSE, () => {});
    await connection.write(CHAR_REQUEST, new Uint8Array([0x01, 0x00]));
    expect((await dropped)?.message).toMatch(/before authentication/);
  });

  it('drops a connection that has not authenticated within 30 seconds', async () => {
    const clock = new ManualClock();
    const daemon = new FakeDaemon({ pairingKey: ID, clock });
    const connection = await daemon.connect(daemon.id);
    let reason: Error | undefined;
    connection.onDisconnected((error) => (reason = error));
    await clock.advance(AUTH_TIMEOUT_MS - 1);
    expect(daemon.connected).toBe(true);
    await clock.advance(1);
    expect(daemon.connected).toBe(false);
    expect(reason?.message).toMatch(/not authenticated/);
    expect(daemon.drops).toEqual(['not authenticated in time']);
  });

  it('disconnects a second central without a challenge', async () => {
    const daemon = new FakeDaemon({ pairingKey: ID });
    const first = await daemon.connect(daemon.id);
    const second = await daemon.connect(daemon.id);
    await expect(second.read(CHAR_CHALLENGE)).rejects.toThrow(/disconnected/);
    const reason = await new Promise<Error | undefined>((resolve) => second.onDisconnected(resolve));
    expect(reason?.message).toMatch(/busy/);
    // The first is untouched, and once it goes the next one is served.
    expect((await first.read(CHAR_INFO)).length).toBeGreaterThan(0);
    await first.disconnect();
    const third = await daemon.connect(daemon.id);
    expect((await third.read(CHAR_CHALLENGE)).length).toBe(65);
    await third.disconnect();
  });

  it('pauses advertising for a minute after five refusals, then resets the count and is found again', async () => {
    const clock = new ManualClock();
    const daemon = new FakeDaemon({ pairingKey: ID, clock });
    for (let i = 0; i < 5; i += 1) {
      const connection = await daemon.connect(daemon.id);
      await connection.write(CHAR_AUTH, new Uint8Array(97));
      await connection.disconnect();
    }
    expect(daemon.advertising).toBe(false);
    expect(daemon.refusalsInARow).toBe(5);
    await expect(daemon.connect(daemon.id)).rejects.toThrow(/not found/);

    const found: Peripheral[] = [];
    const stop = daemon.scan((peripheral) => found.push(peripheral));
    await clock.advance(ADVERTISING_PAUSE_MS - 1);
    expect(found).toEqual([]);
    await clock.advance(1);
    expect(found.map((p) => p.name)).toEqual([daemon.name]);
    expect(daemon.refusalsInARow).toBe(0);
    stop();
  });

  it('sorts a scan strongest first, ties by name, one per SSID, no hidden, clamped', () => {
    const list = scanList([
      { ssid: 'b', signal: 50, secure: true },
      { ssid: '', signal: 99, secure: true },
      { ssid: 'a', signal: 50, secure: false },
      { ssid: 'c', signal: 140, secure: true },
      { ssid: 'b', signal: 70, secure: true },
      { ssid: 'd', signal: -5, secure: false },
    ]);
    expect(list).toEqual([
      { ssid: 'c', signal: 100, secure: true },
      { ssid: 'b', signal: 70, secure: true },
      { ssid: 'a', signal: 50, secure: false },
      { ssid: 'd', signal: 0, secure: false },
    ]);
  });

  it('joins after a delay, never, or falls back on a wrong passphrase', async () => {
    const clock = new ManualClock();
    const daemon = new FakeDaemon({ pairingKey: ID, clock, joinAfterMs: 8_000, passphrases: { Blackraven: 'right' } });
    expect(daemon.handle({ op: 'wifi.configure', ssid: 'Blackraven', psk: 'right' })).toEqual({ ok: true });
    expect(daemon.wifi).toEqual({ connected: false, join: { ssid: 'Blackraven', state: 'joining' } });
    await clock.advance(8_000);
    expect(daemon.wifi).toEqual({ connected: true, ssid: 'Blackraven', ip: FAKE_IP, join: { ssid: 'Blackraven', state: 'joined' } });

    // From Blackraven to Neighbours 5G with a wrong passphrase: down while
    // trying, then back on Blackraven, as NetworkManager's fallback would.
    daemon.passphrases = { 'Neighbours 5G': 'right' };
    daemon.handle({ op: 'wifi.configure', ssid: 'Neighbours 5G', psk: 'wrong' });
    expect(daemon.wifi.connected).toBe(false);
    await clock.advance(8_000);
    expect(daemon.wifi).toEqual({ connected: true, ssid: 'Blackraven', ip: FAKE_IP, join: { ssid: 'Neighbours 5G', state: 'failed', reason: 'wrong_passphrase', message: 'NetworkManager: no secrets' } });

    daemon.joinAfterMs = 'never';
    daemon.handle({ op: 'wifi.configure', ssid: 'Cafe Guest' });
    await clock.advance(600_000);
    expect(daemon.wifi).toEqual({ connected: false, join: { ssid: 'Cafe Guest', state: 'joining' } });
    await tick();
  });
});
