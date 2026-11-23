// SPDX-License-Identifier: MIT
// devices_tests.cpp: the paired devices' file (devices.h; G9, §7.2 item 16): what the writer writes the reader takes
// back whole; an entry that breaks a rule is left out; anything not of the writer's shape leaves the file unread.
#include <doctest/doctest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "grace/spaces/devices.h"

using namespace grace::spaces;
namespace serve = grace::serve;

namespace {

PairedDeviceRecord device(std::string id, std::string name, std::string paired = "2026-11-23", std::uint8_t fill = 7) {
  PairedDeviceRecord d;
  d.id = std::move(id);
  d.name = std::move(name);
  d.paired = std::move(paired);
  d.hash.fill(fill);
  return d;
}

std::string with_entry(std::string_view entry) {
  return "graceui_devices = 1\n\n[[device]]\n" + std::string(entry);
}

const std::string kHash(64, 'a');

}  // namespace

TEST_CASE("the devices file: written and read back whole, the cookie never in it (G9)") {
  const std::vector<PairedDeviceRecord> devices = {device("0123456789abcdef", "Phone"),
                                                   device("fedcba9876543210", "Lounge \"tablet\" \\ 2", "2026-01-31", 200),
                                                   device("00112233445566ff", "Téléphone", "2026-12-01", 0)};
  const std::string text = write_devices(devices);
  CHECK(text.find("graceui_devices = 1") != std::string::npos);
  CHECK(text.find("never the cookie") != std::string::npos);
  const DevicesRead back = read_devices(text);
  REQUIRE(back.ok);
  CHECK(back.left_out == 0);
  REQUIRE(back.devices.size() == 3);
  for (std::size_t i = 0; i < devices.size(); ++i) {
    CHECK(back.devices[i].id == devices[i].id);
    CHECK(back.devices[i].name == devices[i].name);
    CHECK(back.devices[i].paired == devices[i].paired);
    CHECK(back.devices[i].hash == devices[i].hash);
  }
  // The server's view: ids and hashes.
  const std::vector<serve::DeviceKey> keys = device_keys(back.devices);
  REQUIRE(keys.size() == 3);
  CHECK(std::string(keys[1].id.data(), keys[1].id.size()) == "fedcba9876543210");
  CHECK(keys[1].hash == devices[1].hash);
  // None: a file with no device.
  const DevicesRead none = read_devices(write_devices({}));
  CHECK(none.ok);
  CHECK(none.devices.empty());
}

TEST_CASE("the devices file: an entry that breaks a rule is left out, the others kept (G9)") {
  const std::string good = "id = \"0123456789abcdef\"\nname = \"Phone\"\npaired = \"2026-11-23\"\nhash = \"" + kHash + "\"\n";
  CHECK(read_devices(with_entry(good)).devices.size() == 1);
  const auto left_out = [&](std::string_view entry) {
    const DevicesRead read = read_devices(with_entry(good) + "\n[[device]]\n" + std::string(entry));
    CHECK(read.ok);
    CHECK(read.devices.size() == 1);
    return read.left_out == 1;
  };
  CHECK(left_out("id = \"0123456789ABCDEF\"\nname = \"A\"\npaired = \"2026-11-23\"\nhash = \"" + kHash + "\"\n"));
  CHECK(left_out("id = \"0123456789abcde\"\nname = \"A\"\npaired = \"2026-11-23\"\nhash = \"" + kHash + "\"\n"));
  CHECK(left_out("id = \"1123456789abcdef\"\nname = \"\"\npaired = \"2026-11-23\"\nhash = \"" + kHash + "\"\n"));
  CHECK(left_out("id = \"1123456789abcdef\"\nname = \"" + std::string(33, 'x') + "\"\npaired = \"2026-11-23\"\nhash = \"" +
                 kHash + "\"\n"));
  CHECK(left_out("id = \"1123456789abcdef\"\nname = \"A\\u0007\"\npaired = \"2026-11-23\"\nhash = \"" + kHash + "\"\n"));
  CHECK(left_out("id = \"1123456789abcdef\"\nname = \"A\"\npaired = \"2026-13-01\"\nhash = \"" + kHash + "\"\n"));
  CHECK(left_out("id = \"1123456789abcdef\"\nname = \"A\"\npaired = \"2026-11-23\"\nhash = \"" + std::string(64, 'A') +
                 "\"\n"));
  CHECK(left_out("id = \"1123456789abcdef\"\nname = \"A\"\npaired = \"2026-11-23\"\nhash = \"" + kHash.substr(2) + "\"\n"));
  CHECK(left_out("id = \"1123456789abcdef\"\nname = \"A\"\npaired = \"2026-11-23\"\n"));  // no hash
  CHECK(left_out("id = \"1123456789abcdef\"\nid = \"2123456789abcdef\"\nname = \"A\"\npaired = \"2026-11-23\"\nhash = \"" +
                 kHash + "\"\n"));  // a key twice
  CHECK(left_out("id = \"1123456789abcdef\"\nname = \"A\"\\\"\npaired = \"2026-11-23\"\nhash = \"" + kHash + "\"\n"));
  CHECK(left_out(good));  // the same id again
  // Past sixteen devices.
  std::vector<PairedDeviceRecord> many;
  for (int i = 0; i < 18; ++i) {
    char id[17];
    std::snprintf(id, sizeof id, "%016x", i + 1);
    many.push_back(device(id, "Device " + std::to_string(i + 1)));
  }
  CHECK(read_devices(write_devices(many)).devices.size() == serve::kMaxDevices);
  std::string text = "graceui_devices = 1\n";
  for (const PairedDeviceRecord& d : many) {
    text += "[[device]]\nid = \"" + d.id + "\"\nname = \"" + d.name + "\"\npaired = \"2026-11-23\"\nhash = \"" + kHash + "\"\n";
  }
  const DevicesRead capped = read_devices(text);
  CHECK(capped.devices.size() == serve::kMaxDevices);
  CHECK(capped.left_out == 2);
}

TEST_CASE("the devices file: anything not of the writer's shape leaves it unread (G9)") {
  const std::string good = "id = \"0123456789abcdef\"\nname = \"Phone\"\npaired = \"2026-11-23\"\nhash = \"" + kHash + "\"\n";
  CHECK_FALSE(read_devices("").ok);
  CHECK_FALSE(read_devices("[[device]]\n" + good).ok);                       // no version line
  CHECK_FALSE(read_devices("graceui_devices = 2\n[[device]]\n" + good).ok);  // another version
  CHECK_FALSE(read_devices(with_entry(good) + "cookie = \"x\"\n").ok);       // a key it does not know
  CHECK_FALSE(read_devices(with_entry(good) + "[device]\n").ok);             // another table
  CHECK_FALSE(read_devices("graceui_devices = 1\nid = \"0123456789abcdef\"\n").ok);  // a key outside a device
  CHECK_FALSE(read_devices(with_entry(good) + "name\n").ok);                 // a line of another shape
  CHECK_FALSE(read_devices(with_entry(good) + "graceui_devices = 1\n").ok);  // the version twice, after a device
  // Comments, blank lines and Windows line ends are fine.
  std::string crlf;
  for (const char c : "# a note\r\n\r\ngraceui_devices = 1\r\n[[device]]\r\n" + good) {
    if (c == '\n') {
      crlf += "\r\n";
    } else if (c != '\0') {
      crlf += c;
    }
  }
  CHECK(read_devices(crlf).devices.size() == 1);
}
