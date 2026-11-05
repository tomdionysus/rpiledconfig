//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "led.h"
#include "loggers/logger.h"

namespace rpiledconfig::led {

// The LED through its sysfs node, /sys/class/leds/ACT by default: trigger, brightness and
// max_brightness. Built everywhere, because it only writes files, and a test points it
// at a directory pretending to be the node.
class LedSysfs : public Led {
 public:
  // active_low: the LED lights at brightness 0 (led.active_low), so set() writes the
  // opposite level.
  LedSysfs(std::shared_ptr<loggers::Logger> logger, std::filesystem::path path, bool active_low = false);

  // Reads the trigger the kernel has (the one in brackets), sets none, and learns
  // max_brightness. Failure when the node is not there or cannot be written, which is the
  // udev rule not being installed (packaging/60-rpiledconfig.rules).
  types::Status begin() override;
  void set(bool on) override;
  void end() override;

  std::string describe() const override;

  // What begin() found, for the tests and the logs.
  const std::string& saved_trigger() const { return _saved_trigger; }

 private:
  bool _write(const char* file, const std::string& value, std::string& error);
  bool _read(const char* file, std::string& out, std::string& error);

  std::shared_ptr<loggers::Logger> _logger;
  std::filesystem::path _path;
  std::string _saved_trigger;
  std::string _max_brightness = "1";
  bool _active_low = false;
  bool _begun = false;
};

// The current trigger out of a trigger file's "none [mmc0] heartbeat" form.
std::string parse_current_trigger(const std::string& trigger_file);

}  // namespace rpiledconfig::led
