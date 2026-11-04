//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/vectors.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "protocol/channel.h"
#include "util.h"

using namespace rpiledconfig;
using namespace rpiledconfig::protocol;

TEST(VectorsTest, TheInputsAreTheProtocols) {
  EXPECT_EQ(util::to_hex(vectors::x()), "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
  EXPECT_EQ(util::to_hex(vectors::y()), "2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40");
  EXPECT_EQ(vectors::compute().machine_id.to_hex(), "0123456789abcdef");
}

TEST(VectorsTest, TheDerivedValuesAreTheOnesComputedByHand) {
  const auto v = vectors::compute();
  EXPECT_EQ(util::to_hex(v.id_bytes), "0123456789abcdef");
  EXPECT_EQ(v.crc16_check, 0x29B1);
  EXPECT_EQ(util::to_hex(v.led_message), "52050123456789abcdef07a6");
  EXPECT_EQ(util::to_hex(v.led_codeword), "52050123456789abcdef07a62dbecfdf83914ddbbd24721a");
  EXPECT_EQ(v.led_frame_runs.rfind("+4,-4,+1,-2,+2,-2,+2,-2,", 0), 0u) << v.led_frame_runs;
  const std::string end = ",+2,-1,+1,-2,+2,-2,+1,-4";
  EXPECT_EQ(v.led_frame_runs.substr(v.led_frame_runs.size() - end.size()), end) << v.led_frame_runs;
  EXPECT_EQ(v.fingerprint, "55c5");
  // docs/protocol.md, computed in Python after it reproduced RFC 9382's vectors.
  EXPECT_EQ(util::to_hex(v.w), "d11cf43f75a28b8c30fd3da84ab44c51c0c8755713eed6dcd9b73907be6af3f7");
  EXPECT_EQ(util::to_hex(v.p_a),
            "040dd445d16102c3424f2451006a7aa6e8bfd8a4ccb182905cccf632c27e995012d5f4a0911c67271eb2016e8edc631a1f3633f36ea0f8d67773d127d38697f5a2");
  EXPECT_EQ(util::to_hex(v.p_b),
            "0489104b39198d13c02718251d7dc12cee4dc5629b47875240c477cc04600088bcd7cba55f04f2be712ff8b2f971bda93e9323ef61d0e787f89a167b72c8dfd71d");
  EXPECT_EQ(util::to_hex(v.k),
            "044fe243908f378aa1c2a69538822e6ed908c3225d8692575507c649901245150aaa8513bcd739d8100f64e283d32db26502af6ea9bcaa6bb5b58a6bf20f3228b4");
  EXPECT_EQ(util::to_hex(v.ke), "cf4d7f005d568e9b7966f25dbf973474");
  EXPECT_EQ(util::to_hex(v.c_a), "a716256c3ba4e991e180bb91ed619b2c12fef2882ab978a71609b4321bef47cf");
  EXPECT_EQ(util::to_hex(v.c_b), "e0bf8b08cb4f684c6ee7ca0f1a82861ed41fbf3ce5babb07b346c048ebcdd551");
  EXPECT_EQ(util::to_hex(v.session_key), "85de6febab32c56e9d93d31f4b364360487cba16d7fde011c8708bb95b45f60a");
}

TEST(VectorsTest, TheStatusRequestOpensUnderSWithCounterZero) {
  const auto v = vectors::compute();
  ASSERT_EQ(v.status_request_wire.size(), 39u);
  EXPECT_EQ(util::to_hex(types::Bytes(v.status_request_wire.begin(), v.status_request_wire.begin() + 8)), "0000000000000000");

  Opener daemon(v.session_key, Direction::app_to_daemon);
  const auto opened = daemon.open(v.status_request_wire);
  ASSERT_TRUE(opened) << opened.error;
  EXPECT_EQ(util::to_string(opened.value), "{\"op\":\"status\"}");

  ASSERT_EQ(v.status_request_fragments.size(), 3u);
  EXPECT_EQ(v.status_request_fragments[0].size(), 20u);
  EXPECT_EQ(v.status_request_fragments[2].size(), 2u);
}

// --print-vectors is compared line by line with the app's: the labels, their order and
// the format are part of the contract.
TEST(VectorsTest, PrintsOneLabelledHexLineEachInAFixedOrder) {
  const auto text = vectors::print(vectors::compute());

  std::vector<std::string> labels;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const auto colon = line.find(": ");
    ASSERT_NE(colon, std::string::npos) << line;
    labels.push_back(line.substr(0, colon));

    const auto value = line.substr(colon + 2);
    EXPECT_FALSE(value.empty()) << line;
    const char* allowed = labels.back() == "led_frame_runs" ? "0123456789,+-" : "0123456789abcdef,";
    EXPECT_EQ(value.find_first_not_of(allowed), std::string::npos) << line;
  }

  const std::vector<std::string> expected{"machine_id",
                                          "id_bytes",
                                          "crc16_check",
                                          "led_message",
                                          "led_codeword",
                                          "led_frame_runs",
                                          "fingerprint",
                                          "w",
                                          "pA",
                                          "pB",
                                          "K",
                                          "Ke",
                                          "cA",
                                          "cB",
                                          "S",
                                          "status_request_wire",
                                          "status_request_fragments_mtu20"};
  EXPECT_EQ(labels, expected);

  EXPECT_NE(text.find("w: d11cf43f75a28b8c30fd3da84ab44c51c0c8755713eed6dcd9b73907be6af3f7\n"), std::string::npos);
  EXPECT_NE(text.find("fingerprint: 55c5\n"), std::string::npos);
  EXPECT_NE(text.find("crc16_check: 29b1\n"), std::string::npos);
  EXPECT_NE(text.find("led_codeword: 52050123456789abcdef07a62dbecfdf83914ddbbd24721a\n"), std::string::npos);
  EXPECT_EQ(text.find("crc8"), std::string::npos) << "version 1's frame is gone";

  // Deterministic: the app's copy is fixed bytes.
  EXPECT_EQ(text, vectors::print(vectors::compute()));
}
