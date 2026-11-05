//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "wpa_control.h"

#include <cctype>

#include "util.h"

namespace rpiledconfig::wifi::wpa {

namespace {

std::string_view trim_newlines(std::string_view text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
  return text;
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

Event parse_event(std::string_view message) {
  message = trim_newlines(message);

  // "<3>": wpa_msg's level.
  if (!message.empty() && message.front() == '<') {
    const auto close = message.find('>');
    if (close != std::string_view::npos) message.remove_prefix(close + 1);
  }

  Event event;
  const auto space = message.find(' ');
  if (space == std::string_view::npos) {
    event.name = std::string(message);
  } else {
    event.name = std::string(message.substr(0, space));
    event.argument = std::string(message.substr(space + 1));
  }
  return event;
}

std::string reply_text(std::string_view reply) { return std::string(trim_newlines(reply)); }

bool is_refusal(std::string_view reply) {
  const auto text = trim_newlines(reply);
  return text == "FAIL" || text == "UNKNOWN COMMAND" || text.empty();
}

std::optional<std::string> decode_ssid(std::string_view text) {
  std::string out;

  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c != '\\') {
      out.push_back(c);
      continue;
    }

    if (++i >= text.size()) return std::nullopt;
    switch (text[i]) {
      case '\\':
        out.push_back('\\');
        break;
      case '"':
        out.push_back('"');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'e':
        out.push_back('\033');
        break;
      case 'x': {
        // Two hex digits follow: text[i + 1] and text[i + 2].
        if (i + 2 >= text.size()) return std::nullopt;
        const int high = hex_value(text[i + 1]);
        const int low = hex_value(text[i + 2]);
        if (high < 0 || low < 0) return std::nullopt;
        out.push_back(static_cast<char>(high * 16 + low));
        i += 2;
        break;
      }
      default:
        return std::nullopt;
    }
  }

  return out;
}

std::optional<std::string> decode_hex_text(std::string_view hex) {
  const auto bytes = util::from_hex(std::string(trim_newlines(hex)));
  if (!bytes) return std::nullopt;
  return util::to_string(*bytes);
}

std::optional<std::string> status_mac(std::string_view status) {
  std::size_t start = 0;
  while (start < status.size()) {
    auto end = status.find('\n', start);
    if (end == std::string_view::npos) end = status.size();
    const auto line = status.substr(start, end - start);

    if (line.rfind("address=", 0) == 0) {
      std::string mac;
      for (const char c : line.substr(8)) {
        if (c == ':') continue;
        if (hex_value(c) < 0) return std::nullopt;
        mac.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      }
      if (mac.size() != 12) return std::nullopt;
      return mac;
    }

    start = end + 1;
  }
  return std::nullopt;
}

}  // namespace rpiledconfig::wifi::wpa
