//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <memory>
#include <string>

#include "logger.h"

namespace rpiledconfig::loggers {

// A logger that says which part of the daemon is talking: "(setup) ...". Each component
// wraps the one it was given, so the scope is decided where the component is written
// rather than where it is constructed.
class LoggerScoped : public Logger {
 public:
  LoggerScoped(std::string scope, std::shared_ptr<Logger> logger);

  void debug(const std::string& log) override;
  void info(const std::string& log) override;
  void warn(const std::string& log) override;
  void error(const std::string& log) override;
  void raw(const std::string& log) override;
  std::shared_ptr<Logger> base_logger() override;
  void set_level(LogLevel level) override;

 private:
  std::string _scope;
  std::shared_ptr<Logger> _logger;
};

}  // namespace rpiledconfig::loggers
