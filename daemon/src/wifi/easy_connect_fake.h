//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <memory>
#include <string>

#include "easy_connect.h"
#include "loggers/logger.h"

namespace rpiledconfig::wifi {

// An enrollee a test drives: start() answers a fixed URI (or the failure set), and the
// test plays the phone with deliver() or fail_exchange(). On a development machine, and on
// a Pi run with wifi.driver: fake, it is what wifi.easyconnect talks to, so the app's
// screens can be tried without the radio: nothing is ever received there.
class EasyConnectFake : public EasyConnect {
 public:
  static constexpr const char* fake_uri = "DPP:C:81/1,81/6,81/11;M:000000000000;V:2;K:MDkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDIgACfake;;";

  explicit EasyConnectFake(std::shared_ptr<loggers::Logger> logger);

  void start(types::Executor on, types::Handler<std::string> started, types::Handler<EasyConnectCredentials> received) override;
  void stop() override;
  bool active() const override { return _active; }
  std::string describe() const override { return "fake"; }

  // --- The phone ---

  void deliver(EasyConnectCredentials credentials);
  void fail_exchange(const std::string& reason);

  // --- What a test sets and looks at ---

  void fail_start(const std::string& reason) { _start_error = reason; }
  int starts() const { return _starts; }
  int stops() const { return _stops; }

 private:
  std::shared_ptr<loggers::Logger> _logger;
  types::Executor _on;
  types::Handler<EasyConnectCredentials> _received;
  std::string _start_error;
  bool _active = false;
  int _starts = 0;
  int _stops = 0;
};

}  // namespace rpiledconfig::wifi
