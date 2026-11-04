//
// RPiLEDConfig - app
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <array>
#include <deque>
#include <optional>
#include <vector>

#include "codec.h"
#include "detector.h"
#include "protocol.h"

// The LED decoder for one stream of brightnesses: timestamped samples in, a machine ID out
// (docs/protocol.md, "Decoding").
//
// The frame repeats unchanged, so the stream is cyclic and a sample from any part of any
// cycle belongs to one known symbol. The reader does not wait for a sync or a second pass:
// it keeps two frame periods of samples and, once it holds most of a frame, folds them onto
// one cycle at clock rates across the tolerance and scores every start (the sync must
// agree, every data symbol adds how decisive it is), refines the best few, and reads each
// word by matching its five symbols against the 16 words. A byte whose words are unseen or
// uncertain is an erasure; Reed-Solomon fills and repairs the rest; the CRC decides.
namespace rpiledconfig::reader {

struct ReaderConfig {
  double symbol = symbol_ms;
};

struct ReaderState {
  bool led_seen = false;
  // The share of the frame's symbol slots a sample fell in while the LED was seen, 0..1.
  double coverage = 0;
  double contrast = 0;
  bool level = false;
  // Tries made, and whole frame periods seen without a read.
  int tries = 0;
  int frames_failed = 0;
  // The fewest bytes erased at the best alignment of the last try, out of 16.
  int erasures = static_cast<int>(codeword_length);
  std::optional<Read> result;
  // Frame periods of samples the read drew on, a part period counting as one.
  int frames_combined = 0;
};

class Reader {
 public:
  explicit Reader(ReaderConfig config = {});

  const ReaderState& push(double t, double y);
  const ReaderState& state() const { return _state; }

  // Reads the samples held now, whatever the schedule: for tests and the bench.
  bool try_now();

 private:
  struct Sample {
    double t;
    double d;
  };
  struct Alignment {
    double start;
    double symbol;
    double score;
  };

  std::vector<Alignment> _align(const std::vector<Sample>& samples) const;
  bool _read_window(const std::vector<Sample>& samples, const std::vector<Alignment>& candidates, int& fewest);
  void _mark(double t);
  void _clear();

  ReaderConfig _config;
  Detector _detector;
  std::deque<Sample> _samples;
  double _last_try = -1e18;
  double _first_t = 0;
  bool _started = false;
  std::array<std::uint8_t, frame_symbols> _slots{};
  int _slots_seen = 0;
  ReaderState _state;
};

}  // namespace rpiledconfig::reader
