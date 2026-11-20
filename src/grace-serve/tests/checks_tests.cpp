// The request checks (checks.h): Host, Origin, Fetch Metadata, a page load, the session cookie in a flood of others, the
// API request's order, a JSON body's type, and the token's shape.
#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>

#include "grace/serve/checks.h"
#include "support.h"

using namespace grace::serve;
using namespace grace::serve::test;

namespace {

// A token for the tests: 32 invented bytes, encoded.
std::string token(std::uint8_t seed) {
  std::array<std::uint8_t, kTokenBytes> bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<std::uint8_t>(seed * 31 + i * 7);
  }
  std::array<char, kTokenChars> text{};
  encode_token(bytes, text);
  return std::string(text.data(), text.size());
}

// Parses a GET or POST on 127.0.0.1:8642 with these fields and runs `check` on its head.
template <class Check>
auto with_head(std::string_view method, std::string_view fields, Check check, const HeadRules& rules = loopback_rules(),
               std::string_view host = "127.0.0.1:8642") {
  HeadParser parser(rules);
  const std::string head = std::string(method) + " /api HTTP/1.1\r\nHost: " + std::string(host) + "\r\n" +
                           (method == "POST" ? "Content-Length: 0\r\n" : "") + std::string(fields) + "\r\n";
  const HeadParser::Fed fed = parser.feed(head);
  REQUIRE(fed.state == HeadParser::State::complete);
  return check(parser.head());
}

}  // namespace

TEST_CASE("Host matches exactly, on each listener") {
  Listener loopback;
  loopback.port = 8642;
  CHECK(check_host("127.0.0.1:8642", loopback) == HostMatch::loopback);
  CHECK(check_host("localhost:8642", loopback) == HostMatch::localhost);
  CHECK(check_host("[::1]:8642", loopback) == HostMatch::ipv6_loopback);
  CHECK(check_host("127.0.0.1:8642 ", loopback) == HostMatch::none);
  CHECK(check_host("127.0.0.1:86420", loopback) == HostMatch::none);
  CHECK(check_host("127.0.0.1:864", loopback) == HostMatch::none);
  CHECK(check_host("127.0.0.1.:8642", loopback) == HostMatch::none);
  CHECK(check_host("127.0.0.2:8642", loopback) == HostMatch::none);
  CHECK(check_host("[::1]", loopback) == HostMatch::none);
  CHECK(check_host("::1:8642", loopback) == HostMatch::none);
  CHECK(check_host("localhost.:8642", loopback) == HostMatch::none);
  CHECK(check_host("Localhost:8642", loopback) == HostMatch::none);
  CHECK(check_host("", loopback) == HostMatch::none);
  loopback.port = 1024;
  CHECK(check_host("127.0.0.1:1024", loopback) == HostMatch::loopback);
  loopback.port = 65535;
  CHECK(check_host("127.0.0.1:65535", loopback) == HostMatch::loopback);
  CHECK(check_host("127.0.0.1:65536", loopback) == HostMatch::none);

  const HeadRules rules = network_rules();
  std::size_t address = 99;
  CHECK(check_host("192.0.2.7:8642", rules.listener, &address) == HostMatch::network);
  CHECK(address == 0);
  CHECK(check_host("[2001:db8::7]:8642", rules.listener, &address) == HostMatch::network);
  CHECK(address == 1);
  CHECK(check_host("192.0.2.8:8642", rules.listener) == HostMatch::none);
  CHECK(check_host("127.0.0.1:8642", rules.listener) == HostMatch::none);
  CHECK(check_host("[::1]:8642", rules.listener) == HostMatch::none);
}

TEST_CASE("Origin is the server's own: one origin on loopback, the named address on the network") {
  const auto origin = [](std::string_view value, std::string_view host = "127.0.0.1:8642") {
    return with_head(
        "POST", "Origin: " + std::string(value) + "\r\n",
        [](const RequestHead& head) { return check_origin(head, loopback_rules().listener); }, loopback_rules(), host);
  };
  CHECK(origin("http://127.0.0.1:8642") == OriginCheck::ok);
  CHECK(origin("http://127.0.0.1:8642", "localhost:8642") == OriginCheck::ok);
  CHECK(origin("http://localhost:8642", "localhost:8642") == OriginCheck::mismatch);  // one origin (item 23)
  CHECK(origin("http://[::1]:8642") == OriginCheck::mismatch);
  CHECK(origin("null") == OriginCheck::mismatch);
  CHECK(origin("https://127.0.0.1:8642") == OriginCheck::mismatch);
  CHECK(origin("http://127.0.0.1:8642/") == OriginCheck::mismatch);
  CHECK(origin("http://127.0.0.1") == OriginCheck::mismatch);
  CHECK(origin("http://127.0.0.1:8643") == OriginCheck::mismatch);
  CHECK(origin("HTTP://127.0.0.1:8642") == OriginCheck::mismatch);
  CHECK(origin("http://attacker.example") == OriginCheck::mismatch);
  CHECK(with_head("POST", "", [](const RequestHead& head) { return check_origin(head, loopback_rules().listener); }) ==
        OriginCheck::missing);

  const HeadRules network = network_rules();
  const auto network_origin = [&network](std::string_view value, std::string_view host) {
    return with_head(
        "POST", "Origin: " + std::string(value) + "\r\n",
        [&network](const RequestHead& head) { return check_origin(head, network.listener); }, network, host);
  };
  CHECK(network_origin("http://192.0.2.7:8642", "192.0.2.7:8642") == OriginCheck::ok);
  CHECK(network_origin("http://[2001:db8::7]:8642", "[2001:db8::7]:8642") == OriginCheck::ok);
  CHECK(network_origin("http://[2001:db8::7]:8642", "192.0.2.7:8642") == OriginCheck::mismatch);
  CHECK(network_origin("http://127.0.0.1:8642", "192.0.2.7:8642") == OriginCheck::mismatch);  // a forged loopback
}

