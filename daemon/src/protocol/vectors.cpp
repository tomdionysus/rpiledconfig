//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "vectors.h"

#include "auth.h"
#include "channel.h"
#include "crc16.h"
#include "fragments.h"
#include "keys.h"
#include "led/led_modulator.h"
#include "led_code.h"
#include "spake2.h"
#include "util.h"

namespace rpiledconfig::protocol::vectors {

types::Bytes x() {
  types::Bytes out;
  for (std::uint8_t i = 0x01; i <= 0x20; ++i) out.push_back(i);
  return out;
}

types::Bytes y() {
  types::Bytes out;
  for (std::uint8_t i = 0x21; i <= 0x40; ++i) out.push_back(i);
  return out;
}

Vectors compute() {
  Vectors v;
  v.machine_id = machine_id::MachineId::from_value(machine_id_value);
  v.id_bytes = v.machine_id.id_bytes();
  v.crc16_check = crc16(util::to_bytes(crc16_check_input));
  v.led_message = led_message(v.machine_id);
  v.led_codeword = led_codeword(v.machine_id);

  for (const auto& run : led::frame_runs(v.machine_id)) {
    if (!v.led_frame_runs.empty()) v.led_frame_runs += ",";
    v.led_frame_runs += (run.on ? "+" : "-") + std::to_string(run.length);
  }

  v.fingerprint = fingerprint(v.machine_id);
  v.w = password_scalar(v.machine_id);
  v.p_b = spake2::share(spake2::Role::b, v.w, y());

  // Both sides, so that the vectors are only printed when they agree.
  const auto app = app_auth(v.w, x(), v.p_b);
  const auto daemon = accept_auth(v.w, y(), v.p_b, app.value.write);
  if (!app || !daemon) return v;

  v.p_a.assign(app.value.write.begin(), app.value.write.begin() + spake2::share_length);
  v.k = daemon.value.k;
  v.ke = daemon.value.ke;
  v.c_a = daemon.value.ca;
  v.c_b = daemon.value.cb;
  v.session_key = session_key(v.ke);

  Sealer app_to_daemon(v.session_key, Direction::app_to_daemon);
  v.status_request_wire = app_to_daemon.seal(util::to_bytes(status_request_plaintext));
  v.status_request_fragments = fragment(v.status_request_wire, fragment_size);

  return v;
}

std::string print(const Vectors& v) {
  std::string out;

  const auto line = [&out](const char* label, const std::string& value) {
    out += label;
    out += ": ";
    out += value;
    out += "\n";
  };

  line("machine_id", v.machine_id.to_hex());
  line("id_bytes", util::to_hex(v.id_bytes));

  const std::uint8_t crc[2] = {static_cast<std::uint8_t>(v.crc16_check >> 8), static_cast<std::uint8_t>(v.crc16_check & 0xFF)};
  line("crc16_check", util::to_hex(crc, 2));
  line("led_message", util::to_hex(v.led_message));
  line("led_codeword", util::to_hex(v.led_codeword));
  line("led_frame_runs", v.led_frame_runs);
  line("fingerprint", v.fingerprint);
  line("w", util::to_hex(v.w));
  line("pA", util::to_hex(v.p_a));
  line("pB", util::to_hex(v.p_b));
  line("K", util::to_hex(v.k));
  line("Ke", util::to_hex(v.ke));
  line("cA", util::to_hex(v.c_a));
  line("cB", util::to_hex(v.c_b));
  line("S", util::to_hex(v.session_key));
  line("status_request_wire", util::to_hex(v.status_request_wire));

  std::string fragments;
  for (const auto& piece : v.status_request_fragments) {
    if (!fragments.empty()) fragments += ",";
    fragments += util::to_hex(piece);
  }
  line("status_request_fragments_mtu20", fragments);

  return out;
}

}  // namespace rpiledconfig::protocol::vectors
