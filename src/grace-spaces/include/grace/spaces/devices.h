// SPDX-License-Identifier: MIT
// devices.h: the devices paired with the browser view, as GraceUI keeps them between sessions (G9, §7.2 item 16):
// Game\GraceUI\user\devices.toml, a file of its own and never a settings file, so that Copy settings, the bug report and
// recordings never carry it. Each device is its id, the name given it on the pairing page, the date it paired and the
// SHA-256 of its cookie; never the cookie itself. The reader takes only the one shape the writer writes: an entry that
// breaks its rules is left out, and anything else in the file (another table, a key it does not know, a string it
// cannot read) leaves the whole file unread, so no device is paired by a file GraceUI did not write. Pure: no files, no
// clocks, no Win32; the host reads, writes and dates.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "grace/serve/pairing.h"

namespace grace::spaces {

struct PairedDeviceRecord {
  std::string id;      // kDeviceIdChars of lowercase hex (serve::valid_device_id)
  std::string name;    // 1 to 32 characters (serve::valid_device_name)
  std::string paired;  // the PC's local date it paired, YYYY-MM-DD
  std::array<std::uint8_t, 32> hash{};
};

struct DevicesRead {
  bool ok = false;                          // false: not GraceUI's devices file, and nothing is taken from it
  std::vector<PairedDeviceRecord> devices;  // at most serve::kMaxDevices, each id once, in the file's order
  std::size_t left_out = 0;                 // entries that broke a rule, came twice or past the cap
};

// devices.toml's text, as the writer writes it.
DevicesRead read_devices(std::string_view text);
// The file for these devices (each one valid, at most kMaxDevices; any other is left out).
std::string write_devices(std::span<const PairedDeviceRecord> devices);
// YYYY-MM-DD with a month 01 to 12 and a day 01 to 31.
bool valid_paired_date(std::string_view date) noexcept;
// The server's view of them: ids and hashes (serve::Server::set_devices).
std::vector<serve::DeviceKey> device_keys(std::span<const PairedDeviceRecord> devices);

}  // namespace grace::spaces
