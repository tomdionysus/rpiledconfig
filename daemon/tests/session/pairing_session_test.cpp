//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "session/pairing_session.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "helpers/setup_fixture_helper.h"
#include "protocol/json.h"

using namespace rpiledconfig;
using namespace std::chrono_literals;
using Characteristic = ble::Characteristic;

namespace {

types::Bytes wrong_key() { return protocol::password_scalar(pairing_key::PairingKey::from_value(0x0badf00d0badf00dULL)); }

}  // namespace

TEST(PairingSessionTest, InfoNamesTheProtocolAndTheVersionAndNothingOfTheKey) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_NE(connection, 0u);

  const auto info = protocol::Json::parse(util::to_string(f.read(connection, Characteristic::info)));
  ASSERT_TRUE(info);
  EXPECT_EQ(info->get("protocol")->as_number(), 4);
  EXPECT_EQ(info->get("version")->as_string(), "0.0.0-test");
  EXPECT_EQ(info->get("fingerprint"), nullptr);
  EXPECT_EQ(f.ble->advertised_name(), f.ble_name);
}

TEST(PairingSessionTest, TheChallengeIsAnUncompressedPointNewForEveryConnection) {
  SetupFixture f;
  f.start();

  const auto a = f.connect();
  const auto b = f.connect();
  const auto n_a = f.read(a, Characteristic::challenge);
  const auto n_b = f.read(b, Characteristic::challenge);

  ASSERT_EQ(n_a.size(), 65u);
  EXPECT_EQ(n_a[0], 0x04);
  EXPECT_EQ(n_b.size(), 65u);
  EXPECT_NE(n_a, n_b);
  EXPECT_EQ(f.read(a, Characteristic::challenge), n_a) << "the same until an Auth write";
  EXPECT_TRUE(f.read(a, Characteristic::auth_result).empty()) << "nothing before an attempt";
}

TEST(PairingSessionTest, TheRightPasswordIsAcceptedAndAnsweredWithCb) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();

  const auto result = f.authenticate(connection, f.w);
  ASSERT_EQ(result.size(), 33u);
  EXPECT_EQ(result[0], protocol::auth_accepted);

  EXPECT_EQ(types::Bytes(result.begin() + 1, result.end()), f.last_keys.cb);
  EXPECT_EQ(f.read(connection, Characteristic::auth_result), result) << "readable as well as notified";
  EXPECT_TRUE(f.ble->connected(connection));
}

TEST(PairingSessionTest, TheWrongPasswordIsRefusedAndTheChallengeRenewed) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();

  const auto before = f.read(connection, Characteristic::challenge);
  const auto result = f.authenticate(connection, wrong_key());
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0], protocol::auth_refused);
  EXPECT_NE(f.read(connection, Characteristic::challenge), before);
  EXPECT_TRUE(f.ble->connected(connection)) << "one refusal is not the end";

  // And the right one still works after a refusal.
  EXPECT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
}

// docs/protocol.md, "Authentication": a read of Challenge on an authenticated connection
// starts over, as an app that starts again on a link the operating system kept does.
TEST(PairingSessionTest, ReadingChallengeAfterAuthenticationStartsOver) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();

  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  const auto old_p_b = f.last_p_b;
  const auto old_phone = f.phones.at(connection);
  ASSERT_TRUE(f.request(connection, "{\"op\":\"status\"}"));

  const auto fresh = f.read(connection, Characteristic::challenge);
  EXPECT_EQ(fresh.size(), 65u);
  EXPECT_NE(fresh, old_p_b) << "a new pB";
  EXPECT_TRUE(f.read(connection, Characteristic::auth_result).empty()) << "AuthResult as before any attempt";
  EXPECT_EQ(f.read(connection, Characteristic::challenge), fresh) << "and stable until an Auth write";
  EXPECT_TRUE(f.ble->connected(connection));

  // A fresh start: authenticating again works, under a new S.
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  EXPECT_NE(f.phones.at(connection), old_phone);
  EXPECT_TRUE(f.request(connection, "{\"op\":\"status\"}"));
}

