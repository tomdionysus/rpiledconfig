//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <string>
#include <vector>

#include "protocol/messages.h"
#include "types/status.h"

namespace rpiledconfig::wifi {

using protocol::WifiNetwork;
using protocol::WifiStatus;

// The WiFi backend: what the status, wifi.scan, wifi.configure and wifi.forget requests
// are answered from, and what setup mode checks before entering. Every call answers on
// the executor it is given and never inline, because NetworkManager answers over the bus
// and the fake has to behave the same.
//
// A failure's code is one of the protocol's: wifi_unavailable when the backend itself is
// not answering, wifi_failed when it refused with the reason in the error.
class Wifi {
 public:
  virtual ~Wifi() = default;

  virtual void status(types::Executor on, types::Handler<WifiStatus> handler) = 0;
  virtual void scan(types::Executor on, types::Handler<std::vector<WifiNetwork>> handler) = 0;

  // Saves the connection and asks for it to be activated; success once that has been
  // asked for, not once it is up. psk is empty for an open network.
  virtual void configure(const std::string& ssid, const std::string& psk, types::Executor on, types::StatusHandler handler) = 0;

  // Removes the saved connection.
  virtual void forget(types::Executor on, types::StatusHandler handler) = 0;

  virtual std::string describe() const = 0;
};

}  // namespace rpiledconfig::wifi
