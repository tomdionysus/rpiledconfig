//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/keys.h"

#include <gtest/gtest.h>

#include "util.h"

using namespace rpiledconfig;
using namespace rpiledconfig::protocol;

// docs/protocol.md, "Test vectors": w, the fingerprint and S for 0x0123456789abcdef. The expected
// values were computed outside this code base from the derivations in the document.
TEST(KeysTest, ThePasswordScalarOfTheVectorsId) {
  const auto id = machine_id::MachineId::from_value(0x0123456789abcdefULL);
  EXPECT_EQ(util::to_hex(password_scalar(id)), "d11cf43f75a28b8c30fd3da84ab44c51c0c8755713eed6dcd9b73907be6af3f7");
}

TEST(KeysTest, TheFingerprintOfTheVectorsId) {
  const auto id = machine_id::MachineId::from_value(0x0123456789abcdefULL);
  EXPECT_EQ(fingerprint(id), "55c5");
}

TEST(KeysTest, TheSessionKeyOfTheVectorsKe) {
  EXPECT_EQ(util::to_hex(session_key(util::from_hex("cf4d7f005d568e9b7966f25dbf973474").value())),
            "85de6febab32c56e9d93d31f4b364360487cba16d7fde011c8708bb95b45f60a");
}

TEST(KeysTest, SizesAndDependence) {
  const auto a = machine_id::MachineId::from_value(42);
  const auto b = machine_id::MachineId::from_value(43);
  EXPECT_EQ(password_scalar(a).size(), 32u);
  EXPECT_NE(password_scalar(a), password_scalar(b));
  EXPECT_EQ(session_key(types::Bytes(16, 0x01)).size(), key_length);
  EXPECT_EQ(fingerprint(a).size(), 4u);
}
