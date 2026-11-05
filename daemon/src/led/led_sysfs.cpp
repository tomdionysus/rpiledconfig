//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "led_sysfs.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

namespace rpiledconfig::led {

std::string parse_current_trigger(const std::string& trigger_file) {
  const auto open = trigger_file.find('[');
  if (open == std::string::npos) return {};

  const auto close = trigger_file.find(']', open);
  if (close == std::string::npos) return {};

  return trigger_file.substr(open + 1, close - open - 1);
}

LedSysfs::LedSysfs(std::shared_ptr<loggers::Logger> logger, std::filesystem::path path, bool active_low)
    : _logger(std::move(logger)), _path(std::move(path)), _active_low(active_low) {}

types::Status LedSysfs::begin() {
  std::string error;
  std::string trigger;

  if (!_read("trigger", trigger, error)) return types::Status::failure("cannot read the LED's trigger: " + error);

  _saved_trigger = parse_current_trigger(trigger);
  if (_saved_trigger.empty()) {
    // A trigger file with nothing in brackets is not one the kernel wrote; the safe
    // thing to restore later is none.
    _logger->warn("The trigger file of " + _path.string() + " names no current trigger; none will be restored");
    _saved_trigger = "none";
  }

  if (!_write("trigger", "none", error)) return types::Status::failure("cannot take the LED's trigger over: " + error);

  std::string max_brightness;
  if (_read("max_brightness", max_brightness, error)) {
    const auto end = max_brightness.find_first_not_of("0123456789");
    max_brightness = max_brightness.substr(0, end);
    if (!max_brightness.empty() && max_brightness != "0") _max_brightness = max_brightness;
  }

  _begun = true;
  _logger->info("Driving " + _path.string() + " (trigger was " + _saved_trigger + ", max_brightness " + _max_brightness + (_active_low ? ", active low" : "") + ")");

  return types::Status::success();
}

void LedSysfs::set(bool on) {
  std::string error;
  const bool high = on != _active_low;
  if (!_write("brightness", high ? _max_brightness : "0", error)) _logger->warn("Cannot set the LED: " + error);
}

void LedSysfs::end() {
  if (!_begun) return;
  _begun = false;

  std::string error;
  if (!_write("brightness", "0", error)) _logger->warn("Cannot turn the LED off: " + error);
  if (!_write("trigger", _saved_trigger, error)) _logger->warn("Cannot give the LED's trigger back: " + error);

  _logger->info("Restored the trigger " + _saved_trigger + " on " + _path.string());
}

std::string LedSysfs::describe() const { return "sysfs " + _path.string(); }

bool LedSysfs::_write(const char* file, const std::string& value, std::string& error) {
  const auto path = _path / file;

  std::ofstream out(path);
  if (!out) {
    error = path.string() + ": " + std::strerror(errno);
    return false;
  }

  out << value;
  out.flush();

  if (!out) {
    error = path.string() + ": " + std::strerror(errno);
    return false;
  }

  return true;
}

bool LedSysfs::_read(const char* file, std::string& out, std::string& error) {
  const auto path = _path / file;

  std::ifstream in(path);
  if (!in) {
    error = path.string() + ": " + std::strerror(errno);
    return false;
  }

  std::stringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();

  return true;
}

}  // namespace rpiledconfig::led
