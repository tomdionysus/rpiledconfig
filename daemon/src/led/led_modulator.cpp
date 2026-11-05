//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
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

}  // namespace

std::vector<Run> frame_runs(const pairing_key::PairingKey& id) {
  std::vector<Run> runs;

  // Sync: on 5T, off 5T, runs no data can make.
  append(runs, true, static_cast<int>(protocol::led_sync_on));
  append(runs, false, static_cast<int>(protocol::led_sync_off));

  // The codeword, each byte as its high nibble's word then its low nibble's, each word first
  // symbol first.
  for (const auto byte : protocol::led_codeword(id)) {
    for (const int nibble : {byte >> 4, byte & 0x0F}) {
      const auto word = protocol::led_words[static_cast<std::size_t>(nibble)];
      for (int symbol = static_cast<int>(protocol::led_word_symbols) - 1; symbol >= 0; --symbol) append(runs, ((word >> symbol) & 1) != 0, 1);
    }
  }

  return runs;
}

std::size_t frame_symbols() { return protocol::led_frame_symbols; }

std::optional<std::chrono::milliseconds> blink_rate(std::uint8_t value) {
  switch (value) {
    case 0x01:
      return std::chrono::milliseconds(51);
    case 0x02:
      return std::chrono::milliseconds(33);
    case 0x03:
      return std::chrono::milliseconds(77);
    default:
      return std::nullopt;
  }
}

LedModulator::LedModulator(std::shared_ptr<loggers::Logger> logger, types::Executor executor, std::shared_ptr<Led> led,
                           std::shared_ptr<timers::TimerSource> timers)
    : _logger(std::move(logger)), _executor(std::move(executor)), _led(std::move(led)), _timers(std::move(timers)) {}

types::Status LedModulator::start(const pairing_key::PairingKey& key, std::chrono::milliseconds symbol) {
  if (_running) return types::Status::success();

  const auto begun = _led->begin();
  if (!begun) return begun;

  _symbol = symbol;
  _runs = frame_runs(key);
  _index = 0;
  _frames = 0;
  _running = true;
  ++_generation;
  _next = _timers->now();

  // Not the key: it is the pairing secret, and the journal is not (docs/installation.md).
  _logger->info("Blinking the pairing key on " + _led->describe() + ": " + std::to_string(_runs.size()) + " runs, " +
                std::to_string(frame_symbols() * _symbol.count()) + " ms per frame (T = " + std::to_string(_symbol.count()) + " ms)");

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

  _next += _symbol * run.length;
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
