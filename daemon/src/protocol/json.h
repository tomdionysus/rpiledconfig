//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rpiledconfig::protocol {

// A JSON value, written and read by hand (TODO/ACTIVE.md, "Decisions": no JSON library).
// The protocol's messages are flat objects of strings, numbers and booleans, with one
// array of flat objects in wifi.scan's answer, and that is what this is sized for: it
// parses any JSON, but keeps members in a vector rather than a map because a message has
// a handful of them and the writer wants them in the order they were set.
class Json {
 public:
  enum class Kind { null, boolean, number, string, array, object };

  Json() = default;

  static Json null() { return Json(); }
  static Json boolean(bool value);
  static Json number(double value);
  static Json string(std::string value);
  static Json array();
  static Json object();

  Kind kind() const { return _kind; }
  bool is_null() const { return _kind == Kind::null; }
  bool is_boolean() const { return _kind == Kind::boolean; }
  bool is_number() const { return _kind == Kind::number; }
  bool is_string() const { return _kind == Kind::string; }
  bool is_array() const { return _kind == Kind::array; }
  bool is_object() const { return _kind == Kind::object; }

  bool as_boolean() const { return _boolean; }
  double as_number() const { return _number; }
  const std::string& as_string() const { return _string; }

  // Objects. set() replaces a member of the same name in place, so the order stays what
  // it first was.
  Json& set(const std::string& key, Json value);
  const Json* get(const std::string& key) const;
  const std::vector<std::pair<std::string, Json>>& members() const { return _members; }

  // Arrays.
  Json& push(Json value);
  const std::vector<Json>& items() const { return _items; }

  // Compact, with no whitespace, strings escaped, and a number that is a whole one
  // written without a point: 1, not 1.0.
  std::string dump() const;

  // Nothing for anything that is not one JSON value with nothing but whitespace after
  // it. Nesting deeper than 32 is refused: nothing in the protocol nests at all, and a
  // reader that recursed as deep as a sender liked would be a stack to overflow.
  static std::optional<Json> parse(const std::string& text);

 private:
  Kind _kind = Kind::null;
  bool _boolean = false;
  double _number = 0;
  std::string _string;
  std::vector<Json> _items;
  std::vector<std::pair<std::string, Json>> _members;
};

// The escaping on its own, quotes included, for anything that writes a string by hand.
std::string json_quote(const std::string& text);

}  // namespace rpiledconfig::protocol
