//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dbus_test_helper.h"

// org.freedesktop.NetworkManager on the private bus, behaving as NetworkManager does for
// what the daemon uses, from NetworkManager's D-Bus API reference (1.42 to 1.52):
//
// - /org/freedesktop/NetworkManager: GetDevices, AddAndActivateConnection.
// - Devices/1 an ethernet device (DeviceType 1), Devices/2 the WiFi one (DeviceType 2,
//   Interface wlan0) with Device (ActiveConnection, Ip4Config) and Device.Wireless
//   (GetAllAccessPoints, RequestScan, LastScan, ActiveAccessPoint).
// - AccessPoint objects: Ssid, Strength, Flags, WpaFlags, RsnFlags.
// - Settings: ListConnections; Settings.Connection: GetSettings (without secrets), Delete.
// - Connection.Active: Id, State, Connection, and StateChanged(state, reason).
// - IP4Config: AddressData.
//
// RequestScan answers at once and the scan finishes later: LastScan changes then, with
// PropertiesChanged, and access points found by it appear. AddAndActivateConnection
// validates as NetworkManager does (a WPA-PSK passphrase is 8 to 63 characters), saves the
// connection, answers with it and an active connection in state activating, and activates
// it a little later. Delete on the active connection deactivates the device.
class MockNetworkManager : public MockService {
 public:
  using Bytes = rpiledconfig::types::Bytes;

  struct Handlers;

  struct AccessPoint {
    std::string ssid;
    std::uint8_t strength = 0;
    std::uint32_t flags = 0;
    std::uint32_t wpa_flags = 0;
    std::uint32_t rsn_flags = 0;
  };

  struct Saved {
    std::string path;
    std::string id;
    std::string type;
    std::string ssid;
    std::string key_mgmt;
    std::string psk;
  };

  enum class Scan { completes, refused, never_completes };

  MockNetworkManager(std::string address, std::vector<AccessPoint> access_points, bool with_wifi_device = true);
  ~MockNetworkManager() override;

  bool start() { return MockService::start("org.freedesktop.NetworkManager"); }

  // --- What a test sets ---

  void set_scan(Scan scan);
  // Appears only once a scan has finished: how a test tells a fresh list from the cache.
  void found_by_scan(AccessPoint access_point);
  // A connection that was there before (not rpiledconfig's), active on the WiFi device.
  void connect_existing(const std::string& id, const std::string& ssid, const std::string& ip);
  // The next AddAndActivateConnection fails with this error.
  void refuse_add(const std::string& name, const std::string& message);
  // Whether the activation after AddAndActivateConnection succeeds (default) or fails,
  // with the device's NMDeviceStateReason: 7 no-secrets (default), 53 ssid-not-found.
  void activation_succeeds(bool succeeds);
  void activation_fails_with(std::uint32_t device_reason);
  // The active connection stays activating.
  void activation_hangs(bool hangs = true);

  // --- What a test looks at ---

  int scans_requested();
  std::vector<Saved> connections();
  int deletes();
  std::string active_ssid();

 protected:
  int setup() override;
  void teardown() override;

 private:
  struct Object {
    MockNetworkManager* owner = nullptr;
    std::string path;
    std::vector<sd_bus_slot*> slots;
  };
  struct DeviceObject : Object {
    std::uint32_t type = 0;
    std::string interface;
    std::string active = "/";
    std::string ip4 = "/";
    std::string access_point = "/";
    std::int64_t last_scan = -1;
  };
  struct AccessPointObject : Object {
    AccessPoint ap;
  };
  struct ConnectionObject : Object {
    Saved saved;
  };
  struct ActiveObject : Object {
    std::string id;
    std::string connection;
    std::uint32_t state = 1;
  };
  struct Ip4Object : Object {
    std::string address;
  };

  std::string _add_access_point(const AccessPoint& ap);
  std::string _add_connection(Saved saved);
  std::string _activate(const std::string& connection, const std::string& id, const std::string& ssid, const std::string& ip, bool now);
  void _deactivate_device();
  void _finish_scan();
  void _free(Object& object);

  std::vector<AccessPoint> _initial;
  bool _with_wifi_device;
  Scan _scan = Scan::completes;
  std::vector<AccessPoint> _found_by_scan;
  std::string _refuse_name;
  std::string _refuse_message;
  bool _activation_succeeds = true;
  std::uint32_t _failure_reason = 7;
  bool _activation_hangs = false;
  int _scans_requested = 0;
  int _deletes = 0;
  int _next = 1;

  std::vector<sd_bus_slot*> _slots;
  std::unique_ptr<DeviceObject> _ethernet;
  std::unique_ptr<DeviceObject> _wifi;
  std::map<std::string, std::unique_ptr<AccessPointObject>> _access_points;
  std::map<std::string, std::unique_ptr<ConnectionObject>> _connections;
  std::map<std::string, std::unique_ptr<ActiveObject>> _actives;
  std::map<std::string, std::unique_ptr<Ip4Object>> _ip4s;
};
