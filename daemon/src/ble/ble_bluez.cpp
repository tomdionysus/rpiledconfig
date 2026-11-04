//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "ble_bluez.h"

#if RPILEDCONFIG_HAVE_SD_BUS

#include <algorithm>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <cerrno>
#include <cstring>
#include <utility>

#include "dbus/sd_bus_helpers.h"
#include "util.h"

// What this relies on from BlueZ, and where bookworm (5.66) and trixie (5.82 on the Pi
// this was tried on) differ, which for this subset is nowhere that matters:
//
// - GattManager1.RegisterApplication walks the application with GetManagedObjects on its
//   path and replies only when it has; GattService1 needs UUID and Primary,
//   GattCharacteristic1 needs UUID, Service and Flags. Unchanged across both.
// - ReadValue and WriteValue carry `device` (o) and `mtu` (q) in their options on both
//   (`mtu` arrived in 5.62), `offset` (q) only when non-zero, and `link` (s). BlueZ does
//   not slice a read at an offset: the application returns the value from the offset on.
// - A long write (ATT Prepare and Execute Write) reaches WriteValue once, with the
//   contiguous pieces merged and offset 0 (shared/gatt-server.c, since 5.4x), so a 48-byte
//   Auth write at an MTU of 23 arrives whole.
// - A PropertiesChanged on Value becomes a notification to every device subscribed to that
//   characteristic, every time, changed or not; Notifying is not consulted. StartNotify
//   and StopNotify are called when the first subscriber arrives and the last leaves.
// - LEAdvertisement1: Discoverable (5.53 on, not experimental) is what puts the general
//   discoverable flag in a peripheral advertisement when the adapter itself is not
//   discoverable. LocalName goes into the scan response on legacy advertising, so the
//   flags, the 128-bit UUID and the name fit. Release is called only when BlueZ drops the
//   advertisement itself, never after UnregisterAdvertisement.
// - On both, bluetooth.conf lets any user send to org.bluez, and lets root (bluetoothd)
//   call into an application's GattCharacteristic1, LEAdvertisement1, ObjectManager and
//   Properties interfaces, so an unprivileged daemon needs no bus policy of its own.

namespace rpiledconfig::ble {

namespace {

constexpr Characteristic all_characteristics[] = {
    Characteristic::info, Characteristic::challenge, Characteristic::auth, Characteristic::auth_result, Characteristic::request, Characteristic::response, Characteristic::blink,
};

constexpr const char* error_not_permitted = "org.bluez.Error.NotPermitted";
constexpr const char* error_invalid_offset = "org.bluez.Error.InvalidOffset";

std::vector<std::string> flags_of(Characteristic characteristic) {
  switch (characteristic) {
    case Characteristic::info:
    case Characteristic::challenge:
      return {"read"};
    case Characteristic::auth:
    case Characteristic::request:
    case Characteristic::blink:
      return {"write"};
    case Characteristic::auth_result:
      return {"read", "notify"};
    case Characteristic::response:
      return {"notify"};
  }
  return {};
}

bool notifies(Characteristic characteristic) { return characteristic == Characteristic::auth_result || characteristic == Characteristic::response; }

std::size_t index_of(Characteristic characteristic) { return static_cast<std::size_t>(characteristic); }

int append_strings(sd_bus_message* reply, const std::vector<std::string>& strings) {
  int r = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "s");
  if (r < 0) return r;
  for (const auto& s : strings) {
    r = sd_bus_message_append_basic(reply, SD_BUS_TYPE_STRING, s.c_str());
    if (r < 0) return r;
  }
  return sd_bus_message_close_container(reply);
}

}  // namespace

// The C callbacks sd-bus calls, with the object they were registered for as userdata.
// A friend, so the members they reach stay private.
struct BleBluezHandlers {
  using Object = BleBluez::CharacteristicObject;

  static int uuid(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append_basic(reply, SD_BUS_TYPE_STRING, ble::uuid(static_cast<Object*>(userdata)->characteristic));
  }

