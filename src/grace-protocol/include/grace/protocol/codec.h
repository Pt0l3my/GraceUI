// SPDX-License-Identifier: MIT
// codec.h: what the generated decoders and encoders (messages.h, from protocol.toml) are built on (D159). Every reader
// checks the JSON value's type and its bounds before it copies anything, and none throws: a value that fails sets the
// error with the field's path (never the value itself, §6.7) and answers false. Pure: no I/O, threads or clocks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace grace::protocol {

// Frames keep their fields in the schema's order when encoded, so they read as the design writes them.
using Json = nlohmann::ordered_json;

// The largest integer both views hold exactly: the web app's numbers are doubles, so a u64 field is at most 2^53 - 1.
inline constexpr std::uint64_t kMaxSafeInteger = (std::uint64_t{1} << 53) - 1;

struct DecodeError {
  std::string field;  // where it failed: "spaces[2].settings_schema.hash"
  std::string why;    // what was wrong, in words; never the value
};

// Every byte of `text` is UTF-8 (no overlong form, no surrogate, nothing past U+10FFFF).
bool valid_utf8(std::string_view text) noexcept;
// An id, a topic or a key: 1 to 64 of a-z, 0-9, '.', '_', '-' and '/'.
bool valid_id(std::string_view text) noexcept;

// The bytes `value` takes encoded, or more than `limit` once it is past it (the count stops there).
std::size_t encoded_size(const Json& value, std::size_t limit) noexcept;

// The member `name` of the object `object`: null if it is absent (an error then if `required`).
const Json* field(const Json& object, const char* name, bool required, const std::string& path, DecodeError& error);

bool fail(DecodeError& error, const std::string& path, const char* why);

bool read_string(const Json& value, std::size_t max, std::string& out, const std::string& path, DecodeError& error);
bool read_id(const Json& value, std::string& out, const std::string& path, DecodeError& error);
bool read_u32(const Json& value, std::uint32_t& out, const std::string& path, DecodeError& error);
bool read_u64(const Json& value, std::uint64_t& out, const std::string& path, DecodeError& error);
bool read_f64(const Json& value, double& out, const std::string& path, DecodeError& error);
bool read_bool(const Json& value, bool& out, const std::string& path, DecodeError& error);
// The member `name`, to move from: as field(), for a decoder that owns the frame's parsed value.
Json* field_mut(Json& object, const char* name, bool required, const std::string& path, DecodeError& error);
// A JSON value (`object_only`: an object) moved into `out`, leaving null behind: a frame-bounded field, never copied.
bool take_json(Json& value, bool object_only, Json& out, const std::string& path, DecodeError& error);

// Appends a value's text, invalid UTF-8 replaced (strings are checked when read; this never throws on them).
void append_json(std::string& out, const Json& value);
// Appends `,"name":` and the value's text.
void append_field(std::string& out, const char* name, const Json& value);

// A json field bounded only by its frame's own limit: not measured again.
inline constexpr std::size_t kWithinFrame = static_cast<std::size_t>(-1);

// Any JSON value (`object_only`: an object), at most `max` bytes encoded, unless `max` is kWithinFrame.
bool read_json(const Json& value, std::size_t max, bool object_only, Json& out, const std::string& path,
               DecodeError& error);

// A list of at most `max` items, each read by `item(value, out, path, error)`.
template <class T, class Read>
bool read_list(const Json& value, std::size_t max, std::vector<T>& out, const std::string& path, DecodeError& error,
               Read item) {
  if (!value.is_array()) {
    return fail(error, path, "not a list");
  }
  if (value.size() > max) {
    return fail(error, path, "too many items");
  }
  out.clear();
  out.reserve(value.size());
  std::size_t index = 0;
  for (const Json& element : value) {
    T one{};
    if (!item(element, one, path + "[" + std::to_string(index) + "]", error)) {
      return false;
    }
    out.push_back(std::move(one));
    ++index;
  }
  return true;
}

}  // namespace grace::protocol
