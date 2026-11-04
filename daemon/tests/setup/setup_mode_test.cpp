//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "setup/setup_mode.h"

#include <gtest/gtest.h>

#include <chrono>

#include "helpers/setup_fixture_helper.h"

using namespace rpiledconfig;
using namespace std::chrono_literals;

namespace {

wifi::WifiStatus connected_to(const std::string& ssid) {
  wifi::WifiStatus status;
  status.connected = true;
  status.ssid = ssid;
  status.ip = "10.0.0.5";
  return status;
}

types::Bytes wrong_key() { return protocol::password_scalar(machine_id::MachineId::from_value(0xdeadbeefULL)); }

}  // namespace

TEST(SetupModeTest, EntersAtStartWhenThereIsNoWifi) {
  SetupFixture f;
  EXPECT_FALSE(f.setup->active());

  f.start();

  EXPECT_TRUE(f.setup->active());
  EXPECT_TRUE(f.modulator->running());
  EXPECT_EQ(f.led->begins(), 1);
  EXPECT_TRUE(f.ble->advertising());
  EXPECT_EQ(f.ble->advertised_name(), "RPiLEDConfig-" + f.fingerprint);
  EXPECT_TRUE(f.logger->contains("LED frame 1 starts")) << f.logger->text();
}

TEST(SetupModeTest, IdlesAtStartWhenWifiIsConnectedAndChecksAgainLater) {
  SetupFixture f;
  f.wifi->set_status(connected_to("home"));

  f.start();
  EXPECT_FALSE(f.setup->active());
  EXPECT_FALSE(f.ble->started());
  EXPECT_FALSE(f.modulator->running());
  EXPECT_EQ(f.wifi->status_calls(), 1);

  f.advance(59s);
  EXPECT_EQ(f.wifi->status_calls(), 1);
  f.advance(1s);
  EXPECT_EQ(f.wifi->status_calls(), 2);
  EXPECT_FALSE(f.setup->active());

  // The connection goes: the next check enters setup mode.
  f.wifi->set_status({});
  f.advance(60s);
  EXPECT_TRUE(f.setup->active());
  EXPECT_TRUE(f.ble->advertising());
}

TEST(SetupModeTest, AWifiBackendThatCannotBeAskedMeansSetupMode) {
  SetupFixture f;
  f.wifi->fail(protocol::errors::wifi_unavailable, "no NetworkManager");

  f.start();
  EXPECT_TRUE(f.setup->active());
}

TEST(SetupModeTest, LeavesAfterTheTimeoutAndComesBackOnTheNextCheck) {
  SetupFixture f;
  f.start();
  ASSERT_TRUE(f.setup->active());

  f.advance(899s);
  EXPECT_TRUE(f.setup->active());
  f.advance(1s);
  EXPECT_FALSE(f.setup->active());
  EXPECT_FALSE(f.ble->started());
  EXPECT_FALSE(f.modulator->running());
  EXPECT_EQ(f.led->ends(), 1);

  // Still no WiFi a minute later: setup mode again, LED and all.
  f.advance(60s);
  EXPECT_TRUE(f.setup->active());
  EXPECT_EQ(f.led->begins(), 2);
  EXPECT_EQ(f.ble->starts(), 2);
}

TEST(SetupModeTest, AlwaysNeverLeaves) {
  setup::SetupMode::Options options;
  options.always = true;
  SetupFixture f(options);
  f.wifi->set_status(connected_to("home"));

  f.start();
  EXPECT_TRUE(f.setup->active()) << "connected or not";

  f.advance(2h);
  EXPECT_TRUE(f.setup->active());

  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  ASSERT_TRUE(f.request(connection, "{\"op\":\"finish\"}"));
  EXPECT_FALSE(f.ble->connected(connection)) << "finish still hangs up";
  EXPECT_TRUE(f.setup->active()) << "but setup mode stays on";
}

