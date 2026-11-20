// The codecs (codec.h) against their standards' own vectors: SHA-1 (RFC 3174 §7.3, FIPS 180-2's examples), Base64
// (RFC 4648 §10), base64url's strictness, and UTF-8 fed a byte at a time (RFC 3629).
#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "grace/serve/codec.h"
#include "support.h"

using namespace grace::serve;
using namespace grace::serve::test;

namespace {

std::string hex(std::span<const std::uint8_t> bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  for (const std::uint8_t byte : bytes) {
    out.push_back(kDigits[byte >> 4]);
    out.push_back(kDigits[byte & 15]);
  }
  return out;
}

std::string sha1_of(std::string_view text, std::size_t repeat = 1) {
  Sha1 sha;
  for (std::size_t i = 0; i < repeat; ++i) {
    sha.update(text);
  }
  const std::array<std::uint8_t, 20> digest = sha.finish();
  return hex(digest);
}

std::string base64(std::string_view text) {
  const std::vector<std::uint8_t> bytes = bytes_of(text);
  std::string out(base64_size(bytes.size()), '?');
  const std::size_t written = base64_encode(bytes, out);
  CHECK(written == out.size());
  return out;
}

std::optional<std::string> unbase64(std::string_view text, bool url = false) {
  std::array<std::uint8_t, 64> out{};
  const std::optional<std::size_t> size = url ? base64url_decode(text, out) : base64_decode(text, out);
  if (!size) {
    return std::nullopt;
  }
  return text_of(std::span<const std::uint8_t>(out.data(), *size));
}

}  // namespace

TEST_CASE("SHA-1: the RFC 3174 and FIPS vectors") {
  CHECK(sha1_of("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
  CHECK(sha1_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  CHECK(sha1_of("a", 1000000) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
  CHECK(sha1_of("0123456701234567012345670123456701234567012345670123456701234567", 10) ==
        "dea356a2cddd90c7a7ecedc5ebb563934f460452");
  CHECK(sha1_of("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  // Lengths at the padding's edges: 55, 56 and 64 bytes, fed whole and a byte at a time.
  const std::string fifty_five(55, 'x');
  Sha1 bytewise;
  for (const char c : fifty_five) {
    bytewise.update(std::string_view(&c, 1));
  }
  CHECK(hex(bytewise.finish()) == sha1_of(fifty_five));
  CHECK(sha1_of(std::string(56, 'x')) != sha1_of(std::string(55, 'x')));
  CHECK(sha1_of(std::string(64, 'x')) == sha1_of("x", 64));
}

TEST_CASE("Base64: RFC 4648's vectors, both ways") {
  CHECK(base64("") == "");
  CHECK(base64("f") == "Zg==");
  CHECK(base64("fo") == "Zm8=");
  CHECK(base64("foo") == "Zm9v");
  CHECK(base64("foob") == "Zm9vYg==");
  CHECK(base64("fooba") == "Zm9vYmE=");
  CHECK(base64("foobar") == "Zm9vYmFy");
  for (const std::string_view text : {"", "f", "fo", "foo", "foob", "fooba", "foobar"}) {
    CAPTURE(text);
    CHECK(unbase64(base64(text)) == std::string(text));
  }
  const std::vector<std::uint8_t> all = {0xFB, 0xFF, 0xBF};
  std::array<char, 4> out{};
  CHECK(base64_encode(all, out) == 4);
  CHECK(std::string_view(out.data(), 4) == "+/+/");
  std::array<char, 3> small{};
  CHECK(base64_encode(all, small) == 0);
}

TEST_CASE("Base64 decoding is strict") {
  CHECK_FALSE(unbase64("Zg"));        // no padding
  CHECK_FALSE(unbase64("Zg="));       // too little
  CHECK_FALSE(unbase64("Zg==="));
  CHECK_FALSE(unbase64("Z==="));
  CHECK_FALSE(unbase64("===="));
  CHECK_FALSE(unbase64("Zh=="));      // the unused bits are not zero
  CHECK_FALSE(unbase64("Zm9="));
  CHECK_FALSE(unbase64("Zm=v"));      // padding inside
  CHECK_FALSE(unbase64("Zg==Zg=="));
  CHECK_FALSE(unbase64("Zm9 v"));
  CHECK_FALSE(unbase64("Zm9v\n"));
  CHECK_FALSE(unbase64("Zm-v"));      // base64url's alphabet
  CHECK(unbase64("Zm8=") == std::string("fo"));
  std::array<std::uint8_t, 2> small{};
  CHECK_FALSE(base64_decode("Zm9v", small));

  CHECK(unbase64("Zm8", true) == std::string("fo"));
  CHECK(unbase64("-_-_", true) == std::string("\xFB\xFF\xBF"));
  CHECK_FALSE(unbase64("Zm8=", true));  // no padding in base64url
  CHECK_FALSE(unbase64("Z", true));
  CHECK_FALSE(unbase64("Zm9", true));   // unused bits not zero
  CHECK_FALSE(unbase64("+/+/", true));

  CHECK(base64_size(16) == 24);
  CHECK(base64_size(20) == 28);
  CHECK(base64url_size(32) == 43);
}

TEST_CASE("UTF-8, a byte at a time: overlong forms, surrogates and past U+10FFFF refused") {
  const auto valid = [](std::string_view text) { return valid_utf8(text); };
  CHECK(valid(""));
  CHECK(valid("plain"));
  CHECK(valid("caf\xC3\xA9"));
  CHECK(valid("\xE0\xA0\x80"));          // U+0800, the first three-byte character
  CHECK(valid("\xED\x9F\xBF"));          // U+D7FF, before the surrogates
  CHECK(valid("\xEE\x80\x80"));          // U+E000, after them
  CHECK(valid("\xF0\x90\x80\x80"));      // U+10000
  CHECK(valid("\xF4\x8F\xBF\xBF"));      // U+10FFFF
  CHECK(valid("\xEF\xBB\xBF"));
  CHECK_FALSE(valid("\x80"));
  CHECK_FALSE(valid("\xC0\x80"));        // an overlong NUL
  CHECK_FALSE(valid("\xC1\xBF"));
  CHECK_FALSE(valid("\xE0\x9F\xBF"));    // an overlong three-byte form
  CHECK_FALSE(valid("\xF0\x8F\xBF\xBF"));
  CHECK_FALSE(valid("\xED\xA0\x80"));    // U+D800
  CHECK_FALSE(valid("\xED\xBF\xBF"));    // U+DFFF
  CHECK_FALSE(valid("\xF4\x90\x80\x80"));  // U+110000
  CHECK_FALSE(valid("\xF5\x80\x80\x80"));
  CHECK_FALSE(valid("\xFF"));
  CHECK_FALSE(valid("\xC3"));            // cut short
  CHECK_FALSE(valid("\xE3\x81"));
  CHECK_FALSE(valid("\xC3("));

  // A character split between two feeds is whole once both halves are in; a failure stays.
  Utf8Stream stream;
  CHECK(stream.feed(0xF0));
  CHECK(stream.feed(0x9F));
  CHECK_FALSE(stream.complete());
  CHECK(stream.feed(0x98));
  CHECK(stream.feed(0x80));
  CHECK(stream.complete());
  CHECK_FALSE(stream.feed(0xBF));
  CHECK_FALSE(stream.feed('a'));
  CHECK_FALSE(stream.complete());
}
