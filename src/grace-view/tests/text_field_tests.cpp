// SPDX-License-Identifier: MIT
// text_field_tests.cpp: GraceUI's own text field (text_field.h; §5.6, D57, G38).
#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "grace/view/text_field.h"

using grace::view::TextField;
using Move = TextField::Move;

namespace {

// Ten pixels a character, whatever it is.
float tens(std::string_view run) {
  float w = 0;
  for (const char b : run) {
    if ((static_cast<unsigned char>(b) & 0xC0u) != 0x80u) {
      w += 10;
    }
  }
  return w;
}

TextField typed(std::string_view text, std::size_t max = 1024) {
  TextField f(std::string_view{}, max);
  std::size_t at = 0;
  while (at < text.size()) {
    // ASCII only in these helpers: one byte a character.
    f.type(static_cast<unsigned char>(text[at]));
    ++at;
  }
  return f;
}

}  // namespace

TEST_CASE("typing: characters go in at the caret, over a selection, within the room") {
  TextField f = typed("keeper");
  CHECK(f.text() == "keeper");
  CHECK(f.caret() == 6);
  f.move(Move::home, false);
  CHECK(f.type(U'A'));
  CHECK(f.text() == "Akeeper");
  f.move(Move::end, false);
  f.move(Move::left, true);
  f.move(Move::left, true);
  CHECK(f.selection() == "er");
  CHECK(f.type(U'!'));
  CHECK(f.text() == "Akeep!");
  CHECK_FALSE(f.selected());
  // A control character, a lone surrogate and a new line are not typed.
  CHECK_FALSE(f.type(U'\n'));
  CHECK_FALSE(f.type(U'\t'));
  CHECK_FALSE(f.type(char32_t{0x1B}));
  CHECK_FALSE(f.type(char32_t{0xD800}));
  CHECK(f.text() == "Akeep!");
  // The room is in bytes, and a character never goes in by halves.
  TextField small = typed("abc", 4);
  CHECK(small.type(U'd'));
  CHECK_FALSE(small.type(U'e'));
  small.move(Move::left, false);
  small.backspace();
  CHECK(small.text() == "abd");
  CHECK_FALSE(small.type(U'é'));  // two bytes: one is left
  CHECK(small.text() == "abd");
}

TEST_CASE("UTF-8: the caret steps over a whole character, the field keeps only valid text") {
  TextField f("aé\U0001F56F", 64);  // a, é and a candle: 1, 2 and 4 bytes
  CHECK(f.text().size() == 7);
  CHECK(f.characters() == 3);
  f.move(Move::left, false);
  CHECK(f.caret() == 3);
  f.move(Move::left, false);
  CHECK(f.caret() == 1);
  CHECK(f.backspace());
  CHECK(f.text() == "é\U0001F56F");
  f.place(1);  // inside é: back to its start
  CHECK(f.caret() == 0);
  // Invalid bytes, an overlong form and an encoded surrogate are dropped; a cut-off character too.
  TextField bad(std::string_view("o\xFFk\xC0\xAF\xED\xA0\x80!\xE2\x82"), 64);
  CHECK(bad.text() == "ok!");
  // Built over the room: cut at a character's boundary.
  TextField cut("abé", 3);
  CHECK(cut.text() == "ab");
}

TEST_CASE("paste: one paragraph, its control characters as spaces, cut to the room") {
  TextField f = typed("the ");
  CHECK(f.paste("old\r\nbridge\tkeeper\n"));
  CHECK(f.text() == "the old bridge keeper ");
  TextField room = typed("ab", 5);
  CHECK(room.paste("cdefgh"));
  CHECK(room.text() == "abcde");
  CHECK_FALSE(room.paste("x"));
  CHECK_FALSE(typed("").paste(""));
  CHECK_FALSE(typed("").paste("\xFF"));
}

TEST_CASE("Backspace, Delete and their word forms") {
  TextField f = typed("the old  bridge");
  CHECK(f.backspace(true));
  CHECK(f.text() == "the old  ");
  CHECK(f.backspace(true));
  CHECK(f.text() == "the ");
  f.move(Move::home, false);
  CHECK(f.erase(true));
  CHECK(f.text() == "");  // the word and the spaces after it, as Ctrl+Delete goes
  CHECK_FALSE(f.erase());
  CHECK_FALSE(f.backspace());
  TextField g = typed("abc");
  g.move(Move::home, false);
  CHECK(g.erase());
  CHECK(g.text() == "bc");
}

