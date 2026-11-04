import { describe, expect, it } from 'vitest';
import { bytesToHex, hexToBytes, utf8Decode, utf8Encode } from './bytes';
import { appChannel, ChannelError, daemonChannel, nonce, Opener, Sealer } from './channel';
import { DIRECTION_APP_TO_DAEMON, DIRECTION_DAEMON_TO_APP } from './constants';

const KEY = hexToBytes('1cb3100675b3259832f74f6170b48f7340628a4076c585ed1df209ba455c1cab');

describe('channel', () => {
  it('builds the nonce as direction || counter, big-endian', () => {
    expect(bytesToHex(nonce(DIRECTION_APP_TO_DAEMON, 0))).toBe('000000010000000000000000');
    expect(bytesToHex(nonce(DIRECTION_DAEMON_TO_APP, 258))).toBe('000000020000000000000102');
  });

  it('numbers messages from 0 and the other side opens them in order', () => {
    const app = appChannel(KEY);
    const daemon = daemonChannel(KEY);
    const first = app.sealer.seal(utf8Encode('one'));
    const second = app.sealer.seal(utf8Encode('two'));
    expect(bytesToHex(first.subarray(0, 8))).toBe('0000000000000000');
    expect(bytesToHex(second.subarray(0, 8))).toBe('0000000000000001');
    expect(utf8Decode(daemon.opener.open(first))).toBe('one');
    expect(utf8Decode(daemon.opener.open(second))).toBe('two');
    // And back the other way, with its own counter.
    const reply = daemon.sealer.seal(utf8Encode('ok'));
    expect(bytesToHex(reply.subarray(0, 8))).toBe('0000000000000000');
    expect(utf8Decode(app.opener.open(reply))).toBe('ok');
  });

  it('refuses a replay and anything older', () => {
    const app = appChannel(KEY);
    const daemon = daemonChannel(KEY);
    const first = app.sealer.seal(utf8Encode('one'));
    const second = app.sealer.seal(utf8Encode('two'));
    daemon.opener.open(second);
    expect(() => daemon.opener.open(second)).toThrow(ChannelError);
    expect(() => daemon.opener.open(first)).toThrow(/already seen/);
    // Later ones still pass; a gap is not a replay.
    const fourth = (app.sealer.seal(utf8Encode('three')), app.sealer.seal(utf8Encode('four')));
    expect(utf8Decode(daemon.opener.open(fourth))).toBe('four');
  });

  it('refuses a message sealed for the other direction', () => {
    const wire = new Sealer(KEY, DIRECTION_DAEMON_TO_APP).seal(utf8Encode('backwards'));
    const daemonSide = new Opener(KEY, DIRECTION_APP_TO_DAEMON);
    let caught: unknown;
    try {
      daemonSide.open(wire);
    } catch (error) {
      caught = error;
    }
    expect(caught).toBeInstanceOf(ChannelError);
    expect((caught as ChannelError).kind).toBe('auth');
  });

  it('refuses a tampered byte and a short message', () => {
    const app = appChannel(KEY);
    const daemon = daemonChannel(KEY);
    const wire = app.sealer.seal(utf8Encode('payload'));
    wire[12] ^= 0x01;
    expect(() => daemon.opener.open(wire)).toThrow(/authentication/);
    expect(() => daemon.opener.open(wire.subarray(0, 20))).toThrow(/short/);
    // A failed message does not advance the counter.
    expect(utf8Decode(daemon.opener.open(app.sealer.seal(utf8Encode('next'))))).toBe('next');
  });

  it('wants a 32-byte key', () => {
    expect(() => new Sealer(new Uint8Array(16), DIRECTION_APP_TO_DAEMON)).toThrow();
  });
});
