//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "led.h"
#include "loggers/logger.h"
#include "pairing_key/pairing_key.h"
#include "timers/timer_source.h"
#include "types/status.h"

namespace rpiledconfig::led {

// The LED channel of docs/protocol.md, frame version 7: a symbol period T the phone chooses,
// and a frame of sync and the 16-byte codeword (protocol/led_code.h) as five-symbol words,
// repeated back to back.
inline constexpr std::chrono::milliseconds symbol_time{51};  // the normal rate

// The T a byte written to Blink asks for (docs/protocol.md, Symbols), or nothing for a byte
// that is not a rate. A fixed list, as Blink needs no authentication.
std::optional<std::chrono::milliseconds> blink_rate(std::uint8_t value);

// A stretch of the LED in one state, in units of T. Maximal: two adjacent symbols at the
// same level are one run, which is what a camera sees.
struct Run {
  bool on;
  int length;

  bool operator==(const Run& other) const { return on == other.on && length == other.length; }
};

// One frame for an id, as the runs the LED goes through, starting with the sync's 5T on.
// Pure, so the protocol's vector is checked against it without a clock.
std::vector<Run> frame_runs(const pairing_key::PairingKey& id);

// How many T a frame is: 170.
std::size_t frame_symbols();

// Drives an Led through frames until stopped. Runs on its executor; the timer source is
// the one made for that executor (timers/timer_source.h), so a test steps through a frame
// with ManualTimerSource and LedFake.
class LedModulator : public std::enable_shared_from_this<LedModulator> {
 public:
  LedModulator(std::shared_ptr<loggers::Logger> logger, types::Executor executor, std::shared_ptr<Led> led, std::shared_ptr<timers::TimerSource> timers);

  // Takes the LED over and starts blinking this key at this T, the first frame now. Failure is
  // the LED refusing (begin()), and nothing then blinks. Called on the executor.
  types::Status start(const pairing_key::PairingKey& key, std::chrono::milliseconds symbol = symbol_time);

  // Turns the LED off and gives it back. Called on the executor.
  void stop();

  bool running() const { return _running; }
  std::uint64_t frames_started() const { return _frames; }

  std::chrono::milliseconds symbol() const { return _symbol; }

 private:
  void _step();

  std::shared_ptr<loggers::Logger> _logger;
  types::Executor _executor;
  std::shared_ptr<Led> _led;
  std::shared_ptr<timers::TimerSource> _timers;
  std::chrono::milliseconds _symbol = symbol_time;

  std::vector<Run> _runs;
  std::size_t _index = 0;
  std::uint64_t _frames = 0;
  std::uint64_t _generation = 0;
  bool _running = false;

  // When the current run ends, kept absolute so that late timer callbacks do not add up
  // over a frame: the 5% the decoder tolerates is 425 ms, and a timer is late by less.
  std::chrono::steady_clock::time_point _next;
  std::shared_ptr<timers::Timer> _timer;
};

}  // namespace rpiledconfig::led
