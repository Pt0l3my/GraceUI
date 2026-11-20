// checks.h: the request checks of §7.2 as pure functions: Host (item 5), Origin (item 6), Fetch Metadata (items 3 and
// 7), the session cookie (item 3), the ticket's and the cookie's shape, and a JSON body's Content-Type (item 9). The
// cookie's random bytes are the I/O part's; only its shape and its comparison are here. Pure: no allocation.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "grace/serve/head.h"
#include "grace/serve/limits.h"
#include "grace/serve/refusal.h"

namespace grace::serve {

// The session cookie's name (§7.2 item 3).
inline constexpr std::string_view kSessionCookie = "en_session";

// Host, exactly (item 5): on the loopback listener 127.0.0.1:PORT, localhost:PORT or [::1]:PORT, byte for byte (so
// with no other case, no leading zero in the port and nothing after it); on the network listener one of its own
// addresses with the port, so that a loopback Host there is none. With HostMatch::network, `address` says which.
HostMatch check_host(std::string_view host, const Listener& listener, std::size_t* address = nullptr) noexcept;

enum class OriginCheck : std::uint8_t { ok, missing, mismatch };

// Origin, exactly the server's own (item 6): http://127.0.0.1:PORT on the loopback listener, whichever loopback name
// the Host used, since the dashboard has one origin (item 23); on the network listener http:// and the address the Host
// named, with the port. "null" and every other value is a mismatch.
OriginCheck check_origin(const RequestHead& head, const Listener& listener) noexcept;

// Fetch Metadata for an API request (item 7): no Sec-Fetch-Site, or exactly "same-origin".
bool fetch_site_allowed(const RequestHead& head) noexcept;

// A top-level page load (item 3), which may be given the session cookie or a redirect (item 23): a GET whose
// Sec-Fetch-Mode is "navigate" with no Sec-Fetch-Dest or "document" (a frame's navigation is not top-level), or a GET
// with no Fetch Metadata at all.
bool is_page_load(const RequestHead& head) noexcept;

enum class CookieCheck : std::uint8_t {
  matched,    // a cookie of this name holds the value
  missing,    // no cookie of this name (or no Cookie field)
  mismatch,   // cookies of this name, none holding the value
  oversized,  // a Cookie field past kMaxCookie: the page says to clear this address's cookies
};

// The Cookie field's pairs ("a=1; b=2") are read in order; every one with exactly this name is compared with the value
// in constant time, and any match passes, since another program on 127.0.0.1 may set a cookie of the same name (item
// 3); every other cookie is ignored. A value that is not a token (valid_token) matches nothing.
CookieCheck check_cookie(std::optional<std::string_view> cookie_field, std::string_view name,
                         std::string_view value) noexcept;
CookieCheck check_session_cookie(std::optional<std::string_view> cookie_field, std::string_view session) noexcept;

// An API request: a POST or a WebSocket upgrade. Origin (missing or another, 403), then Fetch Metadata (403), then the
// credential cookie (403, or 431 when oversized). The network listener never accepts the session cookie (item 16).
Refusal check_api_request(const RequestHead& head, const Listener& listener, std::string_view cookie_name,
                          std::string_view cookie_value) noexcept;

// A body that must be JSON (item 9): Content-Type application/json, without case, and no parameter but charset=utf-8.
bool content_type_is_json(const RequestHead& head) noexcept;

// The session cookie's and a ticket's shape: kTokenChars of base64url without padding, the encoding of kTokenBytes.
bool valid_token(std::string_view token) noexcept;
// Writes kTokenBytes random bytes (the I/O part's) as a token.
void encode_token(std::span<const std::uint8_t, kTokenBytes> bytes, std::span<char, kTokenChars> out) noexcept;

// Equal, in a time that depends only on the lengths (which are public: every token has the same).
bool constant_time_equal(std::string_view a, std::string_view b) noexcept;

}  // namespace grace::serve
