//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#if defined(__linux__)

#include <array>
#include <boost/asio/local/datagram_protocol.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "easy_connect.h"
#include "loggers/logger.h"
#include "timers/timer_source.h"

namespace rpiledconfig::wifi {

// The Wi-Fi Easy Connect enrollee through wpa_supplicant's control interface: the datagram
// socket /run/wpa_supplicant/<interface>, root:netdev 0770 where NetworkManager starts
// wpa_supplicant, which is why the unit adds the netdev group.
//
// Two client sockets, as wpa_cli has them: one for commands and their replies, and one
// ATTACHed for events, so an event never arrives where a reply is expected. Each is bound
// by the kernel to an unused name in the abstract namespace (autobind). wpa_supplicant
// replies to whatever address a datagram came from, and an abstract address belongs to
// the network namespace, not the file system: the unit's PrivateTmp, which would hide a
// client socket in /tmp from wpa_supplicant, does not come into it, and nothing is left on
// disk if the daemon dies.
//
// start(): STATUS for the interface's MAC; DPP_BOOTSTRAP_GEN type=qrcode with the channels
// and the MAC; DPP_BOOTSTRAP_GET_URI; DPP_LISTEN on the frequency with role=enrollee. Then
// the events: DPP-CONF-RECEIVED opens a configuration, DPP-CONFOBJ-AKM and -SSID fill it,
// DPP-CONFOBJ-PASS (hex) or -PSK (64 hex) completes it, DPP-CONF-FAILED fails it, and a
// configuration with no passphrase within the settle time (a DPP-only network, which
// NetworkManager cannot be given here) fails too. On any end: DPP_STOP_LISTEN,
// DPP_BOOTSTRAP_REMOVE, DETACH, and both sockets closed.
//
// dpp_config_processing is left as wpa_supplicant has it (0 by default): it then saves
// nothing itself, and NetworkManager, which owns wpa_supplicant's networks on a Pi, is
// told through the Wifi backend instead.
class EasyConnectWpaSupplicant : public EasyConnect, public std::enable_shared_from_this<EasyConnectWpaSupplicant> {
 public:
  struct Options {
    std::string control_dir = "/run/wpa_supplicant";
    std::string interface = "wlan0";
    // docs/protocol.md: channels 1, 6 and 11 in the URI, listening on 6.
    std::string channels = "81/1,81/6,81/11";
    unsigned listen_frequency = 2437;
    std::chrono::milliseconds listen_time{120000};
    std::chrono::milliseconds reply_timeout{3000};
    std::chrono::milliseconds settle{2000};
  };

  EasyConnectWpaSupplicant(std::shared_ptr<loggers::Logger> logger, types::Executor executor, std::shared_ptr<timers::TimerSource> timers, Options options);

  void start(types::Executor on, types::Handler<std::string> started, types::Handler<EasyConnectCredentials> received) override;
  void stop() override;
  bool active() const override { return _active; }
  std::string describe() const override;

  std::string socket_path() const { return _options.control_dir + "/" + _options.interface; }

 private:
  using Socket = boost::asio::local::datagram_protocol::socket;
  using ReplyHandler = std::function<void(types::Result<std::string>)>;

  types::Status _open(Socket& socket);
  void _exchange(Socket& socket, const std::string& command, ReplyHandler handler);
  void _read_events();
  void _on_event(const std::string& message);
  void _fail_start(const std::string& why);
  void _finish(types::Result<EasyConnectCredentials> result);
  void _cleanup();

  std::shared_ptr<loggers::Logger> _logger;
  types::Executor _executor;
  std::shared_ptr<timers::TimerSource> _timers;
  Options _options;

  Socket _commands;
  Socket _events;
  std::array<char, 4096> _event_buffer{};

  // Bumped by every start and every end, so a reply or event for an earlier run does
  // nothing to a later one.
  std::uint64_t _generation = 0;
  bool _active = false;
  bool _listening = false;
  std::string _bootstrap_id;

  types::Executor _on;
  types::Handler<std::string> _started;
  types::Handler<EasyConnectCredentials> _received;

  bool _configuring = false;
  EasyConnectCredentials _credentials;

  std::shared_ptr<timers::Timer> _listen_timer;
  std::shared_ptr<timers::Timer> _settle_timer;
};

}  // namespace rpiledconfig::wifi

#endif
