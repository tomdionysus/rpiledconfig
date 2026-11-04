//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
// The daemon as main.cpp wires it (setup mode, the sessions, BlueZ and NetworkManager
// over one bus connection) against the mock BlueZ and NetworkManager, with the test as
// the app: it reads Info and Challenge, writes Auth, checks cB, and then speaks the
// encrypted channel of docs/protocol.md through Request and Response, all as D-Bus calls
// BlueZ would make for a phone.

#include <gtest/gtest.h>

#include "ble/ble_bluez.h"
#include "dbus_test_helper.h"
#include "led/led_fake.h"
#include "led/led_modulator.h"
#include "machine_id/machine_id.h"
#include "mock_bluez.h"
#include "mock_networkmanager.h"
#include "mocks/logger_mock.h"
#include "protocol/auth.h"
#include "protocol/channel.h"
#include "protocol/crypto.h"
#include "protocol/fragments.h"
#include "protocol/json.h"
#include "protocol/keys.h"
#include "session/requests.h"
#include "setup/setup_mode.h"
#include "timers/timer_source.h"
#include "util.h"
#include "wifi/wifi_networkmanager.h"

using namespace rpiledconfig;
using namespace std::chrono_literals;

namespace {

using Bytes = types::Bytes;

const std::string info = "2a7f0002-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string challenge = "2a7f0003-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string auth = "2a7f0004-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string auth_result = "2a7f0005-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string request = "2a7f0006-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string response = "2a7f0007-7e1d-4c3a-9b5e-0f8a6d4c2b10";

const machine_id::MachineId machine = machine_id::MachineId::from_value(0x0123456789abcdefULL);

// The app's side of one connection.
class Phone {
 public:
  Phone(MockBluez& bluez, std::string device, std::uint16_t mtu) : _bluez(bluez), _device(std::move(device)), _mtu(mtu) {}

  // One attempt with the password scalar given, the whole of docs/protocol.md
  // "Authentication": the AuthResult the phone ends up with (empty when there was none).
  Bytes authenticate(const Bytes& password) {
    if (!_subscribed) {
      _bluez.start_notify(auth_result, _mtu);
      _bluez.start_notify(response, _mtu);
      _subscribed = true;
    }

    const auto p_b = _bluez.read_long(_device, challenge, _mtu);
    if (!p_b.ok || p_b.value.size() != protocol::spake2::share_length) return {};

    const auto app = protocol::app_auth(password, protocol::spake2::random_scalar(), p_b.value);
    if (!app) return {};

    _bluez.take_notifications(auth_result);
    if (!_bluez.write(_device, auth, app.value.write, _mtu).ok) return {};

    std::vector<Bytes> notified;
    if (!eventually([&]() {
          notified = _bluez.take_notifications(auth_result);
          return !notified.empty();
        })) {
      return {};
    }

    // At an MTU of 23 a notification carries 20 bytes and 0x01 || cB is 33: the
    // notification says something changed, and a read gets all of it.
    Bytes result = notified.front();
    if (!result.empty() && result[0] == protocol::auth_accepted && result.size() < 1 + 32) {
      const auto read = _bluez.read_long(_device, auth_result, _mtu);
      if (read.ok) result = read.value;
    }

    if (result.size() == 33 && result[0] == protocol::auth_accepted) {
      if (Bytes(result.begin() + 1, result.end()) == app.value.keys.cb) {
        const auto s = protocol::session_key(app.value.keys.ke);
        _to_daemon.emplace(s, protocol::Direction::app_to_daemon);
        _from_daemon.emplace(s, protocol::Direction::daemon_to_app);
        _verified = true;
      }
    }
    return result;
  }

  bool verified() const { return _verified; }

  // A request sealed, fragmented and written as the app would; the decrypted answer.
  std::optional<protocol::Json> ask(const std::string& json) {
    if (!_to_daemon) return std::nullopt;

    _bluez.take_notifications(response);
    for (const auto& piece : protocol::fragment(_to_daemon->seal(util::to_bytes(json)), _mtu - 3u)) {
      if (!_bluez.write(_device, request, piece, _mtu).ok) return std::nullopt;
    }

    protocol::Reassembler reassembler;
    std::optional<Bytes> message;
    eventually([&]() {
      for (const auto& fragment : _bluez.take_notifications(response)) {
        auto pushed = reassembler.push(fragment);
        if (pushed.ok && pushed.value) message = pushed.value;
      }
      return message.has_value();
    });
    if (!message) return std::nullopt;

    auto opened = _from_daemon->open(*message);
    if (!opened.ok) return std::nullopt;
    return protocol::Json::parse(util::to_string(opened.value));
  }

