//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace rpiledconfig::protocol {

namespace {

constexpr int max_depth = 32;

class Parser {
 public:
  explicit Parser(const std::string& text) : _text(text) {}

  std::optional<Json> parse_document() {
    auto value = parse_value(0);
    if (!value) return std::nullopt;

    skip_whitespace();
    if (_pos != _text.size()) return std::nullopt;

    return value;
  }

 private:
  void skip_whitespace() {
    while (_pos < _text.size() && (_text[_pos] == ' ' || _text[_pos] == '\t' || _text[_pos] == '\n' || _text[_pos] == '\r')) ++_pos;
  }

  bool take(char c) {
    skip_whitespace();
    if (_pos < _text.size() && _text[_pos] == c) {
      ++_pos;
      return true;
    }
    return false;
  }

  bool take_word(const char* word) {
    std::size_t i = 0;
    while (word[i] != '\0') {
      if (_pos + i >= _text.size() || _text[_pos + i] != word[i]) return false;
      ++i;
    }
    _pos += i;
    return true;
  }

  std::optional<Json> parse_value(int depth) {
    if (depth > max_depth) return std::nullopt;

    skip_whitespace();
    if (_pos >= _text.size()) return std::nullopt;

    const char c = _text[_pos];

    if (c == '{') return parse_object(depth);
    if (c == '[') return parse_array(depth);
    if (c == '"') {
      auto s = parse_string();
      if (!s) return std::nullopt;
      return Json::string(std::move(*s));
    }
    if (c == 't') return take_word("true") ? std::optional<Json>(Json::boolean(true)) : std::nullopt;
    if (c == 'f') return take_word("false") ? std::optional<Json>(Json::boolean(false)) : std::nullopt;
    if (c == 'n') return take_word("null") ? std::optional<Json>(Json::null()) : std::nullopt;
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number();

    return std::nullopt;
  }

  std::optional<Json> parse_object(int depth) {
    if (!take('{')) return std::nullopt;

    Json object = Json::object();

    if (take('}')) return object;

    while (true) {
      skip_whitespace();
      auto key = parse_string();
      if (!key) return std::nullopt;

      if (!take(':')) return std::nullopt;

      auto value = parse_value(depth + 1);
      if (!value) return std::nullopt;

      object.set(*key, std::move(*value));

      if (take(',')) continue;
      if (take('}')) return object;
      return std::nullopt;
    }
  }

  std::optional<Json> parse_array(int depth) {
    if (!take('[')) return std::nullopt;

    Json array = Json::array();

    if (take(']')) return array;

    while (true) {
      auto value = parse_value(depth + 1);
      if (!value) return std::nullopt;

      array.push(std::move(*value));

      if (take(',')) continue;
      if (take(']')) return array;
      return std::nullopt;
    }
  }

  std::optional<Json> parse_number() {
    const std::size_t start = _pos;

    if (_pos < _text.size() && _text[_pos] == '-') ++_pos;

    const auto digits = [&]() {
      const std::size_t from = _pos;
      while (_pos < _text.size() && _text[_pos] >= '0' && _text[_pos] <= '9') ++_pos;
      return _pos > from;
    };

    // A leading zero stands alone: 0, 0.5, but not 01.
    const std::size_t integer_start = _pos;
    if (!digits()) return std::nullopt;
    if (_text[integer_start] == '0' && _pos - integer_start > 1) return std::nullopt;

    if (_pos < _text.size() && _text[_pos] == '.') {
      ++_pos;
      if (!digits()) return std::nullopt;
    }

    if (_pos < _text.size() && (_text[_pos] == 'e' || _text[_pos] == 'E')) {
      ++_pos;
      if (_pos < _text.size() && (_text[_pos] == '+' || _text[_pos] == '-')) ++_pos;
      if (!digits()) return std::nullopt;
    }

    const std::string token = _text.substr(start, _pos - start);
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(value)) return std::nullopt;

    return Json::number(value);
  }

