// The string checks (text.h): UTF-8 that decodes, and the shapes of an id, a display name, caps and a notice's text.
#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "grace/abi/text.h"

using namespace grace::abi;
using namespace std::string_view_literals;

TEST_CASE("UTF-8: every well-formed sequence decodes, and nothing else") {
  CHECK(valid_utf8(""));
  CHECK(valid_utf8("plain"));
  CHECK(valid_utf8("caf\xC3\xA9"));                  // é, two bytes
  CHECK(valid_utf8("\xE3\x81\x82"));                 // あ, three bytes
  CHECK(valid_utf8("\xF0\x9F\x98\x80"));             // U+1F600, four bytes
  CHECK(valid_utf8("\xF4\x8F\xBF\xBF"));             // U+10FFFF, the last code point

  CHECK_FALSE(valid_utf8("\x80"));                   // a continuation byte alone
  CHECK_FALSE(valid_utf8("\xC3"));                   // a lead byte with nothing after it
  CHECK_FALSE(valid_utf8("\xE3\x81"));               // a three-byte sequence cut short
  CHECK_FALSE(valid_utf8("\xC0\xAF"));               // an overlong '/'
  CHECK_FALSE(valid_utf8("\xE0\x80\xAF"));           // another
  CHECK_FALSE(valid_utf8("\xF0\x80\x80\xAF"));       // and another
  CHECK_FALSE(valid_utf8("\xED\xA0\x80"));           // U+D800, a surrogate
  CHECK_FALSE(valid_utf8("\xED\xBF\xBF"));           // U+DFFF
  CHECK_FALSE(valid_utf8("\xF4\x90\x80\x80"));       // U+110000, beyond Unicode
  CHECK_FALSE(valid_utf8("\xF8\x88\x80\x80\x80"));   // a five-byte form
  CHECK_FALSE(valid_utf8("\xFF"));
  CHECK_FALSE(valid_utf8("ok\xC3("));                // a lead byte followed by ASCII

  std::size_t at = 0;
  char32_t point = 0;
  const std::string_view mixed = "a\xC3\xA9\xF0\x9F\x98\x80";
  REQUIRE(next_code_point(mixed, at, point));
  CHECK(point == U'a');
  REQUIRE(next_code_point(mixed, at, point));
  CHECK(point == 0xE9);
  REQUIRE(next_code_point(mixed, at, point));
  CHECK(point == 0x1F600);
  CHECK(at == mixed.size());
  CHECK_FALSE(next_code_point(mixed, at, point));  // nothing left
}

TEST_CASE("an id is 3 to 32 of a-z, 0-9 and '-'") {
  CHECK(valid_id("eldennet"));
  CHECK(valid_id("abc"));
  CHECK(valid_id("hello-notice"));
  CHECK(valid_id("mod-2"));
  CHECK(valid_id(std::string(32, 'a')));
  CHECK_FALSE(valid_id("ab"));                      // too short
  CHECK_FALSE(valid_id(std::string(33, 'a')));      // too long
  CHECK_FALSE(valid_id("Elden"));                   // a capital
  CHECK_FALSE(valid_id("my mod"));                  // a space
  CHECK_FALSE(valid_id("my_mod"));                  // an underscore
  CHECK_FALSE(valid_id("caf\xC3\xA9"));             // beyond ASCII
  CHECK_FALSE(valid_id(""));
  CHECK_FALSE(valid_id("ab\0c"sv));                 // a NUL inside
}

TEST_CASE("a display name is 1 to 64 bytes of UTF-8 text") {
  CHECK(valid_name("Hello notice"));
  CHECK(valid_name("Caf\xC3\xA9 des r\xC3\xAAves"));
  CHECK(valid_name(std::string(64, 'n')));
  CHECK_FALSE(valid_name(""));
  CHECK_FALSE(valid_name(std::string(65, 'n')));
  CHECK_FALSE(valid_name("two\nlines"));            // a name is one line
  CHECK_FALSE(valid_name("tab\there"));
  CHECK_FALSE(valid_name("bell\x07"));
  CHECK_FALSE(valid_name("del\x7F"));
  CHECK_FALSE(valid_name("c1\xC2\x85"));            // U+0085, a C1 control
  CHECK_FALSE(valid_name("bad\xC3"));
}

TEST_CASE("caps are cap names separated by single spaces") {
  CHECK(valid_caps(""));
  CHECK(valid_caps("notices"));
  CHECK(valid_caps("notices panels input"));
  CHECK_FALSE(valid_caps(" notices"));
  CHECK_FALSE(valid_caps("notices "));
  CHECK_FALSE(valid_caps("notices  panels"));
  CHECK_FALSE(valid_caps("Notices"));
  CHECK_FALSE(valid_caps("notices,panels"));
  CHECK_FALSE(valid_caps(std::string(33, 'c')));    // a name longer than an id may be
  std::string many;
  while (many.size() < 256) {
    many += many.empty() ? "abcdefg" : " abcdefg";
  }
  CHECK_FALSE(valid_caps(many));                    // past 256 bytes
}

TEST_CASE("a notice's text is 1 to 512 bytes of UTF-8, line feeds allowed") {
  CHECK(valid_notice_text("Saved."));
  CHECK(valid_notice_text("First line\nsecond line"));
  CHECK(valid_notice_text("\xE3\x81\x82\xE3\x81\x84"));
  CHECK(valid_notice_text(std::string(512, 'x')));
  CHECK_FALSE(valid_notice_text(""));
  CHECK_FALSE(valid_notice_text(std::string(513, 'x')));
  CHECK_FALSE(valid_notice_text("carriage\rreturn"));
  CHECK_FALSE(valid_notice_text("tab\t"));
  CHECK_FALSE(valid_notice_text("\xED\xA0\x80"));
}

TEST_CASE("a name too long for its room keeps its end, from a character's start (the review's L12)") {
  using grace::abi::utf8_tail_start;
  CHECK(utf8_tail_start("second-copy\\graceui.dll", 64) == 0);  // it fits: whole
  const std::string_view ascii = "abcdefghij";
  CHECK(utf8_tail_start(ascii, 4) == 6);  // "ghij"
  // "x" then three of U+3042 (three bytes each) then "\\graceui.dll": a cut inside a character moves past it.
  const std::string_view mixed = "x\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\\graceui.dll";
  const std::size_t tail = 12;  // "\\graceui.dll"
  CHECK(utf8_tail_start(mixed, tail) == mixed.size() - tail);
  CHECK(utf8_tail_start(mixed, tail + 1) == mixed.size() - tail);      // one byte of U+3042: not kept
  CHECK(utf8_tail_start(mixed, tail + 2) == mixed.size() - tail);      // two of its three: not kept
  CHECK(utf8_tail_start(mixed, tail + 3) == mixed.size() - tail - 3);  // the whole character
  std::size_t at = utf8_tail_start(mixed, tail + 5);
  CHECK(grace::abi::valid_utf8(mixed.substr(at)));
  CHECK(utf8_tail_start("", 0) == 0);
  CHECK(utf8_tail_start("\xE3\x81\x82", 2) == 3);  // nothing whole fits: empty
}
