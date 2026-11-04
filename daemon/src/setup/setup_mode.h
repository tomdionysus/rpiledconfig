//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>

#include "ble/ble_service.h"
#include "led/led_modulator.h"
#include "loggers/logger.h"
#include "machine_id/machine_id.h"
#include "session/pairing_session.h"
#include "session/request_handler.h"
#include "timers/timer_source.h"
#include "types/status.h"
#include "wifi/wifi.h"

namespace rpiledconfig::setup {

// Setup mode (docs/protocol.md, "Setup mode"): the LED blinking and the BLE service
// advertising. Entered at start when there is no WiFi connection, left on finish or after
// setup.timeout; outside it the daemon idles and checks WiFi every setup.recheck. One
// executor for all of it, the sessions included, and the timer source made for it.
class SetupMode : public ble::BleListener, public std::enable_shared_from_this<SetupMode> {
 public:
  struct Options {
    bool always = false;
    std::chrono::seconds timeout{900};
    std::chrono::seconds recheck{60};

    // Five refused attempts in a row across connections pause advertising for a minute.
    int refusals_to_pause = 5;
    std::chrono::seconds advertising_pause{60};

    // Per connection: see PairingSession::Options.
    std::chrono::milliseconds auth_timeout{30000};
    int max_refusals = 3;
  };

  struct Identity {
    machine_id::MachineId id;
    types::Bytes w;
    std::string fingerprint;
    std::string version;
  };

  SetupMode(std::shared_ptr<loggers::Logger> logger, types::Executor executor, std::shared_ptr<timers::TimerSource> timers,
            std::shared_ptr<led::LedModulator> modulator, std::shared_ptr<ble::BleService> ble, std::shared_ptr<wifi::Wifi> wifi,
            std::shared_ptr<session::RequestHandler> requests, Identity identity, Options options);

  // Asks the WiFi backend and enters setup mode if there is no connection. On the executor.
  void start();

  // Leaves setup mode if in it and stops the checks. On the executor.
  void stop();

  bool active() const { return _active; }
  bool advertising_paused() const { return _paused; }
  std::size_t sessions() const { return _sessions.size(); }
  int consecutive_refusals() const { return _consecutive_refusals; }

  // --- BleListener -------------------------------------------------------------------

  void on_connect(ble::ConnectionId connection) override;
  void on_disconnect(ble::ConnectionId connection) override;
  void on_write(ble::ConnectionId connection, ble::Characteristic characteristic, types::Bytes value) override;
  types::Bytes on_read(ble::ConnectionId connection, ble::Characteristic characteristic) override;

 private:
  void _check();
  void _enter(const std::string& why);
  void _leave(const std::string& why);
  void _schedule_recheck();
  void _on_authenticated();
  void _on_refused();
  void _on_finished();

  std::shared_ptr<loggers::Logger> _logger;
  types::Executor _executor;
  std::shared_ptr<timers::TimerSource> _timers;
  std::shared_ptr<led::LedModulator> _modulator;
  std::shared_ptr<ble::BleService> _ble;
  std::shared_ptr<wifi::Wifi> _wifi;
  std::shared_ptr<session::RequestHandler> _requests;
  Identity _identity;
  Options _options;

  std::map<ble::ConnectionId, std::shared_ptr<session::PairingSession>> _sessions;
  bool _started = false;
  bool _active = false;
  bool _paused = false;
  int _consecutive_refusals = 0;

  std::shared_ptr<timers::Timer> _timeout;
  std::shared_ptr<timers::Timer> _recheck;
  std::shared_ptr<timers::Timer> _pause;
};

}  // namespace rpiledconfig::setup
