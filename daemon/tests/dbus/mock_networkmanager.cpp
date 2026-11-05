//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "mock_networkmanager.h"

#include <time.h>

#include <cerrno>
#include <cstring>

namespace {

constexpr const char* nm_path = "/org/freedesktop/NetworkManager";
constexpr const char* nm_interface = "org.freedesktop.NetworkManager";
constexpr const char* device_interface = "org.freedesktop.NetworkManager.Device";
constexpr const char* wireless_interface = "org.freedesktop.NetworkManager.Device.Wireless";
constexpr const char* active_interface = "org.freedesktop.NetworkManager.Connection.Active";

constexpr std::uint32_t activating = 1;
constexpr std::uint32_t activated = 2;
constexpr std::uint32_t deactivated = 4;

// One setting value: NetworkManager's settings are strings, byte arrays and booleans for
// everything the daemon sends.
struct Setting {
  std::string text;
  rpiledconfig::types::Bytes bytes;
  int boolean = -1;
};

using Settings = std::map<std::string, std::map<std::string, Setting>>;

int read_settings(sd_bus_message* m, Settings& out) {
  int r = sd_bus_message_enter_container(m, 'a', "{sa{sv}}");
  while (r >= 0 && (r = sd_bus_message_enter_container(m, 'e', "sa{sv}")) > 0) {
    const char* group = nullptr;
    r = sd_bus_message_read(m, "s", &group);
    if (r >= 0) r = sd_bus_message_enter_container(m, 'a', "{sv}");
    while (r >= 0 && (r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
      const char* key = nullptr;
      r = sd_bus_message_read(m, "s", &key);
      if (r < 0) break;

      char type = 0;
      const char* contents = nullptr;
      r = sd_bus_message_peek_type(m, &type, &contents);
      if (r < 0) break;

      auto& setting = out[group][key];
      if (std::strcmp(contents, "s") == 0) {
        const char* s = nullptr;
        r = sd_bus_message_read(m, "v", "s", &s);
        if (r >= 0) setting.text = s;
      } else if (std::strcmp(contents, "b") == 0) {
        r = sd_bus_message_read(m, "v", "b", &setting.boolean);
      } else if (std::strcmp(contents, "ay") == 0) {
        const void* data = nullptr;
        size_t size = 0;
        r = sd_bus_message_enter_container(m, 'v', "ay");
        if (r >= 0) r = sd_bus_message_read_array(m, 'y', &data, &size);
        if (r >= 0) setting.bytes.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + size);
        if (r >= 0) r = sd_bus_message_exit_container(m);
      } else {
        r = sd_bus_message_skip(m, "v");
      }
      if (r >= 0) r = sd_bus_message_exit_container(m);
    }
    if (r >= 0) r = sd_bus_message_exit_container(m);
    if (r >= 0) r = sd_bus_message_exit_container(m);
  }
  if (r >= 0) r = sd_bus_message_exit_container(m);
  return r;
}

std::int64_t now_ms() {
  struct timespec ts{};
  ::clock_gettime(CLOCK_BOOTTIME, &ts);
  return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

int append_paths(sd_bus_message* reply, const std::vector<std::string>& paths) {
  int r = sd_bus_message_open_container(reply, 'a', "o");
  for (const auto& path : paths) {
    if (r >= 0) r = sd_bus_message_append(reply, "o", path.c_str());
  }
  if (r >= 0) r = sd_bus_message_close_container(reply);
  return r;
}

}  // namespace

struct MockNetworkManager::Handlers {
  // --- NetworkManager ---

  static int get_devices(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<MockNetworkManager*>(userdata);
    std::vector<std::string> paths{self->_ethernet->path};
    if (self->_wifi) paths.push_back(self->_wifi->path);

    sd_bus_message* reply = nullptr;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r >= 0) r = append_paths(reply, paths);
    if (r >= 0) r = sd_bus_send(nullptr, reply, nullptr);
    sd_bus_message_unref(reply);
    return r;
  }

