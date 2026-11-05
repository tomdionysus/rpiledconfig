//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#if RPILEDCONFIG_HAVE_SD_BUS

#include <systemd/sd-bus.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sd_bus_loop.h"
#include "types/status.h"

namespace rpiledconfig::dbus {

// Reading what BlueZ and NetworkManager send, which is mostly a{sv} dictionaries of
// variants whose types are documented but not promised. Every reader checks the variant's
// type before reading it, and a value of an unexpected type is skipped rather than
// failing the whole message: a newer daemon adding or retyping a key must not break us.
//
// The int results are sd-bus's: negative errno on a malformed message, otherwise >= 0.

// Walks an a{sv}. on_entry is called with the message positioned at each value's variant;
// it returns > 0 when it read the variant, 0 to have it skipped, < 0 to stop.
int read_dict(sd_bus_message* message, const std::function<int(const std::string& key, sd_bus_message* message)>& on_entry);

// Reads the variant the message is positioned at when it holds the given type, and
// returns 1; returns 0 and reads nothing when it holds something else.
int read_variant(sd_bus_message* message, std::string& out);               // s, o or g
int read_variant(sd_bus_message* message, bool& out);                      // b
int read_variant(sd_bus_message* message, std::uint8_t& out);              // y
int read_variant(sd_bus_message* message, std::uint16_t& out);             // q
int read_variant(sd_bus_message* message, std::uint32_t& out);             // u
int read_variant(sd_bus_message* message, std::int64_t& out);              // x
int read_variant(sd_bus_message* message, types::Bytes& out);              // ay
int read_variant(sd_bus_message* message, std::vector<std::string>& out);  // as or ao

// Reads an array of strings or object paths (as, ao) the message is positioned at.
int read_strings(sd_bus_message* message, std::vector<std::string>& out);

// Appends {key: variant} to an open a{sv}. Named by type rather than overloaded: a string
// literal would otherwise convert to bool before it converted to std::string.
int append_string_entry(sd_bus_message* message, const char* key, const std::string& value, char type = 's');
int append_bool_entry(sd_bus_message* message, const char* key, bool value);
int append_bytes_entry(sd_bus_message* message, const char* key, const types::Bytes& value);

// Whether a failed call means the far end is not there at all (no owner for the name,
// no reply, the bus itself gone), as against the far end answering "no".
bool is_unavailable(const CallError& error);

// The last path element, for logs: dev_AA_BB_CC_DD_EE_FF from a BlueZ device path.
std::string basename(const std::string& path);

}  // namespace rpiledconfig::dbus

#endif
