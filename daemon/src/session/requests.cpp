//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "requests.h"

#include <utility>

namespace rpiledconfig::session {

using namespace protocol;

std::string wifi_error_code(const std::string& code) { return code.empty() ? errors::wifi_failed : code; }

Requests::Requests(std::shared_ptr<loggers::Logger> logger, std::shared_ptr<wifi::Wifi> wifi, std::string hostname,
                   std::shared_ptr<wifi::EasyConnect> easyconnect, std::shared_ptr<system::Uninstaller> uninstaller)
    : _logger(std::move(logger)),
      _wifi(std::move(wifi)),
      _hostname(std::move(hostname)),
      _easyconnect_backend(std::move(easyconnect)),
      _uninstaller(std::move(uninstaller)) {}

void Requests::closed() {
  if (!_easyconnect_backend || !_easyconnect_backend->active()) return;
  _logger->info("The connection that started Wi-Fi Easy Connect has gone; stopping it");
  _easyconnect_backend->stop();
}

void Requests::_easyconnect(types::Executor on, Done done) {
  if (!_easyconnect_backend) {
    return types::complete(std::move(on), std::move(done),
                           error_response(errors::easyconnect_unavailable, "Wi-Fi Easy Connect is off on this Pi (wifi.easyconnect: off)"));
  }

  _logger->info("Starting Wi-Fi Easy Connect");

  // What arrives is applied like a wifi.configure, and only logged: the app learns the
  // outcome from status, as it does after wifi.configure.
  auto apply = [logger = _logger, wifi = _wifi, on](types::Result<wifi::EasyConnectCredentials> received) {
    if (!received) {
      logger->warn("Wi-Fi Easy Connect ended without a network: " + received.error);
      return;
    }

    logger->info("Configuring WiFi for " + received.value.ssid + " from Wi-Fi Easy Connect");
    wifi->configure(received.value.ssid, received.value.passphrase, false, on, [logger, ssid = received.value.ssid](types::Status status) {
      if (!status) logger->warn("Could not configure " + ssid + " from Wi-Fi Easy Connect: " + status.error);
    });
  };

  _easyconnect_backend->start(
      on,
      [done](types::Result<std::string> uri) {
        if (!uri) return done(error_response(uri.code.empty() ? std::string(errors::easyconnect_unavailable) : uri.code, uri.error));
        done(easyconnect_response(uri.value));
      },
      std::move(apply));
}

void Requests::handle(const Request& request, types::Executor on, Done done) {
  if (request.op == ops::status) {
    return _wifi->status(on, [done, hostname = _hostname](types::Result<WifiStatus> result) {
      if (!result) return done(error_response(wifi_error_code(result.code), result.error));
      done(status_response(hostname, result.value));
    });
  }

  if (request.op == ops::wifi_scan) {
    return _wifi->scan(on, [done](types::Result<std::vector<WifiNetwork>> result) {
      if (!result) return done(error_response(wifi_error_code(result.code), result.error));
      done(scan_response(std::move(result.value)));
    });
  }

  if (request.op == ops::wifi_configure) {
    _logger->info("Configuring WiFi for " + request.ssid);
    return _wifi->configure(request.ssid, request.psk, request.hidden, on, [done](types::Status status) {
      if (!status) return done(error_response(wifi_error_code(status.code), status.error));
      done(ok_response());
    });
  }

  if (request.op == ops::wifi_forget) {
    _logger->info("Forgetting the WiFi connection");
    return _wifi->forget(on, [done](types::Result<int> forgotten) {
      if (!forgotten) return done(error_response(wifi_error_code(forgotten.code), forgotten.error));
      done(forgotten_response(forgotten.value));
    });
  }

  if (request.op == ops::wifi_easyconnect) return _easyconnect(std::move(on), std::move(done));

  if (request.op == ops::wifi_easyconnect_stop) {
    if (_easyconnect_backend && _easyconnect_backend->active()) {
      _logger->info("Stopping Wi-Fi Easy Connect");
      _easyconnect_backend->stop();
    }
    return types::complete(std::move(on), std::move(done), ok_response());
  }

  if (request.op == ops::finish) return types::complete(std::move(on), std::move(done), ok_response());

  if (request.op == ops::uninstall) {
    if (!_uninstaller) {
      return types::complete(std::move(on), std::move(done),
                             error_response(errors::uninstall_unavailable, "uninstalling from the phone is off on this Pi (uninstall.driver: off)"));
    }

    _logger->warn("A phone asked for RPiLEDConfig to be removed from this Pi");
    return _uninstaller->start(on, [done](types::Status status) {
      if (!status) return done(error_response(status.code.empty() ? std::string(errors::uninstall_unavailable) : status.code, status.error));
      done(ok_response());
    });
  }

  _logger->warn("A request with an op this daemon does not know: " + request.op);
  types::complete(std::move(on), std::move(done), error_response(errors::unknown_op, "this daemon does not know " + request.op));
}

}  // namespace rpiledconfig::session