TEST(SetupModeTest, FiveRefusalsInARowPauseAdvertisingForAMinuteWhileTheLedGoesOn) {
  SetupFixture f;
  f.start();

  // Three on one connection (which is dropped), two on the next.
  auto first = f.connect();
  for (int i = 0; i < 3; ++i) f.authenticate(first, wrong_key());
  EXPECT_FALSE(f.ble->connected(first));
  EXPECT_TRUE(f.ble->advertising());

  auto second = f.connect();
  f.authenticate(second, wrong_key());
  EXPECT_TRUE(f.ble->advertising());
  f.authenticate(second, wrong_key());

  EXPECT_FALSE(f.ble->advertising());
  EXPECT_TRUE(f.setup->advertising_paused());
  EXPECT_TRUE(f.setup->active());
  EXPECT_TRUE(f.modulator->running()) << "the LED keeps blinking";
  EXPECT_EQ(f.connect(), 0u) << "nobody can connect while paused";

  f.advance(59s);
  EXPECT_FALSE(f.ble->advertising());
  f.advance(1s);
  EXPECT_TRUE(f.ble->advertising());
  EXPECT_FALSE(f.setup->advertising_paused());
  EXPECT_EQ(f.setup->consecutive_refusals(), 0);
}

TEST(SetupModeTest, AnAcceptedAttemptResetsTheRefusalCount) {
  SetupFixture f;
  f.start();

  auto connection = f.connect();
  f.authenticate(connection, wrong_key());
  f.authenticate(connection, wrong_key());
  EXPECT_EQ(f.setup->consecutive_refusals(), 2);

  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  EXPECT_EQ(f.setup->consecutive_refusals(), 0);
  f.ble->phone_disconnect(connection);
  f.drain();

  // Four more refusals do not reach five.
  connection = f.connect();
  for (int i = 0; i < 3; ++i) f.authenticate(connection, wrong_key());
  connection = f.connect();
  f.authenticate(connection, wrong_key());
  EXPECT_TRUE(f.ble->advertising());
}

TEST(SetupModeTest, StopLeavesSetupModeAndGivesTheLedBack) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_TRUE(f.ble->connected(connection));

  f.setup->stop();
  f.drain();

  EXPECT_FALSE(f.setup->active());
  EXPECT_FALSE(f.ble->started());
  EXPECT_FALSE(f.ble->connected(connection));
  EXPECT_EQ(f.setup->sessions(), 0u);
  EXPECT_EQ(f.led->ends(), 1);
  EXPECT_FALSE(f.led->on());

  // And nothing comes back later.
  f.advance(1h);
  EXPECT_FALSE(f.setup->active());
  EXPECT_EQ(f.timers->pending(), 0u);
}

TEST(SetupModeTest, APhoneThatHangsUpIsForgotten) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  EXPECT_EQ(f.setup->sessions(), 1u);

  f.ble->phone_disconnect(connection);
  f.drain();
  EXPECT_EQ(f.setup->sessions(), 0u);
  EXPECT_TRUE(f.setup->active());
}

// docs/protocol.md: Easy Connect listening stops when the connection closes...
TEST(SetupModeTest, EasyConnectStopsWhenThePhoneHangsUp) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  const auto answer = f.request(connection, "{\"op\":\"wifi.easyconnect\"}");
  ASSERT_TRUE(answer);
  EXPECT_NE(answer->find("\"uri\":\"DPP:"), std::string::npos) << *answer;
  ASSERT_TRUE(f.easyconnect->active());

  f.ble->phone_disconnect(connection);
  f.drain();
  EXPECT_FALSE(f.easyconnect->active());
}

// ...and when the session starts over, which is a new session on the same link.
TEST(SetupModeTest, EasyConnectStopsWhenTheSessionStartsOver) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  ASSERT_TRUE(f.request(connection, "{\"op\":\"wifi.easyconnect\"}"));
  ASSERT_TRUE(f.easyconnect->active());

  f.read(connection, ble::Characteristic::challenge);
  EXPECT_FALSE(f.easyconnect->active());
}

TEST(SetupModeTest, AnEasyConnectNetworkIsConfiguredAndShowsInStatus) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  ASSERT_TRUE(f.request(connection, "{\"op\":\"wifi.easyconnect\"}"));

  f.easyconnect->deliver(wifi::EasyConnectCredentials{"home", "correct horse", "psk"});
  f.drain();
  EXPECT_EQ(f.wifi->configured_ssid(), "home");

  const auto status = f.request(connection, "{\"op\":\"status\"}");
  ASSERT_TRUE(status);
  EXPECT_NE(status->find("\"ssid\":\"home\""), std::string::npos) << *status;
}

TEST(SetupModeTest, GoesOnWithoutTheLedWhenItCannotBeDriven) {
  SetupFixture f;
  f.led->fail_begin("permission denied");

  f.start();
  EXPECT_TRUE(f.setup->active());
  EXPECT_FALSE(f.modulator->running());
  EXPECT_TRUE(f.ble->advertising());
  EXPECT_TRUE(f.logger->contains("permission denied")) << f.logger->text();
}
