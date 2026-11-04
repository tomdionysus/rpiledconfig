//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <functional>
#include <string>

#include "protocol/messages.h"
#include "types/status.h"

namespace rpiledconfig::session {

// What answers a decrypted request. The session knows nothing of WiFi; it hands the
// request here and sends back whatever JSON comes out, on the executor it gave, never
// inline.
class RequestHandler {
 public:
  virtual ~RequestHandler() = default;

  using Done = std::function<void(std::string response_json)>;

  virtual void handle(const protocol::Request& request, types::Executor on, Done done) = 0;

  // The session that sent requests here has closed, or started over: anything it left
  // running (Wi-Fi Easy Connect listening) stops. One session at a time is served
  // (docs/protocol.md), so there is no question of whose.
  virtual void closed() {}
};

}  // namespace rpiledconfig::session