  static int service(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append_basic(reply, SD_BUS_TYPE_OBJECT_PATH, BleBluez::service_path);
  }

  static int flags(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return append_strings(reply, flags_of(static_cast<Object*>(userdata)->characteristic));
  }

  static int value(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    const auto& bytes = static_cast<Object*>(userdata)->value;
    return sd_bus_message_append_array(reply, SD_BUS_TYPE_BYTE, bytes.data(), bytes.size());
  }

  static int notifying(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    const int on = static_cast<Object*>(userdata)->notifying ? 1 : 0;
    return sd_bus_message_append_basic(reply, SD_BUS_TYPE_BOOLEAN, &on);
  }

  static int read_value(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    auto* object = static_cast<Object*>(userdata);
    return object->owner->_read_value(message, *object, error);
  }

  static int write_value(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    auto* object = static_cast<Object*>(userdata);
    return object->owner->_write_value(message, *object, error);
  }

  static int start_notify(sd_bus_message* message, void* userdata, sd_bus_error*) {
    auto* object = static_cast<Object*>(userdata);
    object->owner->_set_notifying(*object, true);
    return sd_bus_reply_method_return(message, "");
  }

  static int stop_notify(sd_bus_message* message, void* userdata, sd_bus_error*) {
    auto* object = static_cast<Object*>(userdata);
    object->owner->_set_notifying(*object, false);
    return sd_bus_reply_method_return(message, "");
  }

  // --- The service ---

  static int service_uuid(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append_basic(reply, SD_BUS_TYPE_STRING, ble::service_uuid);
  }

  static int primary(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    const int yes = 1;
    return sd_bus_message_append_basic(reply, SD_BUS_TYPE_BOOLEAN, &yes);
  }

  // --- The advertisement ---

  static int type(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append_basic(reply, SD_BUS_TYPE_STRING, "peripheral");
  }

  static int service_uuids(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    return append_strings(reply, {ble::service_uuid});
  }

  static int local_name(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* userdata, sd_bus_error*) {
    return sd_bus_message_append_basic(reply, SD_BUS_TYPE_STRING, static_cast<BleBluez*>(userdata)->_advertisement.local_name.c_str());
  }

  static int discoverable(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    const int yes = 1;
    return sd_bus_message_append_basic(reply, SD_BUS_TYPE_BOOLEAN, &yes);
  }

  // 0 is no timeout. Without it BlueZ applies the adapter's DiscoverableTimeout (180 s on
  // Raspberry Pi OS) to a discoverable advertisement, and phones stopped finding the Pi
  // three minutes into setup mode while the daemon still believed it was advertising.
  static int discoverable_timeout(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    const std::uint16_t none = 0;
    return sd_bus_message_append_basic(reply, SD_BUS_TYPE_UINT16, &none);
  }

  static int release(sd_bus_message* message, void* userdata, sd_bus_error*) {
    auto* self = static_cast<BleBluez*>(userdata);
    if (self->_advertisement_registered) self->_logger->warn("BlueZ released the advertisement (the adapter went away or was powered off)");
    self->_advertisement_registered = false;
    return sd_bus_reply_method_return(message, "");
  }

  // --- Signals ---

  static int device_properties_changed(sd_bus_message* message, void* userdata, sd_bus_error*) {
    static_cast<BleBluez*>(userdata)->_on_device_properties_changed(message);
    return 0;
  }

  static int interfaces_removed(sd_bus_message* message, void* userdata, sd_bus_error*) {
    static_cast<BleBluez*>(userdata)->_on_interfaces_removed(message);
    return 0;
  }

  static int name_owner_changed(sd_bus_message* message, void* userdata, sd_bus_error*) {
    const char* name = nullptr;
    const char* old_owner = nullptr;
    const char* new_owner = nullptr;
    if (sd_bus_message_read(message, "sss", &name, &old_owner, &new_owner) < 0) return 0;
    static_cast<BleBluez*>(userdata)->_on_bluez_owner_changed(new_owner != nullptr && *new_owner != '\0');
    return 0;
  }
};

