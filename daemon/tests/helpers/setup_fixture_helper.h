//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <boost/asio/io_context.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ble/ble_fake.h"
#include "led/led_fake.h"
#include "led/led_modulator.h"
#include "machine_id/machine_id.h"
#include "mocks/logger_mock.h"
#include "protocol/auth.h"
#include "protocol/channel.h"
#include "protocol/crypto.h"
#include "protocol/fragments.h"
#include "protocol/keys.h"
#include "session/requests.h"
#include "setup/setup_mode.h"
#include "system/uninstaller_fake.h"
#include "timers/timer_source.h"
#include "util.h"
#include "wifi/easy_connect_fake.h"
#include "wifi/wifi_fake.h"

// The daemon's setup mode on fakes, with the test as the phone. Everything runs on the
// test's own io_context, which the test drains between steps, and on a manual clock it
// advances: nothing here waits for real time.
struct SetupFixture {
  using Characteristic = rpiledconfig::ble::Characteristic;
  using ConnectionId = rpiledconfig::ble::ConnectionId;
  using Bytes = rpiledconfig::types::Bytes;

  boost::asio::io_context io;
  std::shared_ptr<MockLogger> logger = std::make_shared<MockLogger>();
  std::shared_ptr<rpiledconfig::timers::ManualTimerSource> timers = std::make_shared<rpiledconfig::timers::ManualTimerSource>();
  rpiledconfig::machine_id::MachineId id = rpiledconfig::machine_id::MachineId::from_value(0x0123456789abcdefULL);
  Bytes w = rpiledconfig::protocol::password_scalar(id);
  std::string fingerprint = rpiledconfig::protocol::fingerprint(id);

  std::shared_ptr<rpiledconfig::led::LedFake> led;
  std::shared_ptr<rpiledconfig::led::LedModulator> modulator;
  std::shared_ptr<rpiledconfig::ble::BleFake> ble;
  std::shared_ptr<rpiledconfig::wifi::WifiFake> wifi;
  std::shared_ptr<rpiledconfig::wifi::EasyConnectFake> easyconnect;
  std::shared_ptr<rpiledconfig::system::UninstallerFake> uninstaller;
  std::shared_ptr<rpiledconfig::session::Requests> requests;
  std::shared_ptr<rpiledconfig::setup::SetupMode> setup;

  explicit SetupFixture(rpiledconfig::setup::SetupMode::Options options = {}) {
    led = std::make_shared<rpiledconfig::led::LedFake>([this]() { return timers->now(); });
    modulator = std::make_shared<rpiledconfig::led::LedModulator>(logger, io.get_executor(), led, timers, id);
    ble = std::make_shared<rpiledconfig::ble::BleFake>(logger, io.get_executor());
    wifi = std::make_shared<rpiledconfig::wifi::WifiFake>(logger);
    easyconnect = std::make_shared<rpiledconfig::wifi::EasyConnectFake>(logger);
    uninstaller = std::make_shared<rpiledconfig::system::UninstallerFake>(logger);
    requests = std::make_shared<rpiledconfig::session::Requests>(logger, wifi, "testpi", easyconnect, uninstaller);

    rpiledconfig::setup::SetupMode::Identity identity;
    identity.id = id;
    identity.w = w;
    identity.fingerprint = fingerprint;
    identity.version = "0.0.0-test";

    setup = std::make_shared<rpiledconfig::setup::SetupMode>(logger, io.get_executor(), timers, modulator, ble, wifi, requests, identity, options);
  }

  ~SetupFixture() {
    setup->stop();
    drain();
  }

  // Runs everything that has been posted, and everything that posts.
  void drain() {
    io.restart();
    io.run();
  }

  void advance(std::chrono::milliseconds by) {
    timers->advance(by);
    drain();
  }

  void start() {
    setup->start();
    drain();
  }

  // --- The phone -----------------------------------------------------------------------

  ConnectionId connect(std::size_t mtu = rpiledconfig::ble::default_mtu) {
    const auto connection = ble->phone_connect(mtu);
    drain();
    return connection;
  }

  // A write of Blink (docs/protocol.md): 0x01 to blink, 0x00 to stop.
  void blink(ConnectionId connection, std::uint8_t value = 0x01) { write(connection, Characteristic::blink, Bytes{value}); }

  Bytes read(ConnectionId connection, Characteristic characteristic) { return ble->phone_read(connection, characteristic); }

  void write(ConnectionId connection, Characteristic characteristic, Bytes value) {
    ble->phone_write(connection, characteristic, std::move(value));
    drain();
  }

  // One attempt with the password scalar given, as the app makes it: reads pB from
  // Challenge, writes pA || cA, and gives back what AuthResult was notified (empty when
  // nothing was). On an accepted attempt whose cB verifies, the phone's channel is set up.
  Bytes authenticate(ConnectionId connection, const Bytes& password) {
    const auto p_b = read(connection, Characteristic::challenge);
    const auto app = rpiledconfig::protocol::app_auth(password, rpiledconfig::protocol::spake2::random_scalar(), p_b);
    if (!app) return {};

    ble->take_notifications(connection);
    write(connection, Characteristic::auth, app.value.write);

    for (const auto& notification : ble->take_notifications(connection)) {
      if (notification.characteristic == Characteristic::auth_result) {
        const auto& value = notification.value;
        if (value.size() == 33 && value[0] == rpiledconfig::protocol::auth_accepted && Bytes(value.begin() + 1, value.end()) == app.value.keys.cb) {
          phones[connection] = std::make_shared<Phone>(rpiledconfig::protocol::session_key(app.value.keys.ke));
          last_p_b = p_b;
          last_keys = app.value.keys;
        }
        return value;
      }
    }

    return {};
  }

  // The phone's end of the channel after an accepted attempt.
  struct Phone {
    explicit Phone(const Bytes& session_key)
        : to_daemon(session_key, rpiledconfig::protocol::Direction::app_to_daemon),
          from_daemon(session_key, rpiledconfig::protocol::Direction::daemon_to_app) {}

    rpiledconfig::protocol::Sealer to_daemon;
    rpiledconfig::protocol::Opener from_daemon;
  };

  std::map<ConnectionId, std::shared_ptr<Phone>> phones;
  Bytes last_p_b;
  rpiledconfig::protocol::spake2::Keys last_keys;

  // Seals and fragments a request the way the app would, writes it, and gives back the
  // decrypted response (nothing when none came).
  std::optional<std::string> request(ConnectionId connection, const std::string& json) {
    auto phone = phones.at(connection);
    return send_wire(connection, phone->to_daemon.seal(rpiledconfig::util::to_bytes(json)));
  }

  std::optional<std::string> send_wire(ConnectionId connection, const Bytes& wire) {
    const auto fragment_size = ble->mtu(connection) - 3;
    ble->take_notifications(connection);

    for (const auto& piece : rpiledconfig::protocol::fragment(wire, fragment_size)) write(connection, Characteristic::request, piece);

    return response(connection);
  }

  std::optional<std::string> response(ConnectionId connection) {
    auto phone = phones.at(connection);
    rpiledconfig::protocol::Reassembler reassembler;

    for (const auto& notification : ble->take_notifications(connection)) {
      if (notification.characteristic != Characteristic::response) continue;

      auto pushed = reassembler.push(notification.value);
      if (!pushed || !pushed.value) continue;

      auto opened = phone->from_daemon.open(*pushed.value);
      if (!opened) return std::nullopt;
      return rpiledconfig::util::to_string(opened.value);
    }

    return std::nullopt;
  }
};
