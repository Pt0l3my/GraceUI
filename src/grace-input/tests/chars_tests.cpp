// Characters made whole (chars.h) and keys named alike from every source (keys.h).
#include <doctest/doctest.h>

#include <vector>

#include "grace/input/chars.h"
#include "grace/input/keys.h"

namespace {

using grace::input::DbcsJoiner;
using grace::input::kReplacement;
using grace::input::Utf16Pairer;

std::vector<char32_t> pair_all(std::initializer_list<std::uint16_t> units, bool flush = false) {
  Utf16Pairer pairer;
  std::vector<char32_t> out;
  char32_t got[2];
  for (const std::uint16_t unit : units) {
    const int count = pairer.push(unit, got);
    out.insert(out.end(), got, got + count);
  }
  if (flush) {
    const int count = pairer.flush(got);
    out.insert(out.end(), got, got + count);
  }
  return out;
}

using Points = std::vector<char32_t>;

}  // namespace

TEST_CASE("characters of the Basic Multilingual Plane pass one by one") {
  CHECK(pair_all({0x61, 0xE9, 0x4E2D, 0x0D}) == Points{0x61, 0xE9, 0x4E2D, 0x0D});
}

TEST_CASE("a surrogate pair becomes one code point, at the low half") {
  Utf16Pairer pairer;
  char32_t got[2];
  CHECK(pairer.push(0xD83D, got) == 0);
  CHECK(pairer.waiting());
  REQUIRE(pairer.push(0xDE00, got) == 1);
  CHECK(got[0] == 0x1F600);
  CHECK_FALSE(pairer.waiting());
  CHECK(pair_all({0xD800, 0xDC00, 0xDBFF, 0xDFFF}) == Points{0x10000, 0x10FFFF});
  CHECK(pair_all({0x61, 0xD83E, 0xDD70, 0x62}) == Points{0x61, 0x1F970, 0x62});
}

TEST_CASE("a lone high half becomes U+FFFD, and what follows it still counts") {
  CHECK(pair_all({0xD83D, 0x62}) == Points{kReplacement, 0x62});
  CHECK(pair_all({0xD83D, 0xD83D, 0xDE00}) == Points{kReplacement, 0x1F600});
  CHECK(pair_all({0xD83D}, true) == Points{kReplacement});
  CHECK(pair_all({0xD83D, 0x00}) == Points{kReplacement});
}

TEST_CASE("a lone low half, and swapped halves, become U+FFFD") {
  CHECK(pair_all({0xDE00}) == Points{kReplacement});
  CHECK(pair_all({0xDE00, 0xD83D, 0x63}) == Points{kReplacement, kReplacement, 0x63});
  CHECK(pair_all({0xDE00, 0xD83D}, true) == Points{kReplacement, kReplacement});
}

TEST_CASE("NUL is no character, and flushing with nothing waiting gives nothing") {
  CHECK(pair_all({0x00, 0x00}, true).empty());
}

TEST_CASE("a whole code point (WM_UNICHAR) ends a waiting half first") {
  Utf16Pairer pairer;
  char32_t got[2];
  CHECK(pairer.push(0xD83D, got) == 0);
  REQUIRE(pairer.push_whole(0x1F642, got) == 2);
  CHECK(got[0] == kReplacement);
  CHECK(got[1] == 0x1F642);
  REQUIRE(pairer.push_whole(0x41, got) == 1);
  CHECK(got[0] == 0x41);
}

TEST_CASE("WM_UNICHAR's value is a code point only when it is one") {
  CHECK(grace::input::from_utf32(0x1F600) == 0x1F600);
  CHECK(grace::input::from_utf32(0x41) == 0x41);
  CHECK(grace::input::from_utf32(0xD800) == kReplacement);
  CHECK(grace::input::from_utf32(0xDFFF) == kReplacement);
  CHECK(grace::input::from_utf32(0x110000) == kReplacement);
}

TEST_CASE("an ANSI window's lead byte waits for the byte after it") {
  DbcsJoiner joiner;
  std::uint8_t bytes[2];
  REQUIRE(joiner.push(0x41, false, bytes) == 1);
  CHECK(bytes[0] == 0x41);
  CHECK(joiner.push(0x82, true, bytes) == 0);
  CHECK(joiner.waiting());
  REQUIRE(joiner.push(0xA0, false, bytes) == 2);
  CHECK(bytes[0] == 0x82);
  CHECK(bytes[1] == 0xA0);
  // Whatever follows a lead byte is its trail byte, a lead byte included: the code page judges the pair.
  CHECK(joiner.push(0x82, true, bytes) == 0);
  REQUIRE(joiner.push(0x82, true, bytes) == 2);
  CHECK(bytes[1] == 0x82);
  CHECK(joiner.push(0x82, true, bytes) == 0);
  joiner.reset();
  CHECK_FALSE(joiner.waiting());
  REQUIRE(joiner.push(0x42, false, bytes) == 1);
  CHECK(bytes[0] == 0x42);
}

