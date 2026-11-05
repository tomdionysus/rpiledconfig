//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "helpers/setup_fixture_helper.h"
#include "protocol/json.h"

// A stranger with a radio, driven by arbitrary bytes: connects, hangs up, reads and writes
// any characteristic with anything, lets time pass, guesses the key. Everything below the
// radio is the daemon's real code on fakes. The same run serves the test suite, from random
// bytes, and the fuzzers, from bytes chosen for coverage.
//
// Crashes and memory errors are what the sanitizers catch. What this checks itself is what no
// sanitizer can: that nothing privileged happens without the key.
namespace attacker {

class Bytes {
 public:
  Bytes(const std::uint8_t* data, std::size_t size) : _data(data), _size(size) {}

  bool empty() const { return _at >= _size; }
  std::uint8_t byte() { return empty() ? 0 : _data[_at++]; }

  rpiledconfig::types::Bytes take(std::size_t count) {
    count = std::min(count, _size - _at);
    rpiledconfig::types::Bytes out(_data + _at, _data + _at + count);
    _at += count;
    return out;
  }

 private:
  const std::uint8_t* _data;
  std::size_t _size;
  std::size_t _at = 0;
};

inline rpiledconfig::ble::Characteristic characteristic(std::uint8_t value) { return static_cast<rpiledconfig::ble::Characteristic>(value % 7); }

// `violated` is told of anything that must never happen.
inline void run(const std::uint8_t* data, std::size_t size, const std::function<void(const std::string&)>& violated) {
  using namespace rpiledconfig;
  using namespace std::chrono_literals;

  Bytes in(data, size);

  setup::SetupMode::Options options;
  options.always = (in.byte() & 1) != 0;
  SetupFixture f(options);
  f.start();

  // Whoever holds the key may do all of this; nobody else may do any of it.
  bool key_proved = false;
  ble::ConnectionId connection = 0;
  std::vector<ble::ConnectionId> connections;

  for (int step = 0; step < 48 && !in.empty(); ++step) {
    switch (in.byte() % 11) {
      case 0: {
        // Any MTU at all, below what ATT allows included: it is the other end's word.
        const std::size_t mtu = in.byte() * 2u + (in.byte() & 1u);
        const auto made = f.connect(mtu);
        if (made != 0) {
          connection = made;
          connections.push_back(made);
        }
        break;
      }
      case 1:
        if (connection != 0 && f.ble->connected(connection)) {
          f.ble->phone_disconnect(connection);
          f.drain();
        }
        break;
      case 2:
        if (connection != 0 && f.ble->connected(connection)) f.read(connection, characteristic(in.byte()));
        break;
      case 3:
        if (connection != 0 && f.ble->connected(connection)) {
          const auto which = characteristic(in.byte());
          f.write(connection, which, in.take(in.byte() * 2u));
        }
        break;
      case 4:
        if (connection != 0 && f.ble->connected(connection)) f.write(connection, ble::Characteristic::blink, in.take(in.byte() % 3));
        break;
      case 5:
        f.advance(std::chrono::seconds(in.byte()) * 2);
        break;
      case 6: {
        // A guess: anything, as a key.
        if (connection == 0 || !f.ble->connected(connection)) break;
        auto guess = in.take(8);
        guess.resize(8);
        pairing_key::PairingKey key;
        std::copy(guess.begin(), guess.end(), key.bytes.begin());
        if (key == f.id) break;
        const auto answer = f.authenticate(connection, protocol::password_scalar(key));
        if (!answer.empty() && answer[0] == protocol::auth_accepted) violated("a wrong key was accepted");
        break;
      }
      case 7:
        // The phone that read the LED: from here on the attacker holds the key too, and the
        // parsers behind authentication are reachable.
        if (connection != 0 && f.ble->connected(connection)) {
          const auto answer = f.authenticate(connection, f.w);
          if (!answer.empty() && answer[0] == protocol::auth_accepted) key_proved = true;
        }
        break;
      case 8:
        if (connection != 0 && f.ble->connected(connection) && f.phones.count(connection) != 0) {
          const auto text = in.take(in.byte() * 2u);
          f.request(connection, std::string(text.begin(), text.end()));
        }
        break;
      case 9:
        if (connection != 0 && f.ble->connected(connection) && f.phones.count(connection) != 0) {
          static const char* const ops[] = {"status", "wifi.scan", "wifi.configure", "wifi.forget", "wifi.easyconnect", "wifi.easyconnect.stop", "finish", "uninstall"};
          const auto value = in.take(in.byte() % 64);
          std::string json = "{\"op\":\"" + std::string(ops[in.byte() % 8]) + "\",\"ssid\":";
          json += protocol::json_quote(std::string(value.begin(), value.end()));
          json += ",\"psk\":" + protocol::json_quote(std::string(value.rbegin(), value.rend())) + ",\"hidden\":" + ((in.byte() & 1) != 0 ? "true" : "false") + "}";
          f.request(connection, json);
        }
        break;
      case 10:
        // A sealed message that is not what it says, or a fragment of nothing.
        if (connection != 0 && f.ble->connected(connection)) f.write(connection, ble::Characteristic::request, in.take(in.byte()));
        break;
    }

    if (!key_proved) {
      if (!f.wifi->configured_ssid().empty()) violated("a network was configured without the key");
      if (f.wifi->forgets() != 0) violated("a forget ran without the key");
      if (f.uninstaller->starts() != 0) violated("an uninstall started without the key");
      if (f.easyconnect->active()) violated("Easy Connect started without the key");
    }
  }

  if (f.logger->contains(f.id.to_hex())) violated("the key was logged");
}

}  // namespace attacker
