//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <string>

#include "pairing_key/pairing_key.h"
#include "types/status.h"

namespace rpiledconfig::protocol {

// The derivations of docs/protocol.md, "Keys".

inline constexpr std::size_t key_length = 32;

// w, the SPAKE2 password scalar: 32 bytes big-endian. Empty when OpenSSL failed.
types::Bytes password_scalar(const pairing_key::PairingKey& id);

// S = HKDF(ikm = Ke, salt = empty, info = "session-key v2", L = 32).
types::Bytes session_key(const types::Bytes& ke);

}  // namespace rpiledconfig::protocol
