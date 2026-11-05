// The protocol's constants, every one from docs/protocol.md. A change here is a
// change to the document and to daemon/src/protocol/ as well.

/** Protocol version carried in the Info characteristic. 4: a one-time key; 3: Blink and uninstall; 2: SPAKE2. */
export const PROTOCOL_VERSION = 4;

export const SESSION_KEY_INFO = 'session-key v2';
export const KEY_LENGTH = 32;

/** SPAKE2's AAD (docs/protocol.md, Authentication). */
export const AUTH_AAD = 'RPiLEDConfig auth v2';
/** The Auth write: pA (65) || cA (32). */
export const AUTH_WRITE_LENGTH = 97;
/** AuthResult on success: 0x01 || cB (32). */
export const AUTH_RESULT_ACCEPTED_LENGTH = 33;
/** Writes are never long, so the Auth write needs an MTU of 97 + 3. */
export const MIN_AUTH_MTU = AUTH_WRITE_LENGTH + 3;

export const SERVICE_UUID = '2a7f0001-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_INFO = '2a7f0002-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_CHALLENGE = '2a7f0003-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_AUTH = '2a7f0004-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_AUTH_RESULT = '2a7f0005-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_REQUEST = '2a7f0006-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_RESPONSE = '2a7f0007-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_BLINK = '2a7f0008-7e1d-4c3a-9b5e-0f8a6d4c2b10';

/** Blink's values: blink the key on the LED at a rate (src/led/rates.ts), or stop. */
export const BLINK_NORMAL = 0x01;
export const BLINK_FAST = 0x02;
export const BLINK_ROBUST = 0x03;
export const BLINK_STOP = 0x00;
/** The LED stops this long after the last rate written. */
export const BLINK_TIMEOUT_MS = 120_000;

export const AUTH_ACCEPTED = 0x01;
export const AUTH_REFUSED = 0x00;

/** The 4-byte nonce direction, big-endian. */
export const DIRECTION_APP_TO_DAEMON = 1;
export const DIRECTION_DAEMON_TO_APP = 2;
export const COUNTER_LENGTH = 8;
export const GCM_TAG_LENGTH = 16;
export const GCM_NONCE_LENGTH = 12;

/** Fragment flags: bit 0 marks the last fragment; the rest must be zero. */
export const FLAG_LAST = 0x01;
export const MAX_MESSAGE_LENGTH = 65536;
/** ATT overhead: a write or notification carries MTU - 3 bytes. */
export const ATT_OVERHEAD = 3;
export const MIN_FRAGMENT_SIZE = 20;

/** Limits the daemon enforces, so the app can explain a dropped link. */
export const REFUSALS_PER_CONNECTION = 3;
export const AUTH_TIMEOUT_MS = 30_000;
