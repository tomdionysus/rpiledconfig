//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "wifi_fake.h"

#include <utility>

namespace rpiledconfig::wifi {

WifiFake::WifiFake(std::shared_ptr<loggers::Logger> logger) : _logger(std::move(logger)) {}

void WifiFake::status(types::Executor on, types::Handler<WifiStatus> handler) {
  ++_status_calls;

  if (_failing()) return types::complete(std::move(on), std::move(handler), types::Result<WifiStatus>::failure(_fail_code, _fail_reason));

  types::complete(std::move(on), std::move(handler), types::Result<WifiStatus>::success(_status));
}

void WifiFake::scan(types::Executor on, types::Handler<std::vector<WifiNetwork>> handler) {
  ++_scans;

  if (_failing()) return types::complete(std::move(on), std::move(handler), types::Result<std::vector<WifiNetwork>>::failure(_fail_code, _fail_reason));

  types::complete(std::move(on), std::move(handler), types::Result<std::vector<WifiNetwork>>::success(_networks));
}

void WifiFake::configure(const std::string& ssid, const std::string& psk, bool hidden, types::Executor on, types::StatusHandler handler) {
  ++_configures;

  if (_failing()) return types::complete(std::move(on), std::move(handler), types::Status::failure(_fail_code, _fail_reason));

  _configured_ssid = ssid;
  _configured_psk = psk;
  _configured_hidden = hidden;
  _logger->info("Configured " + ssid + (psk.empty() ? " (open)" : " (with a passphrase)") + " (fake: nothing was saved)");

  protocol::JoinState join;
  join.ssid = ssid;
  join.state = protocol::join_states::joining;

  if (!_join_failure_reason.empty()) {
    join.state = protocol::join_states::failed;
    join.reason = _join_failure_reason;
    join.message = _join_failure_message;
    _status.connected = false;
    _status.ssid.clear();
    _status.ip.clear();
  } else if (_connect_on_configure) {
    join.state = protocol::join_states::joined;
    _status.connected = true;
    _status.ssid = ssid;
    _status.ip = "192.0.2.10";
  }
  _status.join = join;

  types::complete(std::move(on), std::move(handler), types::Status::success());
}

void WifiFake::forget(types::Executor on, types::StatusHandler handler) {
  ++_forgets;

  if (_failing()) return types::complete(std::move(on), std::move(handler), types::Status::failure(_fail_code, _fail_reason));

  _configured_ssid.clear();
  _configured_psk.clear();
  _status = WifiStatus{};

  types::complete(std::move(on), std::move(handler), types::Status::success());
}

}  // namespace rpiledconfig::wifi
