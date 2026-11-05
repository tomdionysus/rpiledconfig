//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "fragments.h"

#include <algorithm>

namespace rpiledconfig::protocol {

std::vector<types::Bytes> fragment(const types::Bytes& message, std::size_t fragment_size) {
  // One byte of flags leaves nothing for the message: a size that small is a bug in the
  // caller, and the least surprising answer is the smallest legal fragment.
  if (fragment_size < 2) fragment_size = 2;

  const std::size_t payload = fragment_size - 1;
  std::vector<types::Bytes> out;

  std::size_t offset = 0;
  do {
    const std::size_t take = std::min(payload, message.size() - offset);
    const bool last = offset + take >= message.size();

    types::Bytes piece;
    piece.reserve(1 + take);
    piece.push_back(last ? flag_last : 0x00);
    piece.insert(piece.end(), message.begin() + static_cast<std::ptrdiff_t>(offset), message.begin() + static_cast<std::ptrdiff_t>(offset + take));
    out.push_back(std::move(piece));

    offset += take;
  } while (offset < message.size());

  return out;
}

Reassembler::Reassembler(std::size_t limit) : _limit(limit) {}

types::Result<std::optional<types::Bytes>> Reassembler::push(const types::Bytes& fragment) {
  using Out = types::Result<std::optional<types::Bytes>>;

  if (fragment.empty()) {
    _buffer.clear();
    return Out::failure("an empty fragment");
  }

  const std::uint8_t flags = fragment[0];
  if ((flags & ~flag_last) != 0) {
    _buffer.clear();
    return Out::failure("a fragment with unknown flag bits set");
  }

  if (_buffer.size() + fragment.size() - 1 > _limit) {
    _buffer.clear();
    return Out::failure("a message longer than " + std::to_string(_limit) + " bytes");
  }

  _buffer.insert(_buffer.end(), fragment.begin() + 1, fragment.end());

  if ((flags & flag_last) == 0) return Out::success(std::nullopt);

  types::Bytes message;
  message.swap(_buffer);
  return Out::success(std::move(message));
}

}  // namespace rpiledconfig::protocol
