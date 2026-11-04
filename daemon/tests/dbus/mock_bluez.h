//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dbus_test_helper.h"

// org.bluez on the private bus, behaving as bluetoothd does for what the daemon uses, from
// BlueZ's doc/org.bluez.*.rst and src/gatt-database.c and src/advertising.c (5.66 to 5.82):
//
// - /org/bluez/hci0 with Adapter1 (Powered, writable), GattManager1 and
//   LEAdvertisingManager1; an ObjectManager at /.
// - RegisterApplication walks the application with GetManagedObjects on its path and
//   answers only then; it refuses one with no GattService1, a characteristic without
//   UUID, Service or Flags, or a flag it does not know.
// - RegisterAdvertisement reads the advertisement's properties with Properties.GetAll and
//   refuses a Type other than broadcast or peripheral.
// - Device objects with Device1 (Address, Connected, Disconnect), added and removed with
//   InterfacesAdded and InterfacesRemoved.
// - The phone: ReadValue and WriteValue with options device (o), mtu (q), link (s) and
//   offset (q, only when not 0); StartNotify and StopNotify; a PropertiesChanged on a
//   characteristic's Value is a notification, cut to the ATT MTU less 3 as the ATT layer
//   cuts it.
class MockBluez : public MockService {
 public:
  using Bytes = rpiledconfig::types::Bytes;

  struct Application {
    std::string owner;
    std::string path;
    std::string service_path;
    std::string service_uuid;
    bool primary = false;
    std::map<std::string, std::string> characteristic_paths;               // by UUID
    std::map<std::string, std::vector<std::string>> characteristic_flags;  // by UUID
  };

  struct Advertisement {
    std::string owner;
    std::string path;
    std::string type;
    std::string local_name;
    std::vector<std::string> service_uuids;
    bool discoverable = false;
  };

  struct Outcome {
    bool ok = false;
    Bytes value;
    std::string error;
  };

  // The C callbacks, defined in the .cpp; public so the vtables there can name them.
  struct Handlers;

  MockBluez(std::string address, bool powered = true);
  ~MockBluez() override;

  bool start() { return MockService::start("org.bluez"); }

  // --- The adapter and the registrations ---

  bool powered();
  std::optional<Application> application();
  std::optional<Advertisement> advertisement();
  int advertisement_registrations();
  int advertisement_unregistrations();
  int application_unregistrations();

  // The next RegisterApplication is refused with this error.
  void refuse_application(const std::string& message);

  // --- Devices ---

  // A central connects: a device object, Connected true. Gives back its path.
  std::string connect_device(const std::string& address);

  // The phone hangs up: Connected goes false.
  void hang_up(const std::string& device);

  // BlueZ forgets the device: InterfacesRemoved.
  void remove_device(const std::string& device);

  bool connected(const std::string& device);
  int disconnect_calls(const std::string& device);

  // --- The phone's GATT client ---

  Outcome read(const std::string& device, const std::string& uuid, std::uint16_t mtu = 23, std::uint16_t offset = 0);

  // A read as the ATT layer does it: Read, then Read Blob at each offset, mtu - 1 bytes a
  // time, until a short piece.
  Outcome read_long(const std::string& device, const std::string& uuid, std::uint16_t mtu = 23);

  Outcome write(const std::string& device, const std::string& uuid, const Bytes& value, std::uint16_t mtu = 23);
  Outcome start_notify(const std::string& uuid, std::uint16_t mtu = 23);
  Outcome stop_notify(const std::string& uuid);

  // A characteristic's property as the application reports it (Notifying, say).
  std::optional<bool> characteristic_bool(const std::string& uuid, const std::string& property);

  // What has been notified on a characteristic since the last take, in order.
  std::vector<Bytes> take_notifications(const std::string& uuid);

 protected:
  int setup() override;
  void teardown() override;

 private:
  struct Device {
    MockBluez* owner = nullptr;
    std::string path;
    std::string address;
    bool connected = true;
    int disconnects = 0;
    sd_bus_slot* slot = nullptr;
  };

  Outcome _call_characteristic(const std::string& uuid, const char* member, const std::function<int(sd_bus_message*)>& append);

  bool _powered;
  std::string _refuse_application;
  std::optional<Application> _application;
  std::optional<Advertisement> _advertisement;
  int _advertisement_registrations = 0;
  int _advertisement_unregistrations = 0;
  int _application_unregistrations = 0;

  std::map<std::string, std::unique_ptr<Device>> _devices;
  std::map<std::string, bool> _subscribed;                   // by UUID
  std::map<std::string, std::vector<Bytes>> _notifications;  // by UUID
  std::uint16_t _notify_mtu = 23;

  std::vector<sd_bus_slot*> _slots;
};