  const std::string& device() const { return _device; }

 private:
  MockBluez& _bluez;
  std::string _device;
  std::uint16_t _mtu;
  bool _subscribed = false;
  bool _verified = false;
  std::optional<protocol::Sealer> _to_daemon;
  std::optional<protocol::Opener> _from_daemon;
};

bool ok(const std::optional<protocol::Json>& answer) {
  if (!answer || !answer->is_object()) return false;
  const auto* field = answer->get("ok");
  return field && field->is_boolean() && field->as_boolean();
}

std::string text(const std::optional<protocol::Json>& answer) { return answer ? answer->dump() : "(no answer)"; }

class EndToEndTest : public ::testing::Test {
 protected:
  void SetUp() override {
    REQUIRE_TEST_BUS();

    bluez = std::make_unique<MockBluez>(test_bus_address());
    ASSERT_TRUE(bluez->start());
    nm = std::make_unique<MockNetworkManager>(test_bus_address(),
                                              std::vector<MockNetworkManager::AccessPoint>{{"Home", 70, 0x1, 0, 0x188}, {"Cafe", 40, 0, 0, 0}});
    ASSERT_TRUE(nm->start());

    daemon.run([this]() {
      loop = std::make_shared<dbus::SdBusLoop>(logger, daemon.executor(), test_bus_address());
      if (!loop->open()) return;

      led = std::make_shared<led::LedFake>([this]() { return timers->now(); });
      modulator = std::make_shared<led::LedModulator>(logger, daemon.executor(), led, timers, machine);
      ble = std::make_shared<ble::BleBluez>(logger, loop, "hci0");
      wifi = std::make_shared<wifi::WifiNetworkManager>(logger, loop);
      auto requests = std::make_shared<session::Requests>(logger, wifi, "testpi");

      setup::SetupMode::Identity identity;
      identity.id = machine;
      identity.w = protocol::password_scalar(machine);
      identity.fingerprint = protocol::fingerprint(machine);
      identity.version = "0.0.0-test";

      setup = std::make_shared<setup::SetupMode>(logger, daemon.executor(), timers, modulator, ble, wifi, requests, identity, setup::SetupMode::Options{});
      setup->start();
    });
    ASSERT_TRUE(setup) << logger->text();
    ASSERT_TRUE(eventually([this]() { return bluez->advertisement().has_value(); })) << logger->text();
  }

  void TearDown() override {
    if (setup) {
      daemon.run([this]() {
        setup->stop();
        setup.reset();
        ble.reset();
        wifi.reset();
        modulator.reset();
        loop->close();
      });
    }
    nm.reset();
    bluez.reset();
  }

  Phone connect(const std::string& address, std::uint16_t mtu) { return Phone(*bluez, bluez->connect_device(address), mtu); }

  void advance(std::chrono::milliseconds by) {
    daemon.run([&]() { timers->advance(by); });
  }

  Bytes key = protocol::password_scalar(machine);
  Bytes wrong_key = protocol::password_scalar(machine_id::MachineId::from_value(0x0badf00d0badf00dULL));

  std::shared_ptr<MockLogger> logger = std::make_shared<MockLogger>();
  std::shared_ptr<timers::ManualTimerSource> timers = std::make_shared<timers::ManualTimerSource>();
  DaemonThread daemon;
  std::unique_ptr<MockBluez> bluez;
  std::unique_ptr<MockNetworkManager> nm;
  std::shared_ptr<dbus::SdBusLoop> loop;
  std::shared_ptr<led::LedFake> led;
  std::shared_ptr<led::LedModulator> modulator;
  std::shared_ptr<ble::BleBluez> ble;
  std::shared_ptr<wifi::WifiNetworkManager> wifi;
  std::shared_ptr<setup::SetupMode> setup;
};

}  // namespace

