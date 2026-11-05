//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "uninstaller_fake.h"

#include <utility>

#include "protocol/messages.h"

namespace rpiledconfig::system {

UninstallerFake::UninstallerFake(std::shared_ptr<loggers::Logger> logger) : _logger(std::move(logger)) {}

void UninstallerFake::start(types::Executor on, types::StatusHandler done) {
  ++_starts;

  if (!_error.empty()) {
    return types::complete(std::move(on), std::move(done), types::Status::failure(protocol::errors::uninstall_unavailable, _error));
  }

  _logger->warn(std::string("uninstall.driver is fake: ") + uninstall_unit + " would be started now, and nothing is removed");
  types::complete(std::move(on), std::move(done), types::Status::success());
}

}  // namespace rpiledconfig::system
