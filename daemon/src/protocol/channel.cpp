//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "channel.h"

#include <utility>

#include "crypto.h"

namespace rpiledconfig::protocol {

namespace {

void append_u64(types::Bytes& out, std::uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
}

std::uint64_t read_u64(const types::Bytes& in) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < counter_length; ++i) value = (value << 8) | in[i];
  return value;
}

}  // namespace

types::Bytes make_nonce(Direction direction, std::uint64_t counter) {
  types::Bytes nonce;
  nonce.reserve(crypto::gcm_nonce_length);

  const auto d = static_cast<std::uint32_t>(direction);
  nonce.push_back(static_cast<std::uint8_t>((d >> 24) & 0xFF));
  nonce.push_back(static_cast<std::uint8_t>((d >> 16) & 0xFF));
  nonce.push_back(static_cast<std::uint8_t>((d >> 8) & 0xFF));
  nonce.push_back(static_cast<std::uint8_t>(d & 0xFF));
  append_u64(nonce, counter);

  return nonce;
}

Sealer::Sealer(types::Bytes session_key, Direction direction) : _key(std::move(session_key)), _direction(direction) {}

types::Bytes Sealer::seal(const types::Bytes& plaintext) {
  const auto counter = _counter++;
  const auto sealed = crypto::aes256gcm_seal(_key, make_nonce(_direction, counter), plaintext);
  if (sealed.empty()) return {};

  types::Bytes wire;
  wire.reserve(counter_length + sealed.size());
  append_u64(wire, counter);
  wire.insert(wire.end(), sealed.begin(), sealed.end());

  return wire;
}

Opener::Opener(types::Bytes session_key, Direction direction) : _key(std::move(session_key)), _direction(direction) {}

types::Result<types::Bytes> Opener::open(const types::Bytes& wire) {
  if (wire.size() < counter_length + crypto::gcm_tag_length) return types::Result<types::Bytes>::failure("a message shorter than a counter and a tag");

  const auto counter = read_u64(wire);
  if (_last && counter <= *_last) return types::Result<types::Bytes>::failure("counter " + std::to_string(counter) + " was already used");

  const types::Bytes body(wire.begin() + counter_length, wire.end());
  const auto plaintext = crypto::aes256gcm_open(_key, make_nonce(_direction, counter), body);
  if (!plaintext) return types::Result<types::Bytes>::failure("the tag did not verify");

  _last = counter;
  return types::Result<types::Bytes>::success(*plaintext);
}

Channel::Channel(const types::Bytes& session_key) : _from_app(session_key, Direction::app_to_daemon), _to_app(session_key, Direction::daemon_to_app) {}

}  // namespace rpiledconfig::protocol
