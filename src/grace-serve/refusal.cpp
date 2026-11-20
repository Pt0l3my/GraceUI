// refusal.cpp: each refusal's status, name and fixed body (refusal.h).
#include "grace/serve/refusal.h"

namespace grace::serve {

std::uint16_t status_of(Refusal refusal) noexcept {
  switch (refusal) {
    case Refusal::none:
      return 0;
    case Refusal::bad_request_line:
    case Refusal::leading_empty_line:
    case Refusal::bad_target:
    case Refusal::bare_cr:
    case Refusal::bare_lf:
    case Refusal::nul_byte:
    case Refusal::obs_fold:
    case Refusal::space_before_colon:
    case Refusal::bad_field_name:
    case Refusal::bad_field_value:
    case Refusal::missing_host:
    case Refusal::duplicate_field:
    case Refusal::bad_content_length:
    case Refusal::get_with_body:
    case Refusal::post_without_length:
    case Refusal::ws_version_missing:
    case Refusal::bad_upgrade:
    case Refusal::no_connection_upgrade:
    case Refusal::bad_ws_key:
    case Refusal::upgrade_with_body:
    case Refusal::upgrade_not_get:
      return 400;
    case Refusal::origin_missing:
    case Refusal::origin_mismatch:
    case Refusal::cross_site:
    case Refusal::cookie_missing:
    case Refusal::cookie_mismatch:
    case Refusal::session_on_network:
      return 403;
    case Refusal::method_not_allowed:
      return 405;
    case Refusal::body_too_large:
      return 413;
    case Refusal::target_too_long:
      return 414;
    case Refusal::not_json:
      return 415;
    case Refusal::expectation:
      return 417;
    case Refusal::wrong_host:
      return 421;
    case Refusal::upgrade_required:
    case Refusal::ws_version:
      return 426;
    case Refusal::head_too_large:
    case Refusal::too_many_fields:
    case Refusal::cookie_too_large:
      return 431;
    case Refusal::transfer_encoding:
      return 501;
    case Refusal::wrong_version:
      return 505;
  }
  return 400;
}

std::string_view name_of(Refusal refusal) noexcept {
  switch (refusal) {
    case Refusal::none: return "none";
    case Refusal::bad_request_line: return "bad_request_line";
    case Refusal::leading_empty_line: return "leading_empty_line";
    case Refusal::bad_target: return "bad_target";
    case Refusal::bare_cr: return "bare_cr";
    case Refusal::bare_lf: return "bare_lf";
    case Refusal::nul_byte: return "nul_byte";
    case Refusal::obs_fold: return "obs_fold";
    case Refusal::space_before_colon: return "space_before_colon";
    case Refusal::bad_field_name: return "bad_field_name";
    case Refusal::bad_field_value: return "bad_field_value";
    case Refusal::missing_host: return "missing_host";
    case Refusal::duplicate_field: return "duplicate_field";
    case Refusal::bad_content_length: return "bad_content_length";
    case Refusal::get_with_body: return "get_with_body";
    case Refusal::post_without_length: return "post_without_length";
    case Refusal::method_not_allowed: return "method_not_allowed";
    case Refusal::body_too_large: return "body_too_large";
    case Refusal::target_too_long: return "target_too_long";
    case Refusal::expectation: return "expectation";
    case Refusal::wrong_host: return "wrong_host";
    case Refusal::head_too_large: return "head_too_large";
    case Refusal::too_many_fields: return "too_many_fields";
    case Refusal::cookie_too_large: return "cookie_too_large";
    case Refusal::transfer_encoding: return "transfer_encoding";
    case Refusal::wrong_version: return "wrong_version";
    case Refusal::origin_missing: return "origin_missing";
    case Refusal::origin_mismatch: return "origin_mismatch";
    case Refusal::cross_site: return "cross_site";
    case Refusal::cookie_missing: return "cookie_missing";
    case Refusal::cookie_mismatch: return "cookie_mismatch";
    case Refusal::session_on_network: return "session_on_network";
    case Refusal::not_json: return "not_json";
    case Refusal::upgrade_required: return "upgrade_required";
    case Refusal::ws_version: return "ws_version";
    case Refusal::ws_version_missing: return "ws_version_missing";
    case Refusal::bad_upgrade: return "bad_upgrade";
    case Refusal::no_connection_upgrade: return "no_connection_upgrade";
    case Refusal::bad_ws_key: return "bad_ws_key";
    case Refusal::upgrade_with_body: return "upgrade_with_body";
    case Refusal::upgrade_not_get: return "upgrade_not_get";
  }
  return "unknown";
}

std::string_view refusal_body(Refusal refusal) noexcept {
  switch (refusal) {
    case Refusal::head_too_large:
    case Refusal::cookie_too_large:
      return "Your browser sent more cookies than this address takes. Clear this address's cookies in your browser's "
             "settings, then reload the page.\n";
    case Refusal::cookie_missing:
    case Refusal::cookie_mismatch:
      return "Reload the page to sign in again.\n";
    default:
      break;
  }
  switch (status_of(refusal)) {
    case 400: return "Bad request.\n";
    case 403: return "Forbidden.\n";
    case 405: return "Only GET and POST are answered here.\n";
    case 413: return "The request is too large.\n";
    case 414: return "The address is too long.\n";
    case 415: return "The request's body must be JSON.\n";
    case 417: return "Expect is not supported.\n";
    case 421: return "This server answers only at its own address.\n";
    case 426: return "This address takes WebSocket connections only.\n";
    case 431: return "The request's header fields are too large.\n";
    case 501: return "Transfer-Encoding is not supported.\n";
    case 505: return "Only HTTP/1.1 is served here.\n";
    default: return "";
  }
}

}  // namespace grace::serve
