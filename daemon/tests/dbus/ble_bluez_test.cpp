//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "ble/ble_bluez.h"

#include <gtest/gtest.h>

#include <mutex>

#include "dbus_test_helper.h"
#include "mock_bluez.h"
#include "mocks/logger_mock.h"

using namespace rpiledconfig;
using namespace std::chrono_literals;

namespace {

// docs/protocol.md, "BLE".
const std::string service = "2a7f0001-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string info = "2a7f0002-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string challenge = "2a7f0003-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string auth = "2a7f0004-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string auth_result = "2a7f0005-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string request = "2a7f0006-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string response = "2a7f0007-7e1d-4c3a-9b5e-0f8a6d4c2b10";
const std::string blink = "2a7f0008-7e1d-4c3a-9b5e-0f8a6d4c2b10";

using Bytes = types::Bytes;

// What the service told its owner, in order.
class RecordingListener : public ble::BleListener {
 public:
  struct Write {
    ble::ConnectionId connection;
    ble::Characteristic characteristic;
    Bytes value;
  };

  void on_connect(ble::ConnectionId connection) override {
    std::lock_guard<std::mutex> lock(_mutex);
    _events.push_back("connect " + std::to_string(connection));
  }

  void on_disconnect(ble::ConnectionId connection) override {
    std::lock_guard<std::mutex> lock(_mutex);
    _events.push_back("disconnect " + std::to_string(connection));
  }

  void on_write(ble::ConnectionId connection, ble::Characteristic characteristic, Bytes value) override {
    std::lock_guard<std::mutex> lock(_mutex);
    _events.push_back("write " + std::to_string(connection) + " " + ble::name(characteristic));
    _writes.push_back(Write{connection, characteristic, std::move(value)});
  }

  Bytes on_read(ble::ConnectionId connection, ble::Characteristic characteristic) override {
    std::lock_guard<std::mutex> lock(_mutex);
    _events.push_back("read " + std::to_string(connection) + " " + ble::name(characteristic));
    return _answers[characteristic];
  }

  void answer(ble::Characteristic characteristic, Bytes value) {
    std::lock_guard<std::mutex> lock(_mutex);
    _answers[characteristic] = std::move(value);
  }

  std::vector<std::string> events() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _events;
  }

  bool saw(const std::string& event) const {
    for (const auto& e : events()) {
      if (e == event) return true;
    }
    return false;
  }

  std::vector<Write> writes() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _writes;
  }

 private:
  mutable std::mutex _mutex;
  std::vector<std::string> _events;
  std::vector<Write> _writes;
  std::map<ble::Characteristic, Bytes> _answers;
};

class BleBluezTest : public ::testing::Test {
 protected:
  void SetUp() override {
    REQUIRE_TEST_BUS();
    bluez = std::make_unique<MockBluez>(test_bus_address(), adapter_powered);
    if (with_bluez) {
      ASSERT_TRUE(bluez->start());
    }

    loop = std::make_shared<dbus::SdBusLoop>(logger, daemon.executor(), test_bus_address());
    ASSERT_TRUE(daemon.run([this]() { return loop->open(); }).ok);
    ble = std::make_shared<ble::BleBluez>(logger, loop, "hci0");
  }

  void TearDown() override {
    if (ble) {
      daemon.run([this]() {
        ble->stop();
        ble.reset();
        loop->close();
      });
    }
    bluez.reset();
  }

  // Starts and waits for BlueZ to have both registrations.
  void start() {
    const auto started = daemon.run([this]() { return ble->start(ble::Advertisement{"RPiLEDConfig-3a9c"}, listener); });
    ASSERT_TRUE(started.ok) << started.error;
    ASSERT_TRUE(eventually([this]() { return bluez->advertisement().has_value(); })) << logger->text();
  }

  ble::ConnectionId only_connection() {
    for (const auto& e : listener->events()) {
      if (e.rfind("connect ", 0) == 0) return std::stoull(e.substr(8));
    }
    return 0;
  }

