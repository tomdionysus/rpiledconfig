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

types::Bytes wrong_key() { return protocol::password_scalar(pairing_key::PairingKey::from_value(0xdeadbeefULL)); }

}  // namespace

TEST(SetupModeTest, EntersAtStartWhenThereIsNoWifi) {
  SetupFixture f;
  EXPECT_FALSE(f.setup->active());

  f.start();

  EXPECT_TRUE(f.setup->active());
  EXPECT_TRUE(f.ble->advertising());
  EXPECT_EQ(f.ble->advertised_name(), f.ble_name);
  EXPECT_FALSE(f.modulator->running()) << "the LED waits for a phone to ask";
  EXPECT_EQ(f.led->begins(), 0);
}

// docs/protocol.md, Blink: anyone connected may ask, and the blinking outlives the link.
TEST(SetupModeTest, TheLedBlinksWhenAPhoneAsksAndGoesOnAfterItHangsUp) {
  SetupFixture f;
  f.start();

  const auto connection = f.connect();
  f.blink(connection);
  EXPECT_TRUE(f.setup->blinking());
  EXPECT_TRUE(f.modulator->running());
  EXPECT_EQ(f.led->begins(), 1);
  EXPECT_TRUE(f.logger->contains("LED frame 1 starts")) << f.logger->text();

  f.ble->phone_disconnect(connection);
  f.drain();
  EXPECT_TRUE(f.modulator->running());
}

TEST(SetupModeTest, AnAcceptedAuthenticationStopsTheBlinkingAndGivesTheLedBack) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  f.blink(connection);

  // A refusal does not: the phone may have misread, and reads again.
  f.authenticate(connection, wrong_key());
  EXPECT_TRUE(f.modulator->running());

  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  EXPECT_FALSE(f.setup->blinking());
  EXPECT_FALSE(f.modulator->running());
  EXPECT_EQ(f.led->ends(), 1);
  EXPECT_FALSE(f.led->on());
}

// docs/protocol.md, The pairing key.
TEST(SetupModeTest, ThereIsNoKeyUntilAPhoneConnects) {
  SetupFixture f;
  f.start();
  EXPECT_FALSE(f.setup->has_key());
  EXPECT_EQ(f.draws, 0);
  EXPECT_EQ(f.ble->advertised_name(), f.ble_name) << "advertising needs no key";

  f.connect();
  EXPECT_TRUE(f.setup->has_key());
  EXPECT_EQ(f.draws, 1);
}

TEST(SetupModeTest, TheKeyIsNeverInTheLog) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  f.blink(connection);
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  EXPECT_FALSE(f.logger->contains(f.id.to_hex())) << f.logger->text();
}

// docs/protocol.md, The pairing key: nothing said before authentication depends on the key.
TEST(SetupModeTest, TheNameAndInfoAreTheSameWhateverTheKey) {
  SetupFixture f;
  f.start();
  auto connection = f.connect();
  const auto name = f.ble->advertised_name();
  const auto info = f.read(connection, ble::Characteristic::info);
  f.ble->phone_disconnect(connection);
  f.drain();
  f.advance(120s);
  ASSERT_FALSE(f.setup->has_key());

  f.next_key(0xfedcba9876543210ULL);
  connection = f.connect();
  EXPECT_EQ(f.draws, 2);
  EXPECT_EQ(f.ble->advertised_name(), name);
  EXPECT_EQ(f.read(connection, ble::Characteristic::info), info);
}

// What the app does: Blink, hang up, read the LED, come back and authenticate.
TEST(SetupModeTest, TheKeyOutlivesTheConnectionThatAskedForTheBlink) {
  SetupFixture f;
  f.start();
  const auto first = f.connect();
  f.blink(first);
  f.ble->phone_disconnect(first);
  f.drain();

  f.next_key(0x1);  // a redraw would give this, and the phone read the first
  f.advance(10s);
  const auto first_w = protocol::password_scalar(pairing_key::PairingKey::from_value(0x0123456789abcdefULL));
  const auto second = f.connect();
  EXPECT_EQ(f.draws, 1);
  EXPECT_EQ(f.authenticate(second, first_w)[0], protocol::auth_accepted);
}

// A WiFi join can drop the link, and the app authenticates again with the key it has.
TEST(SetupModeTest, TheKeySurvivesTheLinkDroppingAfterAuthentication) {
  SetupFixture f;
  f.start();
  auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  f.ble->phone_disconnect(connection);
  f.drain();

  f.advance(119s);
  connection = f.connect();
  EXPECT_EQ(f.draws, 1);
  EXPECT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
}

TEST(SetupModeTest, TheKeyGoesAfterTwoMinutesWithNoPhoneAndNoBlinking) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  f.ble->phone_disconnect(connection);
  f.drain();

  f.advance(119s);
  EXPECT_TRUE(f.setup->has_key());
  f.advance(1s);
  EXPECT_FALSE(f.setup->has_key());

  const auto old_w = f.w;
  f.next_key(0x2);
  const auto again = f.connect();
  EXPECT_EQ(f.draws, 2);
  EXPECT_EQ(f.authenticate(again, old_w)[0], protocol::auth_refused) << "the old key is no use";
  EXPECT_EQ(f.authenticate(again, f.w)[0], protocol::auth_accepted);
}

