//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

// A directory of the test's own, gone when the test is. Named by the clock and the
// process, so two test binaries at once never share one.
class TempDir {
 public:
  explicit TempDir(const std::string& label) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    _path = std::filesystem::temp_directory_path() / ("rpiledconfig-" + label + "-" + std::to_string(stamp));
    std::filesystem::create_directories(_path);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(_path, ec);
  }

  const std::filesystem::path& path() const { return _path; }

  void write(const std::string& name, const std::string& text) const {
    std::ofstream out(_path / name);
    out << text;
  }

  std::string read(const std::string& name) const {
    std::ifstream in(_path / name);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }

 private:
  std::filesystem::path _path;
};
