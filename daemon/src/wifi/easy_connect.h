//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <string>

#include "types/status.h"

namespace rpiledconfig::wifi {

// What a phone sent by Wi-Fi Easy Connect: the network, and its passphrase (or, from a
// configurator that sent a raw PSK, the PSK as 64 hex characters, which NetworkManager
// takes as psk the same way). akm is the configuration's AKM as wpa_supplicant names it
// (psk, sae, psk+sae, ...), for the log.
struct EasyConnectCredentials {
  std::string ssid;
  std::string passphrase;
  std::string akm;
};

// The daemon as a Wi-Fi Easy Connect (DPP) enrollee (docs/protocol.md, "Copying the
// phone's WiFi"): a bootstrap key whose URI the app hands to the phone's system, and a
// listen for the phone to send its network. Applying what arrives is the caller's (through
// the Wifi backend's configure); this only receives it.
//
// Lives on the executor it was made for; every call is made there, and every answer is
// posted to the executor the caller gives, never inline.
class EasyConnect {
 public:
  virtual ~EasyConnect() = default;

  // Makes a bootstrap key and listens. started gets the key's DPP URI, or a failure whose
  // code is easyconnect_unavailable. received gets, once and later, the network a phone
  // sent or a failure (the exchange failed, or nothing came within the listen time); it is
  // never called after stop(). A start while one is running stops that one first.
  virtual void start(types::Executor on, types::Handler<std::string> started, types::Handler<EasyConnectCredentials> received) = 0;

  // Stops listening and removes the bootstrap key. Nothing more is reported.
  virtual void stop() = 0;

  virtual bool active() const = 0;
  virtual std::string describe() const = 0;
};

}  // namespace rpiledconfig::wifi
