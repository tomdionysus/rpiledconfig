//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "loggers/logger.h"

// A logger that keeps what it was told, so a test can say what it expects to have been
// said, and so a failure's message carries the daemon's own explanation.
class MockLogger : public rpiledconfig::loggers::Logger {
 public:
  void debug(const std::string& log) override { _add("[DEBUG] " + log); }
  void info(const std::string& log) override { _add("[INFO ] " + log); }
  void warn(const std::string& log) override { _add("[WARN ] " + log); }
  void error(const std::string& log) override { _add("[ERROR] " + log); }
  void raw(const std::string& log) override { _add("[-----] " + log); }
  void set_level(rpiledconfig::loggers::LogLevel) override {}
  std::shared_ptr<Logger> base_logger() override { return nullptr; }

  std::vector<std::string> lines() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _lines;
  }

  std::string text() const {
    std::string out;
    for (const auto& line : lines()) out += line + "\n";
    return out;
  }

  bool contains(const std::string& fragment) const {
    for (const auto& line : lines()) {
      if (line.find(fragment) != std::string::npos) return true;
    }
    return false;
  }

  void clear() {
    std::lock_guard<std::mutex> lock(_mutex);
    _lines.clear();
  }

 private:
  void _add(std::string line) {
    std::lock_guard<std::mutex> lock(_mutex);
    _lines.push_back(std::move(line));
  }

  mutable std::mutex _mutex;
  std::vector<std::string> _lines;
};