TEST_CASE("Fetch Metadata: an API request is refused unless same-origin, and a page load is top-level") {
  const auto site = [](std::string_view fields) {
    return with_head("POST", fields, [](const RequestHead& head) { return fetch_site_allowed(head); });
  };
  CHECK(site(""));
  CHECK(site("Sec-Fetch-Site: same-origin\r\n"));
  CHECK_FALSE(site("Sec-Fetch-Site: same-site\r\n"));
  CHECK_FALSE(site("Sec-Fetch-Site: cross-site\r\n"));
  CHECK_FALSE(site("Sec-Fetch-Site: none\r\n"));
  CHECK_FALSE(site("Sec-Fetch-Site: Same-Origin\r\n"));

  const auto page = [](std::string_view method, std::string_view fields) {
    return with_head(method, fields, [](const RequestHead& head) { return is_page_load(head); });
  };
  CHECK(page("GET", ""));
  CHECK(page("GET", "Sec-Fetch-Mode: navigate\r\n"));
  CHECK(page("GET", "Sec-Fetch-Site: none\r\nSec-Fetch-Mode: navigate\r\nSec-Fetch-Dest: document\r\n"));
  CHECK(page("GET", "Sec-Fetch-Site: cross-site\r\nSec-Fetch-Mode: navigate\r\nSec-Fetch-Dest: document\r\n"));
  CHECK_FALSE(page("GET", "Sec-Fetch-Mode: navigate\r\nSec-Fetch-Dest: iframe\r\n"));
  CHECK_FALSE(page("GET", "Sec-Fetch-Mode: cors\r\n"));
  CHECK_FALSE(page("GET", "Sec-Fetch-Mode: no-cors\r\nSec-Fetch-Dest: script\r\n"));
  CHECK_FALSE(page("GET", "Sec-Fetch-Site: same-origin\r\n"));
  CHECK_FALSE(page("POST", ""));
  CHECK_FALSE(page("POST", "Sec-Fetch-Mode: navigate\r\n"));
}

TEST_CASE("the session cookie: any en_session that matches, in constant time, every other cookie ignored") {
  const std::string session = token(1);
  const std::string other = token(2);
  REQUIRE(valid_token(session));
  REQUIRE(session != other);
  const auto check = [&session](std::string_view field) { return check_session_cookie(field, session); };

  CHECK(check("en_session=" + session) == CookieCheck::matched);
  CHECK(check("a=1; en_session=" + session + "; b=2") == CookieCheck::matched);
  CHECK(check("  en_session = " + session + "  ;") == CookieCheck::matched);
  // A narrower Path's cookie of the same name, set by another program on 127.0.0.1, is sent first (item 3).
  CHECK(check("en_session=" + other + "; en_session=" + session) == CookieCheck::matched);
  CHECK(check("en_session=" + session + "; en_session=" + other) == CookieCheck::matched);
  CHECK(check("en_session=" + other) == CookieCheck::mismatch);
  CHECK(check("en_session=") == CookieCheck::mismatch);
  CHECK(check("en_session=\"" + session + "\"") == CookieCheck::mismatch);
  CHECK(check("en_session=" + session + "x") == CookieCheck::mismatch);
  CHECK(check("en_session=" + session.substr(1)) == CookieCheck::mismatch);
  CHECK(check("a=1; b=2") == CookieCheck::missing);
  CHECK(check("EN_SESSION=" + session) == CookieCheck::missing);  // a cookie's name has case
  CHECK(check("en_sessionx=" + session) == CookieCheck::missing);
  CHECK(check("x_en_session=" + session) == CookieCheck::missing);
  CHECK(check("en_session") == CookieCheck::missing);
  CHECK(check(";;;") == CookieCheck::missing);
  CHECK(check("") == CookieCheck::missing);
  CHECK(check_session_cookie(std::nullopt, session) == CookieCheck::missing);

  // A flood of other cookies within the cap still finds it; past the cap, the page says to clear them.
  std::string flood;
  while (flood.size() < kMaxCookie - session.size() - 20) {
    flood += "f" + std::to_string(flood.size()) + "=v; ";
  }
  CHECK(check(flood + "en_session=" + session) == CookieCheck::matched);
  CHECK(check(std::string(kMaxCookie, 'f') + "; en_session=" + session) == CookieCheck::oversized);

  // A value that is not a token matches nothing, even itself.
  CHECK(check_session_cookie(std::string_view("en_session=abc"), "abc") == CookieCheck::mismatch);
  CHECK(check_session_cookie(std::string_view("en_session="), "") == CookieCheck::mismatch);

  // Another credential's name, for the network listener's device cookie to come.
  CHECK(check_cookie("en_device=" + session, "en_device", session) == CookieCheck::matched);
  CHECK(check_cookie("en_session=" + session, "en_device", session) == CookieCheck::missing);
}

