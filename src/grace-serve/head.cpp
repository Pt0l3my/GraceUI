// head.cpp: the request-head parser (head.h): the scan as bytes arrive, then the whole head's syntax, then the Host
// check, then the rest, in the order head.h gives.
#include "grace/serve/head.h"

#include <limits>

#include "ascii.h"
#include "grace/serve/checks.h"

namespace grace::serve {
namespace {

using detail::iequals;
using detail::is_alpha;
using detail::is_digit;
using detail::is_hex;
using detail::is_ows;
using detail::is_token;
using detail::trim_ows;

constexpr std::size_t npos = std::string_view::npos;

// The fields that may appear at most once, with where the head keeps each.
struct Singleton {
  std::string_view name;
  std::optional<std::string_view> RequestHead::* slot;
};

constexpr Singleton kSingletons[] = {
    {"host", &RequestHead::host_field},
    {"origin", &RequestHead::origin},
    {"cookie", &RequestHead::cookie},
    {"content-type", &RequestHead::content_type},
    {"upgrade", &RequestHead::upgrade},
    {"sec-websocket-key", &RequestHead::sec_websocket_key},
    {"sec-websocket-version", &RequestHead::sec_websocket_version},
    {"sec-fetch-site", &RequestHead::sec_fetch_site},
    {"sec-fetch-mode", &RequestHead::sec_fetch_mode},
    {"sec-fetch-dest", &RequestHead::sec_fetch_dest},
};

// pchar but pct-encoded (RFC 3986 §3.3): unreserved, sub-delims, ':' and '@'.
constexpr bool is_pchar(char c) noexcept {
  return is_alpha(c) || is_digit(c) || std::string_view("-._~!$&'()*+,;=:@").find(c) != npos;
}

// origin-form (RFC 9112 §3.2.1): an absolute path, then '?' and a query if any; '%' only before two hex digits. The
// bytes are never decoded: the caller matches them as they are (§7.2 item 21).
constexpr bool origin_form(std::string_view target) noexcept {
  if (target.empty() || target.front() != '/') {
    return false;
  }
  for (std::size_t i = 0; i < target.size(); ++i) {
    const char c = target[i];
    if (c == '%') {
      if (i + 2 >= target.size() || !is_hex(target[i + 1]) || !is_hex(target[i + 2])) {
        return false;
      }
      i += 2;
    } else if (c != '/' && c != '?' && !is_pchar(c)) {
      return false;
    }
  }
  return true;
}

constexpr bool version_syntax(std::string_view version) noexcept {
  return version.size() == 8 && version.substr(0, 5) == "HTTP/" && is_digit(version[5]) && version[6] == '.' &&
         is_digit(version[7]);
}

// Plain digits, no sign and no space, within 64 bits (RFC 9112 §6.3).
constexpr bool parse_length(std::string_view text, std::uint64_t& value) noexcept {
  if (text.empty()) {
    return false;
  }
  std::uint64_t total = 0;
  for (const char c : text) {
    if (!is_digit(c)) {
      return false;
    }
    const auto digit = static_cast<std::uint64_t>(c - '0');
    if (total > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
      return false;
    }
    total = total * 10 + digit;
  }
  value = total;
  return true;
}

// field-value (RFC 9110 §5.5): SP, HTAB, visible ASCII and obs-text; no other control character.
constexpr bool field_value_ok(std::string_view value) noexcept {
  for (const char c : value) {
    const auto byte = static_cast<unsigned char>(c);
    if (c != '\t' && (byte < 0x20 || byte == 0x7F)) {
      return false;
    }
  }
  return true;
}

struct RequestLine {
  std::string_view method;
  std::string_view target;
};

// "method SP request-target SP HTTP-version", with single spaces.
constexpr Refusal read_request_line(std::string_view line, RequestLine& parts) noexcept {
  const std::size_t first = line.find(' ');
  if (first == npos || !is_token(line.substr(0, first))) {
    return Refusal::bad_request_line;
  }
  const std::size_t second = line.find(' ', first + 1);
  if (second == npos || second == first + 1) {
    return Refusal::bad_request_line;
  }
  const std::string_view target = line.substr(first + 1, second - first - 1);
  const std::string_view version = line.substr(second + 1);
  if (!version_syntax(version)) {
    return Refusal::bad_request_line;
  }
  if (version != "HTTP/1.1") {
    return Refusal::wrong_version;
  }
  if (target.size() > kMaxTarget) {
    return Refusal::target_too_long;
  }
  if (!origin_form(target)) {
    return Refusal::bad_target;
  }
  parts = RequestLine{line.substr(0, first), target};
  return Refusal::none;
}

}  // namespace

std::optional<std::string_view> RequestHead::first(std::string_view name) const noexcept {
  for (const Field& field : fields) {
    if (iequals(field.name, name)) {
      return field.value;
    }
  }
  return std::nullopt;
}

bool RequestHead::has_token(std::string_view name, std::string_view token) const noexcept {
  for (const Field& field : fields) {
    if (!iequals(field.name, name)) {
      continue;
    }
    std::string_view rest = field.value;
    while (true) {
      const std::size_t comma = rest.find(',');
      if (iequals(trim_ows(rest.substr(0, comma)), token)) {
        return true;
      }
      if (comma == npos) {
        break;
      }
      rest.remove_prefix(comma + 1);
    }
  }
  return false;
}

HeadParser::HeadParser(const HeadRules& rules) noexcept : rules_(rules) {}

void HeadParser::reset(const HeadRules& rules) noexcept {
  rules_ = rules;
  reset();
}

void HeadParser::reset() noexcept {
  state_ = State::need_more;
  refusal_ = Refusal::none;
  size_ = 0;
  line_start_ = 0;
  first_line_end_ = 0;
  method_ = {};
  transfer_encoding_ = false;
  expect_ = false;
  field_count_ = 0;
  head_ = RequestHead{};
}

void HeadParser::refuse(Refusal refusal) noexcept {
  state_ = State::refused;
  refusal_ = refusal;
}

HeadParser::Fed HeadParser::feed(std::string_view bytes) noexcept {
  std::size_t used = 0;
  while (state_ == State::need_more && used < bytes.size()) {
    if (size_ == kMaxHead) {
      refuse(at_cap());
      break;
    }
    const char c = bytes[used++];
    buffer_[size_++] = c;
    const bool after_cr = size_ >= 2 && buffer_[size_ - 2] == '\r';
    if (c == '\0') {
      refuse(Refusal::nul_byte);
    } else if (after_cr && c != '\n') {
      refuse(Refusal::bare_cr);
    } else if (c == '\n') {
      if (!after_cr) {
        refuse(Refusal::bare_lf);
      } else if (size_ - 2 == line_start_) {
        if (line_start_ == 0) {
          refuse(Refusal::leading_empty_line);
        } else {
          finish();
        }
      } else {
        if (first_line_end_ == 0) {
          first_line_end_ = size_ - 2;
        }
        line_start_ = size_;
      }
    }
  }
  return Fed{state_, used};
}

// The head has not ended within kMaxHead bytes: 414 when the request line is what is too long, otherwise what the
// request line itself would get, or 431.
Refusal HeadParser::at_cap() const noexcept {
  const std::string_view all(buffer_.data(), size_);
  if (first_line_end_ == 0) {
    const std::size_t first = all.find(' ');
    if (first == npos) {
      return Refusal::bad_request_line;
    }
    const std::size_t second = all.find(' ', first + 1);
    return second == npos || second - first - 1 > kMaxTarget ? Refusal::target_too_long : Refusal::bad_request_line;
  }
  RequestLine line;
  const Refusal refusal = read_request_line(all.substr(0, first_line_end_), line);
  return refusal != Refusal::none ? refusal : Refusal::head_too_large;
}

void HeadParser::finish() noexcept {
  head_ = RequestHead{};
  head_.size = size_;
  const std::string_view all(buffer_.data(), size_);
  RequestLine line;
  Refusal refusal = read_request_line(all.substr(0, first_line_end_), line);
  if (refusal == Refusal::none) {
    method_ = line.method;
    head_.target = line.target;
    const std::size_t query = line.target.find('?');
    head_.path = line.target.substr(0, query);
    if (query != npos) {
      head_.has_query = true;
      head_.query = line.target.substr(query + 1);
    }
  }
  // Every CR left is a line's end: a bare one was refused as it arrived. The last two bytes end the empty line.
  std::size_t at = first_line_end_ + 2;
  while (refusal == Refusal::none && at < size_ - 2) {
    const std::size_t end = all.find('\r', at);
    refusal = parse_field(all.substr(at, end - at));
    at = end + 2;
  }
  if (refusal == Refusal::none && !head_.host_field) {
    refusal = Refusal::missing_host;
  }
  if (refusal == Refusal::none) {
    head_.fields = std::span<const Field>(fields_.data(), field_count_);
    refusal = admit();
  }
  if (refusal == Refusal::none) {
    state_ = State::complete;
  } else {
    refuse(refusal);
  }
}

Refusal HeadParser::parse_field(std::string_view line) noexcept {
  if (line.empty()) {
    return Refusal::bad_field_name;  // never: an empty line ends the head
  }
  if (is_ows(line.front())) {
    return Refusal::obs_fold;
  }
  const std::size_t colon = line.find(':');
  if (colon == npos || colon == 0) {
    return Refusal::bad_field_name;
  }
  const std::string_view name = line.substr(0, colon);
  if (is_ows(name.back())) {
    return Refusal::space_before_colon;
  }
  if (!is_token(name)) {
    return Refusal::bad_field_name;
  }
  const std::string_view value = trim_ows(line.substr(colon + 1));
  if (!field_value_ok(value)) {
    return Refusal::bad_field_value;
  }
  if (field_count_ == kMaxFields) {
    return Refusal::too_many_fields;
  }
  fields_[field_count_++] = Field{name, value};

  if (iequals(name, "content-length")) {
    if (head_.has_content_length) {
      return Refusal::duplicate_field;
    }
    if (!parse_length(value, head_.content_length)) {
      return Refusal::bad_content_length;
    }
    head_.has_content_length = true;
  } else if (iequals(name, "transfer-encoding")) {
    transfer_encoding_ = true;
  } else if (iequals(name, "expect")) {
    expect_ = true;
  } else {
    for (const Singleton& singleton : kSingletons) {
      if (iequals(name, singleton.name)) {
        std::optional<std::string_view>& slot = head_.*singleton.slot;
        if (slot) {
          return Refusal::duplicate_field;
        }
        if (singleton.slot == &RequestHead::cookie && value.size() > kMaxCookie) {
          return Refusal::cookie_too_large;
        }
        slot = value;
        break;
      }
    }
  }
  return Refusal::none;
}

// The whole head is well formed: the Host check first (§7.2 item 20), then the method and the body's framing.
Refusal HeadParser::admit() noexcept {
  head_.host = check_host(*head_.host_field, rules_.listener, &head_.host_address);
  if (head_.host == HostMatch::none) {
    return Refusal::wrong_host;
  }
  if (method_ == "GET") {
    head_.method = Method::get;
  } else if (method_ == "POST") {
    head_.method = Method::post;
  } else {
    return Refusal::method_not_allowed;
  }
  if (transfer_encoding_) {
    return Refusal::transfer_encoding;
  }
  if (head_.method == Method::get && head_.content_length != 0) {
    return Refusal::get_with_body;
  }
  if (head_.method == Method::post && !head_.has_content_length) {
    return Refusal::post_without_length;
  }
  if (head_.content_length > rules_.max_content_length) {
    return Refusal::body_too_large;
  }
  if (expect_) {
    return Refusal::expectation;
  }
  return Refusal::none;
}

}  // namespace grace::serve