  bool adapter_powered = true;
  bool with_bluez = true;
  std::shared_ptr<MockLogger> logger = std::make_shared<MockLogger>();
  DaemonThread daemon;
  std::unique_ptr<MockBluez> bluez;
  std::shared_ptr<dbus::SdBusLoop> loop;
  std::shared_ptr<ble::BleBluez> ble;
  std::shared_ptr<RecordingListener> listener = std::make_shared<RecordingListener>();
};

}  // namespace

TEST_F(BleBluezTest, RegistersTheServiceOfTheProtocolAndAdvertisesIt) {
  start();

  const auto app = bluez->application();
  ASSERT_TRUE(app.has_value());
  EXPECT_EQ(app->path, "/com/tomcully/rpiledconfig");
  EXPECT_EQ(app->service_uuid, service);
  EXPECT_TRUE(app->primary);

  const std::map<std::string, std::vector<std::string>> flags = {
      {info, {"read"}}, {challenge, {"read"}}, {auth, {"write"}}, {auth_result, {"read", "notify"}}, {request, {"write"}}, {response, {"notify"}},
      {blink, {"write"}},
  };
  EXPECT_EQ(app->characteristic_flags, flags);

  const auto ad = bluez->advertisement();
  ASSERT_TRUE(ad.has_value());
  EXPECT_EQ(ad->type, "peripheral");
  EXPECT_EQ(ad->local_name, "RPiLEDConfig-3a9c");
  EXPECT_EQ(ad->service_uuids, std::vector<std::string>{service});
  EXPECT_TRUE(ad->discoverable) << "general discoverable, docs/protocol.md";
  EXPECT_EQ(ad->owner, app->owner) << "both from the daemon's one connection";
}

class BleBluezAdapterOffTest : public BleBluezTest {
 protected:
  void SetUp() override {
    adapter_powered = false;
    BleBluezTest::SetUp();
  }
};

TEST_F(BleBluezAdapterOffTest, PowersAnAdapterThatIsOff) {
  start();
  EXPECT_TRUE(bluez->powered());
}

TEST_F(BleBluezTest, AReadIsAnsweredByTheListenerAfterTheCentralIsAnnounced) {
  start();
  listener->answer(ble::Characteristic::info, {'{', '}'});

  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");
  const auto read = bluez->read(device, info, 185);
  ASSERT_TRUE(read.ok) << read.error;
  EXPECT_EQ(read.value, (Bytes{'{', '}'}));

  const auto events = listener->events();
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0], "connect 1");
  EXPECT_EQ(events[1], "read 1 Info");
  EXPECT_EQ(daemon.run([this]() { return ble->mtu(1); }), 185u) << "the mtu option";
}

TEST_F(BleBluezTest, AReadAtAnOffsetIsTheRestOfTheValue) {
  start();
  Bytes value(40);
  for (std::size_t i = 0; i < value.size(); ++i) value[i] = static_cast<std::uint8_t>(i);
  listener->answer(ble::Characteristic::auth_result, value);

  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");

  const auto rest = bluez->read(device, auth_result, 23, 22);
  ASSERT_TRUE(rest.ok) << rest.error;
  EXPECT_EQ(rest.value, Bytes(value.begin() + 22, value.end()));

  const auto whole = bluez->read_long(device, auth_result, 23);
  ASSERT_TRUE(whole.ok) << whole.error;
  EXPECT_EQ(whole.value, value);

  const auto beyond = bluez->read(device, auth_result, 23, 41);
  EXPECT_FALSE(beyond.ok);
  EXPECT_NE(beyond.error.find("org.bluez.Error.InvalidOffset"), std::string::npos) << beyond.error;
}

TEST_F(BleBluezTest, AWriteReachesTheListener) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");

  const Bytes value(48, 0x5a);
  ASSERT_TRUE(bluez->write(device, auth, value, 23).ok);

  ASSERT_TRUE(eventually([this]() { return listener->writes().size() == 1; }));
  const auto write = listener->writes()[0];
  EXPECT_EQ(write.characteristic, ble::Characteristic::auth);
  EXPECT_EQ(write.value, value);
  EXPECT_EQ(listener->events()[0], "connect 1");
}

