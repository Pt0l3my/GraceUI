// The response-head writer (response.h): item 10's headers on every status, Connection: close and a Content-Length, no
// CORS header ever, Allow and the WebSocket version where they belong, the session cookie's attributes, the redirect's
// Location from the listed name only, and nothing written past the buffer.
#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>

#include "grace/serve/checks.h"
#include "grace/serve/response.h"

using namespace grace::serve;

namespace {

constexpr std::uint16_t kStatuses[] = {200, 400, 403, 404, 405, 408, 413, 414, 415, 417,
                                       421, 426, 429, 431, 500, 501, 503, 505};

std::string head_of(const Response& response) {
  std::array<char, kMaxResponseHead> out{};
  const std::size_t size = write_response_head(response, out);
  return std::string(out.data(), size);
}

bool has_line(const std::string& head, std::string_view line) {
  return head.find("\r\n" + std::string(line) + "\r\n") != std::string::npos;
}

std::string session_token() {
  std::array<std::uint8_t, kTokenBytes> bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<std::uint8_t>(i * 13 + 5);
  }
  std::array<char, kTokenChars> text{};
  encode_token(bytes, text);
  return std::string(text.data(), text.size());
}

}  // namespace

TEST_CASE("item 10's headers are one block, word for word") {
  const std::string_view headers = security_headers();
  CHECK(headers.find("Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src "
                     "'self' data:; font-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; "
                     "form-action 'none'\r\n") == 0);
  CHECK(headers.find("X-Content-Type-Options: nosniff\r\n") != std::string_view::npos);
  CHECK(headers.find("Referrer-Policy: no-referrer\r\n") != std::string_view::npos);
  CHECK(headers.find("Cross-Origin-Opener-Policy: same-origin\r\n") != std::string_view::npos);
  CHECK(headers.find("Cross-Origin-Resource-Policy: same-origin\r\n") != std::string_view::npos);
}

TEST_CASE("every status the writer answers with carries the same frame of headers") {
  for (const std::uint16_t status : kStatuses) {
    CAPTURE(status);
    Response response;
    response.status = status;
    response.content_type = "text/plain; charset=utf-8";
    response.content_length = 12;
    const std::string head = head_of(response);
    REQUIRE_FALSE(head.empty());
    CHECK(head.rfind("HTTP/1.1 " + std::to_string(status) + " " + std::string(reason_phrase(status)) + "\r\n", 0) ==
          0);
    CHECK(has_line(head, "Content-Type: text/plain; charset=utf-8"));
    CHECK(has_line(head, "Content-Length: 12"));
    CHECK(has_line(head, "Cache-Control: no-store"));
    CHECK(head.find(std::string(security_headers())) != std::string::npos);
    CHECK(head.substr(head.size() - 4) == "\r\n\r\n");
    CHECK(head.find("Access-Control") == std::string::npos);  // no CORS, and no private network access answer
    CHECK(head.find("Set-Cookie") == std::string::npos);
    CHECK(head.size() < kMaxResponseHead);
    if (status == 426) {
      CHECK(has_line(head, "Upgrade: websocket"));
      CHECK(has_line(head, "Sec-WebSocket-Version: 13"));
      CHECK(has_line(head, "Connection: Upgrade, close"));
    } else {
      CHECK(has_line(head, "Connection: close"));
      CHECK(head.find("Upgrade:") == std::string::npos);
    }
    CHECK((head.find("\r\nAllow: GET, POST\r\n") != std::string::npos) == (status == 405));
    // Every line ends in CRLF, and no CR or LF stands alone.
    for (std::size_t i = 0; i < head.size(); ++i) {
      if (head[i] == '\r') {
        CHECK(head[i + 1] == '\n');
      }
      if (head[i] == '\n') {
        CHECK(head[i - 1] == '\r');
      }
    }
  }
}

