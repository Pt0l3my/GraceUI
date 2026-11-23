// Pairing (pairing.h, G9, §7.2 item 16): the private addresses the network listener may take, the six-digit code's
// rules (two minutes, once, void after five wrong tries, no bias), paired devices matched by the hash of their cookie,
// a device's name and id, and the built-in pairing page with no inline script.
#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <string>
#include <string_view>

#include "grace/serve/checks.h"
#include "grace/serve/pairing.h"

using namespace grace::serve;
using Clock = PairingCode::Clock;

namespace {

std::string token(std::uint8_t seed) {
  std::array<std::uint8_t, kTokenBytes> bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<std::uint8_t>(seed * 31 + i * 7);
  }
  std::array<char, kTokenChars> out{};
  encode_token(bytes, out);
  return std::string(out.data(), out.size());
}

DeviceKey key(char id, std::string_view cookie) {
  DeviceKey k;
  k.id.fill(id);
  k.hash = device_hash(cookie);
  return k;
}

}  // namespace

TEST_CASE("the network listener takes the PC's private IPv4 addresses alone") {
  const auto is = [](std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    const std::array<std::uint8_t, 4> address{a, b, c, d};
    return private_ipv4(address);
  };
  CHECK(is(10, 0, 0, 7));
  CHECK(is(172, 16, 4, 2));
  CHECK(is(172, 31, 255, 254));
  CHECK(is(192, 168, 1, 20));
  CHECK_FALSE(is(172, 15, 0, 1));
  CHECK_FALSE(is(172, 32, 0, 1));
  CHECK_FALSE(is(127, 0, 0, 1));     // loopback
  CHECK_FALSE(is(169, 254, 3, 4));   // link-local
  CHECK_FALSE(is(100, 64, 0, 1));    // shared (carrier) space
  CHECK_FALSE(is(192, 0, 2, 2));     // documentation, as public
  CHECK_FALSE(is(8, 8, 8, 8));
  // The dotted form, on documentation addresses (RFC 5737): no address of a real network in the tests.
  std::array<char, 16> out{};
  const std::array<std::uint8_t, 4> address{203, 0, 113, 254};
  CHECK(std::string_view(out.data(), format_ipv4(address, out)) == "203.0.113.254");
  const std::array<std::uint8_t, 4> zeros{198, 51, 100, 0};
  CHECK(std::string_view(out.data(), format_ipv4(zeros, out)) == "198.51.100.0");
}

TEST_CASE("a pairing code: six digits for two minutes, once, void after five wrong tries") {
  const Clock::time_point t0{};
  std::array<std::uint8_t, 64> random{};
  random[3] = 42;  // the first draw: 42
  PairingCode code;
  CHECK(code.attempt("000042", t0) == PairingCode::Try::none);  // nothing showing yet
  REQUIRE(code.start(random, t0));
  CHECK(code.code(t0) == "000042");
  CHECK(code.attempt("000041", t0) == PairingCode::Try::wrong);
  CHECK(code.attempt("000042", t0 + std::chrono::seconds(30)) == PairingCode::Try::paired);
  CHECK(code.attempt("000042", t0 + std::chrono::seconds(31)) == PairingCode::Try::none);  // once
  // Two minutes, then none.
  REQUIRE(code.start(random, t0));
  CHECK(code.showing(t0 + std::chrono::seconds(119)));
  CHECK(code.attempt("000042", t0 + std::chrono::seconds(120)) == PairingCode::Try::none);
  CHECK(code.code(t0 + std::chrono::seconds(121)).empty());
  // Five wrong tries void it: the right code after them is none.
  REQUIRE(code.start(random, t0));
  for (int i = 0; i < 5; ++i) {
    CHECK(code.attempt("123456", t0) == PairingCode::Try::wrong);
  }
  CHECK(code.attempt("000042", t0) == PairingCode::Try::none);
  // A draw past the last multiple of a million is rejected, so that no code is likelier than another.
  std::array<std::uint8_t, 8> high{0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x07};
  REQUIRE(code.start(high, t0));
  CHECK(code.code(t0) == "000007");
  std::array<std::uint8_t, 4> only_high{0xFF, 0xFF, 0xFF, 0xFF};
  CHECK_FALSE(code.start(only_high, t0));
  CHECK_FALSE(code.showing(t0));
  // A code of another length or with other characters is simply wrong.
  REQUIRE(code.start(random, t0));
  CHECK(code.attempt("42", t0) == PairingCode::Try::wrong);
  CHECK(code.attempt("00004２", t0) == PairingCode::Try::wrong);
}

TEST_CASE("a paired device is matched by the hash of its cookie, among others of any name") {
  const std::string phone = token(1);
  const std::string tablet = token(2);
  const std::array<DeviceKey, 2> devices{key('a', phone), key('b', tablet)};
  CHECK(match_device("en_device=" + tablet, devices) == std::optional<std::size_t>(1));
  CHECK(match_device("other=1; en_device=" + token(9) + "; en_device=" + phone, devices) ==
        std::optional<std::size_t>(0));
  CHECK_FALSE(match_device("en_device=" + token(9), devices).has_value());
  CHECK_FALSE(match_device("en_session=" + phone, devices).has_value());  // the session cookie is not a device's
  CHECK_FALSE(match_device("en_device=not-a-token", devices).has_value());
  CHECK_FALSE(match_device(std::nullopt, devices).has_value());
  CHECK_FALSE(match_device("en_device=" + phone, std::span<const DeviceKey>{}).has_value());
  CHECK_FALSE(match_device("a=" + std::string(kMaxCookie, 'x') + "; en_device=" + phone, devices).has_value());
}

TEST_CASE("a device's name and id") {
  CHECK(valid_device_name("Phone"));
  CHECK(valid_device_name("Tablet in the kitchen"));
  CHECK(valid_device_name("Téléphone"));
  CHECK(valid_device_name(std::string(32, 'x')));
  CHECK_FALSE(valid_device_name(std::string(33, 'x')));
  CHECK_FALSE(valid_device_name(""));
  CHECK_FALSE(valid_device_name("   "));
  CHECK_FALSE(valid_device_name("a\nb"));
  CHECK_FALSE(valid_device_name("\xFF"));
  CHECK(valid_device_id("0123456789abcdef"));
  CHECK_FALSE(valid_device_id("0123456789ABCDEF"));
  CHECK_FALSE(valid_device_id("0123"));
}

TEST_CASE("the pairing page: its script and style by route, no inline script, nothing outside its origin") {
  const std::string_view page = pairing_page();
  CHECK(page.find("<script src=\"/pair.js\"") != std::string_view::npos);
  CHECK(page.find("href=\"/pair.css\"") != std::string_view::npos);
  CHECK(page.find("<script>") == std::string_view::npos);
  CHECK(page.find("style=") == std::string_view::npos);
  CHECK(page.find("http") == std::string_view::npos);
  CHECK(pairing_script().find("\"/api/pair\"") != std::string_view::npos);
  CHECK(pairing_script().find("innerHTML") == std::string_view::npos);  // words as text, never markup (D81)
  CHECK(pairing_style().find("url(") == std::string_view::npos);
}
