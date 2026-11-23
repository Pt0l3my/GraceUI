// response.h: the response-head writer, into a caller's fixed buffer (§7.2 items 10, 19 and 23). Every head it writes
// carries security_headers(), and every one but the 101 a Content-Length and Connection: close; none carries a CORS
// header, and no value it writes comes from the request (the session cookie and the Location are the server's own, and
// their shapes are checked). Pure: no allocation.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "grace/serve/limits.h"

namespace grace::serve {

// Item 10's headers, as one block of lines that every head carries.
std::string_view security_headers() noexcept;

// The reason phrase of a status the writer answers with; empty for any other status.
std::string_view reason_phrase(std::uint16_t status) noexcept;

struct Response {
  std::uint16_t status = 200;
  // The body's media type, such as "text/html; charset=utf-8": letters, digits and " /;=-+._" only, at most 96 bytes.
  // Needed when content_length is not 0.
  std::string_view content_type;
  std::uint64_t content_length = 0;
  // Cache-Control: no-store: on the HTML, the API and every error (item 10); a static file other than HTML may go
  // without.
  bool no_store = true;
  // A session cookie to set (item 3), a token (valid_token): written as
  // en_session=…; Path=/; HttpOnly; SameSite=Strict, with no Domain and no expiry. Empty for none.
  std::string_view session_cookie;
  // A paired device's cookie (G9, §7.2 item 16), a token: en_device=…; Path=/; Max-Age=kDeviceCookieSeconds; HttpOnly;
  // SameSite=Strict, with no Domain, so host-only. Set at pairing and renewed at each page load. Empty for none.
  std::string_view device_cookie;
};

// Writes the head (status line, fields, the empty line) into `out`; returns its size, or 0 when the status is not one
// the writer answers with (101 and 307 have their own writers below), a value has the wrong shape, or `out` is too
// small (kMaxResponseHead is enough for any). A 405 carries Allow: GET, POST; a 426 carries Upgrade: websocket and
// Sec-WebSocket-Version: 13 (RFC 9110 §15.5.22, RFC 6455 §4.4).
std::size_t write_response_head(const Response& response, std::span<char> out) noexcept;

// The redirect of item 23: a 307 to http://127.0.0.1:PORT and `listed_path`, which must be the name of a file as
// graceui.toml lists it, never the request's own bytes; a path not made of letters, digits and "-._~/" after its leading
// '/' is written as "/". Returns the head's size, or 0 (a port of 0, or `out` too small: kMaxResponseHead and
// kMaxTarget together are enough).
std::size_t write_redirect(std::uint16_t port, std::string_view listed_path, std::span<char> out) noexcept;

// The 101 of RFC 6455 §4.2.2, with the Sec-WebSocket-Accept that check_upgrade computed. No Content-Length (RFC 9110
// §8.6 forbids one on a 1xx) and Connection: Upgrade. Returns the head's size, or 0.
std::size_t write_switching_protocols(std::string_view accept, std::span<char> out) noexcept;

}  // namespace grace::serve
