// SPDX-License-Identifier: MIT
// codec.cpp: the readers the generated decoders call (codec.h).
#include "grace/protocol/codec.h"

#include <cmath>

namespace grace::protocol {

bool valid_utf8(std::string_view text) noexcept {
  std::size_t i = 0;
  const std::size_t n = text.size();
  while (i < n) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) {
      ++i;
      continue;
    }
    std::size_t extra = 0;
    std::uint32_t point = 0;
    if ((c & 0xE0u) == 0xC0u) {
      extra = 1;
      point = c & 0x1Fu;
    } else if ((c & 0xF0u) == 0xE0u) {
      extra = 2;
      point = c & 0x0Fu;
    } else if ((c & 0xF8u) == 0xF0u) {
      extra = 3;
      point = c & 0x07u;
    } else {
      return false;
    }
    if (n - i <= extra) {
      return false;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
      const auto next = static_cast<unsigned char>(text[i + k]);
      if ((next & 0xC0u) != 0x80u) {
        return false;
      }
      point = (point << 6) | (next & 0x3Fu);
    }
    static constexpr std::uint32_t kLeast[] = {0, 0x80, 0x800, 0x10000};
    if (point < kLeast[extra] || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) {
      return false;
    }
    i += extra + 1;
  }
  return true;
}

bool valid_id(std::string_view text) noexcept {
  if (text.empty() || text.size() > 64) {
    return false;
  }
  for (const char c : text) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '/')) {
      return false;
    }
  }
  return true;
}

namespace {

// Counts encoded bytes without building the text, stopping once past `limit`.
class Counter {
 public:
  explicit Counter(std::size_t limit) : limit_(limit) {}
  void add(std::size_t n) { total_ += n; }
  bool over() const { return total_ > limit_; }
  std::size_t total() const { return total_; }

 private:
  std::size_t limit_;
  std::size_t total_ = 0;
};

void count(const Json& value, Counter& counter) {
  if (counter.over()) {
    return;
  }
  switch (value.type()) {
    case Json::value_t::object:
      counter.add(2 + (value.empty() ? 0 : value.size() - 1));
      for (auto it = value.begin(); it != value.end() && !counter.over(); ++it) {
        counter.add(it.key().size() + 3);
        count(it.value(), counter);
      }
      break;
    case Json::value_t::array:
      counter.add(2 + (value.empty() ? 0 : value.size() - 1));
      for (const Json& element : value) {
        count(element, counter);
        if (counter.over()) {
          break;
        }
      }
      break;
    case Json::value_t::string:
      counter.add(value.get_ref<const std::string&>().size() + 2);  // escapes only lengthen it: a lower bound
      break;
    case Json::value_t::number_unsigned:
    case Json::value_t::number_integer: {
      // Its digits, and a sign.
      const bool is_signed = value.type() == Json::value_t::number_integer;
      const std::int64_t as_signed = is_signed ? value.get<std::int64_t>() : 0;
      const bool negative = as_signed < 0;
      std::uint64_t n = !is_signed ? value.get<std::uint64_t>()
                        : negative ? 0 - static_cast<std::uint64_t>(as_signed)
                                   : static_cast<std::uint64_t>(as_signed);
      std::size_t digits = 1;
      while (n >= 10) {
        n /= 10;
        ++digits;
      }
      counter.add(digits + (negative ? 1 : 0));
      break;
    }
    case Json::value_t::boolean:
      counter.add(value.get<bool>() ? 4 : 5);
      break;
    case Json::value_t::number_float:
      counter.add(24);  // the longest shortest form a double takes: an upper bound
      break;
    default:
      counter.add(4);  // null
      break;
  }
}

}  // namespace

std::size_t encoded_size(const Json& value, std::size_t limit) noexcept {
  try {
    Counter counter(limit);
    count(value, counter);
    return counter.total();
  } catch (...) {
    return limit + 1;
  }
}

bool fail(DecodeError& error, const std::string& path, const char* why) {
  error.field = path;
  error.why = why;
  return false;
}

