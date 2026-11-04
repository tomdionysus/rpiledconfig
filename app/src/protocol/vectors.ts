// The protocol's fixed test vectors (docs/protocol.md, Test vectors), computed
// once with this code and held here as hex. The daemon's tests print the
// same bytes with --print-vectors; vectors.test.ts checks the code still
// reproduces these, so a change that breaks compatibility fails here.

export const MACHINE_ID = '0123456789abcdef';
/** x, the app's SPAKE2 scalar: 32 bytes 01..20. */
export const X = '0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20';
/** y, the daemon's: 32 bytes 21..40. */
export const Y = '2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40';

const FRAME_RUNS =
  '+4,-4,+1,-2,+2,-2,+2,-2,+1,-1,+2,-2,+1,-1,+1,-1,+1,-1,+1,-1,+1,-1,+2,-2,+2,-2,+1,-1,+1,-1,+1,-1,+1,-1,+1,-1,+1,-1,+2,-2,+1,-1,+2,-2,+1,-1,+1,-1,+2,-1,+1,-2,+2,-2,+1,-1,+1,-1,+2,-2,+2,-2,+2,-1,+1,-2,+1,-1,+2,-1,+1,-1,+1,-1,+1,-2,+1,-1,+1,-1,+2,-2,+1,-1,+2,-1,+1,-2,+2,-2,+2,-2,+2,-1,+1,-1,+1,-1,+1,-2,+1,-1,+2,-1,+1,-2,+2,-1,+1,-1,+1,-1,+1,-2,+2,-1,+1,-1,+1,-1,+1,-2,+1,-1,+1,-1,+1,-1,+1,-1,+2,-1,+1,-1,+1,-1,+1,-2,+2,-2,+1,-1,+2,-1,+1,-2,+1,-1,+1,-1,+2,-2,+2,-1,+1,-2,+2,-1,+1,-2,+2,-1,+1,-1,+1,-1,+1,-1,+1,-2,+2,-1,+1,-2,+1,-1,+2,-1,+1,-1,+1,-1,+1,-1,+1,-1,+1,-2,+2,-1,+1,-1,+1,-1,+1,-1,+1,-1,+1,-2,+1,-1,+1,-1,+1,-1,+1,-1,+2,-1,+1,-1,+1,-2,+1,-1,+2,-2,+1,-1,+1,-1,+2,-2,+2,-2,+1,-1,+2,-1,+1,-2,+2,-1,+1,-1,+1,-2,+2,-1,+1,-2,+2,-1,+1,-1,+1,-2,+2,-1,+1,-1,+1,-1,+1,-2,+2,-2,+1,-1,+2,-2,+1,-1,+2,-2,+1,-1,+1,-1,+2,-1,+1,-1,+1,-2,+1,-1,+2,-2,+1,-1,+1,-1,+1,-1,+2,-1,+1,-2,+2,-2,+1,-4';

export const VECTORS = {
  machine_id: MACHINE_ID,
  id_bytes: '0123456789abcdef',
  /** CRC-16/CCITT-FALSE of ASCII 123456789. */
  crc16_check: '29b1',
  /** The LED frame's message: magic 0x52, version 0x05, id_bytes, CRC-16 (docs/protocol.md, The codeword). */
  led_message: '52050123456789abcdef07a6',
  /** The message and its RS(24, 12) parity. */
  led_codeword: '52050123456789abcdef07a62dbecfdf83914ddbbd24721a',
  fingerprint: '55c5',
  // SPAKE2 for x and y above, computed in Python (docs/protocol.md) after it
  // reproduced RFC 9382's vectors.
  w: 'd11cf43f75a28b8c30fd3da84ab44c51c0c8755713eed6dcd9b73907be6af3f7',
  pA: '040dd445d16102c3424f2451006a7aa6e8bfd8a4ccb182905cccf632c27e995012d5f4a0911c67271eb2016e8edc631a1f3633f36ea0f8d67773d127d38697f5a2',
  pB: '0489104b39198d13c02718251d7dc12cee4dc5629b47875240c477cc04600088bcd7cba55f04f2be712ff8b2f971bda93e9323ef61d0e787f89a167b72c8dfd71d',
  K: '044fe243908f378aa1c2a69538822e6ed908c3225d8692575507c649901245150aaa8513bcd739d8100f64e283d32db26502af6ea9bcaa6bb5b58a6bf20f3228b4',
  Ke: 'cf4d7f005d568e9b7966f25dbf973474',
  cA: 'a716256c3ba4e991e180bb91ed619b2c12fef2882ab978a71609b4321bef47cf',
  cB: 'e0bf8b08cb4f684c6ee7ca0f1a82861ed41fbf3ce5babb07b346c048ebcdd551',
  S: '85de6febab32c56e9d93d31f4b364360487cba16d7fde011c8708bb95b45f60a',
  /** counter 0 || AES-256-GCM(S, nonce 00000001||counter, `{"op":"status"}`) || tag */
  status_request_wire: '00000000000000007db06bfa7ade4d2aa92177f951835e7d18014a51a3a551ba7e37ac5ea5d55d',
  /** The wire above in 20-byte fragments: a flags byte and 19 bytes of slice each. */
  status_request_fragments_mtu20: [
    '0000000000000000007db06bfa7ade4d2aa92177',
    '00f951835e7d18014a51a3a551ba7e37ac5ea5d5',
    '015d',
  ],
  /** The frame as physical runs in units of T, + on and - off, sync first: 398 T in 294 runs. */
  frame_runs: FRAME_RUNS,
};
