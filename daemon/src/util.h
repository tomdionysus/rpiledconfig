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
#include <string>
#include <vector>

namespace rpiledconfig::util {

// Lowercase hex, which is how every byte string is logged, printed and compared with the
// app's vectors.
std::string to_hex(const std::uint8_t* data, std::size_t length);
std::string to_hex(const std::vector<std::uint8_t>& data);

// The reverse, either case. Nothing for an odd length or a character that is not hex.
std::optional<std::vector<std::uint8_t>> from_hex(const std::string& text);

std::vector<std::uint8_t> to_bytes(const std::string& text);
std::string to_string(const std::vector<std::uint8_t>& bytes);

// Now, as 2026-11-03T12:34:56.789Z, for log lines.
std::string zulu_time();

// The text with every control character replaced, so that what came over the air, or what
// anything else said, cannot forge a line in the log or drive a terminal reading it.
std::string printable(const std::string& text);

}  // namespace rpiledconfig::util
