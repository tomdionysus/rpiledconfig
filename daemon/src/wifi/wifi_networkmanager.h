//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#if RPILEDCONFIG_HAVE_SD_BUS

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dbus/sd_bus_loop.h"
#include "loggers/logger.h"
#include "wifi.h"

namespace rpiledconfig::wifi {

// NetworkManager over sd-bus.
//
// - status: the first device with DeviceType 2 (WiFi); its ActiveConnection, connected
//   when that is activated; the SSID from the active access point (the connection's Id
//   if there is none); the IP from the device's Ip4Config AddressData.
// - scan: RequestScan, then GetAllAccessPoints once LastScan changes, or after the scan
//   timeout, or at once if NetworkManager refuses the scan (polkit, or a scan too soon
//   after the last): the cached list is better than no list.
// - configure: AddAndActivateConnection with 802-11-wireless and, with a passphrase,
//   802-11-wireless-security wpa-psk; ok once NetworkManager has answered with the active
//   connection (docs/protocol.md: saved and activation asked for). Connections this
//   daemon made earlier are then deleted, so there is only ever one.
// - forget: Settings.Connection.Delete on every connection this daemon made, which it
//   knows by their Id starting with connection_id_prefix (docs/configuration.md).
//
// Failures: wifi_unavailable when NetworkManager is not there or has no WiFi device,
// wifi_failed with NetworkManager's own message when it answered no.
//
// It does its work on the loop's executor and answers on the executor each caller gives.
class WifiNetworkManager : public Wifi, public std::enable_shared_from_this<WifiNetworkManager> {
 public:
  static constexpr const char* nm_service = "org.freedesktop.NetworkManager";
  static constexpr const char* nm_path = "/org/freedesktop/NetworkManager";
  static constexpr const char* nm_interface = "org.freedesktop.NetworkManager";
  static constexpr const char* device_interface = "org.freedesktop.NetworkManager.Device";
  static constexpr const char* wireless_interface = "org.freedesktop.NetworkManager.Device.Wireless";
  static constexpr const char* access_point_interface = "org.freedesktop.NetworkManager.AccessPoint";
  static constexpr const char* settings_path = "/org/freedesktop/NetworkManager/Settings";
  static constexpr const char* settings_interface = "org.freedesktop.NetworkManager.Settings";
  static constexpr const char* connection_interface = "org.freedesktop.NetworkManager.Settings.Connection";
  static constexpr const char* active_connection_interface = "org.freedesktop.NetworkManager.Connection.Active";
  static constexpr const char* ip4_config_interface = "org.freedesktop.NetworkManager.IP4Config";

  // NM_DEVICE_TYPE_WIFI and NM_ACTIVE_CONNECTION_STATE_ACTIVATED / _DEACTIVATED.
  static constexpr std::uint32_t device_type_wifi = 2;
  static constexpr std::uint32_t active_state_activated = 2;
  static constexpr std::uint32_t active_state_deactivated = 4;

  // How a connection this daemon made is named, and so recognised: rpiledconfig-<ssid>.
  static constexpr const char* connection_id_prefix = "rpiledconfig-";

  struct Options {
    // How long a scan is waited for before the access points NetworkManager already has
    // are answered with.
    std::chrono::milliseconds scan_timeout{10000};

    // How long the activation after a configure is watched for its outcome (docs/protocol.md,
    // The latest join); after it the join is left as it was last seen.
    std::chrono::milliseconds activation_watch{90000};
  };

  WifiNetworkManager(std::shared_ptr<loggers::Logger> logger, std::shared_ptr<dbus::SdBusLoop> loop, Options options);
  WifiNetworkManager(std::shared_ptr<loggers::Logger> logger, std::shared_ptr<dbus::SdBusLoop> loop)
      : WifiNetworkManager(std::move(logger), std::move(loop), Options{}) {}

  void status(types::Executor on, types::Handler<WifiStatus> handler) override;
  void scan(types::Executor on, types::Handler<std::vector<WifiNetwork>> handler) override;
  void configure(const std::string& ssid, const std::string& psk, bool hidden, types::Executor on, types::StatusHandler handler) override;
  void forget(types::Executor on, types::StatusHandler handler) override;
  std::string describe() const override;

  // The (secure) answer for an access point's flags: privacy, or any WPA or RSN flag.
  static bool is_secure(std::uint32_t flags, std::uint32_t wpa_flags, std::uint32_t rsn_flags);

  // What a watched activation reports (the ActivationWatch, outside the class); on the
  // loop's executor. Not for callers.
  void _join_outcome(const std::string& ssid, const std::string& state, const std::string& reason, const std::string& message);

 private:
  struct StatusJob;
  struct ScanJob;
  struct ConfigureJob;
  struct ForgetJob;

  using DeviceHandler = std::function<void(types::Result<std::string> device)>;
  using PathsHandler = std::function<void(types::Result<std::vector<std::string>> paths)>;

  void _find_wifi_device(DeviceHandler done);
  void _check_device(std::shared_ptr<std::vector<std::string>> devices, std::size_t index, DeviceHandler done);

  void _status_device(std::shared_ptr<StatusJob> job);
  void _status_active(std::shared_ptr<StatusJob> job);
  void _status_ssid(std::shared_ptr<StatusJob> job);
  void _status_ip(std::shared_ptr<StatusJob> job);

  void _scan_request(std::shared_ptr<ScanJob> job);
  void _scan_collect(std::shared_ptr<ScanJob> job, const std::string& why);
  void _scan_access_point(std::shared_ptr<ScanJob> job, std::size_t index);

  void _configure_add(std::shared_ptr<ConfigureJob> job);
  void _watch_activation(const std::string& active, const std::string& device, const std::string& ssid);

  // Every saved connection whose Id starts with connection_id_prefix, except keep.
  void _own_connections(const std::string& keep, PathsHandler done);
  void _delete_connections(std::shared_ptr<std::vector<std::string>> paths, std::size_t index, types::StatusHandler done);

  std::shared_ptr<loggers::Logger> _logger;
  std::shared_ptr<dbus::SdBusLoop> _loop;
  Options _options;
  // The most recent configure and what became of it; read by status. Loop's executor only.
  std::optional<protocol::JoinState> _last_join;
};

}  // namespace rpiledconfig::wifi

#endif
