import { describe, expect, it } from 'vitest';
import { utf8Decode, utf8Encode } from './bytes';
import { decodeRequest, decodeResponse, encodeRequest, encodeResponse, MessageError } from './messages';

describe('messages', () => {
  it('encodes requests as the document shows them', () => {
    expect(utf8Decode(encodeRequest({ op: 'status' }))).toBe('{"op":"status"}');
    expect(utf8Decode(encodeRequest({ op: 'wifi.scan' }))).toBe('{"op":"wifi.scan"}');
    expect(utf8Decode(encodeRequest({ op: 'wifi.configure', ssid: 'Home', psk: 'secret' }))).toBe('{"op":"wifi.configure","ssid":"Home","psk":"secret"}');
    expect(utf8Decode(encodeRequest({ op: 'wifi.configure', ssid: 'Open' }))).toBe('{"op":"wifi.configure","ssid":"Open"}');
    expect(utf8Decode(encodeRequest({ op: 'wifi.configure', ssid: 'Open', psk: '' }))).toBe('{"op":"wifi.configure","ssid":"Open"}');
    expect(utf8Decode(encodeRequest({ op: 'wifi.configure', ssid: 'Attic', psk: 'secret12', hidden: true }))).toBe(
      '{"op":"wifi.configure","ssid":"Attic","psk":"secret12","hidden":true}',
    );
    expect(utf8Decode(encodeRequest({ op: 'wifi.configure', ssid: 'Attic', hidden: false }))).toBe('{"op":"wifi.configure","ssid":"Attic"}');
    expect(utf8Decode(encodeRequest({ op: 'finish' }))).toBe('{"op":"finish"}');
  });

  it('decodes a status response, with or without a connection', () => {
    const connected = decodeResponse(utf8Encode('{"ok":true,"hostname":"pi","wifi":{"connected":true,"ssid":"Home","ip":"10.0.0.5"}}'), 'status');
    expect(connected).toEqual({ ok: true, hostname: 'pi', wifi: { connected: true, ssid: 'Home', ip: '10.0.0.5' } });
    expect(decodeResponse(utf8Encode('{"ok":true,"wifi":{"connected":false}}'), 'status')).toEqual({ ok: true, wifi: { connected: false } });
    expect(() => decodeResponse(utf8Encode('{"ok":true,"hostname":7,"wifi":{"connected":false}}'), 'status')).toThrow();
    const down = decodeResponse(utf8Encode('{"ok":true,"hostname":"pi","wifi":{"connected":false}}'), 'status');
    expect(down).toEqual({ ok: true, hostname: 'pi', wifi: { connected: false } });
  });

  it('decodes a scan and clamps signal to 0..100', () => {
    const scan = decodeResponse(utf8Encode('{"ok":true,"networks":[{"ssid":"A","signal":120,"secure":true},{"ssid":"B","signal":-3,"secure":false}]}'), 'wifi.scan');
    expect(scan).toEqual({ ok: true, networks: [{ ssid: 'A', signal: 100, secure: true }, { ssid: 'B', signal: 0, secure: false }] });
  });

  it('decodes an error with its code and message', () => {
    const error = decodeResponse(utf8Encode('{"ok":false,"error":"wifi_failed","message":"no such network"}'), 'wifi.configure');
    expect(error).toEqual({ ok: false, error: 'wifi_failed', message: 'no such network' });
    // A newer daemon's unknown code still comes through.
    const unknown = decodeResponse(utf8Encode('{"ok":false,"error":"teapot"}'), 'status');
    expect(unknown).toEqual({ ok: false, error: 'teapot', message: 'teapot' });
  });

  it('refuses shapes that are not the protocol', () => {
    expect(() => decodeResponse(utf8Encode('not json'), 'status')).toThrow(MessageError);
    expect(() => decodeResponse(utf8Encode('[]'), 'status')).toThrow(/ok/);
    expect(() => decodeResponse(utf8Encode('{"ok":true}'), 'status')).toThrow(/status/);
    expect(() => decodeResponse(utf8Encode('{"ok":true,"networks":[{"ssid":1}]}'), 'wifi.scan')).toThrow(/network/);
    expect(() => decodeResponse(utf8Encode('{"ok":false}'), 'status')).toThrow(/error code/);
    expect(() => decodeResponse(new Uint8Array([0xff, 0xfe]), 'status')).toThrow(MessageError);
  });

  it('decodes requests on the daemon side with the protocol error codes', () => {
    expect(decodeRequest(utf8Encode('{"op":"status"}'))).toEqual({ request: { op: 'status' } });
    expect(decodeRequest(utf8Encode('{"op":"wifi.configure","ssid":"Home","psk":"pw"}'))).toEqual({ request: { op: 'wifi.configure', ssid: 'Home', psk: 'pw' } });
    expect(decodeRequest(utf8Encode('{"op":"wifi.configure","ssid":"Home","psk":""}'))).toEqual({ request: { op: 'wifi.configure', ssid: 'Home' } });
    expect(decodeRequest(utf8Encode('{"op":"wifi.configure"}'))).toMatchObject({ error: 'bad_request' });
    expect(decodeRequest(utf8Encode('{"op":"reboot"}'))).toMatchObject({ error: 'unknown_op' });
    expect(decodeRequest(utf8Encode('{}'))).toMatchObject({ error: 'bad_request' });
    expect(decodeRequest(utf8Encode('nope'))).toMatchObject({ error: 'bad_request' });
  });

  it('round-trips a response through the encoder', () => {
    const bytes = encodeResponse({ ok: true, hostname: 'pi', wifi: { connected: false } });
    expect(decodeResponse(bytes, 'status')).toEqual({ ok: true, hostname: 'pi', wifi: { connected: false } });
  });
});
