//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "wifi_networkmanager.h"

#if RPILEDCONFIG_HAVE_SD_BUS

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <cstring>
#include <utility>

#include "dbus/sd_bus_helpers.h"
#include "util.h"

// What this relies on from NetworkManager, the same on bookworm (1.42) and trixie (1.52):
// GetDevices, Device.DeviceType/ActiveConnection/Ip4Config, Device.Wireless
// RequestScan/GetAllAccessPoints/LastScan (1.12 on)/ActiveAccessPoint, AccessPoint
// Ssid/Strength/Flags/WpaFlags/RsnFlags, IP4Config.AddressData (1.0 on),
// Connection.Active State/Id and its StateChanged signal (1.8 on), AddAndActivateConnection,
// Settings.ListConnections and Settings.Connection GetSettings/Delete. Properties change
// through the standard org.freedesktop.DBus.Properties.PropertiesChanged.
//
// Permissions are polkit's (packaging/polkit-rpiledconfig.rules), not the bus's: without
// the rule, RequestScan and AddAndActivateConnection answer
// org.freedesktop.NetworkManager.PermissionDenied, which is reported as wifi_failed.

namespace rpiledconfig::wifi {

namespace {

using protocol::errors::wifi_failed;
using protocol::errors::wifi_unavailable;

std::string code_for(const dbus::CallError& error) { return dbus::is_unavailable(error) ? wifi_unavailable : wifi_failed; }

bool no_object(const std::string& path) { return path.empty() || path == "/"; }

// The first address of an IP4Config's AddressData (aa{sv}), the message positioned at
// its variant.
int read_first_address(sd_bus_message* message, std::string& address) {
  int r = sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, "aa{sv}");
  if (r <= 0) return r;
  r = sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "a{sv}");
  if (r < 0) return r;

  while ((r = sd_bus_message_at_end(message, 0)) == 0) {
    r = dbus::read_dict(message, [&](const std::string& key, sd_bus_message* m) -> int {
      if (key != "address" || !address.empty()) return 0;
      return dbus::read_variant(m, address);
    });
    if (r < 0) return r;
  }
  if (r < 0) return r;

  r = sd_bus_message_exit_container(message);
  if (r < 0) return r;
  return sd_bus_message_exit_container(message);
}

// connection.id from a Settings.Connection.GetSettings reply (a{sa{sv}}).
int read_connection_id(sd_bus_message* message, std::string& id) {
  int r = sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sa{sv}}");
  if (r < 0) return r;

  while ((r = sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}")) > 0) {
    const char* group = nullptr;
    r = sd_bus_message_read_basic(message, SD_BUS_TYPE_STRING, &group);
    if (r < 0) return r;

    if (group != nullptr && std::strcmp(group, "connection") == 0) {
      r = dbus::read_dict(message, [&](const std::string& key, sd_bus_message* m) -> int { return key == "id" ? dbus::read_variant(m, id) : 0; });
    } else {
      r = sd_bus_message_skip(message, "a{sv}");
    }
    if (r < 0) return r;

    r = sd_bus_message_exit_container(message);
    if (r < 0) return r;
  }
  if (r < 0) return r;

  return sd_bus_message_exit_container(message);
}

}  // namespace

// --- The jobs: one per request, alive while its calls are out ------------------------

struct WifiNetworkManager::StatusJob {
  types::Executor on;
  types::Handler<WifiStatus> handler;
  std::string device;
  std::string active;
  std::string ip4;
  WifiStatus status;

  void succeed() { types::complete(on, handler, types::Result<WifiStatus>::success(status)); }
  void fail(const std::string& code, const std::string& why) { types::complete(on, handler, types::Result<WifiStatus>::failure(code, why)); }
};

struct WifiNetworkManager::ScanJob : public std::enable_shared_from_this<ScanJob> {
  std::shared_ptr<WifiNetworkManager> owner;
  types::Executor on;
  types::Handler<std::vector<WifiNetwork>> handler;
  std::string device;
  dbus::Slot match;
  std::unique_ptr<boost::asio::steady_timer> timer;
  bool collecting = false;
  std::vector<std::string> access_points;
  std::vector<WifiNetwork> networks;