namespace {

// Read-only characteristics: Info, Challenge, Auth, Request. Their value depends on who
// is asking, so there is no Value property: BlueZ always asks ReadValue.
const sd_bus_vtable characteristic_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", BleBluezHandlers::uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Service", "o", BleBluezHandlers::service, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Flags", "as", BleBluezHandlers::flags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("ReadValue", "a{sv}", "ay", BleBluezHandlers::read_value, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("WriteValue", "aya{sv}", "", BleBluezHandlers::write_value, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

// AuthResult and Response: Value is what was last notified, and a change to it is the
// notification.
const sd_bus_vtable notifying_characteristic_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", BleBluezHandlers::uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Service", "o", BleBluezHandlers::service, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Flags", "as", BleBluezHandlers::flags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Value", "ay", BleBluezHandlers::value, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Notifying", "b", BleBluezHandlers::notifying, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("ReadValue", "a{sv}", "ay", BleBluezHandlers::read_value, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StartNotify", "", "", BleBluezHandlers::start_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StopNotify", "", "", BleBluezHandlers::stop_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

// Response is notify only: a read of it is not offered.
const sd_bus_vtable notify_only_characteristic_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", BleBluezHandlers::uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Service", "o", BleBluezHandlers::service, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Flags", "as", BleBluezHandlers::flags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Value", "ay", BleBluezHandlers::value, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Notifying", "b", BleBluezHandlers::notifying, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("StartNotify", "", "", BleBluezHandlers::start_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StopNotify", "", "", BleBluezHandlers::stop_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", BleBluezHandlers::service_uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Primary", "b", BleBluezHandlers::primary, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable advertisement_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Type", "s", BleBluezHandlers::type, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ServiceUUIDs", "as", BleBluezHandlers::service_uuids, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("LocalName", "s", BleBluezHandlers::local_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Discoverable", "b", BleBluezHandlers::discoverable, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("DiscoverableTimeout", "q", BleBluezHandlers::discoverable_timeout, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("Release", "", "", BleBluezHandlers::release, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable* vtable_for(Characteristic characteristic) {
  if (characteristic == Characteristic::response) return notify_only_characteristic_vtable;
  if (notifies(characteristic)) return notifying_characteristic_vtable;
  return characteristic_vtable;
}

}  // namespace

std::string BleBluez::characteristic_path(Characteristic characteristic) {
  return std::string(service_path) + "/char" + std::to_string(index_of(characteristic));
}

BleBluez::BleBluez(std::shared_ptr<loggers::Logger> logger, std::shared_ptr<dbus::SdBusLoop> loop, std::string adapter)
    : _logger(std::move(logger)), _loop(std::move(loop)), _adapter(std::move(adapter)) {}

BleBluez::~BleBluez() {
  // The slots point at this object; they go before it does.
  _unexport();
}

types::Status BleBluez::start(const Advertisement& advertisement, std::weak_ptr<BleListener> listener) {
  if (_started) return types::Status::success();
  if (!_loop->is_open()) return types::Status::failure("not connected to " + _loop->describe());

  _advertisement = advertisement;
  _listener = std::move(listener);
  ++_generation;

  if (auto exported = _export(); !exported) {
    _unexport();
    return exported;
  }

  _started = true;
  _advertising_wanted = true;
  _logger->info("Exported the GATT application at " + std::string(application_path) + "; registering it with BlueZ on " + adapter_path());

  _power_then_register();
  _loop->kick();
  return types::Status::success();
}

void BleBluez::stop() {
  if (!_started) return;

  // finish lands here, straight after its answer was notified, so the hang-up lingers like
  // any other. When the process is stopping, the bus closes before the linger is up and
  // BlueZ drops the application with it; the phone then finds the service gone.
  if (_central) disconnect(_central->id);

  _started = false;
  _advertising_wanted = false;
  ++_generation;

  _unregister_advertisement();
  _unregister_application();
  _advertisement_pending = false;

  _unexport();
  _refused_devices.clear();
  _loop->kick();

  _logger->info("Stopped advertising and unregistered from BlueZ");
}

