//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <memory>
#include <string>

#include "loggers/logger.h"
#include "request_handler.h"
#include "system/uninstaller.h"
#include "wifi/easy_connect.h"
#include "wifi/wifi.h"

namespace rpiledconfig::session {

// The requests of docs/protocol.md, "Messages", answered from the WiFi backend. finish is
// answered ok here and acted on by the session, which knows the connection; an op nobody
// knows gets unknown_op, so a newer app learns what it cannot do.
//
// wifi.easyconnect starts the enrollee (null when wifi.easyconnect is off, which answers
// easyconnect_unavailable), and a network it receives is applied through the WiFi
// backend's configure, the same path as wifi.configure. It stops on wifi.easyconnect.stop
// and when the session that started it closes (closed()).
//
// uninstall hands the removal to the uninstaller (null when uninstall.driver is off, which
// answers uninstall_unavailable); the session hangs up after an ok, as after finish.
class Requests : public RequestHandler {
 public:
  Requests(std::shared_ptr<loggers::Logger> logger, std::shared_ptr<wifi::Wifi> wifi, std::string hostname,
           std::shared_ptr<wifi::EasyConnect> easyconnect = nullptr, std::shared_ptr<system::Uninstaller> uninstaller = nullptr);

  void handle(const protocol::Request& request, types::Executor on, Done done) override;
  void closed() override;

 private:
  void _easyconnect(types::Executor on, Done done);

  std::shared_ptr<loggers::Logger> _logger;
  std::shared_ptr<wifi::Wifi> _wifi;
  std::string _hostname;
  std::shared_ptr<wifi::EasyConnect> _easyconnect_backend;
  std::shared_ptr<system::Uninstaller> _uninstaller;
};

// A backend failure as the protocol's error: its own code when it gave one, wifi_failed
// otherwise.
std::string wifi_error_code(const std::string& code);

}  // namespace rpiledconfig::session
