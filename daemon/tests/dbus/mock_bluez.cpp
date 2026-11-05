//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "mock_bluez.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace {

constexpr const char* adapter_path = "/org/bluez/hci0";
constexpr const char* characteristic_interface = "org.bluez.GattCharacteristic1";

// One property value, of the few types BlueZ reads from an application.
struct Prop {
  std::string text;
  int boolean = -1;
  std::vector<std::string> list;
};

int read_prop(sd_bus_message* m, Prop& out) {
  char type = 0;
  const char* contents = nullptr;
  int r = sd_bus_message_peek_type(m, &type, &contents);
  if (r <= 0 || type != 'v' || contents == nullptr) return r < 0 ? r : -ENXIO;

  const std::string c = contents;
  if (c == "s" || c == "o") {
    const char* s = nullptr;
    r = sd_bus_message_read(m, "v", contents, &s);
    if (r >= 0) out.text = s ? s : "";
    return r;
  }
  if (c == "b") {
    int b = 0;
    r = sd_bus_message_read(m, "v", "b", &b);
    if (r >= 0) out.boolean = b;
    return r;
  }
  if (c == "as" || c == "ao") {
    r = sd_bus_message_enter_container(m, 'v', contents);
    if (r < 0) return r;
    r = sd_bus_message_enter_container(m, 'a', contents + 1);
    if (r < 0) return r;
    const char* s = nullptr;
    while ((r = sd_bus_message_read_basic(m, contents[1], &s)) > 0) out.list.emplace_back(s);
    if (r < 0) return r;
    r = sd_bus_message_exit_container(m);
    if (r < 0) return r;
    return sd_bus_message_exit_container(m);
  }
  return sd_bus_message_skip(m, "v");
}

// An a{sv} into a map.
int read_props(sd_bus_message* m, std::map<std::string, Prop>& out) {
  int r = sd_bus_message_enter_container(m, 'a', "{sv}");
  if (r < 0) return r;
  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char* key = nullptr;
    r = sd_bus_message_read(m, "s", &key);
    if (r < 0) return r;
    r = read_prop(m, out[key]);
    if (r < 0) return r;
    r = sd_bus_message_exit_container(m);
    if (r < 0) return r;
  }
  if (r < 0) return r;
  return sd_bus_message_exit_container(m);
}

std::string device_path_for(const std::string& address) {
  std::string path = std::string(adapter_path) + "/dev_" + address;
  std::replace(path.begin(), path.end(), ':', '_');
  return path;
}

bool known_flag(const std::string& flag) {
  static const char* const flags[] = {"broadcast",
                                      "read",
                                      "write-without-response",
                                      "write",
                                      "notify",
                                      "indicate",
                                      "authenticated-signed-writes",
                                      "extended-properties",
                                      "reliable-write",
                                      "writable-auxiliaries",
                                      "encrypt-read",
                                      "encrypt-write",
                                      "encrypt-notify",
                                      "encrypt-indicate",
                                      "encrypt-authenticated-read",
                                      "encrypt-authenticated-write",
                                      "encrypt-authenticated-notify",
                                      "encrypt-authenticated-indicate",
                                      "secure-read",
                                      "secure-write",
                                      "secure-notify",
                                      "secure-indicate",
                                      "authorize"};
  for (const char* f : flags) {
    if (flag == f) return true;
  }
  return false;
}

// The request a deferred reply answers.
struct PendingRegistration {
  MockBluez* self = nullptr;
  sd_bus_message* request = nullptr;
  std::string owner;
  std::string path;
};

}  // namespace

struct MockBluez::Handlers {
  // --- Adapter1 ---

