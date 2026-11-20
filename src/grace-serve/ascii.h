// ascii.h: grace-serve's own character rules (RFC 9110 §5.6), private to the module: tokens, whitespace, case-free
// comparison, and a name with a port matched byte for byte. Pure.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace grace::serve::detail {

constexpr char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

constexpr bool iequals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (lower(a[i]) != lower(b[i])) {
      return false;
    }
  }
  return true;
}

constexpr bool is_alpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool is_hex(char c) noexcept { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// tchar (RFC 9110 §5.6.2).
constexpr bool is_tchar(char c) noexcept {
  return is_alpha(c) || is_digit(c) || std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}

constexpr bool is_token(std::string_view text) noexcept {
  if (text.empty()) {
    return false;
  }
  for (const char c : text) {
    if (!is_tchar(c)) {
      return false;
    }
  }
  return true;
}

constexpr bool is_ows(char c) noexcept { return c == ' ' || c == '\t'; }

constexpr std::string_view trim_ows(std::string_view text) noexcept {
  while (!text.empty() && is_ows(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && is_ows(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

// A port's decimal digits, with no leading zero.
struct PortText {
  std::array<char, 5> digits{};
  std::size_t size = 0;
  constexpr std::string_view view() const noexcept { return {digits.data(), size}; }
};

constexpr PortText port_text(std::uint16_t port) noexcept {
  PortText text;
  std::array<char, 5> reversed{};
  unsigned value = port;
  do {
    reversed[text.size++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value != 0);
  for (std::size_t i = 0; i < text.size; ++i) {
    text.digits[i] = reversed[text.size - 1 - i];
  }
  return text;
}

// `value` is exactly `prefix`, `name`, ':' and the port.
constexpr bool name_port_is(std::string_view value, std::string_view prefix, std::string_view name,
                            std::uint16_t port) noexcept {
  const PortText digits = port_text(port);
  if (name.empty() || value.size() != prefix.size() + name.size() + 1 + digits.size) {
    return false;
  }
  return value.substr(0, prefix.size()) == prefix && value.substr(prefix.size(), name.size()) == name &&
         value[prefix.size() + name.size()] == ':' && value.substr(prefix.size() + name.size() + 1) == digits.view();
}

}  // namespace grace::serve::detail