  static void append_utf8(std::string& out, unsigned code) {
    if (code < 0x80) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (code >> 6)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (code >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (code >> 18)));
      out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }

  std::optional<unsigned> parse_hex4() {
    if (_pos + 4 > _text.size()) return std::nullopt;

    unsigned value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = _text[_pos++];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<unsigned>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<unsigned>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<unsigned>(c - 'A' + 10);
      } else {
        return std::nullopt;
      }
    }
    return value;
  }

  std::optional<std::string> parse_string() {
    if (_pos >= _text.size() || _text[_pos] != '"') return std::nullopt;
    ++_pos;

    std::string out;

    while (true) {
      if (_pos >= _text.size()) return std::nullopt;

      const char c = _text[_pos++];

      if (c == '"') return out;

      // A raw control character is not allowed inside a JSON string.
      if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;

      if (c != '\\') {
        out.push_back(c);
        continue;
      }

      if (_pos >= _text.size()) return std::nullopt;
      const char escape = _text[_pos++];

      switch (escape) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
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
        case 'u': {
          auto code = parse_hex4();
          if (!code) return std::nullopt;

          // A surrogate pair is two escapes for one character outside the BMP.
          if (*code >= 0xD800 && *code <= 0xDBFF) {
            if (_pos + 2 > _text.size() || _text[_pos] != '\\' || _text[_pos + 1] != 'u') return std::nullopt;
            _pos += 2;
            auto low = parse_hex4();
            if (!low || *low < 0xDC00 || *low > 0xDFFF) return std::nullopt;
            *code = 0x10000 + ((*code - 0xD800) << 10) + (*low - 0xDC00);
          } else if (*code >= 0xDC00 && *code <= 0xDFFF) {
            return std::nullopt;
          }

          append_utf8(out, *code);
          break;
        }
        default:
          return std::nullopt;
      }
    }
  }

  const std::string& _text;
  std::size_t _pos = 0;
};

void dump_number(std::string& out, double value) {
  // Whole numbers as whole numbers: a signal of 70 is "70", and "protocol":1 is 1.
  if (std::isfinite(value) && std::floor(value) == value && std::fabs(value) < 9007199254740992.0) {
    out += std::to_string(static_cast<long long>(value));
    return;
  }

  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  out += buffer;
}

void dump_value(std::string& out, const Json& value) {
  switch (value.kind()) {
    case Json::Kind::null:
      out += "null";
      break;
    case Json::Kind::boolean:
      out += value.as_boolean() ? "true" : "false";
      break;
    case Json::Kind::number:
      dump_number(out, value.as_number());
      break;
    case Json::Kind::string:
      out += json_quote(value.as_string());
      break;
    case Json::Kind::array: {
      out.push_back('[');
      bool first = true;
      for (const auto& item : value.items()) {
        if (!first) out.push_back(',');
        first = false;
        dump_value(out, item);
      }
      out.push_back(']');
      break;
    }
    case Json::Kind::object: {
      out.push_back('{');
      bool first = true;
      for (const auto& [key, member] : value.members()) {
        if (!first) out.push_back(',');
        first = false;
        out += json_quote(key);
        out.push_back(':');
        dump_value(out, member);
      }
      out.push_back('}');
      break;
    }
  }
}

}  // namespace

Json Json::boolean(bool value) {
  Json json;
  json._kind = Kind::boolean;
  json._boolean = value;
  return json;
}

Json Json::number(double value) {
  Json json;
  json._kind = Kind::number;
  json._number = value;
  return json;
}

Json Json::string(std::string value) {
  Json json;
  json._kind = Kind::string;
  json._string = std::move(value);
  return json;
}

Json Json::array() {
  Json json;
  json._kind = Kind::array;
  return json;
}

Json Json::object() {
  Json json;
  json._kind = Kind::object;
  return json;
}

Json& Json::set(const std::string& key, Json value) {
  for (auto& [name, member] : _members) {
    if (name == key) {
      member = std::move(value);
      return *this;
    }
  }

  _members.emplace_back(key, std::move(value));
  return *this;
}

const Json* Json::get(const std::string& key) const {
  for (const auto& [name, member] : _members) {
    if (name == key) return &member;
  }
  return nullptr;
}

Json& Json::push(Json value) {
  _items.push_back(std::move(value));
  return *this;
}

std::string Json::dump() const {
  std::string out;
  dump_value(out, *this);
  return out;
}

std::optional<Json> Json::parse(const std::string& text) {
  Parser parser(text);
  return parser.parse_document();
}

std::string json_quote(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');

  for (const char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += buffer;
        } else {
          out.push_back(c);
        }
    }
  }

  out.push_back('"');
  return out;
}

}  // namespace rpiledconfig::protocol
