//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/channel.h"

#include <gtest/gtest.h>

#include "protocol/crypto.h"
#include "util.h"

using namespace rpiledconfig;
using namespace rpiledconfig::protocol;

namespace {

types::Bytes session_key() {
  const auto key = util::from_hex("1cb3100675b3259832f74f6170b48f7340628a4076c585ed1df209ba455c1cab");
  return key ? *key : types::Bytes{};
}

}  // namespace

TEST(ChannelTest, TheNonceIsTheDirectionThenTheCounterBigEndian) {
  EXPECT_EQ(util::to_hex(make_nonce(Direction::app_to_daemon, 0)), "000000010000000000000000");
  EXPECT_EQ(util::to_hex(make_nonce(Direction::daemon_to_app, 1)), "000000020000000000000001");
  EXPECT_EQ(util::to_hex(make_nonce(Direction::app_to_daemon, 0x0102030405060708ULL)), "000000010102030405060708");
}

TEST(ChannelTest, TheWireFormIsCounterCiphertextTag) {
  Sealer sealer(session_key(), Direction::app_to_daemon);
  const auto plaintext = util::to_bytes("{\"op\":\"status\"}");

  const auto wire = sealer.seal(plaintext);
  ASSERT_EQ(wire.size(), counter_length + plaintext.size() + crypto::gcm_tag_length);
  EXPECT_EQ(util::to_hex(types::Bytes(wire.begin(), wire.begin() + 8)), "0000000000000000");

  // The same bytes AES-GCM gives for that nonce, by hand.
  const auto by_hand = crypto::aes256gcm_seal(session_key(), make_nonce(Direction::app_to_daemon, 0), plaintext);
  EXPECT_EQ(types::Bytes(wire.begin() + 8, wire.end()), by_hand);

  const auto second = sealer.seal(plaintext);
  EXPECT_EQ(util::to_hex(types::Bytes(second.begin(), second.begin() + 8)), "0000000000000001");
  EXPECT_NE(second, wire);
}

TEST(ChannelTest, TheOtherEndOpensWhatWasSealed) {
  Sealer app(session_key(), Direction::app_to_daemon);
  Channel daemon(session_key());

  const auto opened = daemon.open_from_app(app.seal(util::to_bytes("{\"op\":\"status\"}")));
  ASSERT_TRUE(opened) << opened.error;
  EXPECT_EQ(util::to_string(opened.value), "{\"op\":\"status\"}");

  Opener app_opener(session_key(), Direction::daemon_to_app);
  const auto back = app_opener.open(daemon.seal_to_app(util::to_bytes("{\"ok\":true}")));
  ASSERT_TRUE(back) << back.error;
  EXPECT_EQ(util::to_string(back.value), "{\"ok\":true}");
}

TEST(ChannelTest, ACounterAtOrBelowTheLastAcceptedIsRejected) {
  Sealer app(session_key(), Direction::app_to_daemon);
  Opener daemon(session_key(), Direction::app_to_daemon);

  const auto first = app.seal(util::to_bytes("{\"op\":\"status\"}"));
  const auto second = app.seal(util::to_bytes("{\"op\":\"wifi.scan\"}"));

  EXPECT_TRUE(daemon.open(second));
  EXPECT_FALSE(daemon.open(second)) << "a replay";
  EXPECT_FALSE(daemon.open(first)) << "an older one";

  // Skipping ahead is not a replay: a lost message does not jam the channel.
  app.seal(util::to_bytes("lost"));
  EXPECT_TRUE(daemon.open(app.seal(util::to_bytes("{\"op\":\"finish\"}"))));
  EXPECT_EQ(daemon.last_accepted(), 3u);
}

TEST(ChannelTest, TheFirstMessageHasCounterZeroAndIsAccepted) {
  Sealer app(session_key(), Direction::app_to_daemon);
  Opener daemon(session_key(), Direction::app_to_daemon);

  EXPECT_FALSE(daemon.last_accepted());
  EXPECT_TRUE(daemon.open(app.seal({})));
  EXPECT_EQ(daemon.last_accepted(), 0u);
}

TEST(ChannelTest, TheWrongDirectionTheWrongKeyAndATamperedTagAreRejected) {
  Sealer app(session_key(), Direction::app_to_daemon);
  const auto wire = app.seal(util::to_bytes("{\"op\":\"status\"}"));

  Opener as_daemon_to_app(session_key(), Direction::daemon_to_app);
  EXPECT_FALSE(as_daemon_to_app.open(wire));

  Opener other_key(crypto::random_bytes(32), Direction::app_to_daemon);
  EXPECT_FALSE(other_key.open(wire));

  auto tampered = wire;
  tampered.back() ^= 0x01;
  Opener daemon(session_key(), Direction::app_to_daemon);
  EXPECT_FALSE(daemon.open(tampered));

  // The counter is part of the nonce: changing it breaks the tag too.
  auto renumbered = wire;
  renumbered[7] = 0x05;
  EXPECT_FALSE(daemon.open(renumbered));

  // A refused message leaves the counter untouched, so the real one still goes through.
  EXPECT_TRUE(daemon.open(wire));

  EXPECT_FALSE(daemon.open(types::Bytes(23, 0x00))) << "shorter than a counter and a tag";
}