TEST_CASE("moving: by character, by word, to the ends, and with Shift a selection") {
  TextField f = typed("keep the  lamp lit");
  f.move(Move::word_left, false);
  CHECK(f.caret() == 15);  // "lit"
  f.move(Move::word_left, false);
  CHECK(f.caret() == 10);  // "lamp", past two spaces
  f.move(Move::word_right, false);
  CHECK(f.caret() == 15);
  f.move(Move::word_left, true);
  CHECK(f.selection() == "lamp ");
  f.move(Move::right, false);  // → on a selection: to its end
  CHECK(f.caret() == 15);
  CHECK_FALSE(f.selected());
  f.move(Move::home, true);
  CHECK(f.selection() == "keep the  lamp ");
  f.move(Move::left, false);  // ← on a selection: to its start
  CHECK(f.caret() == 0);
  f.select_all();
  CHECK(f.cut() == "keep the  lamp lit");
  CHECK(f.text().empty());
  CHECK(f.cut().empty());
}

TEST_CASE("words: runs that are not spaces") {
  CHECK(typed("").words() == 0);
  CHECK(typed("  ").words() == 0);
  CHECK(typed("one").words() == 1);
  CHECK(typed(" hums an  old road song ").words() == 5);
}

TEST_CASE("wrapping: after spaces, a long word broken, and the caret's line") {
  const TextField f = typed("hums an old road song");
  // 80 px: eight characters a line.
  const auto lines = f.wrap(80, tens);
  REQUIRE(lines.size() == 3);
  CHECK(f.text().substr(lines[0].begin, lines[0].end - lines[0].begin) == "hums an ");
  CHECK(f.text().substr(lines[1].begin, lines[1].end - lines[1].begin) == "old road ");  // its space hangs
  CHECK(f.text().substr(lines[2].begin, lines[2].end - lines[2].begin) == "song");
  CHECK(TextField::line_of(lines, 0) == 0);
  CHECK(TextField::line_of(lines, 8) == 1);  // at a break: the next line's start
  CHECK(TextField::line_of(lines, f.text().size()) == 2);
  const TextField word = typed("lanternlight!");
  const auto broken = word.wrap(50, tens);
  REQUIRE(broken.size() == 3);
  CHECK(broken[0].end == 5);
  CHECK(broken[2].end == 13);
  const auto none = TextField().wrap(50, tens);
  REQUIRE(none.size() == 1);
  CHECK(none[0].end == 0);
  // Narrower than one character: one a line, so that it ends.
  CHECK(typed("abc").wrap(1, tens).size() == 3);
}

TEST_CASE("↑ and ↓ in a wrapped field keep the caret's x, and leave at the ends") {
  TextField f = typed("hums an old road song");
  auto lines = f.wrap(80, tens);
  f.place(3);  // "hum|s"
  f.move_line(1, lines, false, tens);
  CHECK(f.caret() == 11);  // "old|"
  f.move_line(1, lines, false, tens);
  CHECK(f.caret() == 20);  // "son|g"
  f.move_line(1, lines, false, tens);
  CHECK(f.caret() == f.text().size());
  f.move_line(-1, lines, true, tens);
  CHECK(f.selected());
  f.place(2);
  f.move_line(-1, lines, false, tens);
  CHECK(f.caret() == 0);
  // From the end of a long first line, the caret stays before the next line's break.
  f.place(7);  // "hums an|"
  f.move_line(1, lines, false, tens);
  CHECK(f.caret() == 15);
}

TEST_CASE("a click: the boundary nearest the pointer") {
  const TextField f = typed("lamp");
  CHECK(f.offset_at(0, 4, -5, tens) == 0);
  CHECK(f.offset_at(0, 4, 4, tens) == 0);
  CHECK(f.offset_at(0, 4, 6, tens) == 1);
  CHECK(f.offset_at(0, 4, 200, tens) == 4);
}