void BleBluez::advertising_pause() {
  if (!_started) return;
  _advertising_wanted = false;
  _unregister_advertisement();
}

void BleBluez::advertising_resume() {
  if (!_started) return;
  _advertising_wanted = true;
  if (_application_registered) _register_advertisement();
}

void BleBluez::notify(ConnectionId connection, Characteristic characteristic, types::Bytes value) {
  if (!_started || !_central || _central->id != connection || !notifies(characteristic)) return;

  auto& object = *_characteristics[index_of(characteristic)];
  object.value = std::move(value);

  // The signal carries the value as it is now, so fragments sent back to back each go
  // out as themselves.
  const int r = sd_bus_emit_properties_changed(_loop->bus(), object.path.c_str(), gatt_characteristic_interface, "Value", nullptr);
  if (r < 0) _logger->error("Cannot notify " + std::string(name(characteristic)) + ": " + std::strerror(-r));

  _loop->kick();
}

void BleBluez::disconnect(ConnectionId connection) {
  if (!_central || _central->id != connection) return;

  const auto device = _central->device;
  _central.reset();

  if (!device.empty()) {
    _refused_devices.insert(device);
    _hang_up(device, disconnect_linger);
  }

  // The listener hears about it the same way whoever hung up.
  if (auto listener = _listener.lock()) {
    boost::asio::post(_loop->executor(), [listener, connection]() { listener->on_disconnect(connection); });
  }
}

// Capped at 185 (docs/protocol.md, Fragments): BlueZ's `mtu` option can be larger than the
// one the phone settled on (517 against Android's 185 on a Blackview A85), and a
// notification over the phone's limit is dropped silently.
std::size_t BleBluez::mtu(ConnectionId connection) const {
  const std::size_t reported = _central && _central->id == connection ? _central->mtu : default_mtu;
  return std::min(reported, max_fragment_mtu);
}

std::string BleBluez::describe() const { return "bluez " + _adapter; }

std::string BleBluez::adapter_path() const { return "/org/bluez/" + _adapter; }

// --- Connections ---------------------------------------------------------------------
//
// A central becomes a connection on its first read or write of this service, not when
// BlueZ reports a device Connected: the Pi may have a keyboard or speaker connected over
// Bluetooth, and those must neither be taken for a phone nor disconnected as a second
// central. Device1.Connected going false (or the device object going away) is what
// ends one.

std::optional<ConnectionId> BleBluez::_central_for(const AccessOptions& options, std::string& why) {
  if (!_started) {
    why = "not in setup mode";
    return std::nullopt;
  }

  if (!options.device.empty() && _refused_devices.count(options.device)) {
    why = "this central has been disconnected";
    return std::nullopt;
  }

  if (_central) {
    if (_central->device == options.device) {
      if (options.mtu > 0) _central->mtu = options.mtu;
      return _central->id;
    }

    // docs/protocol.md: one central at a time; a second is disconnected without a
    // challenge.
    why = "another central is being served";
    if (!options.device.empty()) {
      _refused_devices.insert(options.device);
      _logger->info("A second central, " + dbus::basename(options.device) + ", while " + dbus::basename(_central->device) + " is served");
      _hang_up(options.device, std::chrono::milliseconds(0));
    }
    return std::nullopt;
  }

  auto listener = _listener.lock();
  if (!listener) {
    why = "not serving";
    return std::nullopt;
  }

  const ConnectionId id = _next_connection++;
  _central = Central{id, options.device, options.mtu > 0 ? options.mtu : default_mtu};
  _logger->info("Central " + (options.device.empty() ? std::string("(no device given)") : dbus::basename(options.device)) + " is connection " +
                std::to_string(id) + ", ATT MTU " + std::to_string(_central->mtu));

  // Inline, so the session exists for the read or write that brought the central in.
  listener->on_connect(id);

  if (!_central || _central->id != id) {
    why = "refused";
    return std::nullopt;
  }
  return id;
}

