//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <string>

#include "types/status.h"

namespace rpiledconfig::led {

// The LED that carries the code (the activity LED by default). The modulator takes it over
// with begin(), drives it with set() and gives it back with end(), which restores whatever
// trigger the kernel had on it (default-on or input for PWR, mmc0 for ACT) so that the
// light goes back to its usual job when setup mode ends.
//
// Synchronous: a sysfs write is a few microseconds and the modulator is the only caller,
// on its own strand.
class Led {
 public:
  virtual ~Led() = default;

  virtual types::Status begin() = 0;
  virtual void set(bool on) = 0;
  virtual void end() = 0;

  virtual std::string describe() const = 0;
};

}  // namespace rpiledconfig::led
