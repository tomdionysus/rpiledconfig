//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "protocol/json.h"

#include <gtest/gtest.h>

#include <string>

using rpiledconfig::protocol::Json;
using rpiledconfig::protocol::json_quote;

TEST(JsonTest, WritesAFlatObjectCompactlyInOrder) {
  const auto text = Json::object().set("ok", Json::boolean(true)).set("hostname", Json::string("pi")).set("protocol", Json::number(1)).dump();
  EXPECT_EQ(text, "{\"ok\":true,\"hostname\":\"pi\",\"protocol\":1}");
}

TEST(JsonTest, WritesNestedObjectsAndArrays) {
  Json networks = Json::array();
  networks.push(Json::object().set("ssid", Json::string("home")).set("signal", Json::number(70)).set("secure", Json::boolean(true)));
  networks.push(Json::object().set("ssid", Json::string("cafe")).set("signal", Json::number(30)).set("secure", Json::boolean(false)));

  const auto text = Json::object().set("ok", Json::boolean(true)).set("networks", networks).dump();
  EXPECT_EQ(text, "{\"ok\":true,\"networks\":[{\"ssid\":\"home\",\"signal\":70,\"secure\":true},{\"ssid\":\"cafe\",\"signal\":30,\"secure\":false}]}");
}

TEST(JsonTest, EscapesWhatNeedsEscaping) {
  EXPECT_EQ(json_quote("plain"), "\"plain\"");
  EXPECT_EQ(json_quote("a \"quote\" and a \\ slash"), "\"a \\\"quote\\\" and a \\\\ slash\"");
  EXPECT_EQ(json_quote("line\nbreak\ttab"), "\"line\\nbreak\\ttab\"");
  EXPECT_EQ(json_quote(std::string("nul\x01")), "\"nul\\u0001\"");

  // UTF-8 goes through untouched: an SSID may be anything.
  EXPECT_EQ(json_quote("caf\xc3\xa9"), "\"caf\xc3\xa9\"");
}

TEST(JsonTest, WholeNumbersHaveNoPoint) {
  EXPECT_EQ(Json::number(70).dump(), "70");
  EXPECT_EQ(Json::number(-3).dump(), "-3");
  EXPECT_EQ(Json::number(0).dump(), "0");
  EXPECT_EQ(Json::number(1.5).dump(), "1.5");
}

TEST(JsonTest, ReadsAFlatRequest) {
  const auto json = Json::parse("{\"op\":\"wifi.configure\",\"ssid\":\"home\",\"psk\":\"secret\"}");
  ASSERT_TRUE(json);
  ASSERT_TRUE(json->is_object());

  ASSERT_NE(json->get("op"), nullptr);
  EXPECT_EQ(json->get("op")->as_string(), "wifi.configure");
  EXPECT_EQ(json->get("ssid")->as_string(), "home");
  EXPECT_EQ(json->get("psk")->as_string(), "secret");
  EXPECT_EQ(json->get("missing"), nullptr);
}

TEST(JsonTest, ReadsEveryKindWithWhitespace) {
  const auto json = Json::parse(" { \"a\" : 1 , \"b\" : -2.5e1 , \"c\" : true , \"d\" : null , \"e\" : [ 1 , \"x\" ] , \"f\" : { } } ");
  ASSERT_TRUE(json);

  EXPECT_DOUBLE_EQ(json->get("a")->as_number(), 1);
  EXPECT_DOUBLE_EQ(json->get("b")->as_number(), -25);
  EXPECT_TRUE(json->get("c")->as_boolean());
  EXPECT_TRUE(json->get("d")->is_null());
  ASSERT_TRUE(json->get("e")->is_array());
  ASSERT_EQ(json->get("e")->items().size(), 2u);
  EXPECT_EQ(json->get("e")->items()[1].as_string(), "x");
  EXPECT_TRUE(json->get("f")->is_object());
  EXPECT_TRUE(json->get("f")->members().empty());
}

TEST(JsonTest, ReadsEscapesIncludingUnicode) {
  const auto json = Json::parse("\"a\\\"b\\\\c\\n\\u00e9\\ud83d\\ude00\"");
  ASSERT_TRUE(json);
  EXPECT_EQ(json->as_string(), "a\"b\\c\n\xc3\xa9\xf0\x9f\x98\x80");
}

TEST(JsonTest, RefusesWhatIsNotJson) {
  EXPECT_FALSE(Json::parse(""));
  EXPECT_FALSE(Json::parse("{"));
  EXPECT_FALSE(Json::parse("{\"op\":}"));
  EXPECT_FALSE(Json::parse("{\"op\":\"status\"} trailing"));
  EXPECT_FALSE(Json::parse("{'op':'status'}"));
  EXPECT_FALSE(Json::parse("{\"op\":\"status\",}"));
  EXPECT_FALSE(Json::parse("[1,]"));
  EXPECT_FALSE(Json::parse("tru"));
  EXPECT_FALSE(Json::parse("\"unterminated"));
  EXPECT_FALSE(Json::parse("\"bad \\x escape\""));
  EXPECT_FALSE(Json::parse("\"raw\ncontrol\""));
  EXPECT_FALSE(Json::parse("01"));
}

TEST(JsonTest, RefusesNestingPastThirtyTwo) {
  std::string deep;
  for (int i = 0; i < 40; ++i) deep += "[";
  for (int i = 0; i < 40; ++i) deep += "]";
  EXPECT_FALSE(Json::parse(deep));

  std::string fine;
  for (int i = 0; i < 10; ++i) fine += "[";
  for (int i = 0; i < 10; ++i) fine += "]";
  EXPECT_TRUE(Json::parse(fine));
}

TEST(JsonTest, RoundTrips) {
  const std::string text = "{\"ok\":true,\"hostname\":\"pi \\\"one\\\"\",\"wifi\":{\"connected\":false},\"n\":[1,2,3]}";
  const auto json = Json::parse(text);
  ASSERT_TRUE(json);
  EXPECT_EQ(json->dump(), text);
}
