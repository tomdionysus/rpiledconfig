//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "config.h"

#include <cstdlib>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace rpiledconfig {

namespace {

std::string trim(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string::npos) return {};

  const auto last = text.find_last_not_of(" \t\r");
  return text.substr(first, last - first + 1);
}

// A value is "..." or '...' (the whole value, and then anything after the closing quote
// but a comment is an error), or bare, in which case a `#` after whitespace starts a
// comment and the value is trimmed. Quote a value that has a space-hash in it.
std::optional<std::string> parse_value(const std::string& raw, std::string& error) {
  const std::string text = trim(raw);

  if (!text.empty() && (text[0] == '"' || text[0] == '\'')) {
    const char quote = text[0];
    std::string out;
    std::size_t i = 1;

    for (; i < text.size(); ++i) {
      const char c = text[i];

      if (c == quote) break;

      if (c == '\\' && quote == '"' && i + 1 < text.size()) {
        const char next = text[++i];
        switch (next) {
          case 'n':
            out.push_back('\n');
            break;
          case 't':
            out.push_back('\t');
            break;
          case '\\':
            out.push_back('\\');
            break;
          case '"':
            out.push_back('"');
            break;
          default:
            error = std::string("unknown escape \\") + next + " in a quoted value";
            return std::nullopt;
        }
        continue;
      }

      out.push_back(c);
    }

    if (i >= text.size()) {
      error = "a quoted value with no closing quote";
      return std::nullopt;
    }

    const std::string rest = trim(text.substr(i + 1));
    if (!rest.empty() && rest[0] != '#') {
      error = "text after the closing quote: " + rest;
      return std::nullopt;
    }

    return out;
  }

  // Bare: up to a comment, which is a hash at the start or after whitespace.
  std::string out = text;
  for (std::size_t i = 0; i < out.size(); ++i) {
    if (out[i] == '#' && (i == 0 || out[i - 1] == ' ' || out[i - 1] == '\t')) {
      out = out.substr(0, i);
      break;
    }
  }

  return trim(out);
}

bool parse_bool(const std::string& value, bool& out) {
  if (value == "true") {
    out = true;
    return true;
  }
  if (value == "false") {
    out = false;
    return true;
  }
  return false;
}

bool parse_uint(const std::string& value, std::uint32_t& out) {
  if (value.empty() || value.size() > 9) return false;

  std::uint32_t result = 0;
  for (const char c : value) {
    if (c < '0' || c > '9') return false;
    result = result * 10 + static_cast<std::uint32_t>(c - '0');
  }

  out = result;
  return true;
}

std::string resolve(const std::string& value, const std::filesystem::path& base_dir) {
  const std::filesystem::path path(value);
  if (path.is_absolute()) return path.lexically_normal().string();

  return (base_dir / path).lexically_normal().string();
}

}  // namespace

Config::Config(std::shared_ptr<loggers::Logger> logger) : _logger(std::move(logger)) {}

bool Config::load_from_file(const std::string& filename) {
  std::ifstream in(filename);
  if (!in) {
    _logger->error("Cannot read the configuration " + filename);
    return false;
  }

  std::stringstream buffer;
  buffer << in.rdbuf();

  return load_from_string(buffer.str(), std::filesystem::absolute(filename).parent_path());
}

bool Config::load_from_string(const std::string& text, const std::filesystem::path& base_dir) {
  std::istringstream in(text);
  std::string raw;
  std::set<std::string> seen;
  int line_number = 0;

  while (std::getline(in, raw)) {
    ++line_number;

    const std::string line = trim(raw);
    if (line.empty() || line[0] == '#') continue;

    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      _logger->error("Configuration line " + std::to_string(line_number) + " is not `key: value`: " + line);
      return false;
    }

    const std::string key = trim(line.substr(0, colon));
    if (key.empty() || key.find_first_of(" \t") != std::string::npos) {
      _logger->error("Configuration line " + std::to_string(line_number) + " has no key before the colon: " + line);
      return false;
    }

    std::string error;
    const auto value = parse_value(line.substr(colon + 1), error);
    if (!value) {
      _logger->error("Configuration line " + std::to_string(line_number) + " (" + key + "): " + error);
      return false;
    }

    // Twice is a mistake, not a preference for the later one: the operator who edits the
    // first copy and wonders why nothing changed is the person this error is for.
    if (!seen.insert(key).second) {
      _logger->error("Configuration line " + std::to_string(line_number) + " sets " + key + " again");
      return false;
    }

    if (!_apply(key, *value, line_number, base_dir)) return false;
  }

  return _validate();
}

