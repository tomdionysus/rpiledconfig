//
// RPiLEDConfig - app
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The LED channel of docs/protocol.md, frame version 7. A change here is a change to the
// document, to daemon/src/protocol/led_code.h and to app/src/protocol/constants.ts.
namespace rpiledconfig::reader {

// The symbol time T of the normal rate, in milliseconds. The phone chooses among the
// protocol's rates (docs/protocol.md, Symbols), and a Reader or Tracker is told which.
inline constexpr double symbol_ms = 51.0;

inline constexpr std::uint8_t frame_version = 0x07;
inline constexpr std::size_t id_length = 8;
inline constexpr std::size_t message_length = 10;
inline constexpr std::size_t parity_length = 6;
inline constexpr std::size_t codeword_length = message_length + parity_length;

// Nibble n is sent as words[n], five symbols, the first in the word's highest bit: the 16
// five-symbol words with no run of three, ascending.
inline constexpr std::array<std::uint8_t, 16> words{0b00100, 0b00101, 0b00110, 0b01001, 0b01010, 0b01011, 0b01100, 0b01101,
                                                    0b10010, 0b10011, 0b10100, 0b10101, 0b10110, 0b11001, 0b11010, 0b11011};
inline constexpr std::size_t word_symbols = 5;
inline constexpr std::size_t sync_on = 5;
inline constexpr std::size_t sync_off = 5;
inline constexpr std::size_t sync_symbols = sync_on + sync_off;
inline constexpr std::size_t frame_words = codeword_length * 2;
// 170: the sync and the codeword's 32 words.
inline constexpr std::size_t frame_symbols = sync_symbols + frame_words * word_symbols;

// The decoder tolerates this much clock error over a frame.
inline constexpr double clock_tolerance = 0.05;

}  // namespace rpiledconfig::reader