  void succeed() { types::complete(on, handler, types::Result<std::vector<WifiNetwork>>::success(std::move(networks))); }
  void fail(const std::string& code, const std::string& why) { types::complete(on, handler, types::Result<std::vector<WifiNetwork>>::failure(code, why)); }

  static int on_installed(sd_bus_message* message, void* userdata, sd_bus_error*);
  static int on_properties_changed(sd_bus_message* message, void* userdata, sd_bus_error*);
};

struct WifiNetworkManager::ConfigureJob {
  types::Executor on;
  types::StatusHandler handler;
  std::string ssid;
  std::string psk;
  bool hidden = false;
  std::string device;

  void succeed() { types::complete(on, handler, types::Status::success()); }
  void fail(const std::string& code, const std::string& why) { types::complete(on, handler, types::Status::failure(code, why)); }
};

namespace {

// The activation after a configure, watched for the log: whether the network was joined
// is what an operator reading the journal wants to know, and the app finds out by asking
// status after it reconnects.
// NetworkManager's reasons (NMDeviceStateReason and NMActiveConnectionStateReason) that
// say why a join failed, as the protocol names them. A wrong passphrase ends as the device
// failing with no-secrets (NetworkManager asked for the secret again and nothing could
// answer); a name nothing broadcasts as ssid-not-found.
constexpr std::uint32_t device_state_failed = 120;
constexpr std::uint32_t device_reason_no_secrets = 7;
constexpr std::uint32_t device_reason_ssid_not_found = 53;
constexpr std::uint32_t active_reason_no_secrets = 9;

std::string device_reason_name(std::uint32_t reason) {
  switch (reason) {
    case device_reason_no_secrets:
      return "no secrets";
    case device_reason_ssid_not_found:
      return "SSID not found";
    case 8:
      return "the supplicant disconnected";
    case 9:
      return "the supplicant's configuration failed";
    case 10:
      return "the supplicant failed";
    case 11:
      return "the supplicant timed out";
    case 15:
      return "the DHCP lease failed";
    default:
      return "reason " + std::to_string(reason);
  }
}

std::string join_reason_for(std::uint32_t device_reason) {
  if (device_reason == device_reason_no_secrets) return protocol::join_reasons::wrong_passphrase;
  if (device_reason == device_reason_ssid_not_found) return protocol::join_reasons::not_found;
  return protocol::join_reasons::other;
}

// One configure's activation, watched until it is activated, deactivated, or the watch
// times out: the device's StateChanged says why a failure failed, the active connection's
// says that it did.
struct ActivationWatch : public std::enable_shared_from_this<ActivationWatch> {
  std::weak_ptr<WifiNetworkManager> owner;
  std::string ssid;
  dbus::Slot active_match;
  dbus::Slot device_match;
  std::unique_ptr<boost::asio::steady_timer> timer;
  bool done = false;

  void finish(const std::string& state, const std::string& reason, const std::string& message) {
    if (done) return;
    done = true;
    if (auto self = owner.lock()) self->_join_outcome(ssid, state, reason, message);
    // Not from inside the match's own callback: the timer handler holds the last
    // reference, and cancelling it lets that go once this has returned.
    timer->cancel();
  }

  static int on_active_state_changed(sd_bus_message* message, void* userdata, sd_bus_error*) {
    auto* watch = static_cast<ActivationWatch*>(userdata);

    std::uint32_t state = 0;
    std::uint32_t reason = 0;
    if (sd_bus_message_read(message, "uu", &state, &reason) < 0) return 0;

    if (state == WifiNetworkManager::active_state_activated) {
      watch->finish(protocol::join_states::joined, "", "");
    } else if (state == WifiNetworkManager::active_state_deactivated) {
      // The device usually said why first; this is the fallback.
      const bool secrets = reason == active_reason_no_secrets;
      watch->finish(protocol::join_states::failed, secrets ? protocol::join_reasons::wrong_passphrase : protocol::join_reasons::other,
                    secrets ? "NetworkManager: no secrets" : "NetworkManager: the connection was deactivated (reason " + std::to_string(reason) + ")");
    }
    return 0;
  }

