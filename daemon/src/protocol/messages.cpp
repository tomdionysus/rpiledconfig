//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "messages.h"

#include <algorithm>
#include <utility>

#include "json.h"

namespace rpiledconfig::protocol {

namespace {

// RFC 3629: no overlong forms, surrogates, or anything past U+10FFFF. D-Bus refuses anything else,
// and what reaches the log and NetworkManager should have been refused long before.
bool is_utf8(const std::string& text) {
  std::size_t i = 0;
  while (i < text.size()) {
    const auto c = static_cast<unsigned char>(text[i]);
    std::size_t extra = 0;
    unsigned low = 0x80;
    unsigned high = 0xBF;
    if (c < 0x80) {
      extra = 0;
    } else if (c >= 0xC2 && c <= 0xDF) {
      extra = 1;
    } else if (c >= 0xE0 && c <= 0xEF) {
      extra = 2;
      if (c == 0xE0) low = 0xA0;
      if (c == 0xED) high = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
      extra = 3;
      if (c == 0xF0) low = 0x90;
      if (c == 0xF4) high = 0x8F;
    } else {
      return false;
    }
    if (i + extra >= text.size() + (extra == 0 ? 1 : 0)) return false;
    for (std::size_t k = 1; k <= extra; ++k) {
      const auto next = static_cast<unsigned char>(text[i + k]);
      if (next < (k == 1 ? low : 0x80) || next > (k == 1 ? high : 0xBF)) return false;
    }
    i += extra + 1;
  }
  return true;
}

bool has_control_character(const std::string& text) {
  return std::any_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; });
}

}  // namespace

types::Result<Request> parse_request(const std::string& plaintext) {
  using Out = types::Result<Request>;

  const auto json = Json::parse(plaintext);
  if (!json) return Out::failure(errors::bad_request, "not JSON");
  if (!json->is_object()) return Out::failure(errors::bad_request, "not a JSON object");

  const Json* op = json->get("op");
  if (op == nullptr || !op->is_string() || op->as_string().empty()) return Out::failure(errors::bad_request, "no op");

  Request request;
  request.op = op->as_string();

  if (request.op == ops::wifi_configure) {
    const Json* ssid = json->get("ssid");
    if (ssid == nullptr || !ssid->is_string() || ssid->as_string().empty()) return Out::failure(errors::bad_request, "wifi.configure needs an ssid");
    request.ssid = ssid->as_string();
    // What 802.11 allows a network's name to be, and what can go on to NetworkManager and the log.
    if (request.ssid.size() > 32) return Out::failure(errors::bad_request, "an ssid is at most 32 bytes");
    if (!is_utf8(request.ssid) || has_control_character(request.ssid))
      return Out::failure(errors::bad_request, "an ssid must be text, with no control characters");

    const Json* psk = json->get("psk");
    if (psk != nullptr && !psk->is_null()) {
      if (!psk->is_string()) return Out::failure(errors::bad_request, "psk must be a string");
      request.psk = psk->as_string();
      // Not a rule about what a passphrase should look like, which is the network's business.
      if (!is_utf8(request.psk) || request.psk.find('\0') != std::string::npos) return Out::failure(errors::bad_request, "psk must be text");
    }

    const Json* hidden = json->get("hidden");
    if (hidden != nullptr && !hidden->is_null()) {
      if (!hidden->is_boolean()) return Out::failure(errors::bad_request, "hidden must be true or false");
      request.hidden = hidden->as_boolean();
    }
  }

  return Out::success(std::move(request));
}

std::string ok_response() { return Json::object().set("ok", Json::boolean(true)).dump(); }

std::string error_response(const std::string& code, const std::string& message) {
  return Json::object().set("ok", Json::boolean(false)).set("error", Json::string(code)).set("message", Json::string(message)).dump();
}

std::string status_response(const std::string& hostname, const WifiStatus& wifi) {
  Json wifi_json = Json::object().set("connected", Json::boolean(wifi.connected));
  if (wifi.connected) {
    wifi_json.set("ssid", Json::string(wifi.ssid));
    wifi_json.set("ip", Json::string(wifi.ip));
  }
  if (wifi.join) {
    Json join = Json::object().set("ssid", Json::string(wifi.join->ssid)).set("state", Json::string(wifi.join->state));
    if (wifi.join->state == join_states::failed) {
      join.set("reason", Json::string(wifi.join->reason.empty() ? std::string(join_reasons::other) : wifi.join->reason));
      join.set("message", Json::string(wifi.join->message));
    }
    wifi_json.set("join", std::move(join));
  }

  Json response = Json::object().set("ok", Json::boolean(true));
  if (!hostname.empty()) response.set("hostname", Json::string(hostname));
  return response.set("wifi", std::move(wifi_json)).dump();
}

std::string scan_response(std::vector<WifiNetwork> networks) {
  // Strongest first, and for a tie the name, so the answer is the same for the same scan.
  std::stable_sort(networks.begin(), networks.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
    if (a.signal != b.signal) return a.signal > b.signal;
    return a.ssid < b.ssid;
  });

  Json list = Json::array();
  std::vector<std::string> seen;

  for (const auto& network : networks) {
    if (network.ssid.empty()) continue;
    if (std::find(seen.begin(), seen.end(), network.ssid) != seen.end()) continue;
    seen.push_back(network.ssid);

    const int signal = std::clamp(network.signal, 0, 100);
    list.push(Json::object().set("ssid", Json::string(network.ssid)).set("signal", Json::number(signal)).set("secure", Json::boolean(network.secure)));
  }

  return Json::object().set("ok", Json::boolean(true)).set("networks", std::move(list)).dump();
}

std::string easyconnect_response(const std::string& uri) { return Json::object().set("ok", Json::boolean(true)).set("uri", Json::string(uri)).dump(); }

std::string forgotten_response(int count) { return Json::object().set("ok", Json::boolean(true)).set("forgotten", Json::number(count)).dump(); }

std::string info_json(const std::string& version, const std::string& led) {
  return Json::object().set("protocol", Json::number(protocol_version)).set("version", Json::string(version)).set("led", Json::string(led)).dump();
}

}  // namespace rpiledconfig::protocol
