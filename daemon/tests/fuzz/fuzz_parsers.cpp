//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "protocol/auth.h"
#include "protocol/channel.h"
#include "protocol/fragments.h"
#include "protocol/json.h"
#include "protocol/keys.h"
#include "protocol/messages.h"
#include "protocol/spake2.h"
#include "util.h"

using namespace rpiledconfig;

namespace {

[[noreturn]] void violated(const char* what) {
  std::fprintf(stderr, "VIOLATED: %s\n", what);
  std::abort();
}

}  // namespace

// Each parser on its own, where a fuzzer can go deepest. Built with -DRPILEDCONFIG_FUZZ=ON.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) return 0;
  const types::Bytes bytes(data + 1, data + size);
  const std::string text(bytes.begin(), bytes.end());

  static const auto w = protocol::password_scalar(pairing_key::PairingKey::from_value(0x0123456789abcdefULL));
  static const auto y = protocol::spake2::random_scalar();
  static const auto p_b = protocol::spake2::share(protocol::spake2::Role::b, w, y);

  switch (data[0] % 5) {
    case 0: {
      const auto parsed = protocol::Json::parse(text);
      if (parsed) {
        // What it parses, it can write, and read back the same.
        const auto dumped = parsed->dump();
        const auto again = protocol::Json::parse(dumped);
        if (!again) violated("a parsed value dumped to something unparsable");
        if (again->dump() != dumped) violated("a value changed through a round trip");
      }
      break;
    }
    case 1: {
      const auto request = protocol::parse_request(text);
      if (request && request.value.op == protocol::ops::wifi_configure) {
        if (request.value.ssid.empty() || request.value.ssid.size() > 32) violated("an impossible SSID got through");
        if (request.value.ssid.find('\0') != std::string::npos) violated("a NUL got into an SSID");
        if (request.value.psk.find('\0') != std::string::npos) violated("a NUL got into a passphrase");
      }
      break;
    }
    case 2: {
      protocol::Reassembler reassembler(4096);
      std::size_t at = 0;
      while (at < bytes.size()) {
        const std::size_t length = std::min<std::size_t>(1 + bytes[at] % 64, bytes.size() - at);
        const auto pushed = reassembler.push(types::Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + length)));
        if (pushed && pushed.value && pushed.value->size() > 4096) violated("a message over the limit");
        at += length;
      }
      break;
    }
    case 3:
      if (protocol::accept_auth(w, y, p_b, bytes)) violated("an Auth write nobody could have made was accepted");
      break;
    case 4: {
      static const types::Bytes key(32, 0x42);
      protocol::Opener opener(key, protocol::Direction::app_to_daemon);
      if (opener.open(bytes)) violated("a message nobody could have sealed was opened");
      break;
    }
  }
  return 0;
}
