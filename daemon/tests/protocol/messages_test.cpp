//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/messages.h"

#include <gtest/gtest.h>

#include "protocol/json.h"

using namespace rpiledconfig::protocol;

TEST(MessagesTest, AStatusRequestParses) {
  const auto request = parse_request("{\"op\":\"status\"}");
  ASSERT_TRUE(request) << request.error;
  EXPECT_EQ(request.value.op, "status");
}

TEST(MessagesTest, AConfigureRequestCarriesTheSsidAndPsk) {
  const auto request = parse_request("{\"op\":\"wifi.configure\",\"ssid\":\"home\",\"psk\":\"secret\"}");
  ASSERT_TRUE(request) << request.error;
  EXPECT_EQ(request.value.op, "wifi.configure");
  EXPECT_EQ(request.value.ssid, "home");
  EXPECT_EQ(request.value.psk, "secret");

  // An open network: psk absent or empty.
  EXPECT_TRUE(parse_request("{\"op\":\"wifi.configure\",\"ssid\":\"open\"}"));
  EXPECT_TRUE(parse_request("{\"op\":\"wifi.configure\",\"ssid\":\"open\",\"psk\":\"\"}"));
  EXPECT_TRUE(parse_request("{\"op\":\"wifi.configure\",\"ssid\":\"open\",\"psk\":null}"));
}

// docs/protocol.md, Messages: hidden is optional, for a network typed by hand.
TEST(MessagesTest, AConfigureRequestMayBeForAHiddenNetwork) {
  EXPECT_FALSE(parse_request("{\"op\":\"wifi.configure\",\"ssid\":\"home\"}").value.hidden);

  const auto hidden = parse_request("{\"op\":\"wifi.configure\",\"ssid\":\"attic\",\"psk\":\"secret12\",\"hidden\":true}");
  ASSERT_TRUE(hidden) << hidden.error;
  EXPECT_TRUE(hidden.value.hidden);

  const auto wrong = parse_request("{\"op\":\"wifi.configure\",\"ssid\":\"attic\",\"hidden\":\"yes\"}");
  EXPECT_FALSE(wrong);
  EXPECT_EQ(wrong.code, errors::bad_request);
}

TEST(MessagesTest, WhatIsNotARequestIsBadRequest) {
  for (const char* text : {"", "not json", "[1]", "{}", "{\"op\":1}", "{\"op\":\"\"}", "{\"op\":\"wifi.configure\"}",
                           "{\"op\":\"wifi.configure\",\"ssid\":\"\"}", "{\"op\":\"wifi.configure\",\"ssid\":\"x\",\"psk\":5}"}) {
    const auto request = parse_request(text);
    EXPECT_FALSE(request) << text;
    EXPECT_EQ(request.code, errors::bad_request) << text;
  }
}

// An op nobody knows is a request all the same; the dispatcher answers unknown_op.
TEST(MessagesTest, AnUnknownOpStillParses) {
  const auto request = parse_request("{\"op\":\"reboot\"}");
  ASSERT_TRUE(request);
  EXPECT_EQ(request.value.op, "reboot");
}

TEST(MessagesTest, TheResponsesAreAsTheProtocolShowsThem) {
  EXPECT_EQ(ok_response(), "{\"ok\":true}");
  EXPECT_EQ(error_response(errors::unknown_op, "no such op"), "{\"ok\":false,\"error\":\"unknown_op\",\"message\":\"no such op\"}");

  WifiStatus connected;
  connected.connected = true;
  connected.ssid = "home";
  connected.ip = "192.168.1.20";
  EXPECT_EQ(status_response("pi", connected), "{\"ok\":true,\"hostname\":\"pi\",\"wifi\":{\"connected\":true,\"ssid\":\"home\",\"ip\":\"192.168.1.20\"}}");

  WifiStatus down;
  down.ssid = "stale";
  // docs/protocol.md: a Pi set not to report it leaves the field out, rather than sending it empty.
  EXPECT_EQ(status_response("", down), "{\"ok\":true,\"wifi\":{\"connected\":false}}");
  EXPECT_EQ(status_response("pi", down), "{\"ok\":true,\"hostname\":\"pi\",\"wifi\":{\"connected\":false}}") << "no ssid or ip when not connected";

  EXPECT_EQ(info_json("0.1.0"), "{\"protocol\":4,\"version\":\"0.1.0\"}");
}

TEST(MessagesTest, AScanIsSortedStrongestFirstWithOneEntryPerSsid) {
  const std::vector<WifiNetwork> networks{
      {"cafe", 30, false}, {"home", 55, true}, {"home", 80, true}, {"office", 80, true}, {"", 99, true}, {"far", -5, true}, {"loud", 140, false},
  };

  const auto json = Json::parse(scan_response(networks));
  ASSERT_TRUE(json);
  const auto* list = json->get("networks");
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->items().size(), 5u) << "two homes become one, the hidden network is left out";

  EXPECT_EQ(list->items()[0].get("ssid")->as_string(), "loud");
  EXPECT_EQ(list->items()[0].get("signal")->as_number(), 100) << "clamped";
  EXPECT_EQ(list->items()[1].get("ssid")->as_string(), "home");
  EXPECT_EQ(list->items()[1].get("signal")->as_number(), 80) << "the strongest of the two";
  EXPECT_EQ(list->items()[2].get("ssid")->as_string(), "office");
  EXPECT_EQ(list->items()[3].get("ssid")->as_string(), "cafe");
  EXPECT_FALSE(list->items()[3].get("secure")->as_boolean());
  EXPECT_EQ(list->items()[4].get("ssid")->as_string(), "far");
  EXPECT_EQ(list->items()[4].get("signal")->as_number(), 0);

  EXPECT_EQ(scan_response({}), "{\"ok\":true,\"networks\":[]}");
}