namespace {

int read_access_options(sd_bus_message* message, std::string& device, std::size_t& mtu, std::uint16_t& offset, bool& prepare_authorize) {
  return dbus::read_dict(message, [&](const std::string& key, sd_bus_message* m) -> int {
    if (key == "device") return dbus::read_variant(m, device);
    if (key == "offset") return dbus::read_variant(m, offset);
    if (key == "prepare-authorize") return dbus::read_variant(m, prepare_authorize);
    if (key == "mtu") {
      std::uint16_t value = 0;
      const int r = dbus::read_variant(m, value);
      if (r > 0) mtu = value;
      return r;
    }
    return 0;
  });
}

}  // namespace

int BleBluez::_read_value(sd_bus_message* message, CharacteristicObject& object, sd_bus_error* error) {
  AccessOptions options;
  int r = read_access_options(message, options.device, options.mtu, options.offset, options.prepare_authorize);
  if (r < 0) return r;

  std::string why;
  const auto connection = _central_for(options, why);
  if (!connection) return sd_bus_error_set(error, error_not_permitted, why.c_str());

  types::Bytes value;
  if (auto listener = _listener.lock()) value = listener->on_read(*connection, object.characteristic);

  if (options.offset > value.size()) return sd_bus_error_set(error, error_invalid_offset, "offset beyond the value");

  sd_bus_message* reply = nullptr;
  r = sd_bus_message_new_method_return(message, &reply);
  if (r < 0) return r;
  dbus::Message owned(reply);

  r = sd_bus_message_append_array(reply, SD_BUS_TYPE_BYTE, value.data() + options.offset, value.size() - options.offset);
  if (r < 0) return r;

  return sd_bus_send(nullptr, reply, nullptr);
}

int BleBluez::_write_value(sd_bus_message* message, CharacteristicObject& object, sd_bus_error* error) {
  const void* data = nullptr;
  size_t size = 0;
  int r = sd_bus_message_read_array(message, SD_BUS_TYPE_BYTE, &data, &size);
  if (r < 0) return r;

  const auto* bytes = static_cast<const std::uint8_t*>(data);
  types::Bytes value(bytes, bytes + size);

  AccessOptions options;
  r = read_access_options(message, options.device, options.mtu, options.offset, options.prepare_authorize);
  if (r < 0) return r;

  // Only asked for characteristics with the authorize flag, which these do not have; if
  // it comes anyway it is a question, not a write.
  if (options.prepare_authorize) return sd_bus_reply_method_return(message, "");

  // BlueZ merges a long write before it gets here, so anything not at 0 is a write the
  // protocol never makes.
  if (options.offset != 0) return sd_bus_error_set(error, error_invalid_offset, "writes start at offset 0");

  std::string why;
  const auto connection = _central_for(options, why);
  if (!connection) return sd_bus_error_set(error, error_not_permitted, why.c_str());

  r = sd_bus_reply_method_return(message, "");
  if (r < 0) return r;

  if (auto listener = _listener.lock()) {
    boost::asio::post(_loop->executor(), [listener, connection = *connection, characteristic = object.characteristic, value = std::move(value)]() mutable {
      listener->on_write(connection, characteristic, std::move(value));
    });
  }

  return 1;
}

void BleBluez::_set_notifying(CharacteristicObject& object, bool notifying) {
  if (object.notifying == notifying) return;
  object.notifying = notifying;

  _logger->debug(std::string(notifying ? "Start" : "Stop") + " notifying " + name(object.characteristic));
  sd_bus_emit_properties_changed(_loop->bus(), object.path.c_str(), gatt_characteristic_interface, "Notifying", nullptr);
}

