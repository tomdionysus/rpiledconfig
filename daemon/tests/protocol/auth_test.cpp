//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/auth.h"

#include <gtest/gtest.h>

#include "protocol/keys.h"
#include "protocol/vectors.h"
#include "util.h"

using namespace rpiledconfig;
using namespace rpiledconfig::protocol;

namespace {

types::Bytes vectors_w() { return password_scalar(machine_id::MachineId::from_value(0x0123456789abcdefULL)); }

}  // namespace

// docs/protocol.md, "Test vectors": one complete authentication with x = 01..20 and
// y = 21..40. Expected values computed outside this code base (Python, after it
// reproduced RFC 9382's vectors).
TEST(AuthTest, TheVectorsAuthentication) {
  const auto w = vectors_w();
  const auto p_b = spake2::share(spake2::Role::b, w, vectors::y());
  EXPECT_EQ(util::to_hex(p_b),
            "0489104b39198d13c02718251d7dc12cee4dc5629b47875240c477cc04600088bcd7cba55f04f2be712ff8b2f971bda93e9323ef61d0e787f89a167b72c8dfd71d");

  const auto app = app_auth(w, vectors::x(), p_b);
  ASSERT_TRUE(app) << app.error;
  ASSERT_EQ(app.value.write.size(), auth_write_length);
  EXPECT_EQ(util::to_hex(types::Bytes(app.value.write.begin(), app.value.write.begin() + 65)),
            "040dd445d16102c3424f2451006a7aa6e8bfd8a4ccb182905cccf632c27e995012d5f4a0911c67271eb2016e8edc631a1f3633f36ea0f8d67773d127d38697f5a2");
  EXPECT_EQ(util::to_hex(types::Bytes(app.value.write.begin() + 65, app.value.write.end())),
            "a716256c3ba4e991e180bb91ed619b2c12fef2882ab978a71609b4321bef47cf");

  const auto daemon = accept_auth(w, vectors::y(), p_b, app.value.write);
  ASSERT_TRUE(daemon) << daemon.error;
  EXPECT_EQ(util::to_hex(daemon.value.ke), "cf4d7f005d568e9b7966f25dbf973474");
  EXPECT_EQ(util::to_hex(daemon.value.cb), "e0bf8b08cb4f684c6ee7ca0f1a82861ed41fbf3ce5babb07b346c048ebcdd551");
  EXPECT_EQ(daemon.value.ke, app.value.keys.ke);
  EXPECT_EQ(daemon.value.cb, app.value.keys.cb);
}

TEST(AuthTest, TheDaemonRefusesTheWrongIdAnAlteredWriteAndAWrongLength) {
  const auto w = vectors_w();
  const auto y = spake2::random_scalar();
  const auto p_b = spake2::share(spake2::Role::b, w, y);

  const auto right = app_auth(w, spake2::random_scalar(), p_b);
  ASSERT_TRUE(right);
  EXPECT_TRUE(accept_auth(w, y, p_b, right.value.write));

  const auto wrong_id = app_auth(password_scalar(machine_id::MachineId::from_value(0x0123456789abcdeeULL)), spake2::random_scalar(), p_b);
  ASSERT_TRUE(wrong_id);
  EXPECT_FALSE(accept_auth(w, y, p_b, wrong_id.value.write));

  auto altered = right.value.write;
  altered.back() ^= 0x01;
  EXPECT_FALSE(accept_auth(w, y, p_b, altered));

  // The write for one pB is no good against another: a recording cannot be replayed.
  const auto y2 = spake2::random_scalar();
  EXPECT_FALSE(accept_auth(w, y2, spake2::share(spake2::Role::b, w, y2), right.value.write));

  EXPECT_FALSE(accept_auth(w, y, p_b, types::Bytes(right.value.write.begin(), right.value.write.end() - 1)));
  EXPECT_FALSE(accept_auth(w, y, p_b, types::Bytes(48, 0x00)));
}
