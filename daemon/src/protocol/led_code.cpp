//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "led_code.h"

#include "crc16.h"
#include "reed_solomon.h"

namespace rpiledconfig::protocol {

types::Bytes led_message(const machine_id::MachineId& id) {
  types::Bytes message{led_magic, led_frame_version};
  const auto id_bytes = id.id_bytes();
  message.insert(message.end(), id_bytes.begin(), id_bytes.end());

  const std::uint16_t crc = crc16(message);
  message.push_back(static_cast<std::uint8_t>(crc >> 8));
  message.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  return message;
}

types::Bytes led_codeword(const machine_id::MachineId& id) {
  auto codeword = led_message(id);
  const auto parity = rs::parity(codeword, led_parity_length);
  codeword.insert(codeword.end(), parity.begin(), parity.end());
  return codeword;
}

}  // namespace rpiledconfig::protocol
