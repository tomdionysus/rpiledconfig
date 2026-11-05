//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#if RPILEDCONFIG_HAVE_SD_BUS

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ble_service.h"
#include "dbus/sd_bus_loop.h"
#include "loggers/logger.h"

namespace rpiledconfig::ble {

// BlueZ over sd-bus. While started it exports, on the
// daemon's own connection:
//
//   /com/tomcully/rpiledconfig                 org.freedesktop.DBus.ObjectManager
//   /com/tomcully/rpiledconfig/service0        org.bluez.GattService1
//   /com/tomcully/rpiledconfig/service0/charN  org.bluez.GattCharacteristic1, N = 0..6
//   /com/tomcully/rpiledconfig/advertisement0  org.bluez.LEAdvertisement1
//
// and registers the first with GattManager1 and the last with LEAdvertisingManager1 on
// the adapter. BlueZ then calls ReadValue, WriteValue, StartNotify and StopNotify on the
// characteristics for whichever central is connected; the `device` option says which.
//
// It lives on the loop's executor, and so must its listener: a ReadValue is answered
// inline from the listener's on_read, and on_connect is called inline before the first
// read or write of a new central, so that the session exists when that read arrives.
// on_write and on_disconnect are posted, as BleFake does.
class BleBluez : public BleService, public std::enable_shared_from_this<BleBluez> {
 public:
  static constexpr const char* bluez_service = "org.bluez";
  static constexpr const char* adapter_interface = "org.bluez.Adapter1";
  static constexpr const char* device_interface = "org.bluez.Device1";
  static constexpr const char* gatt_manager_interface = "org.bluez.GattManager1";
  static constexpr const char* gatt_service_interface = "org.bluez.GattService1";
  static constexpr const char* gatt_characteristic_interface = "org.bluez.GattCharacteristic1";
  static constexpr const char* advertising_manager_interface = "org.bluez.LEAdvertisingManager1";
  static constexpr const char* advertisement_interface = "org.bluez.LEAdvertisement1";

  static constexpr const char* application_path = "/com/tomcully/rpiledconfig";
  static constexpr const char* service_path = "/com/tomcully/rpiledconfig/service0";
  static constexpr const char* advertisement_path = "/com/tomcully/rpiledconfig/advertisement0";

  // The characteristics in the order of the Characteristic enum, at service_path/charN.
  static std::string characteristic_path(Characteristic characteristic);

  // How long a dropped session's link stays up before Device1.Disconnect: long enough for
  // the notification sent just before the drop (the answer to finish, the third refusal)
  // to leave BlueZ's queue for the air. The session is closed at once; only the link
  // lingers, and its reads and writes are refused meanwhile.
  static constexpr std::chrono::milliseconds disconnect_linger{500};

  BleBluez(std::shared_ptr<loggers::Logger> logger, std::shared_ptr<dbus::SdBusLoop> loop, std::string adapter);
  ~BleBluez() override;

  // Exports the objects and starts the asynchronous chain: adapter powered, application
  // registered, advertisement registered. A failure in the chain is logged, not returned:
  // BlueZ answers later than this returns. A failure here (no bus, an object that cannot
  // be exported) is returned.
  types::Status start(const Advertisement& advertisement, std::weak_ptr<BleListener> listener) override;
  void stop() override;
  void advertising_pause() override;
  void advertising_resume() override;
  void notify(ConnectionId connection, Characteristic characteristic, types::Bytes value) override;
  void disconnect(ConnectionId connection) override;
  std::size_t mtu(ConnectionId connection) const override;
  std::string describe() const override;

  // /org/bluez/hci0 for the adapter hci0.
  std::string adapter_path() const;

  // --- What a test looks at ----------------------------------------------------------

  bool application_registered() const { return _application_registered; }
  bool advertisement_registered() const { return _advertisement_registered; }

 private:
  friend struct BleBluezHandlers;

  struct CharacteristicObject {
    BleBluez* owner = nullptr;
    Characteristic characteristic = Characteristic::info;
    std::string path;
    types::Bytes value;
    bool notifying = false;
    dbus::Slot slot;
  };

  // The one central being served.
  struct Central {
    ConnectionId id = 0;
    std::string device;
    std::size_t mtu = default_mtu;
  };

  // From the options of a ReadValue or WriteValue.
  struct AccessOptions {
    std::string device;
    std::size_t mtu = 0;
    std::uint16_t offset = 0;
    bool prepare_authorize = false;
  };

  // The central a read or write belongs to, made (and announced) if it is new; nothing
  // when the access must be refused. why says why for the error reply.
  std::optional<ConnectionId> _central_for(const AccessOptions& options, std::string& why);

  int _read_value(sd_bus_message* message, CharacteristicObject& object, sd_bus_error* error);
  int _write_value(sd_bus_message* message, CharacteristicObject& object, sd_bus_error* error);
  void _set_notifying(CharacteristicObject& object, bool notifying);

  void _on_device_properties_changed(sd_bus_message* message);
  void _on_interfaces_removed(sd_bus_message* message);
  void _on_bluez_owner_changed(bool present);
  void _device_gone(const std::string& device, const std::string& why);

  // Device1.Disconnect, after the delay.
  void _hang_up(const std::string& device, std::chrono::milliseconds delay);

  types::Status _export();
  void _unexport();
  void _power_then_register();
  void _register_application();
  void _register_advertisement();
  void _unregister_advertisement();
  void _unregister_application();

  std::shared_ptr<loggers::Logger> _logger;
  std::shared_ptr<dbus::SdBusLoop> _loop;
  std::string _adapter;
  std::weak_ptr<BleListener> _listener;
  Advertisement _advertisement;

  // Bumped by every start and stop, so that an answer from BlueZ to an earlier start
  // does nothing to a later one.
  std::uint64_t _generation = 0;
  bool _started = false;

  dbus::Slot _object_manager_slot;
  dbus::Slot _service_slot;
  dbus::Slot _advertisement_slot;
  dbus::Slot _device_match_slot;
  dbus::Slot _removed_match_slot;
  dbus::Slot _owner_match_slot;
  std::array<std::unique_ptr<CharacteristicObject>, 7> _characteristics;

  bool _application_registered = false;
  bool _advertising_wanted = false;
  bool _advertisement_registered = false;
  bool _advertisement_pending = false;

  std::optional<Central> _central;
  ConnectionId _next_connection = 1;

  // Devices this daemon has hung up on, or refused as a second central, that BlueZ has
  // not yet reported gone. Their reads and writes are refused, so a dropped central
  // whose Disconnect has not landed cannot start over with a fresh challenge.
  std::set<std::string> _refused_devices;
};

}  // namespace rpiledconfig::ble

#endif
