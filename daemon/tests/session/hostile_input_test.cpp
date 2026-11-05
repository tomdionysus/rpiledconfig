//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "helpers/attacker.h"
#include "protocol/auth.h"
#include "protocol/channel.h"
#include "protocol/fragments.h"
#include "protocol/json.h"
#include "protocol/messages.h"
#include "protocol/spake2.h"
#include "util.h"

// What comes in over the air is not to be trusted. Run under the asan preset, these are also
// the memory-safety checks; the fuzzers in tests/fuzz take the same run much further.
using namespace rpiledconfig;

TEST(HostileInputTest, ArbitrarySequencesBreakNothingAndGrantNothing) {
  std::mt19937 random(20261105);
  for (int round = 0; round < 400; ++round) {
    std::vector<std::uint8_t> bytes(random() % 700);
    for (auto& byte : bytes) byte = static_cast<std::uint8_t>(random());
    attacker::run(bytes.data(), bytes.size(), [&](const std::string& what) { ADD_FAILURE() << what << " (round " << round << ")"; });
  }
}

// The harness has to be able to tell: with the key, the privileged things do happen.
TEST(HostileInputTest, TheHarnessReachesWhatItGuards) {
  // connect, prove the key, ask to configure.
  const std::vector<std::uint8_t> script = {1, 0, 0, 7, 9, 4, 'h', 'o', 'm', 'e', 2, 0};
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  ASSERT_TRUE(f.request(connection, "{\"op\":\"wifi.configure\",\"ssid\":\"home\"}"));
  EXPECT_EQ(f.wifi->configured_ssid(), "home");

  bool complained = false;
  attacker::run(script.data(), script.size(), [&](const std::string&) { complained = true; });
  EXPECT_FALSE(complained);
}

TEST(HostileInputTest, JsonThatIsNotOrIsTooDeepIsRefused) {
  const std::vector<std::string> bad = {
      std::string(200000, '['),
      std::string(200000, '{'),
      "{\"a\":" + std::string(100000, '[') + "1" + std::string(100000, ']') + "}",
      "\"\\u",
      "\"\\uD800\"",
      "\"\\uD800\\u0041\"",
      "\"\\uDC00\"",
      "1e99999",
      "-",
      "-e",
      "0123",
      "1.",
      "1e",
      "tru",
      "\"unterminated",
      "\"\\",
      std::string(1, '\0'),
      "{\"a\"",
      "{\"a\":",
      "[1,",
      "[,]",
  };
  for (const auto& text : bad) EXPECT_FALSE(protocol::Json::parse(text).has_value()) << text.substr(0, 32);
}

TEST(HostileInputTest, ValidJsonNestedToTheLimitDumpsAndParsesBack) {
  const std::string text = std::string(30, '[') + "1" + std::string(30, ']');
  const auto parsed = protocol::Json::parse(text);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->dump(), text);
}

TEST(HostileInputTest, ANetworkNameOrPassphraseThatCannotBeOneIsRefused) {
  const auto configure = [](const std::string& ssid, const std::string& psk) {
    return protocol::parse_request("{\"op\":\"wifi.configure\",\"ssid\":" + protocol::json_quote(ssid) + ",\"psk\":" + protocol::json_quote(psk) + "}");
  };

  EXPECT_TRUE(configure("home", "any passphrase, short or long"));
  EXPECT_TRUE(configure(std::string(32, 'x'), "p"));
  EXPECT_TRUE(configure("caf\xc3\xa9", "p\xc3\xa9"));

  const auto refused = [&](const std::string& ssid, const std::string& psk) {
    const auto request = configure(ssid, psk);
    EXPECT_FALSE(request) << ssid;
    EXPECT_EQ(request.code, protocol::errors::bad_request);
  };
  refused(std::string(33, 'x'), "p");
  refused(std::string("ho\0me", 5), "p");
  refused("line\nforged", "p");
  refused("bad\xff", "p");
  refused("home", std::string("p\0q", 3));
  refused("home", "bad\xc3");
}

TEST(HostileInputTest, FragmentsCannotOverrunOrConfuseTheReassembler) {
  protocol::Reassembler reassembler(16);
  EXPECT_FALSE(reassembler.push({}));
  EXPECT_FALSE(reassembler.push({0xFF, 1, 2}));
  EXPECT_FALSE(reassembler.push({0x02}));

  // Over the limit, however it is reached, and it starts clean after.
  EXPECT_FALSE(reassembler.push(types::Bytes(18, 0x00)));
  EXPECT_TRUE(reassembler.push(types::Bytes{0x00, 1, 2, 3}));
  EXPECT_FALSE(reassembler.push(types::Bytes(17, 0x00)));
  const auto fresh = reassembler.push({0x01, 9});
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*fresh.value, (types::Bytes{9}));

  // An empty last fragment is an empty message, not a read past anything.
  const auto empty = reassembler.push({0x01});
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty.value->empty());
}

TEST(HostileInputTest, MalformedAuthWritesAreRefusedNotCrashedOn) {
  const auto w = protocol::password_scalar(pairing_key::PairingKey::from_value(0x0123456789abcdefULL));
  const auto y = protocol::spake2::random_scalar();
  const auto p_b = protocol::spake2::share(protocol::spake2::Role::b, w, y);

  const auto with_point = [](std::uint8_t first, std::uint8_t fill) {
    types::Bytes write(protocol::auth_write_length, fill);
    write[0] = first;
    return write;
  };

  for (const auto& write : {types::Bytes{}, types::Bytes(1, 4), types::Bytes(96, 4), types::Bytes(98, 4), types::Bytes(4096, 0xFF), with_point(0x04, 0x00),
                            with_point(0x04, 0xFF), with_point(0x02, 0x11), with_point(0x03, 0x11), with_point(0x00, 0x00)}) {
    EXPECT_FALSE(protocol::accept_auth(w, y, p_b, write)) << write.size();
  }

  // A spent or missing y is refused as well.
  types::Bytes good_length(protocol::auth_write_length, 0x04);
  EXPECT_FALSE(protocol::accept_auth(w, {}, p_b, good_length));
  EXPECT_FALSE(protocol::accept_auth({}, y, p_b, good_length));
}

TEST(HostileInputTest, SealedMessagesThatAreNotAreRefused) {
  const types::Bytes key(32, 0x42);
  protocol::Opener opener(key, protocol::Direction::app_to_daemon);

  EXPECT_FALSE(opener.open({}));
  EXPECT_FALSE(opener.open(types::Bytes(7, 0)));
  EXPECT_FALSE(opener.open(types::Bytes(8, 0)));
  EXPECT_FALSE(opener.open(types::Bytes(23, 0)));
  EXPECT_FALSE(opener.open(types::Bytes(24, 0)));
  EXPECT_FALSE(opener.open(types::Bytes(70000, 0xAA)));

  // The largest counter there is must not wrap into being acceptable again.
  protocol::Sealer sealer(key, protocol::Direction::app_to_daemon);
  const auto wire = sealer.seal(util::to_bytes("x"));
  ASSERT_TRUE(opener.open(wire));
  EXPECT_FALSE(opener.open(wire)) << "a replay";
}

TEST(HostileInputTest, ControlCharactersCannotForgeALogLine) {
  EXPECT_EQ(util::printable("plain text"), "plain text");
  EXPECT_EQ(util::printable("caf\xc3\xa9"), "caf\xc3\xa9");
  EXPECT_EQ(util::printable("a\nb\rc\td\x1b[2Je\x7f"), "a?b?c?d?[2Je?");
  EXPECT_EQ(util::printable(std::string("a\0b", 3)), "a?b");
}
