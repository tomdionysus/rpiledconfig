//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "easy_connect_fake.h"

#include <utility>

#include "protocol/messages.h"

namespace rpiledconfig::wifi {

EasyConnectFake::EasyConnectFake(std::shared_ptr<loggers::Logger> logger) : _logger(std::move(logger)) {}

void EasyConnectFake::start(types::Executor on, types::Handler<std::string> started, types::Handler<EasyConnectCredentials> received) {
  if (_active) stop();
  ++_starts;

  if (!_start_error.empty()) {
    return types::complete(std::move(on), std::move(started), types::Result<std::string>::failure(protocol::errors::easyconnect_unavailable, _start_error));
  }

  _active = true;
  _on = on;
  _received = std::move(received);
  _logger->info("Wi-Fi Easy Connect listening (fake: no radio is involved)");
  types::complete(std::move(on), std::move(started), types::Result<std::string>::success(fake_uri));
}

void EasyConnectFake::stop() {
  if (!_active) return;
  _active = false;
  _received = nullptr;
  ++_stops;
}

void EasyConnectFake::deliver(EasyConnectCredentials credentials) {
  if (!_active) return;
  auto received = std::move(_received);
  _active = false;
  types::complete(_on, std::move(received), types::Result<EasyConnectCredentials>::success(std::move(credentials)));
}

void EasyConnectFake::fail_exchange(const std::string& reason) {
  if (!_active) return;
  auto received = std::move(_received);
  _active = false;
  types::complete(_on, std::move(received), types::Result<EasyConnectCredentials>::failure(reason));
}

}  // namespace rpiledconfig::wifi
