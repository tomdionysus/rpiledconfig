//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <string>

namespace rpiledconfig::cli {

// What the command line said. Hand-rolled rather than taken from a library: a handful
// of options are not worth a dependency.
struct Options {
  // Empty means nothing was given and Config::locate decides.
  std::string config;

  bool version = false;
  bool help = false;

  // The machine id, read or replaced, then exit: for the person at the console who wants
  // to know what the LED is saying without pointing a camera at it.
  bool show_id = false;
  bool regenerate_id = false;

  // docs/protocol.md's test vectors as hex, one per line, then exit. The app's tests hold
  // the same bytes, and this is how the two are compared.
  bool print_vectors = false;

  // False stops the daemon before it starts. An argument it does not understand is not
  // ignored: a daemon that silently dropped the one telling it where its configuration is
  // would read the wrong one and blink the wrong LED.
  bool ok = true;
  std::string error;
};

Options parse(int argc, char* argv[]);

std::string usage();

}  // namespace rpiledconfig::cli
