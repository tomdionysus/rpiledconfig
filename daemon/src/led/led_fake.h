//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "led.h"

namespace rpiledconfig::led {

// An LED that remembers. Every set() is an event with the time it happened, from the clock
// it was given (a test hands in the manual timer source's), so a test can turn the
// timeline into runs and compare them with the protocol's.
class LedFake : public Led {
 public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;

  struct Event {
    std::chrono::steady_clock::time_point at;
    bool on;
  };

  explicit LedFake(Clock clock = []() { return std::chrono::steady_clock::now(); });

  types::Status begin() override;
  void set(bool on) override;
  void end() override;

  std::string describe() const override { return "fake"; }

  const std::vector<Event>& events() const { return _events; }
  int begins() const { return _begins; }
  int ends() const { return _ends; }
  bool on() const { return _on; }
  bool begun() const { return _begins > _ends; }

  // What begin() answers; a failure stands in for a node that cannot be written.
  void fail_begin(const std::string& reason) { _begin_error = reason; }

  void clear() { _events.clear(); }

 private:
  Clock _clock;
  std::vector<Event> _events;
  std::string _begin_error;
  int _begins = 0;
  int _ends = 0;
  bool _on = false;
};

}  // namespace rpiledconfig::led