TEST(SetupModeTest, TheKeyStaysWhileAPhoneIsConnected) {
  setup::SetupMode::Options options;
  options.always = true;
  SetupFixture f(options);
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  f.advance(1h);
  EXPECT_TRUE(f.ble->connected(connection));
  EXPECT_TRUE(f.setup->has_key());
}

// The LED is showing it, and a phone may be reading it right now.
TEST(SetupModeTest, TheKeyStaysWhileTheLedBlinksAndForTwoMinutesAfter) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  f.blink(connection);
  f.ble->phone_disconnect(connection);
  f.drain();

  f.advance(120s);
  EXPECT_FALSE(f.setup->blinking());
  EXPECT_TRUE(f.setup->has_key());
  f.advance(119s);
  EXPECT_TRUE(f.setup->has_key());
  f.advance(1s);
  EXPECT_FALSE(f.setup->has_key());
}

TEST(SetupModeTest, ARefusalDoesNotDiscardTheKey) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  f.authenticate(connection, wrong_key());
  EXPECT_TRUE(f.setup->has_key());
  EXPECT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
}

TEST(SetupModeTest, FinishDiscardsTheKeyWithSetupAlwaysOn) {
  setup::SetupMode::Options options;
  options.always = true;
  SetupFixture f(options);
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  ASSERT_TRUE(f.request(connection, "{\"op\":\"finish\"}"));
  EXPECT_FALSE(f.setup->has_key());
  f.drain();

  f.next_key(0x3);
  f.connect();
  EXPECT_EQ(f.draws, 2);
  EXPECT_TRUE(f.setup->has_key());
}

TEST(SetupModeTest, LeavingSetupModeDiscardsTheKey) {
  setup::SetupMode::Options options;
  options.timeout = 60s;
  SetupFixture f(options);
  f.start();
  f.connect();
  ASSERT_TRUE(f.setup->has_key());

  f.advance(60s);
  EXPECT_FALSE(f.setup->active());
  EXPECT_FALSE(f.setup->has_key());
}

TEST(SetupModeTest, AConnectionIsDroppedWhenNoKeyCanBeDrawn) {
  SetupFixture f;
  f.fail_draw = true;
  f.start();
  const auto connection = f.connect();
  EXPECT_FALSE(f.ble->connected(connection));
  EXPECT_FALSE(f.setup->has_key());
  EXPECT_TRUE(f.logger->contains("Cannot draw a pairing key")) << f.logger->text();
}

// The frames carry the key this pairing drew, not the one before it.
TEST(SetupModeTest, TheLedBlinksTheKeyOfThisPairing) {
  SetupFixture f;
  f.start();
  f.next_key(0x4);
  const auto connection = f.connect();
  f.blink(connection);

  const auto runs = led::frame_runs(f.id);
  EXPECT_EQ(f.led->begins(), 1);
  ASSERT_FALSE(runs.empty());
  std::vector<led::Run> seen;
  for (std::size_t i = 0; i < runs.size(); ++i) {
    seen.push_back({f.led->on(), runs[i].length});
    f.advance(led::symbol_time * runs[i].length);
  }
  EXPECT_EQ(seen, runs);
}

// docs/protocol.md, Symbols and Blink: the phone chooses the rate.
TEST(SetupModeTest, TheByteWrittenToBlinkChoosesTheRate) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();

  f.blink(connection, 0x02);
  EXPECT_EQ(f.modulator->symbol(), 33ms);
  f.blink(connection, 0x03);
  EXPECT_EQ(f.modulator->symbol(), 77ms);
  f.blink(connection, 0x01);
  EXPECT_EQ(f.modulator->symbol(), 51ms);
  EXPECT_TRUE(f.modulator->running());
}

TEST(SetupModeTest, AskingForTheSameRateKeepsTheFramesAndAnotherStartsThemAgain) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();

  f.blink(connection, 0x01);
  f.blink(connection, 0x01);
  EXPECT_EQ(f.led->begins(), 1);

  f.blink(connection, 0x03);
  EXPECT_EQ(f.led->begins(), 2);
  EXPECT_EQ(f.draws, 1) << "the same key at the new rate";
}

TEST(SetupModeTest, TheFramesRunAtTheRateAskedFor) {
  SetupFixture f;
  f.start();
  f.blink(f.connect(), 0x03);

  const auto started = f.modulator->frames_started();
  f.advance(77ms * led::frame_symbols() - 1ms);
  EXPECT_EQ(f.modulator->frames_started(), started);
  f.advance(1ms);
  EXPECT_EQ(f.modulator->frames_started(), started + 1);
}