TEST_CASE("an empty body needs no type, a static file may be cached, and a 0 length is written") {
  Response response;
  response.status = 404;
  const std::string head = head_of(response);
  CHECK(has_line(head, "Content-Length: 0"));
  CHECK(head.find("\r\nContent-Type:") == std::string::npos);  // X-Content-Type-Options aside

  Response file;
  file.content_type = "font/woff2";
  file.content_length = 70000;
  file.no_store = false;
  const std::string file_head = head_of(file);
  CHECK(file_head.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
  CHECK(has_line(file_head, "Content-Length: 70000"));
  CHECK(file_head.find("Cache-Control") == std::string::npos);
}

TEST_CASE("the session cookie is set with its attributes, and only a token is written") {
  const std::string token = session_token();
  Response response;
  response.content_type = "text/html; charset=utf-8";
  response.content_length = 100;
  response.session_cookie = token;
  const std::string head = head_of(response);
  CHECK(has_line(head, "Set-Cookie: en_session=" + token + "; Path=/; HttpOnly; SameSite=Strict"));
  CHECK(head.find("Domain") == std::string::npos);
  CHECK(head.find("Expires") == std::string::npos);
  CHECK(head.find("Max-Age") == std::string::npos);

  response.session_cookie = "abc";
  CHECK(head_of(response).empty());
  const std::string with_cr = token.substr(0, 42) + "\r";
  response.session_cookie = with_cr;
  CHECK(head_of(response).empty());
}

TEST_CASE("the writer refuses what it does not answer, a bad media type, and a buffer too small") {
  Response response;
  for (const std::uint16_t status : {0, 101, 199, 204, 301, 302, 304, 307, 308, 418, 599, 999}) {
    response.status = static_cast<std::uint16_t>(status);
    CAPTURE(status);
    CHECK(head_of(response).empty());
  }
  response.status = 200;
  response.content_length = 5;
  CHECK(head_of(response).empty());  // a body with no type
  response.content_type = "text/html\r\nX-Injected: 1";
  CHECK(head_of(response).empty());
  response.content_type = "text/\"html\"";
  CHECK(head_of(response).empty());
  const std::string long_type = "text/html; charset=utf-8; a=" + std::string(70, 'b');
  response.content_type = long_type;
  CHECK(head_of(response).empty());

  response.content_type = "text/plain";
  std::array<char, kMaxResponseHead + 8> out{};
  out.fill('#');
  const std::size_t size = write_response_head(response, std::span<char>(out.data(), kMaxResponseHead));
  REQUIRE(size != 0);
  for (std::size_t room = 0; room < size; ++room) {
    CAPTURE(room);
    std::array<char, kMaxResponseHead + 8> small{};
    small.fill('#');
    CHECK(write_response_head(response, std::span<char>(small.data(), room)) == 0);
    for (std::size_t i = room; i < small.size(); ++i) {
      REQUIRE(small[i] == '#');  // nothing past the room given
    }
  }
  CHECK(write_response_head(response, std::span<char>(out.data(), size)) == size);
}

TEST_CASE("the redirect to 127.0.0.1 is built from the listed name, never from the request") {
  std::array<char, kMaxResponseHead> out{};
  const auto redirect = [&out](std::uint16_t port, std::string_view path) {
    const std::size_t size = write_redirect(port, path, out);
    return std::string(out.data(), size);
  };
  const std::string head = redirect(8642, "/settings/browser.html");
  CHECK(head.rfind("HTTP/1.1 307 Temporary Redirect\r\n", 0) == 0);
  CHECK(has_line(head, "Location: http://127.0.0.1:8642/settings/browser.html"));
  CHECK(has_line(head, "Content-Length: 0"));
  CHECK(has_line(head, "Connection: close"));
  CHECK(has_line(head, "Cache-Control: no-store"));
  CHECK(head.find(std::string(security_headers())) != std::string::npos);
  CHECK(head.find("Set-Cookie") == std::string::npos);

  CHECK(has_line(redirect(1024, "/"), "Location: http://127.0.0.1:1024/"));
  CHECK(has_line(redirect(65535, "/app-3f2a.js"), "Location: http://127.0.0.1:65535/app-3f2a.js"));
  // A name that is not a listed file's shape becomes "/": nothing odd reaches the header.
  for (const std::string_view odd : {"", "index.html", "/a?b", "/a#b", "/a b", "/a%2F", "/a\r\nX: 1", "//host/x",
                                     "/caf\xC3\xA9", "/a\"b"}) {
    CAPTURE(odd);
    const std::string written = redirect(8642, odd);
    if (odd == "//host/x") {
      CHECK(has_line(written, "Location: http://127.0.0.1:8642//host/x"));  // still on 127.0.0.1, a path of ours
    } else {
      CHECK(has_line(written, "Location: http://127.0.0.1:8642/"));
    }
  }
  CHECK(redirect(0, "/").empty());
  std::array<char, 32> small{};
  CHECK(write_redirect(8642, "/", small) == 0);
  // The longest Location: a listed name of kMaxTarget bytes still fits a head and a target's room.
  std::array<char, kMaxResponseHead + kMaxTarget> roomy{};
  CHECK(write_redirect(65535, "/" + std::string(kMaxTarget - 1, 'a'), roomy) != 0);
}

TEST_CASE("the longest head write_response_head makes fits kMaxResponseHead") {
  const std::string token = session_token();
  const std::string type = "text/html; charset=utf-8; a=" + std::string(96 - 28, 'b');
  REQUIRE(type.size() == 96);
  Response response;
  response.status = 426;  // the most fields: Upgrade, Sec-WebSocket-Version and Connection's two tokens
  response.content_type = type;
  response.content_length = UINT64_MAX;
  response.session_cookie = token;
  const std::string head = head_of(response);
  REQUIRE_FALSE(head.empty());
  CHECK(head.size() < 800);  // 735 bytes on 2026-11-20
}
