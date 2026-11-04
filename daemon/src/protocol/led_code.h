//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstddef>
#include <cstdint>

#include "machine_id/machine_id.h"
#include "types/status.h"

namespace rpiledconfig::protocol {

// What the LED blinks (docs/protocol.md, "The codeword"): frame version 5, a 12-byte
// message of magic, frame version, id_bytes and a CRC-16 over those ten, followed by 12
// Reed-Solomon parity bytes. The daemon only encodes; reading it back is the app's.

inline constexpr std::uint8_t led_magic = 0x52;
inline constexpr std::uint8_t led_frame_version = 0x05;
inline constexpr std::size_t led_message_length = 12;
inline constexpr std::size_t led_parity_length = 12;
inline constexpr std::size_t led_codeword_length = led_message_length + led_parity_length;

types::Bytes led_message(const machine_id::MachineId& id);
types::Bytes led_codeword(const machine_id::MachineId& id);

}  // namespace rpiledconfig::protocol
