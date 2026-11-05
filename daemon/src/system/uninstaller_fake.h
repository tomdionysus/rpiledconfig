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
#include "uninstaller.h"

namespace rpiledconfig::system {

// Logs that it would uninstall and removes nothing: for the tests, the development Mac, and
// a bench Pi whose package is not to be removed (uninstall.driver: fake).
class UninstallerFake : public Uninstaller {
 public:
  explicit UninstallerFake(std::shared_ptr<loggers::Logger> logger);

  void start(types::Executor on, types::StatusHandler done) override;
  std::string describe() const override { return "fake"; }

  // What start() answers; a failure stands in for systemd or polkit refusing.
  void fail_start(const std::string& reason) { _error = reason; }
  int starts() const { return _starts; }

 private:
  std::shared_ptr<loggers::Logger> _logger;
  std::string _error;
  int _starts = 0;
};

}  // namespace rpiledconfig::system
