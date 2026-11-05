//
// RPiLEDConfig - app
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
// Every recording in app/src/led/fixtures is a test of the reader. A frame version 7
// recording (a `-v7` in its name) must read the ID in `expected` (within
// `expected.withinMs` of the first sample, if given); any other recording, of an older frame
// version or of no LED, must read nothing at all.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "reader.h"

using namespace rpiledconfig::reader;

namespace {

struct Recording {
  std::vector<double> t;
  std::vector<double> y;
  std::string expected;
  double within_ms = 0;
};

std::vector<double> numbers_after(const std::string& text, const std::string& key) {
  std::vector<double> out;
  const auto at = text.find("\"" + key + "\": [");
  if (at == std::string::npos) return out;
  auto pos = text.find('[', at) + 1;
  const auto end = text.find(']', pos);
  std::stringstream in(text.substr(pos, end - pos));
  std::string item;
  while (std::getline(in, item, ',')) {
    if (!item.empty()) out.push_back(std::stod(item));
  }
  return out;
}

Recording load(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();
  Recording r;
  r.t = numbers_after(text, "t");
  r.y = numbers_after(text, "y");
  const auto expected = text.find("\"expected\"");
  if (expected != std::string::npos) {
    const auto id = text.find("\"idHex\": \"", expected);
    const auto close = text.find('}', expected);
    if (id != std::string::npos && id < close) r.expected = text.substr(id + 10, 16);
    const auto within = text.find("\"withinMs\": ", expected);
    if (within != std::string::npos && within < close) r.within_ms = std::stod(text.substr(within + 12, 12));
  }
  return r;
}

std::string hex(const Id& id) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (const auto b : id) {
    out += digits[b >> 4];
    out += digits[b & 15];
  }
  return out;
}

}  // namespace

TEST(RecordingsTest, EveryRecordingReadsWhatItShould) {
  int checked = 0;
  for (const auto& entry : std::filesystem::directory_iterator(LED_READER_FIXTURES)) {
    if (entry.path().extension() != ".json") continue;
    const auto name = entry.path().filename().string();
    const auto r = load(entry.path());
    ASSERT_EQ(r.t.size(), r.y.size()) << name;
    ASSERT_FALSE(r.t.empty()) << name;

    Reader reader;
    std::vector<std::string> ids;
    double read_at = -1;
    for (std::size_t i = 0; i < r.t.size(); ++i) {
      const auto& state = reader.push(r.t[i], r.y[i]);
      if (state.result) {
        const auto id = hex(state.result->id);
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        if (read_at < 0) read_at = r.t[i] - r.t[0];
      }
    }
    const bool version7 = name.find("-v7") != std::string::npos;
    if (version7 && !r.expected.empty()) {
      EXPECT_EQ(ids, std::vector<std::string>{r.expected}) << name;
      if (r.within_ms > 0) EXPECT_LE(read_at, r.within_ms) << name;
      std::printf("[ recording] %s: read at %.2f s\n", name.c_str(), read_at / 1000);
    } else {
      EXPECT_TRUE(ids.empty()) << name << " read an ID from a recording that holds none of frame version 7";
    }
    ++checked;
  }
  EXPECT_GT(checked, 0);
}
