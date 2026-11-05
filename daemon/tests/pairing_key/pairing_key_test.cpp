//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "pairing_key/pairing_key.h"

#include <gtest/gtest.h>

#include <array>
#include <set>

using namespace rpiledconfig;

TEST(PairingKeyTest, TheByteFormIsBigEndian) {
  const auto key = pairing_key::PairingKey::from_value(0x0123456789abcdefULL);
  EXPECT_EQ(key.to_hex(), "0123456789abcdef");
  EXPECT_EQ(key.value(), 0x0123456789abcdefULL);
  EXPECT_EQ(key.key_bytes(), (types::Bytes{0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}));
}

// docs/protocol.md, The pairing key: a key is one-time, so all 64 bits are new every time. A
// repeat among these is a 2^-52 event, not bad luck.
TEST(PairingKeyTest, EveryDrawIsNew) {
  std::set<std::uint64_t> seen;
  for (int i = 0; i < 64; ++i) {
    const auto key = pairing_key::draw();
    ASSERT_TRUE(key) << key.error;
    seen.insert(key.value.value());
  }
  EXPECT_EQ(seen.size(), 64u);
}

// No part of a key is fixed: every byte position takes more than one value.
TEST(PairingKeyTest, NoPartOfAKeyIsFixed) {
  std::array<std::set<std::uint8_t>, pairing_key::key_length> seen;
  for (int i = 0; i < 64; ++i) {
    const auto key = pairing_key::draw();
    ASSERT_TRUE(key) << key.error;
    for (std::size_t b = 0; b < key.value.bytes.size(); ++b) seen[b].insert(key.value.bytes[b]);
  }
  for (const auto& values : seen) EXPECT_GT(values.size(), 1u);
}
