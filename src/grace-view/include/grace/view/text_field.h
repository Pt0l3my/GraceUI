// SPDX-License-Identifier: MIT
// text_field.h: GraceUI's own text field (§5.6, D57, G38): its text in UTF-8, its caret and its selection, the editing
// keys of §5.3 and the lines it wraps to. One paragraph: no new lines (G38 (3)); a control character typed is ignored,
// and one pasted becomes a space. The caret moves by code point (no grapheme clusters: a combining mark is a step of its
// own). Pure: no Win32, no ImGui; the page view draws it and the host gives it characters and keys.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace grace::view {

class TextField {
 public:
  TextField() = default;
  // `text` cut to `max_bytes` at a character's boundary, and to `max_chars` characters if that is not 0 (an editor's
  // field, D217); the caret at its end.
  explicit TextField(std::string_view text, std::size_t max_bytes = 1024, std::size_t max_chars = 0);

  const std::string& text() const noexcept { return text_; }
  std::size_t max_bytes() const noexcept { return max_; }
  std::size_t caret() const noexcept { return caret_; }    // a byte offset, always at a character's boundary
  std::size_t anchor() const noexcept { return anchor_; }  // the selection's other end; equal to caret: none
  bool selected() const noexcept { return caret_ != anchor_; }
  std::size_t selection_begin() const noexcept { return caret_ < anchor_ ? caret_ : anchor_; }
  std::size_t selection_end() const noexcept { return caret_ < anchor_ ? anchor_ : caret_; }
  std::string_view selection() const noexcept;
  // Characters (code points) in the text, and words (runs of characters that are not spaces).
  std::size_t characters() const noexcept;
  std::size_t words() const noexcept;

  // A typed character, over the selection: false (and nothing changes) for a control character, a surrogate, or one
  // that would pass max_bytes.
  bool type(char32_t code_point);
  // Pasted text, over the selection: each control character, tab and new line becomes one space; what would pass
  // max_bytes is cut at a character's boundary. False if nothing was inserted.
  bool paste(std::string_view utf8);
  // The selection, or the character before (Backspace) or after (Delete) the caret; with `word`, to the word's edge.
  bool backspace(bool word = false);
  bool erase(bool word = false);
  // The selection's text, removed (Ctrl+X); empty if none.
  std::string cut();

  enum class Move : std::uint8_t { left, right, word_left, word_right, home, end };
  // Moves the caret; with `select`, the selection's anchor stays. Without it, ← and → on a selection go to its edge.
  void move(Move how, bool select);
  void select_all() noexcept;
  // The text overwritten, then emptied (a secret's field, when it ends).
  void wipe() noexcept;
  // The caret at a byte offset (moved back to a character's boundary), the selection gone or kept.
  void place(std::size_t at, bool select = false) noexcept;

  // The lines the text wraps to at `width`, each a byte range, measured by `measure` (a run's width), which is called
  // with runs of the text. Breaks after a space; a word wider than the line is broken between characters. An empty text
  // is one empty line. The spaces a line ends with stay on it.
  struct Line {
    std::size_t begin = 0;
    std::size_t end = 0;
  };
  template <class Measure>
  std::vector<Line> wrap(float width, Measure&& measure) const;
  // The line of `lines` the caret is on (at a break, the next line's start).
  static std::size_t line_of(const std::vector<Line>& lines, std::size_t at) noexcept;
  // ↑ and ↓ in a wrapped field: the caret to the previous or next line, at the x nearest its own; on the first or the
  // last line, to the text's start or end.
  template <class Measure>
  void move_line(int direction, const std::vector<Line>& lines, bool select, Measure&& measure);
  // The byte offset in [begin, end) of `text` nearest to x from begin (a click in the field).
  template <class Measure>
  std::size_t offset_at(std::size_t begin, std::size_t end, float x, Measure&& measure) const;

 private:
  std::size_t previous(std::size_t at) const noexcept;  // the boundary before `at`
  std::size_t next(std::size_t at) const noexcept;      // the boundary after `at`
  std::size_t word_left(std::size_t at) const noexcept;
  std::size_t word_right(std::size_t at) const noexcept;
  void remove(std::size_t begin, std::size_t end);
  bool insert(std::string_view utf8);  // over the selection, already clean and within max_bytes

  std::string text_;
  std::size_t max_ = 1024;
  std::size_t max_chars_ = 0;  // 0: no cap in characters
  std::size_t caret_ = 0;
  std::size_t anchor_ = 0;
};

// UTF-8 of one code point (1 to 4 bytes).
std::string utf8_of(char32_t code_point);
// Whether `at` is at a character's boundary of `text` (its end included).
bool at_boundary(std::string_view text, std::size_t at) noexcept;

template <class Measure>
std::vector<TextField::Line> TextField::wrap(float width, Measure&& measure) const {
  std::vector<Line> lines;
  std::size_t begin = 0;
  const std::size_t n = text_.size();
  if (n == 0) {
    lines.push_back(Line{0, 0});
    return lines;
  }
  while (begin < n) {
    // The longest run from begin that fits, ending after a space if one does; else as many characters as fit.
    std::size_t fit = begin;        // the end of the longest run that fits
    std::size_t last_break = begin; // the end of the longest such run that ends after a space
    std::size_t at = begin;
    while (at < n) {
      const std::size_t after = next(at);
      if (measure(std::string_view(text_).substr(begin, after - begin)) > width && text_[at] != ' ') {
        break;  // a space may hang past the edge: it stays on the line it ends
      }
      fit = after;
      if (text_[at] == ' ') {
        last_break = after;
      }
      at = after;
    }
    std::size_t end = fit;
    if (fit < n && last_break > begin) {
      end = last_break;  // break after the last space that fits
    }
    if (end == begin) {
      end = next(begin);  // not even one character fits: one a line, so that wrapping always ends
    }
    lines.push_back(Line{begin, end});
    begin = end;
  }
  return lines;
}

template <class Measure>
std::size_t TextField::offset_at(std::size_t begin, std::size_t end, float x, Measure&& measure) const {
  std::size_t best = begin;
  float best_distance = x < 0 ? -x : x;
  for (std::size_t at = begin; at < end;) {
    at = next(at);
    const float w = measure(std::string_view(text_).substr(begin, at - begin));
    const float d = w > x ? w - x : x - w;
    if (d < best_distance) {
      best_distance = d;
      best = at;
    }
  }
  return best;
}

template <class Measure>
void TextField::move_line(int direction, const std::vector<Line>& lines, bool select, Measure&& measure) {
  if (lines.empty()) {
    return;
  }
  const std::size_t line = line_of(lines, caret_);
  std::size_t to = 0;
  if (direction < 0 && line == 0) {
    to = 0;
  } else if (direction > 0 && line + 1 >= lines.size()) {
    to = text_.size();
  } else {
    const Line& from = lines[line];
    const float x = measure(std::string_view(text_).substr(from.begin, caret_ - from.begin));
    const Line& target = lines[direction < 0 ? line - 1 : line + 1];
    // On a line that is not the last, its end is the next line's start: keep the caret before its last character.
    const bool last = &target == &lines.back();
    const std::size_t end = last || target.end == target.begin ? target.end : previous(target.end);
    to = offset_at(target.begin, end, x, measure);
  }
  caret_ = to;
  if (!select) {
    anchor_ = caret_;
  }
}

}  // namespace grace::view