TEST_F(EndToEndTest, AnAppConfiguresWifiAndFinishes) {
  auto phone = connect("AA:BB:CC:DD:EE:01", 185);

  // 1. Info, and the fingerprint the LED's ID gives.
  const auto info_read = bluez->read_long(phone.device(), info, 185);
  ASSERT_TRUE(info_read.ok) << info_read.error;
  const auto info_json = protocol::Json::parse(util::to_string(info_read.value));
  ASSERT_TRUE(info_json.has_value());
  EXPECT_EQ(info_json->get("fingerprint")->as_string(), protocol::fingerprint(machine));
  EXPECT_EQ(info_json->get("protocol")->as_number(), 2);

  // 2 to 5.
  const auto result = phone.authenticate(key);
  ASSERT_EQ(result.size(), 33u);
  EXPECT_EQ(result[0], protocol::auth_accepted);
  ASSERT_TRUE(phone.verified()) << "cB verifies";

  auto status = phone.ask(R"({"op":"status"})");
  ASSERT_TRUE(ok(status)) << text(status);
  EXPECT_EQ(status->get("hostname")->as_string(), "testpi");
  EXPECT_FALSE(status->get("wifi")->get("connected")->as_boolean());

  const auto scan = phone.ask(R"({"op":"wifi.scan"})");
  ASSERT_TRUE(ok(scan)) << text(scan);
  ASSERT_EQ(scan->get("networks")->items().size(), 2u);
  EXPECT_EQ(scan->get("networks")->items()[0].get("ssid")->as_string(), "Home") << "strongest first";
  EXPECT_TRUE(scan->get("networks")->items()[0].get("secure")->as_boolean());
  EXPECT_FALSE(scan->get("networks")->items()[1].get("secure")->as_boolean());

  const auto configured = phone.ask(R"({"op":"wifi.configure","ssid":"Home","psk":"correct horse"})");
  ASSERT_TRUE(ok(configured)) << text(configured);
  const auto saved = nm->connections();
  ASSERT_EQ(saved.size(), 1u);
  EXPECT_EQ(saved[0].ssid, "Home");
  EXPECT_EQ(saved[0].psk, "correct horse");

  ASSERT_TRUE(eventually([&]() {
    status = phone.ask(R"({"op":"status"})");
    return ok(status) && status->get("wifi")->get("connected")->as_boolean();
  })) << text(status);
  EXPECT_EQ(status->get("wifi")->get("ssid")->as_string(), "Home");
  EXPECT_EQ(status->get("wifi")->get("ip")->as_string(), "192.0.2.50");

  const auto forgotten = phone.ask(R"({"op":"wifi.forget"})");
  ASSERT_TRUE(ok(forgotten)) << text(forgotten);
  EXPECT_TRUE(nm->connections().empty());

  ASSERT_TRUE(ok(phone.ask(R"({"op":"wifi.configure","ssid":"Home","psk":"correct horse"})")));

  const auto finished = phone.ask(R"({"op":"finish"})");
  ASSERT_TRUE(ok(finished)) << text(finished);

  // The daemon leaves setup mode and hangs up.
  EXPECT_TRUE(eventually([&]() { return bluez->disconnect_calls(phone.device()) >= 1; }));
  EXPECT_TRUE(eventually([&]() { return !bluez->advertisement().has_value() && !bluez->application().has_value(); }));
  EXPECT_FALSE(daemon.run([this]() { return setup->active(); }));
  EXPECT_FALSE(daemon.run([this]() { return modulator->running(); }));
}

// Everything but the Auth write: that is 97 bytes and needs an MTU of 100 on a real link
// (docs/protocol.md), which the mock does not model; reads and responses are what this
// checks.
TEST_F(EndToEndTest, AtTheDefaultMtuEverythingFitsInTwentyByteFragments) {
  auto phone = connect("AA:BB:CC:DD:EE:01", 23);

  const auto info_read = bluez->read_long(phone.device(), info, 23);
  ASSERT_TRUE(info_read.ok);
  EXPECT_GT(info_read.value.size(), 22u) << "a long read, in pieces";
  EXPECT_TRUE(protocol::Json::parse(util::to_string(info_read.value)).has_value());

  const auto result = phone.authenticate(key);
  ASSERT_EQ(result.size(), 33u);
  ASSERT_TRUE(phone.verified());

  const auto scan = phone.ask(R"({"op":"wifi.scan"})");
  ASSERT_TRUE(ok(scan)) << text(scan);
  EXPECT_EQ(scan->get("networks")->items().size(), 2u);
}

TEST_F(EndToEndTest, AnUnknownOpIsAnsweredUnknownOp) {
  auto phone = connect("AA:BB:CC:DD:EE:01", 185);
  ASSERT_EQ(phone.authenticate(key)[0], protocol::auth_accepted);

  const auto answer = phone.ask(R"({"op":"wifi.teleport"})");
  ASSERT_TRUE(answer.has_value());
  EXPECT_FALSE(ok(answer));
  EXPECT_EQ(answer->get("error")->as_string(), "unknown_op");
}

