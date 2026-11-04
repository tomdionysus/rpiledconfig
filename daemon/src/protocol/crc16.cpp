//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "crc16.h"

namespace rpiledconfig::protocol {

std::uint16_t crc16(const std::uint8_t* data, std::size_t length) {
  std::uint16_t crc = 0xFFFF;

  for (std::size_t i = 0; i < length; ++i) {
    crc ^= static_cast<std::uint16_t>(data[i] << 8);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021) : static_cast<std::uint16_t>(crc << 1);
    }
  }

  return crc;
}

std::uint16_t crc16(const types::Bytes& data) { return crc16(data.data(), data.size()); }

}  // namespace rpiledconfig::protocol
