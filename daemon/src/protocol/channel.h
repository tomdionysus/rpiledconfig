//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstdint>
#include <optional>

#include "types/status.h"

namespace rpiledconfig::protocol {

// The encrypted channel of docs/protocol.md: AES-256-GCM under S, a nonce of direction ||
// counter, and a wire form of counter || ciphertext || tag. One Sealer and one Opener per
// direction; a Channel is the daemon's pair.

enum class Direction : std::uint32_t { app_to_daemon = 1, daemon_to_app = 2 };

inline constexpr std::size_t counter_length = 8;

// The 12-byte nonce: the direction as 4 bytes big-endian, then the counter as 8.
types::Bytes make_nonce(Direction direction, std::uint64_t counter);

// Sends in one direction. The counter starts at 0 and goes up by one per message.
class Sealer {
 public:
  Sealer(types::Bytes session_key, Direction direction);

  // The wire form. Empty on a crypto failure, which never happens with a 32-byte key.
  types::Bytes seal(const types::Bytes& plaintext);

  std::uint64_t next_counter() const { return _counter; }

 private:
  types::Bytes _key;
  Direction _direction;
  std::uint64_t _counter = 0;
};

// Receives in one direction. A counter at or below the last accepted is a replay and is
// refused; a tag that does not verify is a forgery, and the caller drops the connection
// on either.
class Opener {
 public:
  Opener(types::Bytes session_key, Direction direction);

  types::Result<types::Bytes> open(const types::Bytes& wire);

  std::optional<std::uint64_t> last_accepted() const { return _last; }

 private:
  types::Bytes _key;
  Direction _direction;
  std::optional<std::uint64_t> _last;
};

// The daemon's side: it opens what the app sends and seals what it answers.
class Channel {
 public:
  explicit Channel(const types::Bytes& session_key);

  types::Result<types::Bytes> open_from_app(const types::Bytes& wire) { return _from_app.open(wire); }
  types::Bytes seal_to_app(const types::Bytes& plaintext) { return _to_app.seal(plaintext); }

 private:
  Opener _from_app;
  Sealer _to_app;
};

}  // namespace rpiledconfig::protocol