TEST_F(BleBluezTest, NotificationsArePropertiesChangedOnValueInOrder) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");
  ASSERT_TRUE(bluez->read(device, info, 185).ok);
  const auto connection = only_connection();

  ASSERT_TRUE(bluez->start_notify(response, 185).ok);
  EXPECT_EQ(bluez->characteristic_bool(response, "Notifying"), std::optional<bool>(true));

  daemon.run([&]() {
    ble->notify(connection, ble::Characteristic::response, {0x00, 1, 2});
    ble->notify(connection, ble::Characteristic::response, {0x00, 1, 2});
    ble->notify(connection, ble::Characteristic::response, {0x01, 3});
  });

  std::vector<Bytes> got;
  ASSERT_TRUE(eventually([&]() {
    for (auto& n : bluez->take_notifications(response)) got.push_back(n);
    return got.size() == 3;
  }));
  EXPECT_EQ(got[0], (Bytes{0x00, 1, 2}));
  EXPECT_EQ(got[1], (Bytes{0x00, 1, 2})) << "the same value twice is two notifications";
  EXPECT_EQ(got[2], (Bytes{0x01, 3}));

  ASSERT_TRUE(bluez->stop_notify(response).ok);
  EXPECT_EQ(bluez->characteristic_bool(response, "Notifying"), std::optional<bool>(false));
}

TEST_F(BleBluezTest, ASecondCentralIsDisconnectedWithoutAChallenge) {
  start();
  const auto first = bluez->connect_device("AA:BB:CC:DD:EE:01");
  const auto second = bluez->connect_device("AA:BB:CC:DD:EE:02");

  ASSERT_TRUE(bluez->read(first, info).ok);

  const auto refused = bluez->read(second, challenge);
  EXPECT_FALSE(refused.ok);
  EXPECT_NE(refused.error.find("org.bluez.Error.NotPermitted"), std::string::npos) << refused.error;
  EXPECT_TRUE(eventually([&]() { return bluez->disconnect_calls(second) == 1; }));
  EXPECT_FALSE(bluez->connected(second));

  EXPECT_FALSE(listener->saw("read 2 Challenge"));
  EXPECT_FALSE(listener->saw("connect 2"));
  EXPECT_TRUE(bluez->read(first, challenge).ok) << "the first is still served";
  EXPECT_EQ(bluez->disconnect_calls(first), 0);
}

TEST_F(BleBluezTest, DroppingAConnectionDisconnectsTheDeviceAndRefusesItUntilItReconnects) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");
  ASSERT_TRUE(bluez->read(device, info).ok);
  const auto connection = only_connection();

  daemon.run([&]() { ble->disconnect(connection); });

  EXPECT_TRUE(eventually([&]() { return bluez->disconnect_calls(device) == 1; }));
  EXPECT_TRUE(eventually([&]() { return listener->saw("disconnect " + std::to_string(connection)); }));
  EXPECT_FALSE(bluez->connected(device));

  // Back again: a new connection, with a new number.
  bluez->connect_device("AA:BB:CC:DD:EE:01");
  ASSERT_TRUE(eventually([&]() { return bluez->read(device, info).ok; }));
  EXPECT_TRUE(listener->saw("connect " + std::to_string(connection + 1)));
}

TEST_F(BleBluezTest, APhoneThatHangsUpIsReported) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");
  ASSERT_TRUE(bluez->read(device, info).ok);

  bluez->hang_up(device);
  EXPECT_TRUE(eventually([&]() { return listener->saw("disconnect 1"); })) << logger->text();
  EXPECT_EQ(bluez->disconnect_calls(device), 0);
}

TEST_F(BleBluezTest, ADeviceBluezForgetsIsReported) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");
  ASSERT_TRUE(bluez->read(device, info).ok);

  bluez->remove_device(device);
  EXPECT_TRUE(eventually([&]() { return listener->saw("disconnect 1"); })) << logger->text();
}

