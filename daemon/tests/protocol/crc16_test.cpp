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

// docs/protocol.md, "Test vectors": the message's last two bytes are the CRC of the ten
// before them, b6 0d.
TEST(Crc16Test, TheVectorsMessageCarriesItsCrc) {
  const types::Bytes head{0x52, 0x02, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  EXPECT_EQ(protocol::crc16(head), 0xB60D);
}
