//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "wifi/wpa_control.h"

#include <gtest/gtest.h>

using namespace rpiledconfig::wifi;

// The forms below are wpa_supplicant 2.10's: wpa_msg prefixes an attached monitor's
// messages with <level>; dpp_supplicant.c writes DPP-CONFOBJ-SSID with wpa_ssid_txt
// (printf_encode) and DPP-CONFOBJ-PASS with wpa_snprintf_hex.

TEST(WpaControlTest, AnEventIsItsNameAndArgumentWithoutTheLevel) {
  auto event = wpa::parse_event("<3>DPP-CONFOBJ-AKM psk");
  EXPECT_EQ(event.name, "DPP-CONFOBJ-AKM");
  EXPECT_EQ(event.argument, "psk");

  event = wpa::parse_event("<3>DPP-CONF-RECEIVED ");
  EXPECT_EQ(event.name, "DPP-CONF-RECEIVED");
  EXPECT_EQ(event.argument, "");

  event = wpa::parse_event("DPP-CONFOBJ-SSID my network\n");
  EXPECT_EQ(event.name, "DPP-CONFOBJ-SSID");
  EXPECT_EQ(event.argument, "my network") << "spaces in the argument kept, the newline not";
}

TEST(WpaControlTest, RefusalsAreFailAndUnknownCommand) {
  EXPECT_TRUE(wpa::is_refusal("FAIL\n"));
  EXPECT_TRUE(wpa::is_refusal("UNKNOWN COMMAND\n"));
  EXPECT_TRUE(wpa::is_refusal(""));
  EXPECT_FALSE(wpa::is_refusal("OK\n"));
  EXPECT_FALSE(wpa::is_refusal("1"));
  EXPECT_EQ(wpa::reply_text("OK\n"), "OK");
  EXPECT_EQ(wpa::reply_text("DPP:C:81/6;M:0011;V:2;K:abc;;"), "DPP:C:81/6;M:0011;V:2;K:abc;;");
}

TEST(WpaControlTest, TheSsidIsUnescapedAsPrintfEncodeWroteIt) {
  EXPECT_EQ(wpa::decode_ssid("Home"), "Home");
  EXPECT_EQ(wpa::decode_ssid("Tom's \\\"Wi-Fi\\\""), "Tom's \"Wi-Fi\"");
  EXPECT_EQ(wpa::decode_ssid("back\\\\slash"), "back\\slash");
  EXPECT_EQ(wpa::decode_ssid("caf\\xc3\\xa9"), "caf\xc3\xa9") << "UTF-8 bytes come as \\x";
  EXPECT_EQ(wpa::decode_ssid("tab\\there"), "tab\there");

  EXPECT_FALSE(wpa::decode_ssid("bad\\q")) << "an escape printf_encode never writes";
  EXPECT_FALSE(wpa::decode_ssid("short\\x4"));
  EXPECT_FALSE(wpa::decode_ssid("trailing\\"));
}

TEST(WpaControlTest, ThePassphraseIsHex) {
  EXPECT_EQ(wpa::decode_hex_text("636f727265637420686f727365"), "correct horse");
  EXPECT_EQ(wpa::decode_hex_text("636f727265637420686f727365\n"), "correct horse");
  EXPECT_FALSE(wpa::decode_hex_text("63x"));
}

TEST(WpaControlTest, TheMacComesFromStatusWithoutColons) {
  const std::string status =
      "bssid=00:00:00:00:00:00\n"
      "wpa_state=DISCONNECTED\n"
      "p2p_device_address=8a:a2:9e:23:c4:b4\n"
      "address=88:A2:9E:23:C4:B4\n"
      "uuid=0123\n";
  EXPECT_EQ(wpa::status_mac(status), "88a29e23c4b4") << "address=, not p2p_device_address=";
  EXPECT_FALSE(wpa::status_mac("wpa_state=INACTIVE\n"));
  EXPECT_FALSE(wpa::status_mac("address=88:a2:9e\n"));
}
