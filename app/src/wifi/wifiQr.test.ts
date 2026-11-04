import { describe, expect, it } from 'vitest';
import { parseWifiQr, WifiQrError } from './wifiQr';

describe('parseWifiQr', () => {
  it("reads Android's Share code", () => {
    expect(parseWifiQr('WIFI:S:Blackraven;T:WPA;P:correct horse;H:false;;')).toEqual({ ssid: 'Blackraven', psk: 'correct horse', hidden: false });
  });

  it('takes the fields in any order, and an open network without a passphrase', () => {
    expect(parseWifiQr('WIFI:T:nopass;S:Cafe Guest;;')).toEqual({ ssid: 'Cafe Guest', hidden: false });
    expect(parseWifiQr('wifi:P:pw12345678;H:true;S:Hidden one;T:SAE;;')).toEqual({ ssid: 'Hidden one', psk: 'pw12345678', hidden: true });
  });

  it('undoes the escapes for \\ ; , : and "', () => {
    const code = 'WIFI:S:a\\;b\\,c\\:d;T:WPA;P:p\\\\q\\"r\\;s;;';
    expect(parseWifiQr(code)).toEqual({ ssid: 'a;b,c:d', psk: 'p\\q"r;s', hidden: false });
  });

  it('keeps an unescaped colon inside a value', () => {
    expect(parseWifiQr('WIFI:S:x;T:WPA;P:a:b:c;;').psk).toBe('a:b:c');
  });

  it('refuses what it cannot send', () => {
    expect(() => parseWifiQr('https://example.com')).toThrow(WifiQrError);
    expect(() => parseWifiQr('WIFI:T:WPA;P:x;;')).toThrow(/no network name/);
    expect(() => parseWifiQr('WIFI:S:Old;T:WEP;P:abcde;;')).toThrow(/WEP/);
    expect(() => parseWifiQr('WIFI:S:Corp;T:WPA2-EAP;E:PEAP;;')).toThrow(/more than a passphrase/);
    expect(() => parseWifiQr('WIFI:S:x;T:WPA;;')).toThrow(/no passphrase/);
  });
});
