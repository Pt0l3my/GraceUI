// response.cpp: the response heads (response.h), each written whole or not at all into the caller's buffer.
#include "grace/serve/response.h"

#include <algorithm>
#include <array>

#include "ascii.h"
#include "grace/serve/checks.h"
#include "grace/serve/codec.h"
#include "grace/serve/pairing.h"

namespace grace::serve {
namespace {

using detail::is_alpha;
using detail::is_digit;

// §7.2 item 10, word for word.
constexpr std::string_view kSecurityHeaders =
    "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; "
    "font-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "Referrer-Policy: no-referrer\r\n"
    "Cross-Origin-Opener-Policy: same-origin\r\n"
    "Cross-Origin-Resource-Policy: same-origin\r\n";

constexpr std::size_t kMaxMediaType = 96;

class Writer {
 public:
  explicit Writer(std::span<char> out) noexcept : out_(out) {}

  void put(std::string_view text) noexcept {
    if (!ok_ || out_.size() - size_ < text.size()) {
      ok_ = false;
      return;
    }
    std::copy(text.begin(), text.end(), out_.begin() + static_cast<std::ptrdiff_t>(size_));
    size_ += text.size();
  }

  void number(std::uint64_t value) noexcept {
    std::array<char, 20> reversed{};
    std::size_t count = 0;
    do {
      reversed[count++] = static_cast<char>('0' + value % 10);
      value /= 10;
    } while (value != 0);
    std::array<char, 20> digits{};
    for (std::size_t i = 0; i < count; ++i) {
      digits[i] = reversed[count - 1 - i];
    }
    put(std::string_view(digits.data(), count));
  }

  std::size_t done() const noexcept { return ok_ ? size_ : 0; }

 private:
  std::span<char> out_;
  std::size_t size_ = 0;
  bool ok_ = true;
};

constexpr bool media_type_ok(std::string_view type) noexcept {
  if (type.size() > kMaxMediaType) {
    return false;
  }
  for (const char c : type) {
    if (!is_alpha(c) && !is_digit(c) && std::string_view(" /;=-+._").find(c) == std::string_view::npos) {
      return false;
    }
  }
  return true;
}

// A listed file's name as a Location's path: '/' and then letters, digits and "-._~/" only.
constexpr bool listed_path_ok(std::string_view path) noexcept {
  if (path.empty() || path.front() != '/' || path.size() > kMaxTarget) {
    return false;
  }
  for (const char c : path) {
    if (!is_alpha(c) && !is_digit(c) && std::string_view("-._~/").find(c) == std::string_view::npos) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::string_view security_headers() noexcept { return kSecurityHeaders; }

std::string_view reason_phrase(std::uint16_t status) noexcept {
  switch (status) {
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Content Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 417: return "Expectation Failed";
    case 421: return "Misdirected Request";
    case 426: return "Upgrade Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    case 505: return "HTTP Version Not Supported";
    default: return {};
  }
}

std::size_t write_response_head(const Response& response, std::span<char> out) noexcept {
  const std::string_view reason = reason_phrase(response.status);
  if (reason.empty() || response.status == 101 || response.status == 307) {
    return 0;
  }
  if (!media_type_ok(response.content_type) || (response.content_length != 0 && response.content_type.empty())) {
    return 0;
  }
  if ((!response.session_cookie.empty() && !valid_token(response.session_cookie)) ||
      (!response.device_cookie.empty() && !valid_token(response.device_cookie))) {
    return 0;
  }
  Writer writer(out);
  writer.put("HTTP/1.1 ");
  writer.number(response.status);
  writer.put(" ");
  writer.put(reason);
  writer.put("\r\n");
  if (!response.content_type.empty()) {
    writer.put("Content-Type: ");
    writer.put(response.content_type);
    writer.put("\r\n");
  }
  writer.put("Content-Length: ");
  writer.number(response.content_length);
  writer.put("\r\n");
  if (response.status == 405) {
    writer.put("Allow: GET, POST\r\n");
  }
  if (response.status == 426) {
    // RFC 9110 §7.8: whoever sends Upgrade names it in Connection too.
    writer.put("Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\nConnection: Upgrade, close\r\n");
  } else {
    writer.put("Connection: close\r\n");
  }
  if (response.no_store) {
    writer.put("Cache-Control: no-store\r\n");
  }
  if (!response.session_cookie.empty()) {
    writer.put("Set-Cookie: ");
    writer.put(kSessionCookie);
    writer.put("=");
    writer.put(response.session_cookie);
    writer.put("; Path=/; HttpOnly; SameSite=Strict\r\n");
  }
  if (!response.device_cookie.empty()) {
    writer.put("Set-Cookie: ");
    writer.put(kDeviceCookie);
    writer.put("=");
    writer.put(response.device_cookie);
    writer.put("; Path=/; Max-Age=");
    writer.number(kDeviceCookieSeconds);
    writer.put("; HttpOnly; SameSite=Strict\r\n");
  }
  writer.put(kSecurityHeaders);
  writer.put("\r\n");
  return writer.done();
}

std::size_t write_redirect(std::uint16_t port, std::string_view listed_path, std::span<char> out) noexcept {
  if (port == 0) {
    return 0;
  }
  Writer writer(out);
  writer.put("HTTP/1.1 307 Temporary Redirect\r\nLocation: http://127.0.0.1:");
  writer.number(port);
  writer.put(listed_path_ok(listed_path) ? listed_path : std::string_view("/"));
  writer.put("\r\nContent-Length: 0\r\nConnection: close\r\nCache-Control: no-store\r\n");
  writer.put(kSecurityHeaders);
  writer.put("\r\n");
  return writer.done();
}

std::size_t write_switching_protocols(std::string_view accept, std::span<char> out) noexcept {
  std::array<std::uint8_t, 21> digest{};
  const std::optional<std::size_t> decoded = base64_decode(accept, digest);
  if (accept.size() != kAcceptChars || !decoded || *decoded != 20) {
    return 0;
  }
  Writer writer(out);
  writer.put("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ");
  writer.put(accept);
  writer.put("\r\n");
  writer.put(kSecurityHeaders);
  writer.put("\r\n");
  return writer.done();
}

}  // namespace grace::serve
