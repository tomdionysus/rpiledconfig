//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/reed_solomon.h"

#include <gtest/gtest.h>

#include "util.h"

using namespace rpiledconfig;
using namespace rpiledconfig::protocol;

namespace {

types::Bytes hex(const std::string& text) { return *util::from_hex(text); }

// docs/protocol.md, "Test vectors", computed outside this code base.
const std::string message_hex = "52020123456789abcdefb60d";
const std::string codeword_hex = "52020123456789abcdefb60d5be02aacac7c046f2301e7a8";

}  // namespace

// GF(2^8) mod 0x11D, worked by hand: 0x80 * 2 overflows into the polynomial; a^8 is the
// polynomial's low byte; a has order 255.
TEST(ReedSolomonTest, TheFieldIsGf256ModuloX8X4X3X2Plus1) {
  EXPECT_EQ(rs::multiply(0x80, 0x02), 0x1D);
  EXPECT_EQ(rs::multiply(0x02, 0x80), 0x1D);
  EXPECT_EQ(rs::multiply(0x00, 0x53), 0x00);
  EXPECT_EQ(rs::multiply(0x01, 0x53), 0x53);
  EXPECT_EQ(rs::power_of_a(0), 0x01);
  EXPECT_EQ(rs::power_of_a(1), 0x02);
  EXPECT_EQ(rs::power_of_a(8), 0x1D);
  EXPECT_EQ(rs::power_of_a(255), 0x01);

  // Every non-zero element has an inverse among the powers of a.
  for (unsigned i = 0; i < 255; ++i) EXPECT_EQ(rs::multiply(rs::power_of_a(i), rs::power_of_a(255 - i)), 0x01) << i;
}

TEST(ReedSolomonTest, TheGeneratorIsTheProtocols) {
  EXPECT_EQ(util::to_hex(rs::generator(12)), "0144774376dc1f07545c7fd561");
  EXPECT_EQ(rs::generator(12).size(), 13u);
}

TEST(ReedSolomonTest, TheParityOfTheVectorsMessageIsTheProtocols) {
  const auto parity = rs::parity(hex(message_hex), 12);
  EXPECT_EQ(util::to_hex(hex(message_hex)) + util::to_hex(parity), codeword_hex);
}

TEST(ReedSolomonTest, TheCodewordsTwelveSyndromesAreZero) {
  const auto syndromes = rs::syndromes(hex(codeword_hex), 12);
  EXPECT_EQ(syndromes, types::Bytes(12, 0));
}

TEST(ReedSolomonTest, ADamagedCodewordHasSyndromes) {
  auto damaged = hex(codeword_hex);
  damaged[5] ^= 0x40;
  EXPECT_NE(rs::syndromes(damaged, 12), types::Bytes(12, 0));
}

// Any message, not just the vector's: the remainder makes a codeword.
TEST(ReedSolomonTest, EveryEncodedMessageIsACodeword) {
  for (int seed = 0; seed < 20; ++seed) {
    types::Bytes message(12);
    for (std::size_t i = 0; i < message.size(); ++i) message[i] = static_cast<std::uint8_t>(seed * 37 + i * 101);
    auto codeword = message;
    const auto parity = rs::parity(message, 12);
    codeword.insert(codeword.end(), parity.begin(), parity.end());
    EXPECT_EQ(rs::syndromes(codeword, 12), types::Bytes(12, 0)) << seed;
  }
}