TEST_CASE("a window message's key is DirectInput's code for the same key") {
  using grace::input::key_from_message;
  const auto lparam = [](std::uint32_t scan, bool extended) { return (scan << 16) | (extended ? 1u << 24 : 0u) | 1u; };
  CHECK(key_from_message(lparam(0x1E, false), 'A') == 0x1E);         // DIK_A
  CHECK(key_from_message(lparam(0x01, false), 0x1B) == 0x01);        // DIK_ESCAPE
  CHECK(key_from_message(lparam(0x3F, false), 0x74) == 0x3F);        // DIK_F5
  CHECK(key_from_message(lparam(0x1D, true), 0x11) == 0x9D);         // DIK_RCONTROL
  CHECK(key_from_message(lparam(0x48, true), 0x26) == 0xC8);         // DIK_UP
  CHECK(key_from_message(lparam(0x48, false), 0x68) == 0x48);        // DIK_NUMPAD8
  CHECK(key_from_message(lparam(0x1C, true), 0x0D) == 0x9C);         // DIK_NUMPADENTER
  CHECK(key_from_message(lparam(0x45, true), 0x90) == 0x45);         // Num Lock: DIK_NUMLOCK, though Windows marks it extended
  CHECK(key_from_message(lparam(0x45, false), 0x13) == 0xC5);        // Pause: DIK_PAUSE
  CHECK(key_from_message(lparam(0x00, false), 0xE7) == grace::input::kNoKey);  // VK_PACKET: no physical key
  CHECK(key_from_message(0, 0) == grace::input::kNoKey);             // a posted character with no key
}

TEST_CASE("key sets") {
  grace::input::KeyBits bits;
  CHECK_FALSE(bits.any());
  bits.set(0x01);
  bits.set(0xFF);
  bits.set(0x40);
  CHECK(bits.test(0x01));
  CHECK(bits.test(0xFF));
  CHECK(bits.test(0x40));
  CHECK_FALSE(bits.test(0x02));
  CHECK(bits.count() == 3);
  bits.clear(0xFF);
  CHECK(bits.count() == 2);
  CHECK(grace::input::KeyBits::all().count() == 256);
  grace::input::KeyBits other;
  other.set(0x40);
  CHECK((bits & other).count() == 1);
  CHECK((bits | other).count() == 2);
  grace::input::AtomicKeyBits shared;
  shared.or_with(bits);
  CHECK(shared.load() == bits);
  shared.and_with(other);
  CHECK(shared.load() == other);
}

namespace {

Points join_utf8(std::initializer_list<std::uint8_t> bytes, grace::input::Utf8Joiner* keep = nullptr) {
  grace::input::Utf8Joiner own;
  grace::input::Utf8Joiner& joiner = keep != nullptr ? *keep : own;
  Points out;
  char32_t got[2];
  for (const std::uint8_t byte : bytes) {
    const int count = joiner.push(byte, got);
    out.insert(out.end(), got, got + count);
  }
  return out;
}

}  // namespace

TEST_CASE("an ANSI window in UTF-8: its bytes joined into code points (the review's L13)") {
  CHECK(join_utf8({0x41, 0x7A}) == Points{U'A', U'z'});
  CHECK(join_utf8({0xC3, 0xA9}) == Points{U'é'});              // e with an acute accent
  CHECK(join_utf8({0xE2, 0x82, 0xAC}) == Points{U'€'});        // the euro sign
  CHECK(join_utf8({0xE3, 0x81, 0x82}) == Points{U'あ'});        // hiragana a
  CHECK(join_utf8({0xF0, 0x9F, 0x98, 0x80}) == Points{U'\U0001F600'});  // beyond the BMP, whole
  grace::input::Utf8Joiner joiner;
  CHECK(join_utf8({0xE2, 0x82}, &joiner).empty());
  CHECK(joiner.waiting());
  joiner.reset();
  CHECK_FALSE(joiner.waiting());
  CHECK(join_utf8({0x42}, &joiner) == Points{U'B'});
}

TEST_CASE("UTF-8 that is not: cut short, stray, overlong, a surrogate, past U+10FFFF") {
  CHECK(join_utf8({0xE2, 0x82, 0x41}) == Points{kReplacement, U'A'});  // cut short: then the byte counts on its own
  CHECK(join_utf8({0xC3, 0xC3, 0xA9}) == Points{kReplacement, U'é'});
  CHECK(join_utf8({0x80}) == Points{kReplacement});                     // a stray continuation byte
  CHECK(join_utf8({0xC0, 0x80}) == Points{kReplacement, kReplacement});  // C0 never leads; its follower is stray
  CHECK(join_utf8({0xE0, 0x80, 0x80}) == Points{kReplacement});        // overlong
  CHECK(join_utf8({0xED, 0xA0, 0x80}) == Points{kReplacement});        // a surrogate
  CHECK(join_utf8({0xF4, 0x90, 0x80, 0x80}) == Points{kReplacement});  // past U+10FFFF
  CHECK(join_utf8({0xF5}) == Points{kReplacement});
  CHECK(join_utf8({0x00}).empty());
}
