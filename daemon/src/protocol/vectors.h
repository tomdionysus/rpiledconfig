//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <string>
#include <vector>

#include "pairing_key/pairing_key.h"
#include "types/status.h"

namespace rpiledconfig::protocol::vectors {

// docs/protocol.md, "Test vectors": the fixed inputs, and everything the code derives
// from them. --print-vectors prints these and app/src/protocol/vectors.ts holds the same
// bytes; the two test suites are compared through this.

// The inputs.
inline constexpr std::uint64_t pairing_key_value = 0x0123456789abcdefULL;
types::Bytes x();  // the app's SPAKE2 scalar: 32 bytes, 01..20, big-endian
types::Bytes y();  // the daemon's: 32 bytes, 21..40

// {"op":"status"}, sealed under S with counter 0, app to daemon, then cut into fragments
// of 20 bytes (the smallest write a BLE link carries: an ATT MTU of 23 less 3).
inline constexpr char status_request_plaintext[] = "{\"op\":\"status\"}";
inline constexpr std::size_t fragment_size = 20;

// The CRC-16 check input.
inline constexpr char crc16_check_input[] = "123456789";

struct Vectors {
  pairing_key::PairingKey pairing_key;
  types::Bytes key_bytes;
  std::uint16_t crc16_check = 0;
  types::Bytes led_message;
  types::Bytes led_codeword;
  // The frame's runs in units of T, on positive and off negative: +4,-4,+1,-2,...
  std::string led_frame_runs;
  types::Bytes w;
  types::Bytes p_a;
  types::Bytes p_b;
  types::Bytes k;
  types::Bytes ke;
  types::Bytes c_a;
  types::Bytes c_b;
  types::Bytes session_key;
  types::Bytes status_request_wire;
  std::vector<types::Bytes> status_request_fragments;
};

Vectors compute();

// One "label: value" line each, in a fixed order: hex, the fragments hex separated by
// commas, and the frame runs as signed decimals separated by commas.
std::string print(const Vectors& vectors);

}  // namespace rpiledconfig::protocol::vectors
