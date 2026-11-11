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
    const auto started = daemon.run([this]() { return ble->start(ble::Advertisement{"RPiLEDConfig-3a9c", ""}, listener); });
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
      {info, {"read"}},     {challenge, {"read"}},  {auth, {"write"}},  {auth_result, {"read", "notify"}},
      {request, {"write"}}, {response, {"notify"}}, {blink, {"write"}},
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

TEST_F(BleBluezTest, TheAdapterKeepsItsNameUnlessGivenAnAlias) {
  start();
  EXPECT_EQ(bluez->alias(), "");
}

// ble.hide_adapter_name: the adapter is called something else only while advertising.
TEST_F(BleBluezTest, TheAdapterTakesTheAliasWhileAdvertisingAndGetsItsNameBack) {
  const auto started = daemon.run([this]() { return ble->start(ble::Advertisement{"RPiLEDConfig-3a9c", "RPiLEDConfig-3a9c"}, listener); });
  ASSERT_TRUE(started.ok) << started.error;
  ASSERT_TRUE(eventually([this]() { return bluez->advertisement().has_value(); })) << logger->text();
  EXPECT_EQ(bluez->alias(), "RPiLEDConfig-3a9c");

  daemon.run([this]() { ble->stop(); });
  EXPECT_TRUE(eventually([this]() { return bluez->alias().empty(); }));
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
  ASSERT_TRUE(daemon.run([this]() { return ble->start(ble::Advertisement{"RPiLEDConfig-3a9c", ""}, listener); }).ok);

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
  ASSERT_TRUE(daemon.run([this]() { return ble->start(ble::Advertisement{"RPiLEDConfig-3a9c", ""}, listener); }).ok);
  EXPECT_TRUE(eventually([this]() { return logger->contains("Cannot reach the adapter /org/bluez/hci0"); })) << logger->text();
}

// --- What BlueZ should never send -----------------------------------------------------
//
// An a{sv} is ours to distrust: wrong types, nonsense values, or the wrong arguments altogether
// must get an answer or a refusal, and leave the service serving.

namespace {

// An options dictionary of one entry, after the device that makes the call this central's.
std::function<int(sd_bus_message*)> options_with(const std::string& device, const char* key, const char* type, const char* value) {
  return [=](sd_bus_message* m) {
    int r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) r = sd_bus_message_append(m, "{sv}", "device", "o", device.c_str());
    if (r >= 0) r = sd_bus_message_append(m, "{sv}", key, type, value);
    if (r >= 0) r = sd_bus_message_close_container(m);
    return r;
  };
}

}  // namespace

TEST_F(BleBluezTest, OptionsOfTheWrongTypeAreIgnoredAndTheServiceServes) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");
  listener->answer(ble::Characteristic::info, Bytes{1, 2, 3});
  ASSERT_TRUE(bluez->read(device, info).ok);

  for (const char* key : {"offset", "mtu", "prepare-authorize", "link", "no-such-option"}) {
    const auto outcome = bluez->call_characteristic(info, "ReadValue", options_with(device, key, "s", "not what it should be"));
    EXPECT_TRUE(outcome.ok) << key << ": " << outcome.error;
    EXPECT_EQ(outcome.value, (Bytes{1, 2, 3})) << key;
  }

  // A device that is not one is somebody else, and is refused.
  const auto stranger = bluez->call_characteristic(info, "ReadValue", [](sd_bus_message* m) {
    int r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) r = sd_bus_message_append(m, "{sv}", "device", "u", 7u);
    if (r >= 0) r = sd_bus_message_close_container(m);
    return r;
  });
  EXPECT_FALSE(stranger.ok);

  // Nested as far as D-Bus lets a variant go, under a key nobody asked for.
  const auto nested = bluez->call_characteristic(info, "ReadValue", [&](sd_bus_message* m) {
    int r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) r = sd_bus_message_append(m, "{sv}", "device", "o", device.c_str());
    if (r >= 0) r = sd_bus_message_open_container(m, 'e', "sv");
    if (r >= 0) r = sd_bus_message_append_basic(m, 's', "deep");
    for (int depth = 0; r >= 0 && depth < 30; ++depth) r = sd_bus_message_open_container(m, 'v', "v");
    if (r >= 0) r = sd_bus_message_open_container(m, 'v', "s");
    if (r >= 0) r = sd_bus_message_append_basic(m, 's', "bottom");
    for (int depth = 0; r >= 0 && depth < 31; ++depth) r = sd_bus_message_close_container(m);
    if (r >= 0) r = sd_bus_message_close_container(m);
    if (r >= 0) r = sd_bus_message_close_container(m);
    return r;
  });
  EXPECT_TRUE(nested.ok) << nested.error;

  EXPECT_TRUE(bluez->read(device, info).ok) << "still serving";
}

