// limits.h: grace-serve's caps (D158), which are also the sizes of its fixed buffers: a request's head, its target, its
// fields and its Cookie field; the largest body any route takes; a message from a page, reassembled (§6.8); a control
// frame; a response's head; and the shapes of a token and of Sec-WebSocket-Accept. Pure.
#pragma once

#include <cstddef>
#include <cstdint>

namespace grace::serve {

// A request's head, its request line and its final empty line included (SRV §5.1); a longer one is 414 or 431.
inline constexpr std::size_t kMaxHead = 8192;
// The request-target, as sent; a longer one is 414 (§7.2 item 19).
inline constexpr std::size_t kMaxTarget = 1024;
// Header fields in one head; more is 431.
inline constexpr std::size_t kMaxFields = 64;
// The Cookie field's value; a longer one is 431, whose page says to clear this address's cookies (§7.2 item 3).
inline constexpr std::size_t kMaxCookie = 4096;
// The largest Content-Length any route takes: an upload's 32 MiB (§7.2 item 18); each route caps its own below it.
inline constexpr std::uint64_t kMaxUpload = std::uint64_t{32} << 20;

// A message from a page, reassembled from its fragments (§6.8, §7.2 item 22).
inline constexpr std::size_t kMaxMessage = 16384;
// A control frame's payload (RFC 6455 §5.5), and a whole control frame as the server sends it, unmasked.
inline constexpr std::size_t kMaxControlPayload = 125;
inline constexpr std::size_t kMaxControlFrame = 2 + kMaxControlPayload;

// Room for any head write_response_head makes (the longest, a 426 with a cookie and a 96-byte media type, is 735
// bytes); a redirect needs kMaxTarget more for its Location.
inline constexpr std::size_t kMaxResponseHead = 1024;

// The session cookie and a ticket: 256 bits from the OS random source (§7.2 item 3), written as base64url without
// padding.
inline constexpr std::size_t kTokenBytes = 32;
inline constexpr std::size_t kTokenChars = 43;

// Sec-WebSocket-Accept: the Base64 of a SHA-1 digest (RFC 6455 §4.2.2).
inline constexpr std::size_t kAcceptChars = 28;

}  // namespace grace::serve
