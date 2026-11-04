//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "led_modulator.h"

#include <algorithm>
#include <utility>

#include "protocol/led_code.h"

namespace rpiledconfig::led {

namespace {

void append(std::vector<Run>& runs, bool on, int length) {
  if (!runs.empty() && runs.back().on == on) {
    runs.back().length += length;
    return;
  }
  runs.push_back(Run{on, length});
}

// Manchester: a 1 is on then off, a 0 is off then on.
void append_bit(std::vector<Run>& runs, bool bit) {
  append(runs, bit, 1);
  append(runs, !bit, 1);
}

}  // namespace

std::vector<Run> frame_runs(const machine_id::MachineId& id) {
  std::vector<Run> runs;

  // Sync: on 4T, off 4T.
  append(runs, true, 4);
  append(runs, false, 4);

  // Start bit: a Manchester 1.
  append_bit(runs, true);

  // The codeword, each byte most significant bit first.
  for (const auto byte : protocol::led_codeword(id)) {
    for (int bit = 7; bit >= 0; --bit) append_bit(runs, ((byte >> bit) & 1) != 0);
  }

  // Idle: off 4T. Merged with the last bit's off half when there is one, which is what a
  // camera sees anyway.
  append(runs, false, 4);

  return runs;
}

std::size_t frame_half_bits() { return 8 + 2 + protocol::led_codeword_length * 8 * 2 + 4; }

LedModulator::LedModulator(std::shared_ptr<loggers::Logger> logger, types::Executor executor, std::shared_ptr<Led> led,
                           std::shared_ptr<timers::TimerSource> timers, machine_id::MachineId id)
    : _logger(std::move(logger)), _executor(std::move(executor)), _led(std::move(led)), _timers(std::move(timers)), _id(id) {}

types::Status LedModulator::start() {
  if (_running) return types::Status::success();

  const auto begun = _led->begin();
  if (!begun) return begun;

  _runs = frame_runs(_id);
  _index = 0;
  _frames = 0;
  _running = true;
  ++_generation;
  _next = _timers->now();

  // Not the id: it is the pairing secret, and the journal is not (docs/installation.md).
  _logger->info("Blinking the machine id on " + _led->describe() + ": " + std::to_string(_runs.size()) + " runs, " +
                std::to_string(frame_half_bits() * half_bit.count()) + " ms per frame");

  _step();
  return types::Status::success();
}

void LedModulator::stop() {
  if (!_running) return;

  _running = false;
  ++_generation;
  if (_timer) _timer->cancel();
  _timer.reset();

  _led->set(false);
  _led->end();

  _logger->info("Stopped after " + std::to_string(_frames) + " frame(s)");
}

void LedModulator::_step() {
  if (!_running) return;

  if (_index == 0) {
    ++_frames;
    if (_frames == 1) {
      _logger->info("LED frame 1 starts");
    } else {
      _logger->debug("LED frame " + std::to_string(_frames) + " starts");
    }
  }

  const Run& run = _runs[_index];
  _led->set(run.on);

  _next += half_bit * run.length;
  _index = (_index + 1) % _runs.size();

  const auto now = _timers->now();
  const auto delay = _next > now ? std::chrono::duration_cast<std::chrono::milliseconds>(_next - now) : std::chrono::milliseconds(0);

  const auto generation = _generation;
  _timer = _timers->schedule(delay, [weak = weak_from_this(), generation]() {
    auto self = weak.lock();
    if (!self || self->_generation != generation) return;
    self->_step();
  });
}

}  // namespace rpiledconfig::led
