//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "led_code.h"

#include "crc16.h"
#include "reed_solomon.h"

namespace rpiledconfig::protocol {

types::Bytes led_message(const pairing_key::PairingKey& id) {
  types::Bytes message = id.key_bytes();

  // The version byte goes into the CRC and not onto the LED: a frame of another version
  // cannot pass, at no cost in time.
  types::Bytes checked{led_frame_version};
  checked.insert(checked.end(), message.begin(), message.end());
  const std::uint16_t crc = crc16(checked);
  message.push_back(static_cast<std::uint8_t>(crc >> 8));
  message.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  return message;
}

types::Bytes led_codeword(const pairing_key::PairingKey& id) {
  auto codeword = led_message(id);
  const auto parity = rs::parity(codeword, led_parity_length);
  codeword.insert(codeword.end(), parity.begin(), parity.end());
  return codeword;
}

}  // namespace rpiledconfig::protocol