  static int add_and_activate(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* self = static_cast<MockNetworkManager*>(userdata);

    Settings settings;
    int r = read_settings(m, settings);
    if (r < 0) return r;
    const char* device = nullptr;
    const char* specific = nullptr;
    r = sd_bus_message_read(m, "oo", &device, &specific);
    if (r < 0) return r;

    if (!self->_refuse_name.empty()) {
      const auto name = self->_refuse_name;
      const auto message = self->_refuse_message;
      self->_refuse_name.clear();
      return sd_bus_error_set(error, name.c_str(), message.c_str());
    }

    if (!self->_wifi || self->_wifi->path != device) return sd_bus_error_set(error, "org.freedesktop.NetworkManager.UnknownDevice", "Unknown device");

    Saved saved;
    saved.id = settings["connection"]["id"].text;
    saved.type = settings["connection"]["type"].text;
    saved.ssid.assign(settings["802-11-wireless"]["ssid"].bytes.begin(), settings["802-11-wireless"]["ssid"].bytes.end());
    if (saved.ssid.empty())
      return sd_bus_error_set(error, "org.freedesktop.NetworkManager.Settings.Connection.InvalidProperty", "802-11-wireless.ssid: property is missing");

    if (settings.count("802-11-wireless-security")) {
      saved.key_mgmt = settings["802-11-wireless-security"]["key-mgmt"].text;
      saved.psk = settings["802-11-wireless-security"]["psk"].text;
      if (saved.key_mgmt == "wpa-psk" && (saved.psk.size() < 8 || saved.psk.size() > 63)) {
        return sd_bus_error_set(error, "org.freedesktop.NetworkManager.Settings.Connection.InvalidProperty",
                                "802-11-wireless-security.psk: property is invalid");
      }
    }
    if (saved.id.empty()) saved.id = saved.ssid;

    const auto connection = self->_add_connection(saved);
    const auto active = self->_activate(connection, saved.id, saved.ssid, "192.0.2.50", false);
    return sd_bus_reply_method_return(m, "oo", connection.c_str(), active.c_str());
  }

  // --- Device ---

