// checks.cpp: Host, Origin, Fetch Metadata, the credential cookie, a JSON body's type and the token's shape (checks.h).
#include "grace/serve/checks.h"

#include <array>

#include "ascii.h"
#include "grace/serve/codec.h"

namespace grace::serve {
namespace {

using detail::iequals;
using detail::name_port_is;
using detail::trim_ows;

constexpr std::size_t npos = std::string_view::npos;

bool is(const std::optional<std::string_view>& field, std::string_view value) noexcept {
  return field && *field == value;
}

}  // namespace

HostMatch check_host(std::string_view host, const Listener& listener, std::size_t* address) noexcept {
  if (!listener.network) {
    if (name_port_is(host, "", "127.0.0.1", listener.port)) {
      return HostMatch::loopback;
    }
    if (name_port_is(host, "", "localhost", listener.port)) {
      return HostMatch::localhost;
    }
    if (name_port_is(host, "", "[::1]", listener.port)) {
      return HostMatch::ipv6_loopback;
    }
    return HostMatch::none;
  }
  for (std::size_t i = 0; i < listener.addresses.size(); ++i) {
    if (name_port_is(host, "", listener.addresses[i], listener.port)) {
      if (address != nullptr) {
        *address = i;
      }
      return HostMatch::network;
    }
  }
  return HostMatch::none;
}

OriginCheck check_origin(const RequestHead& head, const Listener& listener) noexcept {
  if (!head.origin) {
    return OriginCheck::missing;
  }
  std::string_view own;
  if (!listener.network) {
    own = "127.0.0.1";
  } else if (head.host == HostMatch::network && head.host_address < listener.addresses.size()) {
    own = listener.addresses[head.host_address];
  } else {
    return OriginCheck::mismatch;
  }
  return name_port_is(*head.origin, "http://", own, listener.port) ? OriginCheck::ok : OriginCheck::mismatch;
}

bool fetch_site_allowed(const RequestHead& head) noexcept {
  return !head.sec_fetch_site || *head.sec_fetch_site == "same-origin";
}

bool is_page_load(const RequestHead& head) noexcept {
  if (head.method != Method::get) {
    return false;
  }
  if (!head.sec_fetch_site && !head.sec_fetch_mode && !head.sec_fetch_dest) {
    return true;
  }
  return is(head.sec_fetch_mode, "navigate") && (!head.sec_fetch_dest || is(head.sec_fetch_dest, "document"));
}

CookieCheck check_cookie(std::optional<std::string_view> cookie_field, std::string_view name,
                         std::string_view value) noexcept {
  if (!cookie_field || name.empty()) {
    return CookieCheck::missing;
  }
  if (cookie_field->size() > kMaxCookie) {
    return CookieCheck::oversized;
  }
  const unsigned usable = valid_token(value) ? 1u : 0u;
  bool found = false;
  unsigned matched = 0;
  std::string_view rest = *cookie_field;
  while (true) {
    const std::size_t semicolon = rest.find(';');
    const std::string_view pair = trim_ows(rest.substr(0, semicolon));
    const std::size_t equals = pair.find('=');
    if (equals != npos && trim_ows(pair.substr(0, equals)) == name) {
      found = true;
      // Every cookie of the name is compared, and in full: which one matched, if any, takes no shorter time.
      matched |= usable & (constant_time_equal(trim_ows(pair.substr(equals + 1)), value) ? 1u : 0u);
    }
    if (semicolon == npos) {
      break;
    }
    rest.remove_prefix(semicolon + 1);
  }
  if (matched != 0) {
    return CookieCheck::matched;
  }
  return found ? CookieCheck::mismatch : CookieCheck::missing;
}

CookieCheck check_session_cookie(std::optional<std::string_view> cookie_field, std::string_view session) noexcept {
  return check_cookie(cookie_field, kSessionCookie, session);
}

Refusal check_api_request(const RequestHead& head, const Listener& listener, std::string_view cookie_name,
                          std::string_view cookie_value) noexcept {
  switch (check_origin(head, listener)) {
    case OriginCheck::missing:
      return Refusal::origin_missing;
    case OriginCheck::mismatch:
      return Refusal::origin_mismatch;
    case OriginCheck::ok:
      break;
  }
  if (!fetch_site_allowed(head)) {
    return Refusal::cross_site;
  }
  if (listener.network && cookie_name == kSessionCookie) {
    return Refusal::session_on_network;
  }
  switch (check_cookie(head.cookie, cookie_name, cookie_value)) {
    case CookieCheck::matched:
      return Refusal::none;
    case CookieCheck::missing:
      return Refusal::cookie_missing;
    case CookieCheck::mismatch:
      return Refusal::cookie_mismatch;
    case CookieCheck::oversized:
      return Refusal::cookie_too_large;
  }
  return Refusal::cookie_mismatch;
}

bool content_type_is_json(const RequestHead& head) noexcept {
  if (!head.content_type) {
    return false;
  }
  const std::string_view value = *head.content_type;
  const std::size_t semicolon = value.find(';');
  if (!iequals(trim_ows(value.substr(0, semicolon)), "application/json")) {
    return false;
  }
  return semicolon == npos || iequals(trim_ows(value.substr(semicolon + 1)), "charset=utf-8");
}

bool content_type_is_octets(const RequestHead& head) noexcept {
  return head.content_type && iequals(trim_ows(*head.content_type), "application/octet-stream");
}

bool valid_token(std::string_view token) noexcept {
  if (token.size() != kTokenChars) {
    return false;
  }
  std::array<std::uint8_t, kTokenBytes> bytes{};
  const std::optional<std::size_t> decoded = base64url_decode(token, bytes);
  return decoded && *decoded == kTokenBytes;
}

void encode_token(std::span<const std::uint8_t, kTokenBytes> bytes, std::span<char, kTokenChars> out) noexcept {
  base64url_encode(bytes, out);
}

bool constant_time_equal(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) {
    return false;
  }
  unsigned difference = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    difference |= static_cast<unsigned>(static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
  }
  return difference == 0;
}

}  // namespace grace::serve
