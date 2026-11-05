// What the WiFi screen calls the Pi it is talking to. A Pi can be set not to report its hostname
// (status.report_hostname, docs/configuration.md); the screen then says so, and shows the one-time
// key, the one thing that is certainly this Pi's. Kept apart from the screen so it can be tested.
import type { StatusResponse } from '@/protocol/messages';

export interface PiIdentity {
  title: string;
  notice?: string;
}

export function piIdentity(status: StatusResponse | undefined, keyHex: string | undefined, bleName: string): PiIdentity {
  // Not yet asked: nothing is known about the hostname either way.
  if (!status) return { title: bleName };
  if (status.hostname) return { title: status.hostname };
  return keyHex
    ? { title: keyHex, notice: 'This Pi is set not to report its hostname, so it is shown by its one-time key.' }
    : { title: bleName, notice: 'This Pi is set not to report its hostname, so it is shown by its Bluetooth name.' };
}
