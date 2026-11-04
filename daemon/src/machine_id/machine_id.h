//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "types/status.h"

namespace rpiledconfig::machine_id {

// The 64-bit machine ID (docs/protocol.md, "The machine ID"). Kept as its byte form,
// id_bytes, big-endian, because every derivation starts from that and the LED sends it
// most significant bit first.
inline constexpr std::size_t id_length = 8;

struct MachineId {
  std::array<std::uint8_t, id_length> bytes{};

  // All 64 bits of value.
  static MachineId from_value(std::uint64_t value);
  std::uint64_t value() const;

  // The 8 bytes, big-endian: what the keys and the CRC are computed over.
  types::Bytes id_bytes() const;

  // 16 lowercase hex characters: the file's form and --show-id's.
  std::string to_hex() const;

  // Nothing for anything but 16 hex characters (either case; surrounding whitespace and a
  // trailing newline are forgiven, because that is how the file is written). A 6- or
  // 12-character id from frame version 4 or 3 is refused: it cannot be blinked, and
  // padding it would silently change the secret.
  static std::optional<MachineId> from_hex(const std::string& text);

  bool operator==(const MachineId& other) const { return bytes == other.bytes; }
  bool operator!=(const MachineId& other) const { return !(*this == other); }
};

// The file is <state dir>/machine-id.
inline constexpr const char* file_name = "machine-id";

std::filesystem::path path_in(const std::filesystem::path& state_dir);

// Reads the file. Failure for a file that is missing, unreadable or not an id.
types::Result<MachineId> load(const std::filesystem::path& state_dir);

// Draws a new id from the kernel's random source and writes it, mode 0600, making the
// directory if it is not there. What --regenerate-id does, and the first start.
types::Result<MachineId> generate(const std::filesystem::path& state_dir);

// The daemon's start: the id that is there, or a new one.
types::Result<MachineId> load_or_generate(const std::filesystem::path& state_dir);

// 8 bytes from getrandom(2) on Linux, blocking until the pool is seeded, because an id
// drawn before the kernel has entropy at first boot is the one thing this must never
// be; arc4random_buf elsewhere.
types::Result<MachineId> random_id();

}  // namespace rpiledconfig::machine_id