  static int device_type(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "u", static_cast<DeviceObject*>(userdata)->type);
  }
  static int device_interface_name(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", static_cast<DeviceObject*>(userdata)->interface.c_str());
  }
  static int device_active(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "o", static_cast<DeviceObject*>(userdata)->active.c_str());
  }
  static int device_ip4(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "o", static_cast<DeviceObject*>(userdata)->ip4.c_str());
  }

  // --- Device.Wireless ---

  static int get_all_access_points(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* device = static_cast<DeviceObject*>(userdata);
    std::vector<std::string> paths;
    for (const auto& [path, ap] : device->owner->_access_points) paths.push_back(path);

    sd_bus_message* reply = nullptr;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r >= 0) r = append_paths(reply, paths);
    if (r >= 0) r = sd_bus_send(nullptr, reply, nullptr);
    sd_bus_message_unref(reply);
    return r;
  }

  static int request_scan(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* device = static_cast<DeviceObject*>(userdata);
    auto* self = device->owner;
    const int r = sd_bus_message_skip(m, "a{sv}");
    if (r < 0) return r;

    ++self->_scans_requested;
    if (self->_scan == Scan::refused)
      return sd_bus_error_set(error, "org.freedesktop.NetworkManager.PermissionDenied", "Not authorized to perform this operation");
    if (self->_scan == Scan::completes) self->later(std::chrono::milliseconds(50), [self]() { self->_finish_scan(); });
    return sd_bus_reply_method_return(m, "");
  }

  static int last_scan(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "x", static_cast<DeviceObject*>(userdata)->last_scan);
  }
  static int active_access_point(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "o", static_cast<DeviceObject*>(userdata)->access_point.c_str());
  }

  // --- AccessPoint ---

  static int ap_ssid(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    const auto& ssid = static_cast<AccessPointObject*>(userdata)->ap.ssid;
    return sd_bus_message_append_array(reply, 'y', ssid.data(), ssid.size());
  }
  static int ap_strength(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "y", static_cast<AccessPointObject*>(userdata)->ap.strength);
  }
  static int ap_flags(sd_bus*, const char*, const char*, const char* property, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    const auto& ap = static_cast<AccessPointObject*>(userdata)->ap;
    const std::uint32_t value = std::strcmp(property, "Flags") == 0 ? ap.flags : std::strcmp(property, "WpaFlags") == 0 ? ap.wpa_flags : ap.rsn_flags;
    return sd_bus_message_append(reply, "u", value);
  }

  // --- Settings ---

  static int list_connections(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<MockNetworkManager*>(userdata);
    std::vector<std::string> paths;
    for (const auto& [path, connection] : self->_connections) paths.push_back(path);

    sd_bus_message* reply = nullptr;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r >= 0) r = append_paths(reply, paths);
    if (r >= 0) r = sd_bus_send(nullptr, reply, nullptr);
    sd_bus_message_unref(reply);
    return r;
  }

  static int get_settings(sd_bus_message* m, void* userdata, sd_bus_error*) {
    const auto& saved = static_cast<ConnectionObject*>(userdata)->saved;

    sd_bus_message* reply = nullptr;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r >= 0) r = sd_bus_message_open_container(reply, 'a', "{sa{sv}}");

    if (r >= 0) r = sd_bus_message_open_container(reply, 'e', "sa{sv}");
    if (r >= 0) r = sd_bus_message_append(reply, "s", "connection");
    if (r >= 0) r = sd_bus_message_append(reply, "a{sv}", 2, "id", "s", saved.id.c_str(), "type", "s", saved.type.c_str());
    if (r >= 0) r = sd_bus_message_close_container(reply);

    if (r >= 0) r = sd_bus_message_open_container(reply, 'e', "sa{sv}");
    if (r >= 0) r = sd_bus_message_append(reply, "s", "802-11-wireless");
    if (r >= 0) r = sd_bus_message_open_container(reply, 'a', "{sv}");
    if (r >= 0) r = sd_bus_message_open_container(reply, 'e', "sv");
    if (r >= 0) r = sd_bus_message_append(reply, "s", "ssid");
    if (r >= 0) r = sd_bus_message_open_container(reply, 'v', "ay");
    if (r >= 0) r = sd_bus_message_append_array(reply, 'y', saved.ssid.data(), saved.ssid.size());
    if (r >= 0) r = sd_bus_message_close_container(reply);
    if (r >= 0) r = sd_bus_message_close_container(reply);
    if (r >= 0) r = sd_bus_message_close_container(reply);
    if (r >= 0) r = sd_bus_message_close_container(reply);

    // Secrets are not in GetSettings: key-mgmt is, psk is not.
    if (r >= 0 && !saved.key_mgmt.empty()) {
      r = sd_bus_message_open_container(reply, 'e', "sa{sv}");
      if (r >= 0) r = sd_bus_message_append(reply, "s", "802-11-wireless-security");
      if (r >= 0) r = sd_bus_message_append(reply, "a{sv}", 1, "key-mgmt", "s", saved.key_mgmt.c_str());
      if (r >= 0) r = sd_bus_message_close_container(reply);
    }

    if (r >= 0) r = sd_bus_message_close_container(reply);
    if (r >= 0) r = sd_bus_send(nullptr, reply, nullptr);
    sd_bus_message_unref(reply);
    return r;
  }

  static int delete_connection(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* connection = static_cast<ConnectionObject*>(userdata);
    auto* self = connection->owner;
    const std::string path = connection->path;

    ++self->_deletes;

    if (self->_wifi && self->_wifi->active != "/") {
      auto active = self->_actives.find(self->_wifi->active);
      if (active != self->_actives.end() && active->second->connection == path) self->_deactivate_device();
    }

    // Replied before the object goes, because the object's slot is what is running.
    const int r = sd_bus_reply_method_return(m, "");
    auto it = self->_connections.find(path);
    if (it != self->_connections.end()) {
      auto doomed = std::move(it->second);
      self->_connections.erase(it);
      self->later(std::chrono::milliseconds(0), [self, doomed = std::shared_ptr<ConnectionObject>(std::move(doomed))]() { self->_free(*doomed); });
    }
    return r;
  }

  // --- Connection.Active ---

  static int active_id(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", static_cast<ActiveObject*>(userdata)->id.c_str());
  }
  static int active_state(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "u", static_cast<ActiveObject*>(userdata)->state);
  }
  static int active_connection(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "o", static_cast<ActiveObject*>(userdata)->connection.c_str());
  }

  // --- IP4Config ---

  static int address_data(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    const auto& address = static_cast<Ip4Object*>(userdata)->address;
    return sd_bus_message_append(reply, "aa{sv}", 1, 2, "address", "s", address.c_str(), "prefix", "u", 24u);
  }
};