  static int on_device_state_changed(sd_bus_message* message, void* userdata, sd_bus_error*) {
    auto* watch = static_cast<ActivationWatch*>(userdata);

    std::uint32_t new_state = 0;
    std::uint32_t old_state = 0;
    std::uint32_t reason = 0;
    if (sd_bus_message_read(message, "uuu", &new_state, &old_state, &reason) < 0) return 0;

    if (new_state == device_state_failed)
      watch->finish(protocol::join_states::failed, join_reason_for(reason), "NetworkManager: " + device_reason_name(reason));
    return 0;
  }
};

}  // namespace

// --- Construction --------------------------------------------------------------------

WifiNetworkManager::WifiNetworkManager(std::shared_ptr<loggers::Logger> logger, std::shared_ptr<dbus::SdBusLoop> loop, Options options)
    : _logger(std::move(logger)), _loop(std::move(loop)), _options(options) {}

std::string WifiNetworkManager::describe() const { return "networkmanager"; }

bool WifiNetworkManager::is_secure(std::uint32_t flags, std::uint32_t wpa_flags, std::uint32_t rsn_flags) {
  // NM_802_11_AP_FLAGS_PRIVACY is WEP (or any encryption); the WPA and RSN flags say
  // which key management, and any of them means a passphrase is needed.
  return (flags & 0x1u) != 0 || wpa_flags != 0 || rsn_flags != 0;
}

// --- Finding the WiFi device ---------------------------------------------------------

void WifiNetworkManager::_find_wifi_device(DeviceHandler done) {
  _loop->call(_loop->method_call(nm_service, nm_path, nm_interface, "GetDevices"),
              [self = shared_from_this(), done = std::move(done)](sd_bus_message* reply, const dbus::CallError* error) {
                if (error) return done(types::Result<std::string>::failure(code_for(*error), "NetworkManager: " + error->describe()));

                auto devices = std::make_shared<std::vector<std::string>>();
                if (dbus::read_strings(reply, *devices) < 0)
                  return done(types::Result<std::string>::failure(wifi_failed, "NetworkManager: a malformed device list"));

                self->_check_device(devices, 0, done);
              });
}

void WifiNetworkManager::_check_device(std::shared_ptr<std::vector<std::string>> devices, std::size_t index, DeviceHandler done) {
  if (index >= devices->size()) return done(types::Result<std::string>::failure(wifi_unavailable, "NetworkManager has no WiFi device"));

  const auto& path = (*devices)[index];
  _loop->get_property(nm_service, path.c_str(), device_interface, "DeviceType",
                      [self = shared_from_this(), devices, index, done](sd_bus_message* reply, const dbus::CallError* error) {
                        std::uint32_t type = 0;
                        if (!error && dbus::read_variant(reply, type) > 0 && type == device_type_wifi) {
                          return done(types::Result<std::string>::success((*devices)[index]));
                        }
                        if (error && dbus::is_unavailable(*error)) return done(types::Result<std::string>::failure(wifi_unavailable, error->describe()));

                        self->_check_device(devices, index + 1, done);
                      });
}

// --- status --------------------------------------------------------------------------

void WifiNetworkManager::status(types::Executor on, types::Handler<WifiStatus> handler) {
  auto job = std::make_shared<StatusJob>();
  job->on = std::move(on);
  job->handler = std::move(handler);

  boost::asio::post(_loop->executor(), [self = shared_from_this(), job]() {
    job->status.join = self->_last_join;
    self->_find_wifi_device([self, job](types::Result<std::string> device) {
      if (!device) return job->fail(device.code, device.error);
      job->device = device.value;
      self->_status_device(job);
    });
  });
}

