//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "ble/ble_service.h"

#include <gtest/gtest.h>

#include <string>

using namespace rpiledconfig;

// docs/protocol.md, BLE: the service's characteristics, their UUIDs and their names.
TEST(BleServiceTest, EachCharacteristicHasTheProtocolsUuidAndName) {
  const struct {
    ble::Characteristic characteristic;
    const char* uuid;
    const char* name;
  } table[] = {
      {ble::Characteristic::info, "2a7f0002-7e1d-4c3a-9b5e-0f8a6d4c2b10", "Info"},
      {ble::Characteristic::challenge, "2a7f0003-7e1d-4c3a-9b5e-0f8a6d4c2b10", "Challenge"},
      {ble::Characteristic::auth, "2a7f0004-7e1d-4c3a-9b5e-0f8a6d4c2b10", "Auth"},
      {ble::Characteristic::auth_result, "2a7f0005-7e1d-4c3a-9b5e-0f8a6d4c2b10", "AuthResult"},
      {ble::Characteristic::request, "2a7f0006-7e1d-4c3a-9b5e-0f8a6d4c2b10", "Request"},
      {ble::Characteristic::response, "2a7f0007-7e1d-4c3a-9b5e-0f8a6d4c2b10", "Response"},
      {ble::Characteristic::blink, "2a7f0008-7e1d-4c3a-9b5e-0f8a6d4c2b10", "Blink"},
  };
  for (const auto& row : table) {
    EXPECT_STREQ(ble::uuid(row.characteristic), row.uuid) << row.name;
    EXPECT_STREQ(ble::name(row.characteristic), row.name);
  }
  EXPECT_STREQ(ble::service_uuid, "2a7f0001-7e1d-4c3a-9b5e-0f8a6d4c2b10");
}

// docs/protocol.md, BLE: the advertised name is the prefix, a hyphen and four hex digits.
TEST(BleServiceTest, TheLocalNameIsThePrefixAHyphenAndTheSuffix) {
  EXPECT_EQ(ble::local_name("RPiLEDConfig", "3a9c"), "RPiLEDConfig-3a9c");
  EXPECT_EQ(ble::local_name("Kitchen Pi", "0b1f"), "Kitchen Pi-0b1f");
}
