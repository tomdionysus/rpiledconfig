//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "ble_service.h"

namespace rpiledconfig::ble {

const char* uuid(Characteristic characteristic) {
  switch (characteristic) {
    case Characteristic::info:
      return "2a7f0002-7e1d-4c3a-9b5e-0f8a6d4c2b10";
    case Characteristic::challenge:
      return "2a7f0003-7e1d-4c3a-9b5e-0f8a6d4c2b10";
    case Characteristic::auth:
      return "2a7f0004-7e1d-4c3a-9b5e-0f8a6d4c2b10";
    case Characteristic::auth_result:
      return "2a7f0005-7e1d-4c3a-9b5e-0f8a6d4c2b10";
    case Characteristic::request:
      return "2a7f0006-7e1d-4c3a-9b5e-0f8a6d4c2b10";
    case Characteristic::response:
      return "2a7f0007-7e1d-4c3a-9b5e-0f8a6d4c2b10";
    case Characteristic::blink:
      return "2a7f0008-7e1d-4c3a-9b5e-0f8a6d4c2b10";
  }
  return "";
}

const char* name(Characteristic characteristic) {
  switch (characteristic) {
    case Characteristic::info:
      return "Info";
    case Characteristic::challenge:
      return "Challenge";
    case Characteristic::auth:
      return "Auth";
    case Characteristic::auth_result:
      return "AuthResult";
    case Characteristic::request:
      return "Request";
    case Characteristic::response:
      return "Response";
    case Characteristic::blink:
      return "Blink";
  }
  return "";
}

std::string local_name(const std::string& prefix, const std::string& suffix) { return prefix + "-" + suffix; }

}  // namespace rpiledconfig::ble
