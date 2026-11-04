//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/led_code.h"

#include <gtest/gtest.h>

#include <vector>

#include "protocol/crc16.h"
#include "protocol/reed_solomon.h"
#include "util.h"

using namespace rpiledconfig;

namespace {

const auto vectors_id = machine_id::MachineId::from_value(0x0123456789abcdefULL);

}  // namespace

// docs/protocol.md, "Test vectors", computed outside this code base.
TEST(LedCodeTest, TheMessageIsTheIdAndACrcOverTheVersionAndId) {
  const auto message = protocol::led_message(vectors_id);
  EXPECT_EQ(util::to_hex(message), "0123456789abcdef008c");
  ASSERT_EQ(message.size(), 10u);
}

// The version byte is in the CRC and not on the LED: with any other version the CRC differs.
TEST(LedCodeTest, TheVersionIsFoldedIntoTheCrc) {
  types::Bytes checked{0x05};
  const auto id_bytes = vectors_id.id_bytes();
  checked.insert(checked.end(), id_bytes.begin(), id_bytes.end());
  const auto other = protocol::crc16(checked);
  const auto message = protocol::led_message(vectors_id);
  EXPECT_NE(other, (message[8] << 8) | message[9]);
}

TEST(LedCodeTest, TheCodewordIsTheMessageAndSixParityBytes) {
  const auto codeword = protocol::led_codeword(vectors_id);
  EXPECT_EQ(util::to_hex(codeword), "0123456789abcdef008cc420bb0f15c9");
  EXPECT_EQ(rpiledconfig::protocol::rs::syndromes(codeword, 6), types::Bytes(6, 0));
}

TEST(LedCodeTest, TheGeneratorIsTheProtocols) {
  EXPECT_EQ(util::to_hex(rpiledconfig::protocol::rs::generator(6)), "013f01da20e326");
}

TEST(LedCodeTest, EveryIdGivesAValidCodeword) {
  for (const auto value : {0ULL, 0xffffffffffffffffULL, 0x8000000000000001ULL}) {
    const auto codeword = protocol::led_codeword(machine_id::MachineId::from_value(value));
    ASSERT_EQ(codeword.size(), 16u);
    EXPECT_EQ(rpiledconfig::protocol::rs::syndromes(codeword, 6), types::Bytes(6, 0)) << value;
  }
}

// docs/protocol.md, "Symbols": the 16 five-symbol words with no run of three, ascending.
TEST(LedCodeTest, TheWordsAreTheSixteenWithoutARunOfThree) {
  std::vector<std::uint8_t> words;
  for (std::uint8_t w = 0; w < 32; ++w) {
    bool run_of_three = false;
    for (int shift = 0; shift <= 2; ++shift) {
      const int three = (w >> shift) & 7;
      if (three == 0 || three == 7) run_of_three = true;
    }
    if (!run_of_three) words.push_back(w);
  }
  ASSERT_EQ(words.size(), 16u);
  EXPECT_EQ(std::vector<std::uint8_t>(protocol::led_words.begin(), protocol::led_words.end()), words);
  EXPECT_EQ(protocol::led_frame_symbols, 170u);
}
