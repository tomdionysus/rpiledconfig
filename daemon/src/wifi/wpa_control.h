//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace rpiledconfig::wifi::wpa {

// The text of wpa_supplicant's control interface (wpa_supplicant 2.10, ctrl_iface.c and
// dpp_supplicant.c), apart from the socket so that it is tested everywhere.

// An unsolicited message: "<3>DPP-CONFOBJ-AKM psk" is level 3, name DPP-CONFOBJ-AKM,
// argument "psk". The level prefix is optional; the argument is everything after the
// first space, trailing newline removed.
struct Event {
  std::string name;
  std::string argument;
};
Event parse_event(std::string_view message);

// A reply without its trailing newline. "FAIL" and "UNKNOWN COMMAND" are wpa_supplicant's
// refusals: a command that failed, and one this build does not have (DPP not compiled in).
std::string reply_text(std::string_view reply);
bool is_refusal(std::string_view reply);

// DPP-CONFOBJ-SSID carries the SSID as wpa_ssid_txt() writes it: printable ASCII as
// itself, and \" \\ \n \r \t \e and \xNN for the rest (printf_encode). Nothing for a
// malformed escape.
std::optional<std::string> decode_ssid(std::string_view text);

// DPP-CONFOBJ-PASS carries the passphrase as hex.
std::optional<std::string> decode_hex_text(std::string_view hex);

// The interface's MAC from a STATUS reply's address= line, as the 12 lowercase hex
// characters DPP_BOOTSTRAP_GEN's mac= takes. Nothing when there is none.
std::optional<std::string> status_mac(std::string_view status);

}  // namespace rpiledconfig::wifi::wpa