bool Config::_apply(const std::string& key, const std::string& value, int line, const std::filesystem::path& base_dir) {
  const auto refuse = [&](const std::string& why) {
    _logger->error("Configuration line " + std::to_string(line) + " (" + key + "): " + why);
    return false;
  };

  const auto take_bool = [&](bool& field) {
    if (!parse_bool(value, field)) return refuse("expected true or false, not " + value);
    return true;
  };

  const auto take_uint = [&](std::uint32_t& field) {
    if (!parse_uint(value, field)) return refuse("expected a whole number, not " + value);
    return true;
  };

  if (key == "log.level") {
    const auto level = loggers::parse_log_level(value);
    if (!level) return refuse("expected debug, info, warn or error, not " + value);
    log_level = *level;
    return true;
  }

  // Gone with the machine ID (docs/protocol.md, The pairing key). Still taken: this file is a
  // conffile, so an upgrade leaves the line in place, and a daemon that refused it would not
  // start.
  if (key == "state.dir") {
    _logger->warn("Configuration line " + std::to_string(line) + ": state.dir is no longer used, and can be deleted");
    return true;
  }

  if (key == "led.driver") {
    led_driver = value;
    return true;
  }

  if (key == "led.path") {
    if (value.empty()) return refuse("expected a path");
    led_path = resolve(value, base_dir);
    return true;
  }

  if (key == "led.active_low") return take_bool(led_active_low);

  if (key == "ble.driver") {
    ble_driver = value;
    return true;
  }

  if (key == "ble.adapter") {
    if (value.empty()) return refuse("expected an adapter name such as hci0");
    ble_adapter = value;
    return true;
  }

  if (key == "ble.name_prefix") {
    if (value.empty() || value.size() > 24) return refuse("expected 1 to 24 characters, which is what fits in the advertisement");
    for (const unsigned char c : value) {
      if (c < 0x20 || c > 0x7e) return refuse("expected printable ASCII");
    }
    ble_name_prefix = value;
    return true;
  }

  if (key == "wifi.driver") {
    wifi_driver = value;
    return true;
  }

  if (key == "wifi.interface") {
    if (value.empty() || value.find('/') != std::string::npos) return refuse("expected an interface name such as wlan0");
    wifi_interface = value;
    return true;
  }

  if (key == "wifi.easyconnect") {
    if (value.empty()) return refuse("expected wpa_supplicant, fake or off");
    wifi_easyconnect = value;
    return true;
  }

  if (key == "uninstall.driver") {
    uninstall_driver = value;
    return true;
  }

  if (key == "status.report_hostname") return take_bool(status_report_hostname);
  if (key == "setup.always") return take_bool(setup_always);
  if (key == "setup.timeout") return take_uint(setup_timeout);
  if (key == "setup.recheck") return take_uint(setup_recheck);

  return refuse("not a key this daemon reads");
}

bool Config::_validate() {
  const auto refuse = [&](const std::string& why) {
    _logger->error("Configuration: " + why);
    return false;
  };

  if (led_driver != "sysfs" && led_driver != "fake") return refuse("led.driver must be sysfs or fake, not " + led_driver);
  if (ble_driver != "bluez" && ble_driver != "fake") return refuse("ble.driver must be bluez or fake, not " + ble_driver);
  if (wifi_driver != "networkmanager" && wifi_driver != "fake") return refuse("wifi.driver must be networkmanager or fake, not " + wifi_driver);

  if (wifi_easyconnect.empty()) wifi_easyconnect = wifi_driver == "fake" ? "fake" : "wpa_supplicant";
  if (wifi_easyconnect != "wpa_supplicant" && wifi_easyconnect != "fake" && wifi_easyconnect != "off") {
    return refuse("wifi.easyconnect must be wpa_supplicant, fake or off, not " + wifi_easyconnect);
  }

  if (uninstall_driver != "systemd" && uninstall_driver != "fake" && uninstall_driver != "off") {
    return refuse("uninstall.driver must be systemd, fake or off, not " + uninstall_driver);
  }

  if (setup_timeout == 0) return refuse("setup.timeout must be at least 1 second");
  if (setup_recheck == 0) return refuse("setup.recheck must be at least 1 second");

  return true;
}

std::filesystem::path Config::locate(const std::string& from_command_line) {
  if (!from_command_line.empty()) return from_command_line;

  if (const char* from_environment = std::getenv(environment_variable); from_environment && *from_environment) return from_environment;

  return system_path;
}

}  // namespace rpiledconfig
