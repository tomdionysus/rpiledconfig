//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "types/status.h"

namespace rpiledconfig::protocol {

// Fragments of docs/protocol.md: byte 0 is flags (bit 0: the last fragment of a message;
// any other bit set is refused), the rest is the next slice of the message's wire form.

inline constexpr std::uint8_t flag_last = 0x01;

// The smallest write any BLE link carries: an ATT MTU of 23 less the 3-byte header.
inline constexpr std::size_t minimum_fragment_size = 20;

// A message may not be longer than this, reassembled.
inline constexpr std::size_t maximum_message_length = 65536;

// Cuts a message into fragments of at most fragment_size bytes each, flags byte included,
// so a fragment_size of 20 carries 19 bytes of message. An empty message is one fragment
// of just the flags byte.
std::vector<types::Bytes> fragment(const types::Bytes& message, std::size_t fragment_size);

// Puts them back together. push() answers failure for a fragment that cannot be taken
// (the caller drops the connection), success with nothing while more are expected, and
// success with the message on the last one.
class Reassembler {
 public:
  explicit Reassembler(std::size_t limit = maximum_message_length);

  types::Result<std::optional<types::Bytes>> push(const types::Bytes& fragment);

  // Bytes held of a message in progress.
  std::size_t pending() const { return _buffer.size(); }

  void reset() { _buffer.clear(); }

 private:
  std::size_t _limit;
  types::Bytes _buffer;
};

}  // namespace rpiledconfig::protocol
