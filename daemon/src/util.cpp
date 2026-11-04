//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "util.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace rpiledconfig::util {

namespace {

int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

std::string to_hex(const std::uint8_t* data, std::size_t length) {
  static constexpr char kDigits[] = "0123456789abcdef";

  std::string out;
  out.reserve(length * 2);

  for (std::size_t i = 0; i < length; ++i) {
    out.push_back(kDigits[data[i] >> 4]);
    out.push_back(kDigits[data[i] & 0x0F]);
  }

  return out;
}

std::string to_hex(const std::vector<std::uint8_t>& data) { return to_hex(data.data(), data.size()); }

std::optional<std::vector<std::uint8_t>> from_hex(const std::string& text) {
  if (text.size() % 2 != 0) return std::nullopt;

  std::vector<std::uint8_t> out;
  out.reserve(text.size() / 2);

  for (std::size_t i = 0; i < text.size(); i += 2) {
    const int high = nibble(text[i]);
    const int low = nibble(text[i + 1]);
    if (high < 0 || low < 0) return std::nullopt;

    out.push_back(static_cast<std::uint8_t>((high << 4) | low));
  }

  return out;
}

std::vector<std::uint8_t> to_bytes(const std::string& text) { return std::vector<std::uint8_t>(text.begin(), text.end()); }

std::string to_string(const std::vector<std::uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

std::string zulu_time() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::system_clock::to_time_t(now);
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

  std::tm utc{};
  gmtime_r(&seconds, &utc);

  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << millis << 'Z';
  return out.str();
}

}  // namespace rpiledconfig::util
