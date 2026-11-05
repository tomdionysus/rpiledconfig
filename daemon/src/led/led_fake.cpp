//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "led_fake.h"

#include <utility>

namespace rpiledconfig::led {

LedFake::LedFake(Clock clock) : _clock(std::move(clock)) {}

types::Status LedFake::begin() {
  if (!_begin_error.empty()) return types::Status::failure(_begin_error);

  ++_begins;
  return types::Status::success();
}

void LedFake::set(bool on) {
  _on = on;
  _events.push_back(Event{_clock(), on});
}

void LedFake::end() { ++_ends; }

}  // namespace rpiledconfig::led