void WifiNetworkManager::_status_device(std::shared_ptr<StatusJob> job) {
  _loop->get_all_properties(nm_service, job->device.c_str(), device_interface,
                            [self = shared_from_this(), job](sd_bus_message* reply, const dbus::CallError* error) {
                              if (error) return job->fail(code_for(*error), "NetworkManager: " + error->describe());

                              const int r = dbus::read_dict(reply, [&](const std::string& key, sd_bus_message* m) -> int {
                                if (key == "ActiveConnection") return dbus::read_variant(m, job->active);
                                if (key == "Ip4Config") return dbus::read_variant(m, job->ip4);
                                return 0;
                              });
                              if (r < 0) return job->fail(wifi_failed, "NetworkManager: malformed device properties");

                              if (no_object(job->active)) return job->succeed();
                              self->_status_active(job);
                            });
}

void WifiNetworkManager::_status_active(std::shared_ptr<StatusJob> job) {
  _loop->get_all_properties(nm_service, job->active.c_str(), active_connection_interface,
                            [self = shared_from_this(), job](sd_bus_message* reply, const dbus::CallError* error) {
                              // Gone between the two calls: it was going down, so there is
                              // no connection.
                              if (error) {
                                if (dbus::is_unavailable(*error)) return job->fail(wifi_unavailable, "NetworkManager: " + error->describe());
                                return job->succeed();
                              }

                              std::uint32_t state = 0;
                              std::string id;
                              dbus::read_dict(reply, [&](const std::string& key, sd_bus_message* m) -> int {
                                if (key == "State") return dbus::read_variant(m, state);
                                if (key == "Id") return dbus::read_variant(m, id);
                                return 0;
                              });

                              // Activating is not connected yet: the app asks again.
                              if (state != active_state_activated) return job->succeed();

                              job->status.connected = true;
                              job->status.ssid = id;
                              self->_status_ssid(job);
                            });
}

void WifiNetworkManager::_status_ssid(std::shared_ptr<StatusJob> job) {
  _loop->get_property(nm_service, job->device.c_str(), wireless_interface, "ActiveAccessPoint",
                      [self = shared_from_this(), job](sd_bus_message* reply, const dbus::CallError* error) {
                        std::string access_point;
                        if (error || dbus::read_variant(reply, access_point) <= 0 || no_object(access_point)) return self->_status_ip(job);

                        self->_loop->get_property(nm_service, access_point.c_str(), access_point_interface, "Ssid",
                                                  [self, job](sd_bus_message* reply, const dbus::CallError* error) {
                                                    types::Bytes ssid;
                                                    if (!error && dbus::read_variant(reply, ssid) > 0 && !ssid.empty()) {
                                                      job->status.ssid = util::to_string(ssid);
                                                    }
                                                    self->_status_ip(job);
                                                  });
                      });
}

void WifiNetworkManager::_status_ip(std::shared_ptr<StatusJob> job) {
  if (no_object(job->ip4)) return job->succeed();

  _loop->get_property(nm_service, job->ip4.c_str(), ip4_config_interface, "AddressData", [job](sd_bus_message* reply, const dbus::CallError* error) {
    if (!error) read_first_address(reply, job->status.ip);
    job->succeed();
  });
}

// --- scan ----------------------------------------------------------------------------

void WifiNetworkManager::scan(types::Executor on, types::Handler<std::vector<WifiNetwork>> handler) {
  auto job = std::make_shared<ScanJob>();
  job->owner = shared_from_this();
  job->on = std::move(on);
  job->handler = std::move(handler);

  boost::asio::post(_loop->executor(), [self = shared_from_this(), job]() {
    self->_find_wifi_device([self, job](types::Result<std::string> device) {
      if (!device) {
        job->owner.reset();
        return job->fail(device.code, device.error);
      }
      job->device = device.value;
      self->_scan_request(job);
    });
  });
}

