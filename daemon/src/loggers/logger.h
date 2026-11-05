//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <memory>
#include <optional>
#include <string>

namespace rpiledconfig::loggers {

// Scoped rather than bare DEBUG and ERROR, which collide with macros that more than one
// platform header defines.
enum class LogLevel { debug = 0, info = 1, warn = 2, error = 3 };

// The spelling log.level takes in the configuration. Anything else is refused rather
// than guessed at: "warning" quietly meaning info would be a surprise nobody sees.
inline std::optional<LogLevel> parse_log_level(const std::string& value) {
  if (value == "debug") return LogLevel::debug;
  if (value == "info") return LogLevel::info;
  if (value == "warn") return LogLevel::warn;
  if (value == "error") return LogLevel::error;

  return std::nullopt;
}

inline std::string to_string(LogLevel level) {
  switch (level) {
    case LogLevel::debug:
      return "debug";
    case LogLevel::info:
      return "info";
    case LogLevel::warn:
      return "warn";
    case LogLevel::error:
      return "error";
  }

  return "info";
}

class Logger {
 public:
  virtual ~Logger() = default;
  virtual void debug(const std::string& log) = 0;
  virtual void info(const std::string& log) = 0;
  virtual void warn(const std::string& log) = 0;
  virtual void error(const std::string& log) = 0;

  // Always written, whatever the level: the banner at startup.
  virtual void raw(const std::string& log) = 0;

  virtual void set_level(LogLevel level) = 0;

  virtual std::shared_ptr<Logger> base_logger() = 0;
};

}  // namespace rpiledconfig::loggers
