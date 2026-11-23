// SPDX-License-Identifier: MIT
// devices.cpp: the paired devices' file (devices.h; G9).
#include "grace/spaces/devices.h"

#include <algorithm>
#include <optional>

namespace grace::spaces {
namespace {

constexpr std::string_view kHeader =
    "# GraceUI's paired devices (G9): each device's id, the name it was given, the date it paired, and the SHA-256 of\n"
    "# its cookie, never the cookie. GraceUI writes this file; forget a device on GraceUI's Browser view page.\n";
constexpr std::string_view kVersion = "graceui_devices = 1";
constexpr std::string_view kTable = "[[device]]";
constexpr std::string_view kHex = "0123456789abcdef";

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
    text.remove_suffix(1);
  }
  return text;
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

void append_utf8(std::string& out, std::uint32_t code) {
  if (code < 0x80) {
    out += static_cast<char>(code);
  } else if (code < 0x800) {
    out += static_cast<char>(0xC0 | (code >> 6));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else {
    out += static_cast<char>(0xE0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  }
}

// A TOML basic string on one line: "…" with \" \\ and \uXXXX (no surrogate, no control character). nullopt otherwise.
std::optional<std::string> basic_string(std::string_view text) {
  if (text.size() < 2 || text.front() != '"' || text.back() != '"') {
    return std::nullopt;
  }
  text = text.substr(1, text.size() - 2);
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '"') {
      return std::nullopt;  // a quote must be escaped
    }
    if (c != '\\') {
      out += c;
      continue;
    }
    if (++i >= text.size()) {
      return std::nullopt;
    }
    if (text[i] == '"' || text[i] == '\\') {
      out += text[i];
    } else if (text[i] == 'u' && i + 4 < text.size()) {
      std::uint32_t code = 0;
      for (std::size_t k = 1; k <= 4; ++k) {
        const int v = hex_value(text[i + k]);
        if (v < 0) {
          return std::nullopt;
        }
        code = code << 4 | static_cast<std::uint32_t>(v);
      }
      if (code < 0x20 || code == 0x7F || (code >= 0xD800 && code <= 0xDFFF)) {
        return std::nullopt;
      }
      append_utf8(out, code);
      i += 4;
    } else {
      return std::nullopt;
    }
  }
  return out;
}

std::string toml_string(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out + '"';
}

bool parse_hash(std::string_view text, std::array<std::uint8_t, 32>& out) {
  if (text.size() != 64) {
    return false;
  }
  for (std::size_t i = 0; i < 32; ++i) {
    const std::size_t high = kHex.find(text[2 * i]);  // lowercase only, as written
    const std::size_t low = kHex.find(text[2 * i + 1]);
    if (high == std::string_view::npos || low == std::string_view::npos) {
      return false;
    }
    out[i] = static_cast<std::uint8_t>(high << 4 | low);
  }
  return true;
}

bool valid_record(const PairedDeviceRecord& device) {
  return serve::valid_device_id(device.id) && serve::valid_device_name(device.name) &&
         valid_paired_date(device.paired);
}

}  // namespace

bool valid_paired_date(std::string_view date) noexcept {
  if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
    return false;
  }
  for (const std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
    if (date[i] < '0' || date[i] > '9') {
      return false;
    }
  }
  const int month = (date[5] - '0') * 10 + (date[6] - '0');
  const int day = (date[8] - '0') * 10 + (date[9] - '0');
  return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

DevicesRead read_devices(std::string_view text) {
  DevicesRead out;
  bool versioned = false;
  struct Entry {
    std::optional<std::string> id, name, paired, hash;
    bool broken = false;
  };
  std::vector<Entry> entries;
  while (!text.empty()) {
    const std::size_t end = std::min(text.find('\n'), text.size());
    const std::string_view line = trim(text.substr(0, end));
    text.remove_prefix(std::min(end + 1, text.size()));
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line == kVersion) {
      if (versioned || !entries.empty()) {
        return {};  // once, before any device
      }
      versioned = true;
      continue;
    }
    if (line == kTable) {
      if (!versioned) {
        return {};
      }
      entries.emplace_back();
      continue;
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos || entries.empty()) {
      return {};  // a line of another shape, or a key outside a device
    }
    const std::string_view key = trim(line.substr(0, equals));
    const std::optional<std::string> value = basic_string(trim(line.substr(equals + 1)));
    Entry& entry = entries.back();
    std::optional<std::string>* slot = key == "id"       ? &entry.id
                                       : key == "name"   ? &entry.name
                                       : key == "paired" ? &entry.paired
                                       : key == "hash"   ? &entry.hash
                                                         : nullptr;
    if (slot == nullptr) {
      return {};  // a key it does not know
    }
    if (!value || slot->has_value()) {
      entry.broken = true;  // a string it cannot read, or a key twice: this device is left out
      continue;
    }
    *slot = *value;
  }
  if (!versioned) {
    return {};
  }
  out.ok = true;
  for (const Entry& entry : entries) {
    PairedDeviceRecord device;
    const bool whole = !entry.broken && entry.id && entry.name && entry.paired && entry.hash &&
                       parse_hash(*entry.hash, device.hash);
    if (whole) {
      device.id = *entry.id;
      device.name = *entry.name;
      device.paired = *entry.paired;
    }
    const bool again = whole && std::any_of(out.devices.begin(), out.devices.end(),
                                            [&](const PairedDeviceRecord& d) { return d.id == device.id; });
    if (!whole || !valid_record(device) || again || out.devices.size() >= serve::kMaxDevices) {
      ++out.left_out;
      continue;
    }
    out.devices.push_back(std::move(device));
  }
  return out;
}

std::string write_devices(std::span<const PairedDeviceRecord> devices) {
  std::string out(kHeader);
  out += kVersion;
  out += '\n';
  std::size_t written = 0;
  for (const PairedDeviceRecord& device : devices) {
    if (!valid_record(device) || written >= serve::kMaxDevices) {
      continue;
    }
    std::string hash;
    for (const std::uint8_t byte : device.hash) {
      hash += kHex[byte >> 4];
      hash += kHex[byte & 0xF];
    }
    out += "\n[[device]]\nid = " + toml_string(device.id) + "\nname = " + toml_string(device.name) +
           "\npaired = " + toml_string(device.paired) + "\nhash = " + toml_string(hash) + "\n";
    ++written;
  }
  return out;
}

std::vector<serve::DeviceKey> device_keys(std::span<const PairedDeviceRecord> devices) {
  std::vector<serve::DeviceKey> keys;
  for (const PairedDeviceRecord& device : devices) {
    if (!serve::valid_device_id(device.id)) {
      continue;
    }
    serve::DeviceKey key;
    std::copy_n(device.id.begin(), key.id.size(), key.id.begin());
    key.hash = device.hash;
    keys.push_back(key);
  }
  return keys;
}

}  // namespace grace::spaces