void WifiNetworkManager::_scan_request(std::shared_ptr<ScanJob> job) {
  // The timer holds the job until the scan is collected, whichever way that happens.
  job->timer = std::make_unique<boost::asio::steady_timer>(_loop->executor());
  job->timer->expires_after(_options.scan_timeout);
  job->timer->async_wait([job](const boost::system::error_code& ec) {
    // A handler already queued when the scan finished another way still runs, with no error.
    if (ec || job->collecting || !job->owner) return;
    job->owner->_scan_collect(job, "no scan result within the timeout; answering with the access points NetworkManager already has");
  });

  // Watched before the scan is asked for, so a quick scan cannot finish unseen: the
  // request goes out from the match's install callback.
  sd_bus_slot* slot = nullptr;
  const int r = sd_bus_match_signal_async(_loop->bus(), &slot, nm_service, job->device.c_str(), "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                          &ScanJob::on_properties_changed, &ScanJob::on_installed, job.get());
  if (r < 0) return _scan_collect(job, std::string("cannot watch the scan (") + std::strerror(-r) + ")");
  job->match.reset(slot);
  _loop->kick();
}

int WifiNetworkManager::ScanJob::on_installed(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto job = static_cast<ScanJob*>(userdata)->shared_from_this();
  auto owner = job->owner;
  if (job->collecting || !owner) return 0;

  if (sd_bus_message_is_method_error(message, nullptr)) {
    boost::asio::post(owner->_loop->executor(), [job, owner]() { owner->_scan_collect(job, "cannot watch the scan"); });
    return 0;
  }

  auto request = owner->_loop->method_call(nm_service, job->device.c_str(), wireless_interface, "RequestScan");
  if (request && sd_bus_message_append(request.get(), "a{sv}", 0) < 0) request.reset();

  owner->_loop->call(std::move(request), [job, owner](sd_bus_message*, const dbus::CallError* error) {
    if (!error) return;
    owner->_logger->info("NetworkManager would not scan (" + error->describe() + "); answering with the access points it already has");
    owner->_scan_collect(job, "scan refused");
  });
  return 0;
}

int WifiNetworkManager::ScanJob::on_properties_changed(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* job = static_cast<ScanJob*>(userdata);

  const char* interface = nullptr;
  if (sd_bus_message_read_basic(message, SD_BUS_TYPE_STRING, &interface) < 0 || interface == nullptr || std::strcmp(interface, wireless_interface) != 0)
    return 0;

  bool last_scan = false;
  dbus::read_dict(message, [&](const std::string& key, sd_bus_message*) -> int {
    if (key == "LastScan") last_scan = true;
    return 0;
  });
  if (!last_scan) return 0;

  // Collected after this callback, not inside it: collecting drops the match this is the
  // callback of.
  if (job->collecting || !job->owner) return 0;
  auto shared = job->shared_from_this();
  boost::asio::post(job->owner->_loop->executor(), [shared]() {
    if (shared->collecting || !shared->owner) return;
    shared->owner->_scan_collect(shared, "scan finished");
  });
  return 0;
}

void WifiNetworkManager::_scan_collect(std::shared_ptr<ScanJob> job, const std::string& why) {
  if (job->collecting) return;
  job->collecting = true;
  job->match.reset();
  if (job->timer) job->timer->cancel();

  _logger->debug("Collecting access points: " + why);

  _loop->call(_loop->method_call(nm_service, job->device.c_str(), wireless_interface, "GetAllAccessPoints"),
              [self = shared_from_this(), job](sd_bus_message* reply, const dbus::CallError* error) {
                if (error) {
                  job->owner.reset();
                  return job->fail(code_for(*error), "NetworkManager: " + error->describe());
                }
                if (dbus::read_strings(reply, job->access_points) < 0) {
                  job->owner.reset();
                  return job->fail(wifi_failed, "NetworkManager: a malformed access point list");
                }
                self->_scan_access_point(job, 0);
              });
}

void WifiNetworkManager::_scan_access_point(std::shared_ptr<ScanJob> job, std::size_t index) {
  if (index >= job->access_points.size()) {
    _logger->info("Scan: " + std::to_string(job->networks.size()) + " access points");
    job->owner.reset();
    return job->succeed();
  }

  _loop->get_all_properties(nm_service, job->access_points[index].c_str(), access_point_interface,
                            [self = shared_from_this(), job, index](sd_bus_message* reply, const dbus::CallError* error) {
                              // One that went away between the list and the question is
                              // simply not in the answer.
                              if (!error) {
                                types::Bytes ssid;
                                std::uint8_t strength = 0;
                                std::uint32_t flags = 0, wpa_flags = 0, rsn_flags = 0;

                                const int r = dbus::read_dict(reply, [&](const std::string& key, sd_bus_message* m) -> int {
                                  if (key == "Ssid") return dbus::read_variant(m, ssid);
                                  if (key == "Strength") return dbus::read_variant(m, strength);
                                  if (key == "Flags") return dbus::read_variant(m, flags);
                                  if (key == "WpaFlags") return dbus::read_variant(m, wpa_flags);
                                  if (key == "RsnFlags") return dbus::read_variant(m, rsn_flags);
                                  return 0;
                                });

                                if (r >= 0) job->networks.push_back(WifiNetwork{util::to_string(ssid), strength, is_secure(flags, wpa_flags, rsn_flags)});
                              }
                              self->_scan_access_point(job, index + 1);
                            });
}

// --- configure -----------------------------------------------------------------------

void WifiNetworkManager::configure(const std::string& ssid, const std::string& psk, bool hidden, types::Executor on, types::StatusHandler handler) {
  auto job = std::make_shared<ConfigureJob>();
  job->on = std::move(on);
  job->handler = std::move(handler);
  job->ssid = ssid;
  job->psk = psk;
  job->hidden = hidden;

  boost::asio::post(_loop->executor(), [self = shared_from_this(), job]() {
    self->_find_wifi_device([self, job](types::Result<std::string> device) {
      if (!device) return job->fail(device.code, device.error);
      job->device = device.value;
      self->_configure_add(job);
    });
  });
}

void WifiNetworkManager::_configure_add(std::shared_ptr<ConfigureJob> job) {
  auto message = _loop->method_call(nm_service, nm_path, nm_interface, "AddAndActivateConnection");
  if (!message) return job->fail(wifi_unavailable, "not connected to " + _loop->describe());

  sd_bus_message* m = message.get();

  auto group = [m](const char* name, const std::function<int()>& fill) -> int {
    int r = sd_bus_message_open_container(m, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}");
    if (r < 0) return r;
    r = sd_bus_message_append_basic(m, SD_BUS_TYPE_STRING, name);
    if (r < 0) return r;
    r = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    if (r < 0) return r;
    r = fill();
    if (r < 0) return r;
    r = sd_bus_message_close_container(m);
    if (r < 0) return r;
    return sd_bus_message_close_container(m);
  };

  const std::string id = std::string(connection_id_prefix) + job->ssid;

  // The rest (uuid, interface-name, ipv4 and ipv6 auto) NetworkManager completes itself.
  // A system connection, saved with its passphrase (psk-flags 0), so it survives a
  // reboot with nobody logged in to supply a secret.
  auto connection_settings = [&]() {
    int q = dbus::append_string_entry(m, "id", id);
    if (q >= 0) q = dbus::append_string_entry(m, "type", "802-11-wireless");
    if (q >= 0) q = dbus::append_bool_entry(m, "autoconnect", true);
    return q;
  };
  auto wireless_settings = [&]() {
    int q = dbus::append_bytes_entry(m, "ssid", util::to_bytes(job->ssid));
    if (q >= 0) q = dbus::append_string_entry(m, "mode", "infrastructure");
    // A network typed by hand may not broadcast its SSID: NetworkManager then probes for it.
    if (q >= 0 && job->hidden) q = dbus::append_bool_entry(m, "hidden", true);
    return q;
  };
  auto security_settings = [&]() {
    int q = dbus::append_string_entry(m, "key-mgmt", "wpa-psk");
    if (q >= 0) q = dbus::append_string_entry(m, "psk", job->psk);
    return q;
  };

  int r = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "{sa{sv}}");
  if (r >= 0) r = group("connection", connection_settings);
  if (r >= 0) r = group("802-11-wireless", wireless_settings);
  if (r >= 0 && !job->psk.empty()) r = group("802-11-wireless-security", security_settings);
  if (r >= 0) r = sd_bus_message_close_container(m);
  if (r >= 0) r = sd_bus_message_append(m, "oo", job->device.c_str(), "/");
  if (r < 0) return job->fail(wifi_failed, std::string("cannot build the connection: ") + std::strerror(-r));

  _logger->info("Asking NetworkManager to join " + job->ssid + (job->psk.empty() ? " (open)" : " (WPA-PSK)") + (job->hidden ? " (hidden)" : "") + " as " + id);

  _loop->call(std::move(message), [self = shared_from_this(), job](sd_bus_message* reply, const dbus::CallError* error) {
    if (error) {
      self->_logger->warn("NetworkManager would not add " + job->ssid + ": " + error->name + ": " + error->message);
      return job->fail(code_for(*error), error->describe());
    }

    const char* connection = nullptr;
    const char* active = nullptr;
    if (sd_bus_message_read(reply, "oo", &connection, &active) < 0 || connection == nullptr || active == nullptr) {
      return job->fail(wifi_failed, "NetworkManager: a malformed answer to AddAndActivateConnection");
    }

    const std::string saved = connection;
    self->_logger->info("Saved " + dbus::basename(saved) + " for " + job->ssid + "; activation " + dbus::basename(active) + " asked for");
    job->succeed();

    protocol::JoinState join;
    join.ssid = job->ssid;
    join.state = protocol::join_states::joining;
    self->_last_join = join;
    self->_watch_activation(active, job->device, job->ssid);

    // What the earlier ones become is decided by how this one ends (_join_outcome).
    self->_pending_saved = saved;
  });
}

