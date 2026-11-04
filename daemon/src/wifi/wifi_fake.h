//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "loggers/logger.h"
#include "wifi.h"

namespace rpiledconfig::wifi {

// A WiFi backend a test configures: the networks a scan finds, the status, and a failure
// to inject. configure() connects to what it was given unless told not to, so a test can
// walk the whole setup through.
class WifiFake : public Wifi {
 public:
  WifiFake(std::shared_ptr<loggers::Logger> logger);

  void status(types::Executor on, types::Handler<WifiStatus> handler) override;
  void scan(types::Executor on, types::Handler<std::vector<WifiNetwork>> handler) override;
  void configure(const std::string& ssid, const std::string& psk, types::Executor on, types::StatusHandler handler) override;
  void forget(types::Executor on, types::StatusHandler handler) override;
  std::string describe() const override { return "fake"; }

  // --- What a test sets ----------------------------------------------------------------

  void set_status(WifiStatus status) { _status = std::move(status); }
  void set_networks(std::vector<WifiNetwork> networks) { _networks = std::move(networks); }

  // Every call fails with this until cleared. The code is wifi_unavailable, wifi_failed,
  // or empty for a backend that gave none.
  void fail(const std::string& code, const std::string& reason) {
    _fail = true;
    _fail_code = code;
    _fail_reason = reason;
  }
  void clear_failure() {
    _fail = false;
    _fail_code.clear();
    _fail_reason.clear();
  }

  // Whether configure() makes the status connected (default) or leaves it alone.
  void connect_on_configure(bool connect) { _connect_on_configure = connect; }

  // --- What a test looks at ------------------------------------------------------------

  const WifiStatus& current() const { return _status; }
  const std::string& configured_ssid() const { return _configured_ssid; }
  const std::string& configured_psk() const { return _configured_psk; }
  int configures() const { return _configures; }
  int forgets() const { return _forgets; }
  int status_calls() const { return _status_calls; }
  int scans() const { return _scans; }

 private:
  bool _failing() const { return _fail; }

  std::shared_ptr<loggers::Logger> _logger;
  WifiStatus _status;
  std::vector<WifiNetwork> _networks;
  bool _fail = false;
  std::string _fail_code;
  std::string _fail_reason;
  std::string _configured_ssid;
  std::string _configured_psk;
  bool _connect_on_configure = true;
  int _configures = 0;
  int _forgets = 0;
  int _status_calls = 0;
  int _scans = 0;
};

}  // namespace rpiledconfig::wifi
