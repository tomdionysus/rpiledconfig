//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/crc16.h"

#include <gtest/gtest.h>

#include "util.h"

using namespace rpiledconfig;

TEST(Crc16Test, TheCheckValueOf123456789Is29B1) { EXPECT_EQ(protocol::crc16(util::to_bytes("123456789")), 0x29B1); }

TEST(Crc16Test, NothingIsTheInitialValue) { EXPECT_EQ(protocol::crc16(types::Bytes{}), 0xFFFF); }

// docs/protocol.md, "Test vectors": frame version 7's message ends with the CRC over the
// version byte 0x07 and key_bytes, 00 8c; version 6's, over 0x06, was eb af.
TEST(Crc16Test, TheVectorsMessageCarriesItsCrc) {
  const types::Bytes checked{0x07, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  EXPECT_EQ(protocol::crc16(checked), 0x008C);
  const types::Bytes version6{0x06, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  EXPECT_EQ(protocol::crc16(version6), 0xEBAF);
}
