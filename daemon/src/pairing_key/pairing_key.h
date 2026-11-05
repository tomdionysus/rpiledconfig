//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "types/status.h"

namespace rpiledconfig::pairing_key {

// The one-time pairing key (docs/protocol.md, "The pairing key"). Kept as its byte form,
// key_bytes, big-endian, because every derivation starts from that and the LED sends it
// most significant bit first. In memory only: there is no file, and no way to print one.
inline constexpr std::size_t key_length = 8;

struct PairingKey {
  std::array<std::uint8_t, key_length> bytes{};

  // All 64 bits of value.
  static PairingKey from_value(std::uint64_t value);
  std::uint64_t value() const;

  // The 8 bytes, big-endian: what the keys and the CRC are computed over.
  types::Bytes key_bytes() const;

  // 16 lowercase hex characters, for the test vectors. Never for the log: the key is the
  // whole pairing secret, and the journal is readable by more than root.
  std::string to_hex() const;

  bool operator==(const PairingKey& other) const { return bytes == other.bytes; }
  bool operator!=(const PairingKey& other) const { return !(*this == other); }
};

// From getrandom(2) on Linux, blocking until the pool is seeded, because a key drawn before
// the kernel has entropy at first boot is the one thing this must never be; arc4random_buf
// elsewhere.
types::Status random_bytes(std::uint8_t* out, std::size_t length);

// A new key: 64 new random bits.
types::Result<PairingKey> draw();

}  // namespace rpiledconfig::pairing_key