const Json* field(const Json& object, const char* name, bool required, const std::string& path, DecodeError& error) {
  const auto found = object.find(name);
  if (found == object.end() || found->is_null()) {
    if (required) {
      fail(error, path.empty() ? name : path + "." + name, "missing");
    }
    return nullptr;
  }
  return &*found;
}

Json* field_mut(Json& object, const char* name, bool required, const std::string& path, DecodeError& error) {
  const auto found = object.find(name);
  if (found == object.end() || found->is_null()) {
    if (required) {
      fail(error, path.empty() ? name : path + "." + name, "missing");
    }
    return nullptr;
  }
  return &*found;
}

bool take_json(Json& value, bool object_only, Json& out, const std::string& path, DecodeError& error) {
  if (object_only && !value.is_object()) {
    return fail(error, path, "not an object");
  }
  out = std::move(value);
  return true;
}

void append_json(std::string& out, const Json& value) {
  out += value.dump(-1, ' ', false, Json::error_handler_t::replace);
}

void append_field(std::string& out, const char* name, const Json& value) {
  out += ",\"";
  out += name;
  out += "\":";
  append_json(out, value);
}

bool read_string(const Json& value, std::size_t max, std::string& out, const std::string& path, DecodeError& error) {
  if (!value.is_string()) {
    return fail(error, path, "not a string");
  }
  const auto& text = value.get_ref<const std::string&>();
  if (text.size() > max) {
    return fail(error, path, "too long");
  }
  if (!valid_utf8(text)) {
    return fail(error, path, "not UTF-8");
  }
  out = text;
  return true;
}

bool read_id(const Json& value, std::string& out, const std::string& path, DecodeError& error) {
  if (!value.is_string()) {
    return fail(error, path, "not a string");
  }
  const auto& text = value.get_ref<const std::string&>();
  if (!valid_id(text)) {
    return fail(error, path, "not an id (1 to 64 of a-z, 0-9, '.', '_', '-' and '/')");
  }
  out = text;
  return true;
}

bool read_u64(const Json& value, std::uint64_t& out, const std::string& path, DecodeError& error) {
  if (value.is_number_unsigned()) {
    out = value.get<std::uint64_t>();
  } else if (value.is_number_integer()) {
    const std::int64_t n = value.get<std::int64_t>();
    if (n < 0) {
      return fail(error, path, "negative");
    }
    out = static_cast<std::uint64_t>(n);
  } else {
    return fail(error, path, "not a whole number");
  }
  if (out > kMaxSafeInteger) {
    return fail(error, path, "past 2^53 - 1");
  }
  return true;
}

bool read_u32(const Json& value, std::uint32_t& out, const std::string& path, DecodeError& error) {
  std::uint64_t wide = 0;
  if (!read_u64(value, wide, path, error)) {
    return false;
  }
  if (wide > 0xFFFFFFFFu) {
    return fail(error, path, "past 2^32 - 1");
  }
  out = static_cast<std::uint32_t>(wide);
  return true;
}

bool read_f64(const Json& value, double& out, const std::string& path, DecodeError& error) {
  if (!value.is_number()) {
    return fail(error, path, "not a number");
  }
  out = value.get<double>();
  if (!std::isfinite(out)) {
    return fail(error, path, "not finite");
  }
  return true;
}

bool read_bool(const Json& value, bool& out, const std::string& path, DecodeError& error) {
  if (!value.is_boolean()) {
    return fail(error, path, "not true or false");
  }
  out = value.get<bool>();
  return true;
}

bool read_json(const Json& value, std::size_t max, bool object_only, Json& out, const std::string& path,
               DecodeError& error) {
  if (object_only && !value.is_object()) {
    return fail(error, path, "not an object");
  }
  if (max != kWithinFrame && encoded_size(value, max) > max) {
    return fail(error, path, "too large");
  }
  out = value;
  return true;
}

}  // namespace grace::protocol