TEST_F(BleBluezTest, TheWrongArgumentsAltogetherAreRefused) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");
  ASSERT_TRUE(bluez->read(device, info).ok);

  const auto nothing = [](sd_bus_message*) { return 0; };
  const auto a_string = [](sd_bus_message* m) { return sd_bus_message_append_basic(m, 's', "hello"); };
  const auto bytes_alone = [](sd_bus_message* m) { return sd_bus_message_append_array(m, 'y', "ab", 2); };

  for (const char* member : {"ReadValue", "WriteValue"}) {
    EXPECT_FALSE(bluez->call_characteristic(auth, member, nothing).ok) << member;
    EXPECT_FALSE(bluez->call_characteristic(auth, member, a_string).ok) << member;
  }
  EXPECT_FALSE(bluez->call_characteristic(auth, "WriteValue", bytes_alone).ok);
  EXPECT_FALSE(bluez->call_characteristic(response, "ReadValue", nothing).ok) << "not offered";
  EXPECT_TRUE(listener->writes().empty());

  EXPECT_TRUE(bluez->read(device, info).ok) << "still serving";
}

TEST_F(BleBluezTest, OffsetsPastTheValueAreRefused) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");
  listener->answer(ble::Characteristic::info, Bytes{1, 2, 3});

  EXPECT_EQ(bluez->read(device, info, 23, 3).value, Bytes{}) << "exactly at the end is the empty rest";
  for (const std::uint16_t offset : {std::uint16_t{4}, std::uint16_t{65535}}) {
    const auto outcome = bluez->read(device, info, 23, offset);
    EXPECT_FALSE(outcome.ok) << offset;
    EXPECT_NE(outcome.error.find("InvalidOffset"), std::string::npos) << outcome.error;
  }

  listener->answer(ble::Characteristic::info, Bytes{});
  EXPECT_TRUE(bluez->read(device, info).ok) << "an empty value at 0";
  EXPECT_FALSE(bluez->read(device, info, 23, 1).ok);
}

TEST_F(BleBluezTest, WritesOfAnySizeArriveWholeOrNotAtAll) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");

  EXPECT_TRUE(bluez->write(device, request, Bytes{}).ok);
  EXPECT_TRUE(bluez->write(device, request, Bytes(1024 * 1024, 0xAA)).ok);

  ASSERT_TRUE(eventually([this]() { return listener->writes().size() == 2; })) << logger->text();
  EXPECT_TRUE(listener->writes()[0].value.empty());
  EXPECT_EQ(listener->writes()[1].value.size(), 1024u * 1024u);

  const auto at_an_offset = bluez->call_characteristic(request, "WriteValue", [&](sd_bus_message* m) {
    int r = sd_bus_message_append_array(m, 'y', "ab", 2);
    if (r >= 0) r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) r = sd_bus_message_append(m, "{sv}", "device", "o", device.c_str());
    if (r >= 0) r = sd_bus_message_append(m, "{sv}", "offset", "q", std::uint16_t{1});
    if (r >= 0) r = sd_bus_message_close_container(m);
    return r;
  });
  EXPECT_FALSE(at_an_offset.ok);
  EXPECT_EQ(listener->writes().size(), 2u);
}

// An MTU is what the other end says it is. The session floors it, so nothing here divides up a
// message with room for none of it, and the top is capped at what phones take.
TEST_F(BleBluezTest, AnMtuOfAnySizeIsHeldToTheCap) {
  start();
  const auto device = bluez->connect_device("AA:BB:CC:DD:EE:01");

  ASSERT_TRUE(bluez->read(device, info, 65535).ok);
  ASSERT_TRUE(eventually([this]() { return only_connection() != 0; }));
  const auto connection = only_connection();
  EXPECT_EQ(daemon.run([&]() { return ble->mtu(connection); }), ble::max_fragment_mtu);

  ASSERT_TRUE(bluez->read(device, info, 1).ok);
  EXPECT_EQ(daemon.run([&]() { return ble->mtu(connection); }), 1u);
}

// A first call without a device is not one BlueZ makes, and cannot be hung up on, so what matters
// is that the service still comes back to serving when it is stopped.
TEST_F(BleBluezTest, ACentralWithNoDeviceBreaksNothing) {
  start();
  const auto no_options = [](sd_bus_message* m) {
    int r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) r = sd_bus_message_close_container(m);
    return r;
  };
  EXPECT_TRUE(bluez->call_characteristic(info, "ReadValue", no_options).ok);
  EXPECT_TRUE(listener->saw("connect 1"));

  EXPECT_TRUE(daemon.run([this]() {
    ble->disconnect(only_connection());
    return true;
  }));
  EXPECT_TRUE(eventually([this]() { return listener->saw("disconnect 1"); })) << logger->text();
}