TEST(PairingSessionTest, AfterStartingOverTheOldKeyIsNoLongerHeard) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  f.read(connection, Characteristic::challenge);

  // The old channel's request is now a Request before authentication: dropped.
  EXPECT_FALSE(f.request(connection, "{\"op\":\"status\"}"));
  EXPECT_FALSE(f.ble->connected(connection));
}

TEST(PairingSessionTest, StartingOverRestartsTheThirtySecondClock) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  f.advance(5min);
  ASSERT_TRUE(f.ble->connected(connection)) << "authenticated, so no clock";

  f.read(connection, Characteristic::challenge);
  f.advance(29s);
  EXPECT_TRUE(f.ble->connected(connection));
  f.advance(1s);
  EXPECT_FALSE(f.ble->connected(connection));
}

TEST(PairingSessionTest, ThreeRefusedAttemptsDropTheConnection) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();

  EXPECT_EQ(f.authenticate(connection, wrong_key())[0], protocol::auth_refused);
  EXPECT_EQ(f.authenticate(connection, wrong_key())[0], protocol::auth_refused);
  EXPECT_TRUE(f.ble->connected(connection));

  const auto third = f.authenticate(connection, wrong_key());
  ASSERT_EQ(third.size(), 1u);
  EXPECT_EQ(third[0], protocol::auth_refused) << "the third refusal is still answered";
  EXPECT_FALSE(f.ble->connected(connection));
  EXPECT_EQ(f.setup->sessions(), 0u);
  EXPECT_TRUE(f.setup->active()) << "setup mode goes on";
}

TEST(PairingSessionTest, AMalformedAuthWriteCountsAsARefusal) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();

  f.write(connection, Characteristic::auth, types::Bytes(10, 0x00));
  const auto notified = f.ble->take_notifications(connection);
  ASSERT_EQ(notified.size(), 1u);
  EXPECT_EQ(notified[0].value, types::Bytes{protocol::auth_refused});
}

TEST(PairingSessionTest, NotAuthenticatingWithinThirtySecondsDropsTheConnection) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();

  f.advance(29s);
  EXPECT_TRUE(f.ble->connected(connection));
  f.advance(1s);
  EXPECT_FALSE(f.ble->connected(connection));

  // An authenticated one is not on that clock.
  const auto other = f.connect();
  ASSERT_EQ(f.authenticate(other, f.w)[0], protocol::auth_accepted);
  f.advance(60s);
  EXPECT_TRUE(f.ble->connected(other));
}

TEST(PairingSessionTest, ARequestBeforeAuthenticationIsIgnoredAndDropsTheConnection) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();

  f.write(connection, Characteristic::request, types::Bytes{0x01, 0x00});
  EXPECT_FALSE(f.ble->connected(connection));
  EXPECT_TRUE(f.ble->notifications(connection).empty());
}

TEST(PairingSessionTest, AnAuthenticatedPhoneGetsItsStatus) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  const auto response = f.request(connection, "{\"op\":\"status\"}");
  ASSERT_TRUE(response);
  const auto json = protocol::Json::parse(*response);
  ASSERT_TRUE(json);
  EXPECT_TRUE(json->get("ok")->as_boolean());
  EXPECT_EQ(json->get("hostname")->as_string(), "testpi");
  EXPECT_FALSE(json->get("wifi")->get("connected")->as_boolean());
}

TEST(PairingSessionTest, ResponsesAreFragmentedToTheMtu) {
  SetupFixture f;
  f.start();
  f.wifi->set_networks({{"a-long-network-name-one", 90, true}, {"a-long-network-name-two", 80, true}, {"a-long-network-name-three", 70, false}});

  const auto connection = f.connect(23);
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  auto phone = f.phones.at(connection);
  const auto wire = phone->to_daemon.seal(util::to_bytes("{\"op\":\"wifi.scan\"}"));
  for (const auto& piece : protocol::fragment(wire, 20)) f.write(connection, Characteristic::request, piece);

  const auto notified = f.ble->notifications(connection);
  ASSERT_GT(notified.size(), 3u) << "a scan of three networks does not fit twenty bytes";
  for (std::size_t i = 0; i < notified.size(); ++i) {
    EXPECT_EQ(notified[i].characteristic, Characteristic::response);
    EXPECT_LE(notified[i].value.size(), 20u);
    EXPECT_EQ(notified[i].value[0], i + 1 == notified.size() ? protocol::flag_last : 0x00);
  }

  const auto response = f.response(connection);
  ASSERT_TRUE(response);
  const auto json = protocol::Json::parse(*response);
  ASSERT_TRUE(json);
  EXPECT_EQ(json->get("networks")->items().size(), 3u);
}

