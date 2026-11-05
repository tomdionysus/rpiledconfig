//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstddef>
#include <cstdint>

#include "types/status.h"

namespace rpiledconfig::protocol {

// CRC-16/CCITT-FALSE as docs/protocol.md has it for the LED frame: polynomial 0x1021,
// initial value 0xFFFF, no reflection, no final XOR. The check value for the ASCII bytes
// of "123456789" is 0x29B1.
std::uint16_t crc16(const std::uint8_t* data, std::size_t length);
std::uint16_t crc16(const types::Bytes& data);

}  // namespace rpiledconfig::protocol
