//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "logger_stdio.h"

#include <iostream>

#include "../util.h"

namespace rpiledconfig::loggers {

LoggerStdIO::LoggerStdIO(LogLevel log_level) : _log_level(log_level) {}

void LoggerStdIO::debug(const std::string& log) {
  if (_log_level.load() > LogLevel::debug) return;
  _write("[DEBUG]", log);
}

void LoggerStdIO::info(const std::string& log) {
  if (_log_level.load() > LogLevel::info) return;
  _write("[INFO ]", log);
}

void LoggerStdIO::warn(const std::string& log) {
  if (_log_level.load() > LogLevel::warn) return;
  _write("[WARN ]", log);
}

void LoggerStdIO::error(const std::string& log) { _write("[ERROR]", log); }

void LoggerStdIO::raw(const std::string& log) { _write("[-----]", log); }

std::shared_ptr<Logger> LoggerStdIO::base_logger() { return nullptr; }

void LoggerStdIO::set_level(LogLevel level) { _log_level.store(level); }

void LoggerStdIO::_write(const char* tag, const std::string& log) {
  std::lock_guard<std::mutex> lock(_mutex);

  // Flushed per line: a service that dies takes its buffer with it, and the last line
  // before a crash is the one most worth having.
  std::cout << util::zulu_time() << ' ' << tag << ' ' << log << std::endl;
}

}  // namespace rpiledconfig::loggers