TEST(PairingSessionTest, AReplayedCounterDropsTheConnection) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  auto phone = f.phones.at(connection);
  const auto wire = phone->to_daemon.seal(util::to_bytes("{\"op\":\"status\"}"));

  EXPECT_TRUE(f.send_wire(connection, wire));
  EXPECT_TRUE(f.ble->connected(connection));

  EXPECT_FALSE(f.send_wire(connection, wire)) << "no answer to a replay";
  EXPECT_FALSE(f.ble->connected(connection));
}

TEST(PairingSessionTest, ATamperedMessageDropsTheConnection) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  auto wire = f.phones.at(connection)->to_daemon.seal(util::to_bytes("{\"op\":\"status\"}"));
  wire[wire.size() - 1] ^= 0x01;

  EXPECT_FALSE(f.send_wire(connection, wire));
  EXPECT_FALSE(f.ble->connected(connection));
}

TEST(PairingSessionTest, AFragmentWithUnknownFlagsDropsTheConnection) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  f.write(connection, Characteristic::request, types::Bytes{0x02, 0x00});
  EXPECT_FALSE(f.ble->connected(connection));
}

TEST(PairingSessionTest, WhatIsNotARequestGetsBadRequest) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  const auto response = f.request(connection, "not json at all");
  ASSERT_TRUE(response);
  const auto json = protocol::Json::parse(*response);
  ASSERT_TRUE(json);
  EXPECT_FALSE(json->get("ok")->as_boolean());
  EXPECT_EQ(json->get("error")->as_string(), "bad_request");
  EXPECT_TRUE(f.ble->connected(connection)) << "a bad request is answered, not dropped";
}

TEST(PairingSessionTest, AnUnknownOpGetsUnknownOp) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  const auto response = f.request(connection, "{\"op\":\"reboot\"}");
  ASSERT_TRUE(response);
  const auto json = protocol::Json::parse(*response);
  ASSERT_TRUE(json);
  EXPECT_FALSE(json->get("ok")->as_boolean());
  EXPECT_EQ(json->get("error")->as_string(), "unknown_op");
  EXPECT_TRUE(f.ble->connected(connection));
}

TEST(PairingSessionTest, ConfigureThenFinishLeavesSetupModeAndDisconnects) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  f.blink(connection);
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  auto response = f.request(connection, "{\"op\":\"wifi.configure\",\"ssid\":\"home\",\"psk\":\"secret\"}");
  ASSERT_TRUE(response);
  EXPECT_EQ(*response, "{\"ok\":true}");
  EXPECT_EQ(f.wifi->configured_ssid(), "home");
  EXPECT_EQ(f.wifi->configured_psk(), "secret");

  response = f.request(connection, "{\"op\":\"finish\"}");
  ASSERT_TRUE(response) << "finish is answered before the link goes";
  EXPECT_EQ(*response, "{\"ok\":true}");

  EXPECT_FALSE(f.ble->connected(connection));
  EXPECT_FALSE(f.setup->active());
  EXPECT_FALSE(f.ble->started());
  EXPECT_FALSE(f.modulator->running());
  EXPECT_EQ(f.led->ends(), 1) << "the LED's trigger was given back, once";
}

TEST(PairingSessionTest, TheDaemonsCountersGoUpByOnePerResponse) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  for (int i = 0; i < 3; ++i) ASSERT_TRUE(f.request(connection, "{\"op\":\"status\"}"));
  EXPECT_EQ(f.phones.at(connection)->from_daemon.last_accepted(), 2u);
}
