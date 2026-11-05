//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "logger.h"

namespace rpiledconfig::loggers {

// Lines on standard output, which under systemd is the journal. One line per call and a
// lock around each, so two threads never interleave halves of each other's lines.
class LoggerStdIO : public Logger {
 public:
  explicit LoggerStdIO(LogLevel log_level);

  void debug(const std::string& log) override;
  void info(const std::string& log) override;
  void warn(const std::string& log) override;
  void error(const std::string& log) override;
  void raw(const std::string& log) override;
  std::shared_ptr<Logger> base_logger() override;

  // Atomic, because the level is only known once the configuration has been read, and by
  // then the io thread may already be logging.
  void set_level(LogLevel level) override;

 private:
  void _write(const char* tag, const std::string& log);

  std::atomic<LogLevel> _log_level;
  std::mutex _mutex;
};

}  // namespace rpiledconfig::loggers
