// The protocol's constants, every one from docs/protocol.md. A change here is a
// change to the document and to daemon/src/protocol/ as well.

/** Protocol version carried in the Info characteristic. 3: Blink and uninstall; 2: SPAKE2. */
export const PROTOCOL_VERSION = 3;
/** The oldest daemon the app pairs with: protocol 2 has no Blink and blinks all through setup mode. */
export const MIN_PROTOCOL_VERSION = 2;
/** The first protocol with the Blink characteristic. */
export const BLINK_PROTOCOL_VERSION = 3;

/** Half-bit period of the LED channel, in milliseconds. A bit is 2T. */
export const T_MS = 50;

/** Sync: on for this many T, then off for this many T. */
export const SYNC_ON_T = 4;
export const SYNC_OFF_T = 4;
/** Idle after the CRC: off for this many T. */
export const IDLE_T = 4;

/** The machine ID is 64 bits: 8 id_bytes, 16 hex characters. */
export const ID_BYTES = 8;
export const ID_HEX_LENGTH = 2 * ID_BYTES;

/** The LED frame's 12-byte message: magic, version, id_bytes, CRC-16. */
export const LED_MAGIC = 0x52;
export const LED_FRAME_VERSION = 0x05;
export const LED_MESSAGE_BYTES = 12;
/** RS(24, 12) over GF(2^8): 12 parity bytes after the message. */
export const LED_PARITY_BYTES = 12;
export const LED_CODEWORD_BYTES = LED_MESSAGE_BYTES + LED_PARITY_BYTES;
/** The primitive polynomial of GF(2^8), and the generator's element a. */
export const GF_PRIMITIVE = 0x11d;
export const GF_GENERATOR = 2;

/** Start bit + the codeword's bits: the cells a decoder reads after the sync. */
export const FRAME_BITS = 1 + 8 * LED_CODEWORD_BYTES;

/** A frame from the first T of the sync to the last T of the idle: 398. */
export const FRAME_T = SYNC_ON_T + SYNC_OFF_T + 2 * FRAME_BITS + IDLE_T;

/** CRC-16/CCITT-FALSE: polynomial 0x1021, init 0xFFFF, no reflection, no final XOR. */
export const CRC16_POLY = 0x1021;
export const CRC16_INIT = 0xffff;

export const SESSION_KEY_INFO = 'session-key v2';
export const KEY_LENGTH = 32;
export const FINGERPRINT_BYTES = 2;

/** SPAKE2's AAD (docs/protocol.md, Authentication). */
export const AUTH_AAD = 'RPiLEDConfig auth v2';
/** The Auth write: pA (65) || cA (32). */
export const AUTH_WRITE_LENGTH = 97;
/** AuthResult on success: 0x01 || cB (32). */
export const AUTH_RESULT_ACCEPTED_LENGTH = 33;
/** Writes are never long, so the Auth write needs an MTU of 97 + 3. */
export const MIN_AUTH_MTU = AUTH_WRITE_LENGTH + 3;

export const BLE_NAME_PREFIX = 'RPiLEDConfig-';
export const SERVICE_UUID = '2a7f0001-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_INFO = '2a7f0002-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_CHALLENGE = '2a7f0003-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_AUTH = '2a7f0004-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_AUTH_RESULT = '2a7f0005-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_REQUEST = '2a7f0006-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_RESPONSE = '2a7f0007-7e1d-4c3a-9b5e-0f8a6d4c2b10';
export const CHAR_BLINK = '2a7f0008-7e1d-4c3a-9b5e-0f8a6d4c2b10';

/** Blink's values: blink the ID on the LED, or stop. */
export const BLINK_START = 0x01;
export const BLINK_STOP = 0x00;
/** The LED stops this long after the last BLINK_START. */
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