TEST_F(BleBluezTest, ADisconnectSignalFromAnotherSenderIsIgnored) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");
  ASSERT_TRUE(bluez->read(device, info).ok);

  // Somebody else on the bus claims the phone has gone.
  sd_bus* impostor = nullptr;
  ASSERT_GE(sd_bus_new(&impostor), 0);
  ASSERT_GE(sd_bus_set_address(impostor, test_bus_address().c_str()), 0);
  ASSERT_GE(sd_bus_set_bus_client(impostor, 1), 0);
  ASSERT_GE(sd_bus_start(impostor), 0);
  sd_bus_message* signal = nullptr;
  ASSERT_GE(sd_bus_message_new_signal(impostor, &signal, device.c_str(), "org.freedesktop.DBus.Properties", "PropertiesChanged"), 0);
  ASSERT_GE(sd_bus_message_append(signal, "sa{sv}as", "org.bluez.Device1", 1, "Connected", "b", 0, 0), 0);
  ASSERT_GE(sd_bus_send(impostor, signal, nullptr), 0);
  sd_bus_message_unref(signal);
  sd_bus_flush_close_unref(impostor);

  // The real BlueZ says something after it, so the impostor's signal has been seen by now.
  bluez->hang_up(bluez->connect_device("AA:BB:CC:DD:EE:09"));
  std::this_thread::sleep_for(100ms);

  EXPECT_FALSE(listener->saw("disconnect 1"));
  EXPECT_TRUE(bluez->read(device, info).ok);
}

TEST_F(BleBluezTest, PausingUnregistersTheAdvertisementAndResumingRegistersItAgain) {
  start();
  ASSERT_EQ(bluez->advertisement_registrations(), 1);

  daemon.run([this]() { ble->advertising_pause(); });
  EXPECT_TRUE(eventually([this]() { return !bluez->advertisement().has_value(); }));
  EXPECT_EQ(bluez->advertisement_unregistrations(), 1);
  EXPECT_TRUE(bluez->application().has_value()) << "the service stays registered";

  daemon.run([this]() { ble->advertising_resume(); });
  EXPECT_TRUE(eventually([this]() { return bluez->advertisement().has_value(); }));
  EXPECT_EQ(bluez->advertisement_registrations(), 2);
}

TEST_F(BleBluezTest, StopUnregistersEverythingAndHangsUp) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");
  ASSERT_TRUE(bluez->read(device, info).ok);

  daemon.run([this]() { ble->stop(); });

  EXPECT_TRUE(eventually([this]() { return !bluez->advertisement().has_value() && !bluez->application().has_value(); }));
  EXPECT_EQ(bluez->application_unregistrations(), 1);
  EXPECT_TRUE(eventually([&]() { return bluez->disconnect_calls(device) == 1; }));
  EXPECT_TRUE(eventually([&]() { return listener->saw("disconnect 1"); }));

  // And again: setup mode comes back after a recheck.
  start();
  EXPECT_TRUE(bluez->application().has_value());
}

TEST_F(BleBluezTest, ABluezThatRestartsIsRegisteredWithAgain) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");
  ASSERT_TRUE(bluez->read(device, info).ok);

  // bluetoothd goes, and with it every registration and the link.
  bluez->stop();
  EXPECT_TRUE(eventually([this]() { return listener->saw("disconnect 1"); })) << logger->text();

  bluez = std::make_unique<MockBluez>(test_bus_address());
  ASSERT_TRUE(bluez->start());
  EXPECT_TRUE(eventually([this]() { return bluez->advertisement().has_value(); })) << logger->text();
  EXPECT_TRUE(bluez->application().has_value());
}

TEST_F(BleBluezTest, AnApplicationBluezRefusesIsLoggedAndNotAdvertised) {
  bluez->refuse_application("no, for a reason");
  ASSERT_TRUE(daemon.run([this]() { return ble->start(ble::Advertisement{"RPiLEDConfig-3a9c"}, listener); }).ok);

  EXPECT_TRUE(eventually([this]() { return logger->contains("BlueZ refused the GATT application: no, for a reason"); })) << logger->text();
  EXPECT_FALSE(bluez->advertisement().has_value());
}

class BleBluezWithoutBluezTest : public BleBluezTest {
 protected:
  void SetUp() override {
    with_bluez = false;
    BleBluezTest::SetUp();
  }
};

TEST_F(BleBluezWithoutBluezTest, StartingSaysWhyNothingIsAdvertised) {
  ASSERT_TRUE(daemon.run([this]() { return ble->start(ble::Advertisement{"RPiLEDConfig-3a9c"}, listener); }).ok);
  EXPECT_TRUE(eventually([this]() { return logger->contains("Cannot reach the adapter /org/bluez/hci0"); })) << logger->text();
}