TEST_CASE("an API request: Origin, then Fetch Metadata, then the cookie") {
  const std::string session = token(3);
  const std::string own = "Origin: http://127.0.0.1:8642\r\n";
  const std::string cookie = "Cookie: en_session=" + session + "\r\n";
  const auto api = [&session](std::string_view fields, const HeadRules& rules = loopback_rules(),
                              std::string_view host = "127.0.0.1:8642") {
    return with_head(
        "POST", fields,
        [&session, &rules](const RequestHead& head) {
          return check_api_request(head, rules.listener, kSessionCookie, session);
        },
        rules, host);
  };
  CHECK(api(own + cookie) == Refusal::none);
  CHECK(api(own + "Sec-Fetch-Site: same-origin\r\n" + cookie) == Refusal::none);
  CHECK(api(cookie) == Refusal::origin_missing);
  CHECK(api("Origin: http://attacker.example\r\nSec-Fetch-Site: cross-site\r\n" + cookie) == Refusal::origin_mismatch);
  CHECK(api(own + "Sec-Fetch-Site: cross-site\r\n" + cookie) == Refusal::cross_site);
  CHECK(api(own + "Sec-Fetch-Site: same-site\r\n") == Refusal::cross_site);
  CHECK(api(own) == Refusal::cookie_missing);
  CHECK(api(own + "Cookie: en_session=" + token(4) + "\r\n") == Refusal::cookie_mismatch);
  CHECK(api(own + "Cookie: " + std::string(kMaxCookie - 1, 'f') + "=\r\n") == Refusal::cookie_missing);
  CHECK(status_of(Refusal::origin_missing) == 403);
  CHECK(status_of(Refusal::origin_mismatch) == 403);
  CHECK(status_of(Refusal::cross_site) == 403);
  CHECK(status_of(Refusal::cookie_missing) == 403);
  CHECK(status_of(Refusal::cookie_mismatch) == 403);

  // The network listener never accepts the session cookie (item 16).
  CHECK(api("Origin: http://192.0.2.7:8642\r\n" + cookie, network_rules(), "192.0.2.7:8642") ==
        Refusal::session_on_network);
  CHECK(api("Origin: http://127.0.0.1:8642\r\n" + cookie, network_rules(), "192.0.2.7:8642") ==
        Refusal::origin_mismatch);
}

TEST_CASE("a JSON body's Content-Type") {
  const auto json = [](std::string_view fields) {
    return with_head("POST", fields, [](const RequestHead& head) { return content_type_is_json(head); });
  };
  CHECK(json("Content-Type: application/json\r\n"));
  CHECK(json("Content-Type: Application/JSON\r\n"));
  CHECK(json("Content-Type: application/json; charset=utf-8\r\n"));
  CHECK(json("Content-Type: application/json;charset=UTF-8\r\n"));
  CHECK_FALSE(json(""));
  CHECK_FALSE(json("Content-Type: text/plain\r\n"));
  CHECK_FALSE(json("Content-Type: application/json5\r\n"));
  CHECK_FALSE(json("Content-Type: application/json; charset=latin1\r\n"));
  CHECK_FALSE(json("Content-Type: application/json; charset=utf-8; x=y\r\n"));
  CHECK_FALSE(json("Content-Type: multipart/form-data; boundary=a\r\n"));
  CHECK(status_of(Refusal::not_json) == 415);
}

TEST_CASE("a token is 43 characters of base64url, canonical, and compares in constant time") {
  const std::string a = token(5);
  CHECK(a.size() == kTokenChars);
  CHECK(valid_token(a));
  CHECK(a.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == std::string::npos);
  CHECK_FALSE(valid_token(a.substr(1)));
  CHECK_FALSE(valid_token(a + "A"));
  CHECK_FALSE(valid_token(std::string(42, 'A') + "="));
  CHECK_FALSE(valid_token(std::string(42, 'A') + "+"));  // Base64's alphabet, not base64url's
  CHECK(valid_token(std::string(42, 'A') + "E"));        // the last character's unused bits are zero
  CHECK_FALSE(valid_token(std::string(42, 'A') + "B"));  // and not otherwise: one encoding per value
  CHECK_FALSE(valid_token(""));

  CHECK(constant_time_equal("", ""));
  CHECK(constant_time_equal(a, a));
  CHECK_FALSE(constant_time_equal(a, token(6)));
  CHECK_FALSE(constant_time_equal("abc", "abd"));
  CHECK_FALSE(constant_time_equal("abc", "ab"));
}
