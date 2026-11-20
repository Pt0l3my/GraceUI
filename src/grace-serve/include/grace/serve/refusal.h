// refusal.h: every refusal grace-serve's pure core makes, each with the one status it is answered with (§7.2 items 3 to
// 7 and 19 to 23; SRV §5.2), a fixed name for a log line and a fixed body. Nothing of the request is in any of them.
#pragma once

#include <cstdint>
#include <string_view>

namespace grace::serve {

enum class Refusal : std::uint8_t {
  none,

  // 400: the head's syntax and framing (RFC 9112; §7.2 item 19).
  bad_request_line,     // not "method SP target SP version", or a method that is not a token
  leading_empty_line,   // an empty line before the request line
  bad_target,           // not origin-form, or a byte origin-form does not allow
  bare_cr,              // a CR not followed by LF
  bare_lf,              // an LF not preceded by CR
  nul_byte,             // a NUL anywhere in the head
  obs_fold,             // a field line that starts with whitespace (obsolete line folding)
  space_before_colon,   // whitespace between a field's name and its colon
  bad_field_name,       // no colon, an empty name, or a name that is not a token
  bad_field_value,      // a control character in a field's value
  missing_host,
  duplicate_field,      // a second Host, Content-Length, Cookie, Origin, Upgrade, Sec-WebSocket-*, Sec-Fetch-* or
                        // Content-Type
  bad_content_length,   // not plain digits, or past 2^64 - 1
  get_with_body,        // a GET whose Content-Length is not 0
  post_without_length,  // a POST with no Content-Length

  method_not_allowed,  // 405, with Allow: GET, POST
  body_too_large,      // 413
  target_too_long,     // 414
  expectation,         // 417: any Expect
  wrong_host,          // 421 (§7.2 item 5)
  head_too_large,      // 431
  too_many_fields,     // 431
  cookie_too_large,    // 431: the page says to clear this address's cookies (§7.2 item 3)
  transfer_encoding,   // 501: any Transfer-Encoding, then close
  wrong_version,       // 505: a version other than HTTP/1.1

  // 403: the checks of §7.2 items 3, 6 and 7.
  origin_missing,      // a state-changing request or an upgrade with no Origin
  origin_mismatch,     // an Origin other than the server's own
  cross_site,          // Sec-Fetch-Site present and not same-origin
  cookie_missing,      // no cookie of the credential's name
  cookie_mismatch,     // no cookie of that name with the credential's value
  session_on_network,  // the session cookie asked for on the network listener, which never accepts it (§7.2 item 16)

  not_json,  // 415: a body that must be application/json (§7.2 item 9)

  // The WebSocket handshake (RFC 6455 §4.2; §7.2 item 22).
  upgrade_required,       // 426: no Upgrade at all
  ws_version,             // 426: a Sec-WebSocket-Version other than 13
  ws_version_missing,     // 400
  bad_upgrade,            // 400: an Upgrade other than "websocket"
  no_connection_upgrade,  // 400: Connection without the "upgrade" token
  bad_ws_key,             // 400: no Sec-WebSocket-Key, or one that is not the Base64 of 16 bytes
  upgrade_with_body,      // 400: a body, or bytes sent after the head before the 101
  upgrade_not_get,        // 400
};

// The status a refusal is answered with; 0 for none.
std::uint16_t status_of(Refusal refusal) noexcept;

// A fixed name for a log line, such as "wrong_host".
std::string_view name_of(Refusal refusal) noexcept;

// A fixed plain-text body for the refusal's answer: it never echoes the request (the 421's names no Host, item 19).
std::string_view refusal_body(Refusal refusal) noexcept;

}  // namespace grace::serve