namespace {

using H = MockNetworkManager::Handlers;

const sd_bus_vtable nm_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetDevices", "", "ao", H::get_devices, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AddAndActivateConnection", "a{sa{sv}}oo", "oo", H::add_and_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable device_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("DeviceType", "u", H::device_type, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Interface", "s", H::device_interface_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ActiveConnection", "o", H::device_active, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Ip4Config", "o", H::device_ip4, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_SIGNAL("StateChanged", "uuu", 0),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable wireless_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetAllAccessPoints", "", "ao", H::get_all_access_points, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestScan", "a{sv}", "", H::request_scan, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("LastScan", "x", H::last_scan, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ActiveAccessPoint", "o", H::active_access_point, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable access_point_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Ssid", "ay", H::ap_ssid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Strength", "y", H::ap_strength, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Flags", "u", H::ap_flags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("WpaFlags", "u", H::ap_flags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("RsnFlags", "u", H::ap_flags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable settings_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListConnections", "", "ao", H::list_connections, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable connection_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetSettings", "", "a{sa{sv}}", H::get_settings, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Delete", "", "", H::delete_connection, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable active_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Id", "s", H::active_id, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("State", "u", H::active_state, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Connection", "o", H::active_connection, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_SIGNAL("StateChanged", "uu", 0),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable ip4_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("AddressData", "aa{sv}", H::address_data, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END,
};

}  // namespace

MockNetworkManager::MockNetworkManager(std::string address, std::vector<AccessPoint> access_points, bool with_wifi_device)
    : MockService(std::move(address)), _initial(std::move(access_points)), _with_wifi_device(with_wifi_device) {}

MockNetworkManager::~MockNetworkManager() { stop(); }

int MockNetworkManager::setup() {
  sd_bus_slot* slot = nullptr;

  int r = sd_bus_add_object_vtable(bus(), &slot, nm_path, nm_interface, nm_vtable, this);
  if (r < 0) return r;
  _slots.push_back(slot);

  r = sd_bus_add_object_vtable(bus(), &slot, "/org/freedesktop/NetworkManager/Settings", "org.freedesktop.NetworkManager.Settings", settings_vtable, this);
  if (r < 0) return r;
  _slots.push_back(slot);

  _ethernet = std::make_unique<DeviceObject>();
  _ethernet->owner = this;
  _ethernet->path = "/org/freedesktop/NetworkManager/Devices/1";
  _ethernet->type = 1;
  _ethernet->interface = "eth0";
  r = sd_bus_add_object_vtable(bus(), &slot, _ethernet->path.c_str(), device_interface, device_vtable, _ethernet.get());
  if (r < 0) return r;
  _ethernet->slots.push_back(slot);

  if (_with_wifi_device) {
    _wifi = std::make_unique<DeviceObject>();
    _wifi->owner = this;
    _wifi->path = "/org/freedesktop/NetworkManager/Devices/2";
    _wifi->type = 2;
    _wifi->interface = "wlan0";
    r = sd_bus_add_object_vtable(bus(), &slot, _wifi->path.c_str(), device_interface, device_vtable, _wifi.get());
    if (r < 0) return r;
    _wifi->slots.push_back(slot);
    r = sd_bus_add_object_vtable(bus(), &slot, _wifi->path.c_str(), wireless_interface, wireless_vtable, _wifi.get());
    if (r < 0) return r;
    _wifi->slots.push_back(slot);
  }

  for (const auto& ap : _initial) _add_access_point(ap);
  return 0;
}

void MockNetworkManager::teardown() {
  if (_ethernet) _free(*_ethernet);
  if (_wifi) _free(*_wifi);
  for (auto& [path, object] : _access_points) _free(*object);
  for (auto& [path, object] : _connections) _free(*object);
  for (auto& [path, object] : _actives) _free(*object);
  for (auto& [path, object] : _ip4s) _free(*object);
  for (auto* slot : _slots) sd_bus_slot_unref(slot);
  _slots.clear();
}

void MockNetworkManager::_free(Object& object) {
  for (auto* slot : object.slots) sd_bus_slot_unref(slot);
  object.slots.clear();
}

std::string MockNetworkManager::_add_access_point(const AccessPoint& ap) {
  auto object = std::make_unique<AccessPointObject>();
  object->owner = this;
  object->path = "/org/freedesktop/NetworkManager/AccessPoint/" + std::to_string(_next++);
  object->ap = ap;

  sd_bus_slot* slot = nullptr;
  if (sd_bus_add_object_vtable(bus(), &slot, object->path.c_str(), "org.freedesktop.NetworkManager.AccessPoint", access_point_vtable, object.get()) >= 0) {
    object->slots.push_back(slot);
  }

  const auto path = object->path;
  _access_points[path] = std::move(object);
  return path;
}

std::string MockNetworkManager::_add_connection(Saved saved) {
  auto object = std::make_unique<ConnectionObject>();
  object->owner = this;
  object->path = "/org/freedesktop/NetworkManager/Settings/" + std::to_string(_next++);
  saved.path = object->path;
  object->saved = std::move(saved);

  sd_bus_slot* slot = nullptr;
  if (sd_bus_add_object_vtable(bus(), &slot, object->path.c_str(), "org.freedesktop.NetworkManager.Settings.Connection", connection_vtable, object.get()) >=
      0) {
    object->slots.push_back(slot);
  }

  const auto path = object->path;
  _connections[path] = std::move(object);
  return path;
}

std::string MockNetworkManager::_activate(const std::string& connection, const std::string& id, const std::string& ssid, const std::string& ip, bool now) {
  auto active = std::make_unique<ActiveObject>();
  active->owner = this;
  active->path = "/org/freedesktop/NetworkManager/ActiveConnection/" + std::to_string(_next++);
  active->id = id;
  active->connection = connection;
  active->state = now ? activated : activating;

  sd_bus_slot* slot = nullptr;
  if (sd_bus_add_object_vtable(bus(), &slot, active->path.c_str(), active_interface, active_vtable, active.get()) >= 0) active->slots.push_back(slot);

  const auto path = active->path;
  _actives[path] = std::move(active);

  // The device's previous connection, if any, is replaced.
  _wifi->active = path;
  _wifi->ip4 = "/";
  _wifi->access_point = "/";
  sd_bus_emit_properties_changed(bus(), _wifi->path.c_str(), device_interface, "ActiveConnection", "Ip4Config", nullptr);

  auto finish = [this, path, ssid, ip]() {
    auto it = _actives.find(path);
    if (it == _actives.end() || _wifi->active != path) return;

    const bool ok = _activation_succeeds;
    it->second->state = ok ? activated : deactivated;

    // As NetworkManager does it: the device fails with the reason first (120 is failed,
    // 50 config, the reason NMDeviceStateReason), then the active connection is
    // deactivated with device-disconnected (3).
    if (!ok) sd_bus_emit_signal(bus(), _wifi->path.c_str(), device_interface, "StateChanged", "uuu", 120u, 50u, _failure_reason);
    sd_bus_emit_signal(bus(), path.c_str(), active_interface, "StateChanged", "uu", it->second->state, ok ? 0u : 3u);
    sd_bus_emit_properties_changed(bus(), path.c_str(), active_interface, "State", nullptr);

    if (!ok) return _deactivate_device();

    auto ip4 = std::make_unique<Ip4Object>();
    ip4->owner = this;
    ip4->path = "/org/freedesktop/NetworkManager/IP4Config/" + std::to_string(_next++);
    ip4->address = ip;
    sd_bus_slot* slot = nullptr;
    if (sd_bus_add_object_vtable(bus(), &slot, ip4->path.c_str(), "org.freedesktop.NetworkManager.IP4Config", ip4_vtable, ip4.get()) >= 0)
      ip4->slots.push_back(slot);
    _wifi->ip4 = ip4->path;
    _ip4s[ip4->path] = std::move(ip4);

    for (const auto& [ap_path, ap] : _access_points) {
      if (ap->ap.ssid == ssid) _wifi->access_point = ap_path;
    }

    sd_bus_emit_properties_changed(bus(), _wifi->path.c_str(), device_interface, "Ip4Config", nullptr);
    sd_bus_emit_properties_changed(bus(), _wifi->path.c_str(), wireless_interface, "ActiveAccessPoint", nullptr);
  };

  if (now) {
    finish();
  } else if (!_activation_hangs) {
    later(std::chrono::milliseconds(50), finish);
  }

  return path;
}

void MockNetworkManager::_deactivate_device() {
  _wifi->active = "/";
  _wifi->ip4 = "/";
  _wifi->access_point = "/";
  sd_bus_emit_properties_changed(bus(), _wifi->path.c_str(), device_interface, "ActiveConnection", "Ip4Config", nullptr);
}

void MockNetworkManager::_finish_scan() {
  for (const auto& ap : _found_by_scan) _add_access_point(ap);
  _found_by_scan.clear();

  _wifi->last_scan = now_ms();
  sd_bus_emit_properties_changed(bus(), _wifi->path.c_str(), wireless_interface, "LastScan", nullptr);
}

void MockNetworkManager::set_scan(Scan scan) {
  run([this, scan]() { _scan = scan; });
}

void MockNetworkManager::found_by_scan(AccessPoint access_point) {
  run([this, access_point]() { _found_by_scan.push_back(access_point); });
}

void MockNetworkManager::connect_existing(const std::string& id, const std::string& ssid, const std::string& ip) {
  run([this, id, ssid, ip]() {
    Saved saved;
    saved.id = id;
    saved.type = "802-11-wireless";
    saved.ssid = ssid;
    saved.key_mgmt = "wpa-psk";
    const auto connection = _add_connection(saved);
    _activate(connection, id, ssid, ip, true);
  });
}

void MockNetworkManager::refuse_add(const std::string& name, const std::string& message) {
  run([this, name, message]() {
    _refuse_name = name;
    _refuse_message = message;
  });
}

void MockNetworkManager::activation_succeeds(bool succeeds) {
  run([this, succeeds]() { _activation_succeeds = succeeds; });
}

void MockNetworkManager::activation_fails_with(std::uint32_t device_reason) {
  run([this, device_reason]() {
    _activation_succeeds = false;
    _failure_reason = device_reason;
  });
}

void MockNetworkManager::activation_hangs(bool hangs) {
  run([this, hangs]() { _activation_hangs = hangs; });
}

int MockNetworkManager::scans_requested() {
  return run([this]() { return _scans_requested; });
}

std::vector<MockNetworkManager::Saved> MockNetworkManager::connections() {
  return run([this]() {
    std::vector<Saved> out;
    for (const auto& [path, connection] : _connections) out.push_back(connection->saved);
    return out;
  });
}

int MockNetworkManager::deletes() {
  return run([this]() { return _deletes; });
}

std::string MockNetworkManager::active_ssid() {
  return run([this]() -> std::string {
    if (!_wifi || _wifi->active == "/") return "";
    auto it = _actives.find(_wifi->active);
    if (it == _actives.end()) return "";
    for (const auto& [path, connection] : _connections) {
      if (path == it->second->connection) return connection->saved.ssid;
    }
    return "";
  });
}