  static int get_powered(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "b", static_cast<MockBluez*>(userdata)->_powered ? 1 : 0);
  }

  static int set_powered(sd_bus* bus, const char* path, const char* interface, const char* property, sd_bus_message* value, void* userdata, sd_bus_error*) {
    int b = 0;
    const int r = sd_bus_message_read(value, "b", &b);
    if (r < 0) return r;
    static_cast<MockBluez*>(userdata)->_powered = b != 0;
    return sd_bus_emit_properties_changed(bus, path, interface, property, nullptr);
  }

  static int get_address(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "B8:27:EB:00:00:01");
  }

  // --- GattManager1 ---

  static int register_application(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* self = static_cast<MockBluez*>(userdata);
    const char* path = nullptr;
    int r = sd_bus_message_read(m, "o", &path);
    if (r < 0) return r;

    if (self->_application) return sd_bus_error_set(error, "org.bluez.Error.AlreadyExists", "Already Exists");
    if (!self->_refuse_application.empty()) {
      const auto message = self->_refuse_application;
      self->_refuse_application.clear();
      return sd_bus_error_set(error, "org.bluez.Error.Failed", message.c_str());
    }

    auto* pending = new PendingRegistration{self, sd_bus_message_ref(m), sd_bus_message_get_sender(m), path};
    r = sd_bus_call_method_async(sd_bus_message_get_bus(m), nullptr, pending->owner.c_str(), path, "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
                                 &Handlers::managed_objects, pending, "");
    if (r < 0) {
      sd_bus_message_unref(pending->request);
      delete pending;
      return r;
    }
    return 1;
  }

  static int managed_objects(sd_bus_message* reply, void* userdata, sd_bus_error*) {
    std::unique_ptr<PendingRegistration> pending(static_cast<PendingRegistration*>(userdata));
    auto* self = pending->self;

    auto refuse = [&](const std::string& why) {
      sd_bus_reply_method_errorf(pending->request, "org.bluez.Error.Failed", "%s", why.c_str());
      sd_bus_message_unref(pending->request);
      return 0;
    };

    if (sd_bus_message_is_method_error(reply, nullptr)) return refuse(std::string("GetManagedObjects failed: ") + sd_bus_message_get_error(reply)->message);

    std::map<std::string, std::map<std::string, std::map<std::string, Prop>>> objects;
    int r = sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");
    while (r >= 0 && (r = sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}")) > 0) {
      const char* object = nullptr;
      r = sd_bus_message_read(reply, "o", &object);
      if (r < 0) break;
      r = sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
      while (r >= 0 && (r = sd_bus_message_enter_container(reply, 'e', "sa{sv}")) > 0) {
        const char* interface = nullptr;
        r = sd_bus_message_read(reply, "s", &interface);
        if (r >= 0) r = read_props(reply, objects[object][interface]);
        if (r >= 0) r = sd_bus_message_exit_container(reply);
      }
      if (r >= 0) r = sd_bus_message_exit_container(reply);
      if (r >= 0) r = sd_bus_message_exit_container(reply);
    }
    if (r < 0) return refuse("malformed GetManagedObjects reply");

    Application app;
    app.owner = pending->owner;
    app.path = pending->path;

    for (auto& [path, interfaces] : objects) {
      if (path.rfind(app.path, 0) != 0) continue;
      auto service = interfaces.find("org.bluez.GattService1");
      if (service == interfaces.end()) continue;
      if (service->second["UUID"].text.empty() || service->second["Primary"].boolean < 0) return refuse("service without UUID or Primary");
      app.service_path = path;
      app.service_uuid = service->second["UUID"].text;
      app.primary = service->second["Primary"].boolean == 1;
    }
    if (app.service_path.empty()) return refuse("No object received");

    for (auto& [path, interfaces] : objects) {
      auto characteristic = interfaces.find(characteristic_interface);
      if (characteristic == interfaces.end()) continue;
      auto& props = characteristic->second;
      if (props["UUID"].text.empty() || props["Service"].text != app.service_path) return refuse("characteristic " + path + " without UUID or Service");
      if (props["Flags"].list.empty()) return refuse("characteristic " + path + " without Flags");
      for (const auto& flag : props["Flags"].list) {
        if (!known_flag(flag)) return refuse("unknown flag " + flag);
      }
      app.characteristic_paths[props["UUID"].text] = path;
      app.characteristic_flags[props["UUID"].text] = props["Flags"].list;
    }

    self->_application = app;
    sd_bus_reply_method_return(pending->request, "");
    sd_bus_message_unref(pending->request);
    return 0;
  }

  static int unregister_application(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* self = static_cast<MockBluez*>(userdata);
    const char* path = nullptr;
    const int r = sd_bus_message_read(m, "o", &path);
    if (r < 0) return r;
    if (!self->_application || self->_application->path != path) return sd_bus_error_set(error, "org.bluez.Error.DoesNotExist", "Does Not Exist");
    self->_application.reset();
    self->_subscribed.clear();
    ++self->_application_unregistrations;
    return sd_bus_reply_method_return(m, "");
  }

  // --- LEAdvertisingManager1 ---

  static int register_advertisement(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* self = static_cast<MockBluez*>(userdata);
    const char* path = nullptr;
    int r = sd_bus_message_read(m, "o", &path);
    if (r < 0) return r;
    if (self->_advertisement) return sd_bus_error_set(error, "org.bluez.Error.AlreadyExists", "Already Exists");
    if (!self->_powered) return sd_bus_error_set(error, "org.bluez.Error.Failed", "Not Powered");

    auto* pending = new PendingRegistration{self, sd_bus_message_ref(m), sd_bus_message_get_sender(m), path};
    r = sd_bus_call_method_async(sd_bus_message_get_bus(m), nullptr, pending->owner.c_str(), path, "org.freedesktop.DBus.Properties", "GetAll",
                                 &Handlers::advertisement_properties, pending, "s", "org.bluez.LEAdvertisement1");
    if (r < 0) {
      sd_bus_message_unref(pending->request);
      delete pending;
      return r;
    }
    return 1;
  }

  static int advertisement_properties(sd_bus_message* reply, void* userdata, sd_bus_error*) {
    std::unique_ptr<PendingRegistration> pending(static_cast<PendingRegistration*>(userdata));
    auto* self = pending->self;

    auto answer_error = [&](const char* name, const std::string& why) {
      sd_bus_reply_method_errorf(pending->request, name, "%s", why.c_str());
      sd_bus_message_unref(pending->request);
      return 0;
    };

    if (sd_bus_message_is_method_error(reply, nullptr)) return answer_error("org.bluez.Error.Failed", "Failed to parse advertisement.");

    std::map<std::string, Prop> props;
    if (read_props(reply, props) < 0) return answer_error("org.bluez.Error.Failed", "Failed to parse advertisement.");

    const auto type = props["Type"].text;
    if (type != "broadcast" && type != "peripheral") return answer_error("org.bluez.Error.InvalidArguments", "Invalid arguments in method call");

    Advertisement ad;
    ad.owner = pending->owner;
    ad.path = pending->path;
    ad.type = type;
    ad.local_name = props["LocalName"].text;
    ad.service_uuids = props["ServiceUUIDs"].list;
    ad.discoverable = props["Discoverable"].boolean == 1;

    self->_advertisement = ad;
    ++self->_advertisement_registrations;
    sd_bus_reply_method_return(pending->request, "");
    sd_bus_message_unref(pending->request);
    return 0;
  }

  static int unregister_advertisement(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* self = static_cast<MockBluez*>(userdata);
    const char* path = nullptr;
    const int r = sd_bus_message_read(m, "o", &path);
    if (r < 0) return r;
    if (!self->_advertisement || self->_advertisement->path != path) return sd_bus_error_set(error, "org.bluez.Error.DoesNotExist", "Does Not Exist");
    self->_advertisement.reset();
    ++self->_advertisement_unregistrations;
    return sd_bus_reply_method_return(m, "");
  }

  // --- Device1 ---

  static int device_address(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", static_cast<Device*>(userdata)->address.c_str());
  }

  static int device_connected(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "b", static_cast<Device*>(userdata)->connected ? 1 : 0);
  }

  static int device_disconnect(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* device = static_cast<Device*>(userdata);
    ++device->disconnects;
    if (!device->connected) return sd_bus_error_set(error, "org.bluez.Error.NotConnected", "Not Connected");
    device->connected = false;
    sd_bus_emit_properties_changed(sd_bus_message_get_bus(m), device->path.c_str(), "org.bluez.Device1", "Connected", nullptr);
    return sd_bus_reply_method_return(m, "");
  }

  // --- Notifications: PropertiesChanged on a characteristic ---

  static int characteristic_changed(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<MockBluez*>(userdata);
    if (!self->_application || std::strcmp(sd_bus_message_get_sender(m), self->_application->owner.c_str()) != 0) return 0;

    const std::string path = sd_bus_message_get_path(m);
    std::string uuid;
    for (const auto& [u, p] : self->_application->characteristic_paths) {
      if (p == path) uuid = u;
    }
    if (uuid.empty()) return 0;

    const char* interface = nullptr;
    if (sd_bus_message_read(m, "s", &interface) < 0) return 0;

    int r = sd_bus_message_enter_container(m, 'a', "{sv}");
    while (r >= 0 && (r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
      const char* key = nullptr;
      sd_bus_message_read(m, "s", &key);
      if (std::strcmp(key, "Value") == 0) {
        const void* data = nullptr;
        size_t size = 0;
        sd_bus_message_enter_container(m, 'v', "ay");
        sd_bus_message_read_array(m, 'y', &data, &size);
        sd_bus_message_exit_container(m);

        // Only to a subscriber, and only what fits in one ATT notification.
        if (self->_subscribed[uuid]) {
          const auto* bytes = static_cast<const std::uint8_t*>(data);
          const std::size_t fits = std::min<std::size_t>(size, self->_notify_mtu - 3u);
          self->_notifications[uuid].emplace_back(bytes, bytes + fits);
        }
      } else {
        sd_bus_message_skip(m, "v");
      }
      r = sd_bus_message_exit_container(m);
    }
    return 0;
  }
};