TEST(SetupModeTest, ZeroStopsTheBlinkingAndAnythingElseIsIgnored) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();

  f.write(connection, ble::Characteristic::blink, {0x04});
  f.write(connection, ble::Characteristic::blink, {0x01, 0x01});
  f.write(connection, ble::Characteristic::blink, {});
  EXPECT_FALSE(f.modulator->running());

  f.blink(connection);
  f.write(connection, ble::Characteristic::blink, {0x07});
  EXPECT_TRUE(f.modulator->running());

  f.blink(connection, 0x00);
  EXPECT_FALSE(f.modulator->running());
  EXPECT_EQ(f.led->ends(), 1);

  // And again.
  f.blink(connection);
  EXPECT_TRUE(f.modulator->running());
  EXPECT_EQ(f.led->begins(), 2);
}

TEST(SetupModeTest, TheBlinkingStopsTwoMinutesAfterTheLastAsk) {
  SetupFixture f;
  f.start();
  f.blink(f.connect());

  // A connection that has not authenticated is dropped after 30 s; the ask comes on another.
  f.advance(100s);
  f.blink(f.connect());
  EXPECT_EQ(f.led->begins(), 1) << "asking again restarts the clock, not the LED";

  f.advance(119s);
  EXPECT_TRUE(f.modulator->running());
  f.advance(1s);
  EXPECT_FALSE(f.modulator->running());
  EXPECT_FALSE(f.setup->blinking());
  EXPECT_TRUE(f.setup->active()) << "setup mode goes on";
  EXPECT_TRUE(f.ble->advertising());
}

TEST(SetupModeTest, TheBlinkingStopsWhenSetupModeEnds) {
  setup::SetupMode::Options options;
  options.timeout = 60s;
  SetupFixture f(options);
  f.start();
  f.blink(f.connect());

  f.advance(60s);
  EXPECT_FALSE(f.setup->active());
  EXPECT_FALSE(f.modulator->running());
  EXPECT_EQ(f.led->ends(), 1);
}

// docs/protocol.md, Uninstalling: answered, then the link goes.
TEST(SetupModeTest, UninstallIsAnsweredAndHangsUp) {
  SetupFixture f;
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  const auto answer = f.request(connection, "{\"op\":\"uninstall\"}");
  ASSERT_TRUE(answer);
  EXPECT_EQ(*answer, "{\"ok\":true}");
  EXPECT_EQ(f.uninstaller->starts(), 1);
  EXPECT_FALSE(f.ble->connected(connection));
}

// docs/protocol.md, Uninstalling: nothing is advertised in the seconds before the daemon stops.
TEST(SetupModeTest, AnAcceptedUninstallStopsAdvertisingAtOnce) {
  setup::SetupMode::Options options;
  options.always = true;
  SetupFixture f(options);
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  ASSERT_TRUE(f.request(connection, "{\"op\":\"uninstall\"}"));

  EXPECT_FALSE(f.ble->advertising());
  EXPECT_FALSE(f.setup->active());
  EXPECT_FALSE(f.setup->has_key());
}

// The removal did not stop the daemon: the Pi is not left unreachable for good.
TEST(SetupModeTest, SetupModeComesBackIfTheRemovalNeverStopsTheDaemon) {
  setup::SetupMode::Options options;
  options.always = true;
  SetupFixture f(options);
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);
  ASSERT_TRUE(f.request(connection, "{\"op\":\"uninstall\"}"));
  ASSERT_FALSE(f.ble->advertising());

  f.advance(60s);
  EXPECT_TRUE(f.setup->active());
  EXPECT_TRUE(f.ble->advertising());
}

TEST(SetupModeTest, AnUninstallThatCannotStartIsReportedAndTheLinkStays) {
  SetupFixture f;
  f.uninstaller->fail_start("NoSuchUnit");
  f.start();
  const auto connection = f.connect();
  ASSERT_EQ(f.authenticate(connection, f.w)[0], protocol::auth_accepted);

  const auto answer = f.request(connection, "{\"op\":\"uninstall\"}");
  ASSERT_TRUE(answer);
  EXPECT_NE(answer->find("\"error\":\"uninstall_unavailable\""), std::string::npos) << *answer;
  EXPECT_TRUE(f.ble->connected(connection));
  EXPECT_TRUE(f.ble->advertising());
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

  // Still no WiFi a minute later: setup mode again.
  f.advance(60s);
  EXPECT_TRUE(f.setup->active());
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
  f.blink(first);
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
  EXPECT_TRUE(f.modulator->running()) << "a blink asked for carries on";
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
  f.blink(connection);

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
  const auto connection = f.connect();
  f.blink(connection);
  EXPECT_TRUE(f.setup->active());
  EXPECT_FALSE(f.modulator->running());
  EXPECT_TRUE(f.ble->connected(connection));
  EXPECT_TRUE(f.ble->advertising());
  EXPECT_TRUE(f.logger->contains("permission denied")) << f.logger->text();
}
