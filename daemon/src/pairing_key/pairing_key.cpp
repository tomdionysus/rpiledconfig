//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "pairing_key.h"

#include <cerrno>
#include <cstring>

#if defined(__linux__)
#include <sys/random.h>
#elif defined(__APPLE__)
#include <cstdlib>
#else
#include <fstream>
#endif

#include "util.h"

namespace rpiledconfig::pairing_key {

types::Status random_bytes(std::uint8_t* out, std::size_t length) {
#if defined(__linux__)
  std::size_t have = 0;
  while (have < length) {
    const auto got = ::getrandom(out + have, length - have, 0);
    if (got < 0) {
      if (errno == EINTR) continue;
      return types::Status::failure(std::string("getrandom: ") + std::strerror(errno));
    }
    have += static_cast<std::size_t>(got);
  }
#elif defined(__APPLE__)
  arc4random_buf(out, length);
#else
  std::ifstream urandom("/dev/urandom", std::ios::binary);
  if (!urandom.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(length))) return types::Status::failure("cannot read /dev/urandom");
#endif
  return types::Status::success();
}

PairingKey PairingKey::from_value(std::uint64_t value) {
  PairingKey key;
  for (int i = static_cast<int>(key_length) - 1; i >= 0; --i) {
    key.bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value & 0xFF);
    value >>= 8;
  }
  return key;
}

std::uint64_t PairingKey::value() const {
  std::uint64_t value = 0;
  for (const auto byte : bytes) value = (value << 8) | byte;
  return value;
}

types::Bytes PairingKey::key_bytes() const { return types::Bytes(bytes.begin(), bytes.end()); }

std::string PairingKey::to_hex() const { return util::to_hex(bytes.data(), bytes.size()); }

types::Result<PairingKey> draw() {
  PairingKey key;
  if (const auto got = random_bytes(key.bytes.data(), key.bytes.size()); !got) return types::Result<PairingKey>::failure(got.error);
  return types::Result<PairingKey>::success(key);
}

}  // namespace rpiledconfig::pairing_key