namespace {

const sd_bus_vtable adapter_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_WRITABLE_PROPERTY("Powered", "b", MockBluez::Handlers::get_powered, MockBluez::Handlers::set_powered, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Address", "s", MockBluez::Handlers::get_address, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable gatt_manager_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RegisterApplication", "oa{sv}", "", MockBluez::Handlers::register_application, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("UnregisterApplication", "o", "", MockBluez::Handlers::unregister_application, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable advertising_manager_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RegisterAdvertisement", "oa{sv}", "", MockBluez::Handlers::register_advertisement, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("UnregisterAdvertisement", "o", "", MockBluez::Handlers::unregister_advertisement, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable device_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Address", "s", MockBluez::Handlers::device_address, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Connected", "b", MockBluez::Handlers::device_connected, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("Disconnect", "", "", MockBluez::Handlers::device_disconnect, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

}  // namespace

MockBluez::MockBluez(std::string address, bool powered) : MockService(std::move(address)), _powered(powered) {}

MockBluez::~MockBluez() { stop(); }

int MockBluez::setup() {
  sd_bus_slot* slot = nullptr;

  int r = sd_bus_add_object_manager(bus(), &slot, "/");
  if (r < 0) return r;
  _slots.push_back(slot);

  r = sd_bus_add_object_vtable(bus(), &slot, adapter_path, "org.bluez.Adapter1", adapter_vtable, this);
  if (r < 0) return r;
  _slots.push_back(slot);

  r = sd_bus_add_object_vtable(bus(), &slot, adapter_path, "org.bluez.GattManager1", gatt_manager_vtable, this);
  if (r < 0) return r;
  _slots.push_back(slot);

  r = sd_bus_add_object_vtable(bus(), &slot, adapter_path, "org.bluez.LEAdvertisingManager1", advertising_manager_vtable, this);
  if (r < 0) return r;
  _slots.push_back(slot);

  r = sd_bus_add_match(bus(), &slot,
                       "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',arg0='org.bluez.GattCharacteristic1'",
                       &Handlers::characteristic_changed, this);
  if (r < 0) return r;
  _slots.push_back(slot);

  return 0;
}

void MockBluez::teardown() {
  for (auto& [path, device] : _devices) sd_bus_slot_unref(device->slot);
  _devices.clear();
  for (auto* slot : _slots) sd_bus_slot_unref(slot);
  _slots.clear();
}

bool MockBluez::powered() {
  return run([this]() { return _powered; });
}

std::optional<MockBluez::Application> MockBluez::application() {
  return run([this]() { return _application; });
}

std::optional<MockBluez::Advertisement> MockBluez::advertisement() {
  return run([this]() { return _advertisement; });
}

int MockBluez::advertisement_registrations() {
  return run([this]() { return _advertisement_registrations; });
}

int MockBluez::advertisement_unregistrations() {
  return run([this]() { return _advertisement_unregistrations; });
}

int MockBluez::application_unregistrations() {
  return run([this]() { return _application_unregistrations; });
}

void MockBluez::refuse_application(const std::string& message) {
  run([this, message]() { _refuse_application = message; });
}

std::string MockBluez::connect_device(const std::string& address) {
  return run([this, address]() {
    const auto path = device_path_for(address);
    auto& device = _devices[path];
    if (device) {
      device->connected = true;
      sd_bus_emit_properties_changed(bus(), path.c_str(), "org.bluez.Device1", "Connected", nullptr);
      return path;
    }

    device = std::make_unique<Device>();
    device->owner = this;
    device->path = path;
    device->address = address;
    sd_bus_add_object_vtable(bus(), &device->slot, path.c_str(), "org.bluez.Device1", device_vtable, device.get());
    sd_bus_emit_object_added(bus(), path.c_str());
    return path;
  });
}

void MockBluez::hang_up(const std::string& device) {
  run([this, device]() {
    auto it = _devices.find(device);
    if (it == _devices.end() || !it->second->connected) return;
    it->second->connected = false;
    sd_bus_emit_properties_changed(bus(), device.c_str(), "org.bluez.Device1", "Connected", nullptr);
  });
}

void MockBluez::remove_device(const std::string& device) {
  run([this, device]() {
    auto it = _devices.find(device);
    if (it == _devices.end()) return;
    sd_bus_emit_object_removed(bus(), device.c_str());
    sd_bus_slot_unref(it->second->slot);
    _devices.erase(it);
  });
}

bool MockBluez::connected(const std::string& device) {
  return run([this, device]() {
    auto it = _devices.find(device);
    return it != _devices.end() && it->second->connected;
  });
}

int MockBluez::disconnect_calls(const std::string& device) {
  return run([this, device]() {
    auto it = _devices.find(device);
    return it == _devices.end() ? 0 : it->second->disconnects;
  });
}

MockBluez::Outcome MockBluez::_call_characteristic(const std::string& uuid, const char* member, const std::function<int(sd_bus_message*)>& append) {
  return run([this, uuid, member, &append]() {
    Outcome out;
    if (!_application) {
      out.error = "no application registered";
      return out;
    }
    auto it = _application->characteristic_paths.find(uuid);
    if (it == _application->characteristic_paths.end()) {
      out.error = "no characteristic " + uuid;
      return out;
    }

    sd_bus_message* call = nullptr;
    int r = sd_bus_message_new_method_call(bus(), &call, _application->owner.c_str(), it->second.c_str(), characteristic_interface, member);
    if (r >= 0) r = append(call);
    if (r < 0) {
      sd_bus_message_unref(call);
      out.error = std::strerror(-r);
      return out;
    }

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    r = sd_bus_call(bus(), call, 5 * 1000 * 1000, &error, &reply);
    sd_bus_message_unref(call);

    if (r < 0) {
      out.error = std::string(error.name ? error.name : "") + ": " + (error.message ? error.message : "");
      sd_bus_error_free(&error);
      return out;
    }

    char type = 0;
    if (sd_bus_message_peek_type(reply, &type, nullptr) > 0 && type == 'a') {
      const void* data = nullptr;
      size_t size = 0;
      if (sd_bus_message_read_array(reply, 'y', &data, &size) >= 0) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        out.value.assign(bytes, bytes + size);
      }
    }
    sd_bus_message_unref(reply);
    out.ok = true;
    return out;
  });
}

namespace {

int append_options(sd_bus_message* m, const std::string& device, std::uint16_t mtu, std::uint16_t offset) {
  int r = sd_bus_message_open_container(m, 'a', "{sv}");
  if (r >= 0) r = sd_bus_message_append(m, "{sv}", "device", "o", device.c_str());
  if (r >= 0 && offset != 0) r = sd_bus_message_append(m, "{sv}", "offset", "q", offset);
  if (r >= 0) r = sd_bus_message_append(m, "{sv}", "link", "s", "LE");
  if (r >= 0) r = sd_bus_message_append(m, "{sv}", "mtu", "q", mtu);
  if (r >= 0) r = sd_bus_message_close_container(m);
  return r;
}

}  // namespace

MockBluez::Outcome MockBluez::read(const std::string& device, const std::string& uuid, std::uint16_t mtu, std::uint16_t offset) {
  return _call_characteristic(uuid, "ReadValue", [&](sd_bus_message* m) { return append_options(m, device, mtu, offset); });
}

MockBluez::Outcome MockBluez::read_long(const std::string& device, const std::string& uuid, std::uint16_t mtu) {
  Outcome whole;
  whole.ok = true;
  const std::size_t piece = mtu - 1u;

  while (true) {
    auto part = read(device, uuid, mtu, static_cast<std::uint16_t>(whole.value.size()));
    if (!part.ok) return part;

    const std::size_t take = std::min(piece, part.value.size());
    whole.value.insert(whole.value.end(), part.value.begin(), part.value.begin() + static_cast<std::ptrdiff_t>(take));
    if (take < piece) return whole;
  }
}

MockBluez::Outcome MockBluez::write(const std::string& device, const std::string& uuid, const Bytes& value, std::uint16_t mtu) {
  return _call_characteristic(uuid, "WriteValue", [&](sd_bus_message* m) {
    int r = sd_bus_message_append_array(m, 'y', value.data(), value.size());
    if (r >= 0) r = append_options(m, device, mtu, 0);
    return r;
  });
}

MockBluez::Outcome MockBluez::start_notify(const std::string& uuid, std::uint16_t mtu) {
  auto out = _call_characteristic(uuid, "StartNotify", [](sd_bus_message*) { return 0; });
  if (out.ok) {
    run([this, uuid, mtu]() {
      _subscribed[uuid] = true;
      _notify_mtu = mtu;
    });
  }
  return out;
}

MockBluez::Outcome MockBluez::stop_notify(const std::string& uuid) {
  run([this, uuid]() { _subscribed[uuid] = false; });
  return _call_characteristic(uuid, "StopNotify", [](sd_bus_message*) { return 0; });
}

std::optional<bool> MockBluez::characteristic_bool(const std::string& uuid, const std::string& property) {
  return run([this, uuid, property]() -> std::optional<bool> {
    if (!_application) return std::nullopt;
    auto it = _application->characteristic_paths.find(uuid);
    if (it == _application->characteristic_paths.end()) return std::nullopt;

    sd_bus_error error = SD_BUS_ERROR_NULL;
    int value = 0;
    const int r =
        sd_bus_get_property_trivial(bus(), _application->owner.c_str(), it->second.c_str(), characteristic_interface, property.c_str(), &error, 'b', &value);
    sd_bus_error_free(&error);
    if (r < 0) return std::nullopt;
    return value != 0;
  });
}

std::vector<MockBluez::Bytes> MockBluez::take_notifications(const std::string& uuid) {
  return run([this, uuid]() {
    std::vector<Bytes> out;
    out.swap(_notifications[uuid]);
    return out;
  });
}
