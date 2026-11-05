//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "messages.h"

#include <algorithm>
#include <utility>

#include "json.h"

namespace rpiledconfig::protocol {

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

    const Json* psk = json->get("psk");
    if (psk != nullptr && !psk->is_null()) {
      if (!psk->is_string()) return Out::failure(errors::bad_request, "psk must be a string");
      request.psk = psk->as_string();
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

  return Json::object().set("ok", Json::boolean(true)).set("hostname", Json::string(hostname)).set("wifi", std::move(wifi_json)).dump();
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

std::string info_json(const std::string& version) {
  return Json::object()
      .set("protocol", Json::number(protocol_version))
      .set("version", Json::string(version))
      .dump();
}

}  // namespace rpiledconfig::protocol
