//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "sd_bus_helpers.h"

#if RPILEDCONFIG_HAVE_SD_BUS

#include <cerrno>
#include <cstring>

namespace rpiledconfig::dbus {

namespace {

// The contents of the variant the message is positioned at, or empty when it is not at
// a variant.
std::string variant_contents(sd_bus_message* message) {
  char type = 0;
  const char* contents = nullptr;
  if (sd_bus_message_peek_type(message, &type, &contents) <= 0 || type != SD_BUS_TYPE_VARIANT || contents == nullptr) return {};
  return contents;
}

template <typename T>
int read_basic_variant(sd_bus_message* message, char type, T& out) {
  const char contents[2] = {type, '\0'};
  if (variant_contents(message) != contents) return 0;

  int r = sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, contents);
  if (r < 0) return r;
  r = sd_bus_message_read_basic(message, type, &out);
  if (r < 0) return r;
  r = sd_bus_message_exit_container(message);
  return r < 0 ? r : 1;
}

}  // namespace

int read_dict(sd_bus_message* message, const std::function<int(const std::string& key, sd_bus_message* message)>& on_entry) {
  int r = sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sv}");
  if (r < 0) return r;

  while ((r = sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
    const char* key = nullptr;
    r = sd_bus_message_read_basic(message, SD_BUS_TYPE_STRING, &key);
    if (r < 0) return r;

    r = on_entry(key ? key : "", message);
    if (r < 0) return r;
    if (r == 0) {
      r = sd_bus_message_skip(message, "v");
      if (r < 0) return r;
    }

    r = sd_bus_message_exit_container(message);
    if (r < 0) return r;
  }
  if (r < 0) return r;

  return sd_bus_message_exit_container(message);
}

int read_variant(sd_bus_message* message, std::string& out) {
  const auto contents = variant_contents(message);
  if (contents != "s" && contents != "o" && contents != "g") return 0;

  const char* value = nullptr;
  const int r = read_basic_variant(message, contents[0], value);
  if (r > 0) out = value ? value : "";
  return r;
}

int read_variant(sd_bus_message* message, bool& out) {
  int value = 0;
  const int r = read_basic_variant(message, SD_BUS_TYPE_BOOLEAN, value);
  if (r > 0) out = value != 0;
  return r;
}

int read_variant(sd_bus_message* message, std::uint8_t& out) { return read_basic_variant(message, SD_BUS_TYPE_BYTE, out); }
int read_variant(sd_bus_message* message, std::uint16_t& out) { return read_basic_variant(message, SD_BUS_TYPE_UINT16, out); }
int read_variant(sd_bus_message* message, std::uint32_t& out) { return read_basic_variant(message, SD_BUS_TYPE_UINT32, out); }
int read_variant(sd_bus_message* message, std::int64_t& out) { return read_basic_variant(message, SD_BUS_TYPE_INT64, out); }

int read_variant(sd_bus_message* message, types::Bytes& out) {
  if (variant_contents(message) != "ay") return 0;

  int r = sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, "ay");
  if (r < 0) return r;

  const void* data = nullptr;
  size_t size = 0;
  r = sd_bus_message_read_array(message, SD_BUS_TYPE_BYTE, &data, &size);
  if (r < 0) return r;

  const auto* bytes = static_cast<const std::uint8_t*>(data);
  out.assign(bytes, bytes + size);

  r = sd_bus_message_exit_container(message);
  return r < 0 ? r : 1;
}

int read_variant(sd_bus_message* message, std::vector<std::string>& out) {
  const auto contents = variant_contents(message);
  if (contents != "as" && contents != "ao") return 0;

  int r = sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, contents.c_str());
  if (r < 0) return r;
  r = read_strings(message, out);
  if (r < 0) return r;
  r = sd_bus_message_exit_container(message);
  return r < 0 ? r : 1;
}

int read_strings(sd_bus_message* message, std::vector<std::string>& out) {
  char type = 0;
  const char* contents = nullptr;
  int r = sd_bus_message_peek_type(message, &type, &contents);
  if (r < 0) return r;
  if (r == 0 || type != SD_BUS_TYPE_ARRAY || contents == nullptr || std::strlen(contents) != 1) return -ENXIO;

  const char element = contents[0];
  if (element != SD_BUS_TYPE_STRING && element != SD_BUS_TYPE_OBJECT_PATH) return -ENXIO;

  r = sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, contents);
  if (r < 0) return r;

  out.clear();
  const char* value = nullptr;
  while ((r = sd_bus_message_read_basic(message, element, &value)) > 0) out.emplace_back(value ? value : "");
  if (r < 0) return r;

  return sd_bus_message_exit_container(message);
}

int append_string_entry(sd_bus_message* message, const char* key, const std::string& value, char type) {
  const char contents[2] = {type, '\0'};

  int r = sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
  if (r < 0) return r;
  r = sd_bus_message_append_basic(message, SD_BUS_TYPE_STRING, key);
  if (r < 0) return r;
  r = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, contents);
  if (r < 0) return r;
  r = sd_bus_message_append_basic(message, type, value.c_str());
  if (r < 0) return r;
  r = sd_bus_message_close_container(message);
  if (r < 0) return r;
  return sd_bus_message_close_container(message);
}

int append_bool_entry(sd_bus_message* message, const char* key, bool value) {
  return sd_bus_message_append(message, "{sv}", key, "b", static_cast<int>(value));
}

int append_bytes_entry(sd_bus_message* message, const char* key, const types::Bytes& value) {
  int r = sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
  if (r < 0) return r;
  r = sd_bus_message_append_basic(message, SD_BUS_TYPE_STRING, key);
  if (r < 0) return r;
  r = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "ay");
  if (r < 0) return r;
  r = sd_bus_message_append_array(message, SD_BUS_TYPE_BYTE, value.data(), value.size());
  if (r < 0) return r;
  r = sd_bus_message_close_container(message);
  if (r < 0) return r;
  return sd_bus_message_close_container(message);
}

bool is_unavailable(const CallError& error) {
  // ServiceUnknown and NameHasNoOwner: nobody owns the name. NoReply and Timeout: the
  // owner did not answer (sd-bus says NoReply for its own timeout). Disconnected: our end
  // of the bus is gone. AccessDenied from the bus (not from the service) is a policy that
  // forbids the call, which no retry will change; it counts as failed, not unavailable.
  static const char* const names[] = {
      SD_BUS_ERROR_SERVICE_UNKNOWN, SD_BUS_ERROR_NAME_HAS_NO_OWNER, SD_BUS_ERROR_NO_REPLY, SD_BUS_ERROR_TIMEOUT, SD_BUS_ERROR_DISCONNECTED,
  };

  for (const char* name : names) {
    if (error.name == name) return true;
  }
  return false;
}

std::string basename(const std::string& path) {
  const auto slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace rpiledconfig::dbus

#endif
