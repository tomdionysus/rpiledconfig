//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/led_code.h"

#include <gtest/gtest.h>

#include "protocol/reed_solomon.h"
#include "util.h"

using namespace rpiledconfig;

namespace {

const auto vectors_id = machine_id::MachineId::from_value(0x0123456789abcdefULL);

}  // namespace

// docs/protocol.md, "Test vectors", computed outside this code base.
TEST(LedCodeTest, TheMessageIsMagicVersionIdAndCrc) {
  const auto message = protocol::led_message(vectors_id);
  EXPECT_EQ(util::to_hex(message), "52050123456789abcdef07a6");
  ASSERT_EQ(message.size(), 12u);
  EXPECT_EQ(message[0], 0x52) << "ASCII R";
  EXPECT_EQ(message[1], 0x05) << "frame version 5";
}

TEST(LedCodeTest, TheCodewordIsTheMessageAndTwelveParityBytes) {
  const auto codeword = protocol::led_codeword(vectors_id);
  EXPECT_EQ(util::to_hex(codeword), "52050123456789abcdef07a62dbecfdf83914ddbbd24721a");
  EXPECT_EQ(rpiledconfig::protocol::rs::syndromes(codeword, 12), types::Bytes(12, 0));
}

TEST(LedCodeTest, TheGeneratorIsTheProtocols) {
  EXPECT_EQ(util::to_hex(rpiledconfig::protocol::rs::generator(12)), "0144774376dc1f07545c7fd561");
}

TEST(LedCodeTest, EveryIdGivesAValidCodeword) {
  for (const auto value : {0ULL, 0xffffffffffffffffULL, 0x8000000000000001ULL}) {
    const auto codeword = protocol::led_codeword(machine_id::MachineId::from_value(value));
    ASSERT_EQ(codeword.size(), 24u);
    EXPECT_EQ(rpiledconfig::protocol::rs::syndromes(codeword, 12), types::Bytes(12, 0)) << value;
  }
}