TEST_F(EndToEndTest, ThreeRefusalsDropTheConnection) {
  auto phone = connect("AA:BB:CC:DD:EE:01", 185);

  for (int attempt = 1; attempt <= 3; ++attempt) {
    const auto result = phone.authenticate(wrong_key);
    ASSERT_EQ(result, Bytes{protocol::auth_refused}) << "attempt " << attempt;
  }

  EXPECT_TRUE(eventually([&]() { return bluez->disconnect_calls(phone.device()) == 1; })) << logger->text();
  EXPECT_FALSE(bluez->connected(phone.device()));
  EXPECT_TRUE(bluez->advertisement().has_value()) << "three is not five";
}

TEST_F(EndToEndTest, FiveRefusalsInARowPauseAdvertisingForAMinute) {
  auto first = connect("AA:BB:CC:DD:EE:01", 185);
  for (int i = 0; i < 3; ++i) ASSERT_EQ(first.authenticate(wrong_key), Bytes{protocol::auth_refused});
  ASSERT_TRUE(eventually([&]() { return !bluez->connected(first.device()); }));

  auto second = connect("AA:BB:CC:DD:EE:02", 185);
  ASSERT_TRUE(eventually([&]() { return bluez->read(second.device(), info).ok; })) << "the first is gone, so the second is served";
  ASSERT_EQ(second.authenticate(wrong_key), Bytes{protocol::auth_refused});
  ASSERT_TRUE(bluez->advertisement().has_value());
  ASSERT_EQ(second.authenticate(wrong_key), Bytes{protocol::auth_refused});

  EXPECT_TRUE(eventually([&]() { return !bluez->advertisement().has_value(); })) << "five in a row";
  EXPECT_EQ(bluez->advertisement_unregistrations(), 1);
  EXPECT_TRUE(bluez->application().has_value());
  EXPECT_TRUE(daemon.run([this]() { return modulator->running(); })) << "the LED keeps blinking";

  advance(59s);
  EXPECT_FALSE(bluez->advertisement().has_value());
  advance(1s);
  EXPECT_TRUE(eventually([&]() { return bluez->advertisement().has_value(); }));
  EXPECT_EQ(bluez->advertisement_registrations(), 2);
}

TEST_F(EndToEndTest, ARequestBeforeAuthenticationDropsTheConnection) {
  auto phone = connect("AA:BB:CC:DD:EE:01", 185);
  ASSERT_TRUE(bluez->read(phone.device(), info).ok);

  ASSERT_TRUE(bluez->write(phone.device(), request, Bytes{0x01, 0x00}, 185).ok);
  EXPECT_TRUE(eventually([&]() { return bluez->disconnect_calls(phone.device()) == 1; })) << logger->text();
}

TEST_F(EndToEndTest, AConnectionThatDoesNotAuthenticateIn30SecondsIsDropped) {
  auto phone = connect("AA:BB:CC:DD:EE:01", 185);
  ASSERT_TRUE(bluez->read(phone.device(), challenge).ok);

  advance(29s);
  EXPECT_EQ(bluez->disconnect_calls(phone.device()), 0);
  advance(1s);
  EXPECT_TRUE(eventually([&]() { return bluez->disconnect_calls(phone.device()) == 1; }));
}

// docs/protocol.md: an app that starts again on a link the phone's system kept open reads
// Challenge first, and that starts the session over.
TEST_F(EndToEndTest, AnAppStartingAgainOnAnAuthenticatedLinkStartsOver) {
  auto first_run = connect("AA:BB:CC:DD:EE:01", 185);
  ASSERT_EQ(first_run.authenticate(key)[0], protocol::auth_accepted);
  ASSERT_TRUE(ok(first_run.ask(R"({"op":"status"})")));

  // The same device, a new app run: its own x and keys.
  Phone second_run(*bluez, first_run.device(), 185);
  const auto result = second_run.authenticate(key);
  ASSERT_EQ(result.size(), 33u);
  ASSERT_TRUE(second_run.verified());
  EXPECT_TRUE(ok(second_run.ask(R"({"op":"status"})")));
  EXPECT_EQ(bluez->disconnect_calls(first_run.device()), 0) << "the link is kept";
}

TEST_F(EndToEndTest, ASecondCentralIsDisconnectedWhileTheFirstGoesOn) {
  auto first = connect("AA:BB:CC:DD:EE:01", 185);
  ASSERT_EQ(first.authenticate(key)[0], protocol::auth_accepted);

  auto second = connect("AA:BB:CC:DD:EE:02", 185);
  EXPECT_FALSE(bluez->read(second.device(), challenge).ok);
  EXPECT_TRUE(eventually([&]() { return bluez->disconnect_calls(second.device()) == 1; }));

  EXPECT_TRUE(ok(first.ask(R"({"op":"status"})")));
}
