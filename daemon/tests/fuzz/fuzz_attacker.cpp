//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include <cstdio>
#include <cstdlib>

#include "helpers/attacker.h"

// A stranger with a radio, as coverage-guided input: tests/helpers/attacker.h. Built with
// -DRPILEDCONFIG_FUZZ=ON, by clang.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  attacker::run(data, size, [](const std::string& what) {
    std::fprintf(stderr, "VIOLATED: %s\n", what.c_str());
    std::abort();
  });
  return 0;
}
