//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "cli.h"

#include "config.h"

namespace rpiledconfig::cli {

Options parse(int argc, char* argv[]) {
  Options options;

  for (int i = 1; i < argc && options.ok; ++i) {
    const std::string argument = argv[i];

    // Both spellings for everything that takes a value: --name value and --name=value are
    // each common enough that supporting one and not the other is a papercut in somebody
    // else's script.
    const auto takes = [&](const std::string& name, std::string& out) -> bool {
      if (argument == name) {
        if (i + 1 >= argc || std::string(argv[i + 1]).empty()) {
          options.ok = false;
          options.error = name + " needs a value";
          return true;
        }

        out = argv[++i];
        return true;
      }

      const auto prefix = name + "=";
      if (argument.rfind(prefix, 0) == 0) {
        out = argument.substr(prefix.size());
        if (out.empty()) {
          options.ok = false;
          options.error = name + " needs a value";
        }
        return true;
      }

      return false;
    };

    if (argument == "--version" || argument == "-v") {
      options.version = true;
    } else if (argument == "--help" || argument == "-h") {
      options.help = true;
    } else if (argument == "--print-vectors") {
      options.print_vectors = true;
    } else if (takes("--config", options.config) || takes("-c", options.config)) {
      // Handled, and any error is already recorded.
    } else {
      options.ok = false;
      options.error = "unknown option: " + argument;
    }
  }

  return options;
}

std::string usage() {
  return std::string(
             "Usage: rpiledconfig [options]\n"
             "\n"
             "  -c, --config PATH     the configuration to read\n"
             "      --print-vectors   print the protocol's test vectors and exit\n"
             "  -v, --version         print the version and exit\n"
             "  -h, --help            print this and exit\n"
             "\n"
             "With no --config, the first of these that is set is read:\n"
             "\n"
             "  $") +
         Config::environment_variable + "\n  " + Config::system_path + "\n";
}

}  // namespace rpiledconfig::cli