void BleBluez::_on_device_properties_changed(sd_bus_message* message) {
  const char* path = sd_bus_message_get_path(message);
  if (path == nullptr) return;

  const char* interface = nullptr;
  if (sd_bus_message_read_basic(message, SD_BUS_TYPE_STRING, &interface) < 0 || interface == nullptr || std::strcmp(interface, device_interface) != 0) return;

  bool connected = true;
  bool seen = false;
  dbus::read_dict(message, [&](const std::string& key, sd_bus_message* m) -> int {
    if (key != "Connected") return 0;
    const int r = dbus::read_variant(m, connected);
    if (r > 0) seen = true;
    return r;
  });

  if (seen && !connected) _device_gone(path, "disconnected");
}

void BleBluez::_on_interfaces_removed(sd_bus_message* message) {
  const char* path = nullptr;
  if (sd_bus_message_read_basic(message, SD_BUS_TYPE_OBJECT_PATH, &path) < 0 || path == nullptr) return;

  std::vector<std::string> interfaces;
  if (dbus::read_strings(message, interfaces) < 0) return;

  for (const auto& interface : interfaces) {
    if (interface == device_interface) return _device_gone(path, "removed");
  }
}

void BleBluez::_device_gone(const std::string& device, const std::string& why) {
  _refused_devices.erase(device);

  if (!_central || _central->device != device) return;

  const auto connection = _central->id;
  _central.reset();
  _logger->info("Central " + dbus::basename(device) + " " + why);

  if (auto listener = _listener.lock()) {
    boost::asio::post(_loop->executor(), [listener, connection]() { listener->on_disconnect(connection); });
  }
}

void BleBluez::_on_bluez_owner_changed(bool present) {
  if (!_started) return;

  // Whatever BlueZ knew is gone either way: the registrations, and the central, whose link
  // went with the daemon that held it.
  ++_generation;
  _application_registered = false;
  _advertisement_registered = false;
  _advertisement_pending = false;
  _refused_devices.clear();

  if (_central) {
    const auto connection = _central->id;
    _central.reset();
    if (auto listener = _listener.lock()) {
      boost::asio::post(_loop->executor(), [listener, connection]() { listener->on_disconnect(connection); });
    }
  }

  if (!present) {
    _logger->warn("BlueZ has gone from the bus; registering again when it is back");
    return;
  }

  _logger->info("BlueZ is on the bus again; registering again");
  _power_then_register();
}

void BleBluez::_hang_up(const std::string& device, std::chrono::milliseconds delay) {
  auto send = [loop = _loop, logger = _logger, device]() {
    if (!loop->is_open()) return;

    logger->info("Disconnecting " + dbus::basename(device));
    loop->call(loop->method_call(bluez_service, device.c_str(), device_interface, "Disconnect"),
               [logger, device](sd_bus_message*, const dbus::CallError* error) {
                 if (!error) return;
                 // Gone already, which is what was wanted.
                 if (error->name == "org.bluez.Error.NotConnected") return;
                 logger->warn("BlueZ would not disconnect " + dbus::basename(device) + ": " + error->describe());
               });
  };

  if (delay.count() <= 0) return send();

  auto timer = std::make_shared<boost::asio::steady_timer>(_loop->executor(), delay);
  timer->async_wait([timer, send](const boost::system::error_code& ec) {
    if (!ec) send();
  });
}

// --- Exporting -----------------------------------------------------------------------