void WifiNetworkManager::_watch_activation(const std::string& active, const std::string& device, const std::string& ssid) {
  auto watch = std::make_shared<ActivationWatch>();
  watch->owner = weak_from_this();
  watch->ssid = ssid;
  watch->timer = std::make_unique<boost::asio::steady_timer>(_loop->executor());
  watch->timer->expires_after(_options.activation_watch);

  // The timer handler holds the watch; cancelled or expired, it ends it.
  watch->timer->async_wait([watch](const boost::system::error_code&) {
    watch->active_match.reset();
    watch->device_match.reset();
  });

  sd_bus_slot* slot = nullptr;
  if (sd_bus_match_signal_async(_loop->bus(), &slot, nm_service, active.c_str(), active_connection_interface, "StateChanged",
                                &ActivationWatch::on_active_state_changed, nullptr, watch.get()) >= 0) {
    watch->active_match.reset(slot);
  }
  slot = nullptr;
  if (sd_bus_match_signal_async(_loop->bus(), &slot, nm_service, device.c_str(), device_interface, "StateChanged", &ActivationWatch::on_device_state_changed,
                                nullptr, watch.get()) >= 0) {
    watch->device_match.reset(slot);
  }
  _loop->kick();
}

void WifiNetworkManager::_join_outcome(const std::string& ssid, const std::string& state, const std::string& reason, const std::string& message) {
  // A later configure has moved on; its own watch reports for it.
  if (!_last_join || _last_join->ssid != ssid || _last_join->state != protocol::join_states::joining) return;

  _last_join->state = state;
  _last_join->reason = reason;
  _last_join->message = message;

  const auto saved = std::move(_pending_saved);
  _pending_saved.clear();
  const auto logged = [self = shared_from_this()](types::Status deleted) {
    if (!deleted) self->_logger->warn("Could not delete a connection: " + deleted.error);
  };

  if (state == protocol::join_states::joined) {
    _logger->info("Joined " + ssid);

    // One rpiledconfig connection at a time, and only now that the new one is known to work: a
    // typo must not cost a Pi in the field the network it could get back to.
    _own_connections(saved, [self = shared_from_this(), logged](types::Result<std::vector<std::string>> earlier) {
      if (!earlier || earlier.value.empty()) return;
      self->_delete_connections(std::make_shared<std::vector<std::string>>(std::move(earlier.value)), 0, logged);
    });
  } else {
    _logger->warn("Could not join " + ssid + ": " + message + " (" + reason + ")");

    // The attempt goes and the earlier ones stay, so that it does not autoconnect with a wrong
    // passphrase, and the Pi can return to what worked.
    if (!saved.empty()) _delete_connections(std::make_shared<std::vector<std::string>>(std::vector<std::string>{saved}), 0, logged);
  }
}

