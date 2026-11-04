//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "system/uninstaller_systemd.h"

#include <gtest/gtest.h>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "dbus_test_helper.h"
#include "mocks/logger_mock.h"
#include "protocol/messages.h"

using namespace rpiledconfig;

namespace {

// org.freedesktop.systemd1 on the private bus, as much of it as uninstalling uses:
// Manager.StartUnit(name, mode) answering a job path, or NoSuchUnit for a unit it does not
// know, or AccessDenied when told to refuse (polkit saying no).
class MockSystemd : public MockService {
 public:
  explicit MockSystemd(std::string address) : MockService(std::move(address)) {}
  ~MockSystemd() override { stop(); }

  bool start() { return MockService::start("org.freedesktop.systemd1"); }

  std::vector<std::string> units = {"rpiledconfig-uninstall.service"};
  bool deny = false;

  std::vector<std::pair<std::string, std::string>> started() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _started;
  }

 protected:
  int setup() override {
    sd_bus_slot* slot = nullptr;
    const int r = sd_bus_add_object_vtable(bus(), &slot, "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", vtable, this);
    _slot = slot;
    return r;
  }

  void teardown() override {
    if (_slot) sd_bus_slot_unref(_slot);
    _slot = nullptr;
  }

 public:
  static int start_unit(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    auto* self = static_cast<MockSystemd*>(userdata);
    const char* name = nullptr;
    const char* mode = nullptr;
    int r = sd_bus_message_read(message, "ss", &name, &mode);
    if (r < 0) return r;

    if (self->deny) return sd_bus_error_set(error, "org.freedesktop.DBus.Error.AccessDenied", "Access denied");

    bool known = false;
    for (const auto& unit : self->units) known = known || unit == name;
    if (!known) return sd_bus_error_setf(error, "org.freedesktop.systemd1.NoSuchUnit", "Unit %s not found.", name);

    {
      std::lock_guard<std::mutex> lock(self->_mutex);
      self->_started.emplace_back(name, mode);
    }
    return sd_bus_reply_method_return(message, "o", "/org/freedesktop/systemd1/job/42");
  }

 private:
  static const sd_bus_vtable vtable[];

  sd_bus_slot* _slot = nullptr;
  std::mutex _mutex;
  std::vector<std::pair<std::string, std::string>> _started;
};

const sd_bus_vtable MockSystemd::vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("StartUnit", "ss", "o", MockSystemd::start_unit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

class UninstallerSystemdTest : public ::testing::Test {
 protected:
  void SetUp() override {
    REQUIRE_TEST_BUS();
    systemd = std::make_unique<MockSystemd>(test_bus_address());

    loop = std::make_shared<dbus::SdBusLoop>(logger, daemon.executor(), test_bus_address());
    ASSERT_TRUE(daemon.run([this]() { return loop->open(); }).ok);
    uninstaller = std::make_shared<system::UninstallerSystemd>(logger, loop);
  }

  void TearDown() override {
    if (uninstaller) {
      daemon.run([this]() {
        uninstaller.reset();
        loop->close();
      });
    }
    systemd.reset();
  }

  types::Status uninstall() {
    Awaited<types::Status> answer;
    daemon.run([&]() { uninstaller->start(daemon.executor(), answer.handler()); });
    types::Status out;
    EXPECT_TRUE(answer.wait(out)) << "no answer";
    return out;
  }

  std::shared_ptr<MockLogger> logger = std::make_shared<MockLogger>();
  DaemonThread daemon;
  std::unique_ptr<MockSystemd> systemd;
  std::shared_ptr<dbus::SdBusLoop> loop;
  std::shared_ptr<system::UninstallerSystemd> uninstaller;
};

}  // namespace

// docs/protocol.md, Uninstalling: StartUnit, mode replace, of the package's unit.
TEST_F(UninstallerSystemdTest, StartsThePackagesUninstallUnit) {
  ASSERT_TRUE(systemd->start());

  const auto status = uninstall();
  EXPECT_TRUE(status.ok) << status.error;

  const auto started = systemd->started();
  ASSERT_EQ(started.size(), 1u);
  EXPECT_EQ(started[0].first, "rpiledconfig-uninstall.service");
  EXPECT_EQ(started[0].second, "replace");
}

TEST_F(UninstallerSystemdTest, NoUnitMeansNotInstalledFromThePackage) {
  systemd->units.clear();
  ASSERT_TRUE(systemd->start());

  const auto status = uninstall();
  EXPECT_FALSE(status.ok);
  EXPECT_EQ(status.code, protocol::errors::uninstall_unavailable);
  EXPECT_NE(status.error.find("not installed from the rpiledconfig package"), std::string::npos) << status.error;
}

TEST_F(UninstallerSystemdTest, ARefusalIsUnavailableWithTheReason) {
  systemd->deny = true;
  ASSERT_TRUE(systemd->start());

  const auto status = uninstall();
  EXPECT_FALSE(status.ok);
  EXPECT_EQ(status.code, protocol::errors::uninstall_unavailable);
  EXPECT_NE(status.error.find("Access denied"), std::string::npos) << status.error;
}

TEST_F(UninstallerSystemdTest, NoSystemdIsUnavailable) {
  const auto status = uninstall();
  EXPECT_FALSE(status.ok);
  EXPECT_EQ(status.code, protocol::errors::uninstall_unavailable);
}