types::Status BleBluez::_export() {
  sd_bus* bus = _loop->bus();
  sd_bus_slot* slot = nullptr;

  auto failed = [](const std::string& what, int r) { return types::Status::failure("cannot export " + what + ": " + std::strerror(-r)); };

  int r = sd_bus_add_object_manager(bus, &slot, application_path);
  if (r < 0) return failed("the object manager", r);
  _object_manager_slot.reset(slot);

  r = sd_bus_add_object_vtable(bus, &slot, service_path, gatt_service_interface, service_vtable, this);
  if (r < 0) return failed("the service", r);
  _service_slot.reset(slot);

  for (const auto characteristic : all_characteristics) {
    auto object = std::make_unique<CharacteristicObject>();
    object->owner = this;
    object->characteristic = characteristic;
    object->path = characteristic_path(characteristic);

    r = sd_bus_add_object_vtable(bus, &slot, object->path.c_str(), gatt_characteristic_interface, vtable_for(characteristic), object.get());
    if (r < 0) return failed(name(characteristic), r);
    object->slot.reset(slot);

    _characteristics[index_of(characteristic)] = std::move(object);
  }

  r = sd_bus_add_object_vtable(bus, &slot, advertisement_path, advertisement_interface, advertisement_vtable, this);
  if (r < 0) return failed("the advertisement", r);
  _advertisement_slot.reset(slot);

  // Only BlueZ's own signals count: sender is matched by the bus, which resolves the
  // well-known name, so another local process cannot fake a disconnect.
  const std::string device_match = "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',arg0='" +
                                   std::string(device_interface) + "',path_namespace='" + adapter_path() + "'";
  r = sd_bus_add_match_async(bus, &slot, device_match.c_str(), BleBluezHandlers::device_properties_changed, nullptr, this);
  if (r < 0) return failed("a match for devices", r);
  _device_match_slot.reset(slot);

  r = sd_bus_add_match_async(bus, &slot, "type='signal',sender='org.bluez',path='/',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesRemoved'",
                             BleBluezHandlers::interfaces_removed, nullptr, this);
  if (r < 0) return failed("a match for removed devices", r);
  _removed_match_slot.reset(slot);

  // bluetoothd restarting (an upgrade, a crash, systemctl restart bluetooth) forgets every
  // registration; this is how the daemon knows to make them again.
  r = sd_bus_add_match_async(bus, &slot,
                             "type='signal',sender='org.freedesktop.DBus',path='/org/freedesktop/DBus',interface='org.freedesktop.DBus',member='"
                             "NameOwnerChanged',arg0='org.bluez'",
                             BleBluezHandlers::name_owner_changed, nullptr, this);
  if (r < 0) return failed("a match for BlueZ restarting", r);
  _owner_match_slot.reset(slot);

  return types::Status::success();
}

void BleBluez::_unexport() {
  _owner_match_slot.reset();
  _removed_match_slot.reset();
  _device_match_slot.reset();
  _advertisement_slot.reset();
  for (auto& object : _characteristics) object.reset();
  _service_slot.reset();
  _object_manager_slot.reset();
}

// --- Registering ---------------------------------------------------------------------

void BleBluez::_power_then_register() {
  const auto generation = _generation;

  _loop->get_property(bluez_service, adapter_path().c_str(), adapter_interface, "Powered",
                      [weak = weak_from_this(), generation](sd_bus_message* reply, const dbus::CallError* error) {
                        auto self = weak.lock();
                        if (!self || self->_generation != generation) return;

                        if (error) {
                          self->_logger->error("Cannot reach the adapter " + self->adapter_path() + ": " + error->describe() +
                                               " (is bluetooth.service running, and is ble.adapter right?)");
                          return;
                        }

                        bool powered = false;
                        if (dbus::read_variant(reply, powered) <= 0) {
                          self->_logger->error("The adapter's Powered property is not a boolean");
                          return;
                        }

                        if (powered) return self->_register_application();

                        // rfkill or a fresh boot can leave it off; BlueZ refuses to
                        // advertise on an adapter that is.
                        self->_logger->info("Powering the adapter " + self->adapter_path() + " on");
                        auto set = self->_loop->method_call(bluez_service, self->adapter_path().c_str(), "org.freedesktop.DBus.Properties", "Set");
                        if (set && sd_bus_message_append(set.get(), "ssv", adapter_interface, "Powered", "b", 1) < 0) set.reset();

                        self->_loop->call(std::move(set), [weak, generation](sd_bus_message*, const dbus::CallError* error) {
                          auto self = weak.lock();
                          if (!self || self->_generation != generation) return;
                          if (error) {
                            self->_logger->error("Cannot power the adapter on: " + error->describe() + " (rfkill?)");
                            return;
                          }
                          self->_register_application();
                        });
                      });
}

