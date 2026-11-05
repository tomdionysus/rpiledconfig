//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "pairing_key/pairing_key.h"
#include "types/status.h"

namespace rpiledconfig::protocol {

// What the LED blinks (docs/protocol.md, "The codeword"): frame version 7, a 10-byte
// message of key_bytes and a CRC-16 over the version byte and key_bytes (the version is
// folded into the CRC, not sent), followed by 6 Reed-Solomon parity bytes. The daemon only
// encodes; reading it back is the app's.

inline constexpr std::uint8_t led_frame_version = 0x07;
inline constexpr std::size_t led_message_length = 10;
inline constexpr std::size_t led_parity_length = 6;
inline constexpr std::size_t led_codeword_length = led_message_length + led_parity_length;

// docs/protocol.md, "Symbols": nibble n is sent as led_words[n], five symbols, first symbol
// in the word's highest bit. The 16 five-symbol words with no run of three, ascending.
inline constexpr std::array<std::uint8_t, 16> led_words{0b00100, 0b00101, 0b00110, 0b01001, 0b01010, 0b01011, 0b01100, 0b01101,
                                                        0b10010, 0b10011, 0b10100, 0b10101, 0b10110, 0b11001, 0b11010, 0b11011};
inline constexpr std::size_t led_word_symbols = 5;
inline constexpr std::size_t led_sync_on = 5;
inline constexpr std::size_t led_sync_off = 5;
// 170: the sync and the codeword's 32 words.
inline constexpr std::size_t led_frame_symbols = led_sync_on + led_sync_off + led_codeword_length * 2 * led_word_symbols;

types::Bytes led_message(const pairing_key::PairingKey& id);
types::Bytes led_codeword(const pairing_key::PairingKey& id);

}  // namespace rpiledconfig::protocol
