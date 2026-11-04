//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <string>
#include <vector>

#include "types/status.h"

namespace rpiledconfig::protocol {

// The messages of docs/protocol.md: plaintexts are UTF-8 JSON objects, a request carries
// op, a response carries ok and, when ok is false, an error code and a message for a
// person.

// 2: SPAKE2 authentication (1 was an HMAC challenge).
inline constexpr int protocol_version = 2;

namespace ops {

inline constexpr char status[] = "status";
inline constexpr char wifi_scan[] = "wifi.scan";
inline constexpr char wifi_configure[] = "wifi.configure";
inline constexpr char wifi_forget[] = "wifi.forget";
inline constexpr char wifi_easyconnect[] = "wifi.easyconnect";
inline constexpr char wifi_easyconnect_stop[] = "wifi.easyconnect.stop";
inline constexpr char finish[] = "finish";

}  // namespace ops

namespace errors {

inline constexpr char bad_request[] = "bad_request";
inline constexpr char unknown_op[] = "unknown_op";
inline constexpr char wifi_unavailable[] = "wifi_unavailable";
inline constexpr char wifi_failed[] = "wifi_failed";
inline constexpr char busy[] = "busy";
inline constexpr char easyconnect_unavailable[] = "easyconnect_unavailable";

}  // namespace errors

struct Request {
  std::string op;

  // wifi.configure only. psk is empty for an open network.
  std::string ssid;
  std::string psk;
};

// What the WiFi backend reports and the status response carries.
struct WifiStatus {
  bool connected = false;
  std::string ssid;
  std::string ip;
};

struct WifiNetwork {
  std::string ssid;
  int signal = 0;  // 0..100
  bool secure = true;
};

// Failure, with the code bad_request, for anything that is not a JSON object with a
// string op, or a wifi.configure with no ssid. An op nobody knows is still a Request:
// the dispatcher answers unknown_op, which tells a newer app what it cannot do.
types::Result<Request> parse_request(const std::string& plaintext);

std::string ok_response();
std::string error_response(const std::string& code, const std::string& message);
std::string status_response(const std::string& hostname, const WifiStatus& wifi);

// Sorted by signal, strongest first, and one entry per SSID (the strongest), as the
// protocol promises; a hidden network (empty SSID) is left out, because the app could not
// name it back.
std::string scan_response(std::vector<WifiNetwork> networks);

// wifi.easyconnect's answer: {"ok":true,"uri":"DPP:..."}.
std::string easyconnect_response(const std::string& uri);

// The Info characteristic: {"protocol":2,"version":"...","fingerprint":"..."}.
std::string info_json(const std::string& version, const std::string& fingerprint);

}  // namespace rpiledconfig::protocol
