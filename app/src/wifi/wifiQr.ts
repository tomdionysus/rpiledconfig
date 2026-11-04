// The WiFi QR code that Android's "Share" screen shows for a saved network
// (and that routers print): WIFI:S:<ssid>;T:<WPA|SAE|nopass>;P:<passphrase>;H:<true|false>;;
// Fields come in any order. In a value, a backslash escapes the next
// character, which is how \ ; , : and " are written. The app reads it and
// sends it with wifi.configure (docs/protocol.md, Copying the phone's WiFi).

export interface WifiCredentials {
  ssid: string;
  /** Absent for an open network. */
  psk?: string;
  hidden: boolean;
}

export class WifiQrError extends Error {}

/** Splits on `separator` where it is not escaped, keeping the escapes for the next step. */
function splitUnescaped(text: string, separator: string, limit = Infinity): string[] {
  const parts: string[] = [];
  let current = '';
  for (let i = 0; i < text.length; i += 1) {
    const c = text[i];
    if (c === '\\' && i + 1 < text.length) {
      current += c + text[i + 1];
      i += 1;
    } else if (c === separator && parts.length < limit - 1) {
      parts.push(current);
      current = '';
    } else {
      current += c;
    }
  }
  parts.push(current);
  return parts;
}

function unescape(value: string): string {
  return value.replace(/\\(.)/g, '$1');
}

export function parseWifiQr(text: string): WifiCredentials {
  const trimmed = text.trim();
  if (!/^WIFI:/i.test(trimmed)) throw new WifiQrError('This QR code is not a WiFi network.');
  const fields = new Map<string, string>();
  for (const token of splitUnescaped(trimmed.slice(5), ';')) {
    if (token.length === 0) continue;
    const [key, value] = splitUnescaped(token, ':', 2);
    if (value === undefined) continue;
    fields.set(key.toUpperCase(), unescape(value));
  }
  const ssid = fields.get('S');
  if (!ssid) throw new WifiQrError('This WiFi QR code has no network name.');
  const type = (fields.get('T') ?? 'nopass').toUpperCase();
  const hidden = (fields.get('H') ?? '').toLowerCase() === 'true';
  if (type === 'NOPASS' || type === '') return { ssid, hidden };
  if (type === 'WEP') throw new WifiQrError(`${ssid} uses WEP, which the Pi is not set up to join.`);
  if (type !== 'WPA' && type !== 'WPA2' && type !== 'SAE' && type !== 'WPA3') {
    throw new WifiQrError(`${ssid} uses ${type}, which needs more than a passphrase; type it on the Pi instead.`);
  }
  const psk = fields.get('P');
  if (!psk) throw new WifiQrError(`The QR code for ${ssid} has no passphrase.`);
  return { ssid, psk, hidden };
}