// --- forget --------------------------------------------------------------------------

void WifiNetworkManager::forget(types::Executor on, types::Handler<int> handler) {
  boost::asio::post(_loop->executor(), [self = shared_from_this(), on = std::move(on), handler = std::move(handler)]() mutable {
    self->_own_connections("", [self, on, handler](types::Result<std::vector<std::string>> own) {
      if (!own) return types::complete(on, handler, types::Result<int>::failure(own.code, own.error));

      if (own.value.empty()) {
        self->_logger->info("Nothing to forget: no connection made by rpiledconfig");
        return types::complete(on, handler, types::Result<int>::success(0));
      }

      const int count = static_cast<int>(own.value.size());
      auto paths = std::make_shared<std::vector<std::string>>(std::move(own.value));
      self->_delete_connections(paths, 0, [on, handler, count](types::Status deleted) {
        types::complete(on, handler, deleted ? types::Result<int>::success(count) : types::Result<int>::failure(deleted.code, deleted.error));
      });
    });
  });
}

void WifiNetworkManager::_own_connections(const std::string& keep, PathsHandler done) {
  struct Listing {
    std::vector<std::string> all;
    std::vector<std::string> own;
    std::size_t index = 0;
  };

  _loop->call(_loop->method_call(nm_service, settings_path, settings_interface, "ListConnections"),
              [self = shared_from_this(), keep, done](sd_bus_message* reply, const dbus::CallError* error) {
                using Out = types::Result<std::vector<std::string>>;
                if (error) return done(Out::failure(code_for(*error), "NetworkManager: " + error->describe()));

                auto listing = std::make_shared<Listing>();
                if (dbus::read_strings(reply, listing->all) < 0) return done(Out::failure(wifi_failed, "NetworkManager: a malformed connection list"));

                // One GetSettings at a time, each answer asking for the next.
                auto next = std::make_shared<std::function<void()>>();
                *next = [self, keep, done, listing, weak_next = std::weak_ptr<std::function<void()>>(next)]() {
                  if (listing->index >= listing->all.size()) return done(Out::success(listing->own));

                  const auto path = listing->all[listing->index++];
                  self->_loop->call(self->_loop->method_call(nm_service, path.c_str(), connection_interface, "GetSettings"),
                                    [self, keep, done, listing, path, next = weak_next.lock()](sd_bus_message* reply, const dbus::CallError* error) {
                                      if (error && dbus::is_unavailable(*error))
                                        return done(Out::failure(wifi_unavailable, "NetworkManager: " + error->describe()));

                                      std::string id;
                                      if (!error && read_connection_id(reply, id) >= 0 && id.rfind(connection_id_prefix, 0) == 0 && path != keep) {
                                        listing->own.push_back(path);
                                      }
                                      if (next) (*next)();
                                    });
                };
                (*next)();
              });
}

void WifiNetworkManager::_delete_connections(std::shared_ptr<std::vector<std::string>> paths, std::size_t index, types::StatusHandler done) {
  if (index >= paths->size()) return done(types::Status::success());

  const auto path = (*paths)[index];
  _loop->call(_loop->method_call(nm_service, path.c_str(), connection_interface, "Delete"),
              [self = shared_from_this(), paths, index, done, path](sd_bus_message*, const dbus::CallError* error) {
                if (error)
                  return done(types::Status::failure(code_for(*error), "NetworkManager would not delete " + dbus::basename(path) + ": " + error->describe()));

                self->_logger->info("Deleted the connection " + dbus::basename(path));
                self->_delete_connections(paths, index + 1, done);
              });
}

}  // namespace rpiledconfig::wifi

#endif