void BleBluez::_register_application() {
  const auto generation = _generation;

  auto message = _loop->method_call(bluez_service, adapter_path().c_str(), gatt_manager_interface, "RegisterApplication");
  if (message && sd_bus_message_append(message.get(), "oa{sv}", application_path, 0) < 0) message.reset();

  _loop->call(std::move(message), [weak = weak_from_this(), generation](sd_bus_message*, const dbus::CallError* error) {
    auto self = weak.lock();
    if (!self) return;

    if (self->_generation != generation) {
      // Stopped while BlueZ was walking it: take it back out.
      if (!error) {
        auto undo = self->_loop->method_call(bluez_service, self->adapter_path().c_str(), gatt_manager_interface, "UnregisterApplication");
        if (undo && sd_bus_message_append(undo.get(), "o", application_path) >= 0) self->_loop->call(std::move(undo), nullptr);
      }
      return;
    }

    if (error) {
      self->_logger->error("BlueZ refused the GATT application: " + error->describe());
      return;
    }

    self->_application_registered = true;
    self->_logger->info("GATT application registered");
    self->_register_advertisement();
  });
}

void BleBluez::_register_advertisement() {
  if (!_started || !_advertising_wanted || _advertisement_registered || _advertisement_pending) return;
  _advertisement_pending = true;

  const auto generation = _generation;

  auto message = _loop->method_call(bluez_service, adapter_path().c_str(), advertising_manager_interface, "RegisterAdvertisement");
  if (message && sd_bus_message_append(message.get(), "oa{sv}", advertisement_path, 0) < 0) message.reset();

  _loop->call(std::move(message), [weak = weak_from_this(), generation](sd_bus_message*, const dbus::CallError* error) {
    auto self = weak.lock();
    if (!self) return;

    if (self->_generation != generation) {
      if (!error) {
        auto undo = self->_loop->method_call(bluez_service, self->adapter_path().c_str(), advertising_manager_interface, "UnregisterAdvertisement");
        if (undo && sd_bus_message_append(undo.get(), "o", advertisement_path) >= 0) self->_loop->call(std::move(undo), nullptr);
      }
      return;
    }

    self->_advertisement_pending = false;

    if (error) {
      self->_logger->error("BlueZ refused the advertisement: " + error->describe());
      return;
    }

    self->_advertisement_registered = true;
    self->_logger->info("Advertising " + self->_advertisement.local_name);

    // Paused while BlueZ was answering.
    if (!self->_advertising_wanted) self->_unregister_advertisement();
  });
}

void BleBluez::_unregister_advertisement() {
  if (!_advertisement_registered) return;
  _advertisement_registered = false;

  auto message = _loop->method_call(bluez_service, adapter_path().c_str(), advertising_manager_interface, "UnregisterAdvertisement");
  if (message && sd_bus_message_append(message.get(), "o", advertisement_path) < 0) message.reset();

  _loop->call(std::move(message), [logger = _logger](sd_bus_message*, const dbus::CallError* error) {
    if (error) logger->warn("BlueZ would not unregister the advertisement: " + error->describe());
  });
  _logger->info("Advertising stopped");
}

void BleBluez::_unregister_application() {
  if (!_application_registered) return;
  _application_registered = false;

  auto message = _loop->method_call(bluez_service, adapter_path().c_str(), gatt_manager_interface, "UnregisterApplication");
  if (message && sd_bus_message_append(message.get(), "o", application_path) < 0) message.reset();

  _loop->call(std::move(message), [logger = _logger](sd_bus_message*, const dbus::CallError* error) {
    if (error) logger->warn("BlueZ would not unregister the GATT application: " + error->describe());
  });
}

}  // namespace rpiledconfig::ble

#endif
