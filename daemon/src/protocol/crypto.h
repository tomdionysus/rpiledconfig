//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstddef>
#include <optional>

#include "types/status.h"

namespace rpiledconfig::protocol::crypto {

// The primitives docs/protocol.md names, over OpenSSL's EVP. Nothing here knows what a
// key is for; keys.h and channel.h do.

using types::Bytes;

inline constexpr std::size_t sha256_length = 32;
inline constexpr std::size_t gcm_nonce_length = 12;
inline constexpr std::size_t gcm_tag_length = 16;

Bytes sha256(const Bytes& data);

Bytes hmac_sha256(const Bytes& key, const Bytes& data);

// RFC 5869 with SHA-256. Empty on an OpenSSL failure, which a caller treats as fatal:
// there is no sensible key to fall back to.
Bytes hkdf_sha256(const Bytes& ikm, const Bytes& salt, const Bytes& info, std::size_t length);

// AES-256-GCM with no associated data. seal returns ciphertext || tag (16 bytes); open
// takes the same and gives nothing when the tag does not verify. The key is 32 bytes and
// the nonce 12; anything else is refused rather than padded.
Bytes aes256gcm_seal(const Bytes& key, const Bytes& nonce, const Bytes& plaintext);
std::optional<Bytes> aes256gcm_open(const Bytes& key, const Bytes& nonce, const Bytes& ciphertext_and_tag);

// From OpenSSL's CSPRNG. Empty when it failed, which a caller reports rather than works
// around: a nonce that is not random is not a nonce.
Bytes random_bytes(std::size_t length);

// Constant time, so that a tag comparison leaks nothing about how many bytes matched.
bool equal(const Bytes& a, const Bytes& b);

}  // namespace rpiledconfig::protocol::crypto
