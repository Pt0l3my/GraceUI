// SPDX-License-Identifier: MIT
// text_field.cpp: GraceUI's own text field (text_field.h).
#include "grace/view/text_field.h"

#include <algorithm>

namespace grace::view {
namespace {

bool continuation(unsigned char byte) noexcept { return (byte & 0xC0u) == 0x80u; }

bool control(char32_t c) noexcept { return c < 0x20 || (c >= 0x7F && c < 0xA0) || c == 0x2028 || c == 0x2029; }

// The code point that starts at `at` of valid UTF-8 (the field keeps only valid UTF-8), and its length.
char32_t decode(std::string_view s, std::size_t at, std::size_t& length) noexcept {
  const auto b = static_cast<unsigned char>(s[at]);
  if (b < 0x80) {
    length = 1;
    return b;
  }
  const std::size_t n = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
  length = std::min(n, s.size() - at);
  char32_t c = b & (n == 4 ? 0x07u : n == 3 ? 0x0Fu : 0x1Fu);
  for (std::size_t i = 1; i < length; ++i) {
    c = (c << 6) | (static_cast<unsigned char>(s[at + i]) & 0x3Fu);
  }
  return c;
}

// `utf8` made clean for the field: invalid bytes dropped, every control character (a tab, a new line) one space.
std::string clean(std::string_view utf8) {
  std::string out;
  out.reserve(utf8.size());
  std::size_t at = 0;
  while (at < utf8.size()) {
    const auto b = static_cast<unsigned char>(utf8[at]);
    std::size_t n = b < 0x80 ? 1 : (b & 0xE0u) == 0xC0u ? 2 : (b & 0xF0u) == 0xE0u ? 3 : (b & 0xF8u) == 0xF0u ? 4 : 0;
    bool valid = n != 0 && at + n <= utf8.size();
    for (std::size_t i = 1; valid && i < n; ++i) {
      valid = continuation(static_cast<unsigned char>(utf8[at + i]));
    }
    if (!valid) {
      ++at;  // a byte that starts no character, or a character cut short
      continue;
    }
    std::size_t length = 0;
    const char32_t c = decode(utf8, at, length);
    const bool overlong = (n == 2 && c < 0x80) || (n == 3 && c < 0x800) || (n == 4 && c < 0x10000);
    if (overlong || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) {
      at += n;
      continue;
    }
    if (c == '\r' && at + 1 < utf8.size() && utf8[at + 1] == '\n') {
      at += 1;  // a Windows line end is one new line: one space
      continue;
    }
    if (control(c)) {
      out += ' ';
    } else {
      out.append(utf8.substr(at, n));
    }
    at += n;
  }
  return out;
}

// The longest prefix of `utf8` that is at most `room` bytes and ends at a character's boundary.
std::size_t fitting(std::string_view utf8, std::size_t room) noexcept {
  if (utf8.size() <= room) {
    return utf8.size();
  }
  std::size_t at = room;
  while (at > 0 && continuation(static_cast<unsigned char>(utf8[at]))) {
    --at;
  }
  return at;
}

// The bytes of the first `count` characters of `utf8`.
std::size_t first_chars(std::string_view utf8, std::size_t count) noexcept {
  std::size_t at = 0;
  for (std::size_t n = 0; at < utf8.size() && n < count; ++n) {
    ++at;
    while (at < utf8.size() && continuation(static_cast<unsigned char>(utf8[at]))) {
      ++at;
    }
  }
  return at;
}

}  // namespace

std::string utf8_of(char32_t c) {
  std::string out;
  if (c < 0x80) {
    out += static_cast<char>(c);
  } else if (c < 0x800) {
    out += static_cast<char>(0xC0 | (c >> 6));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    out += static_cast<char>(0xE0 | (c >> 12));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (c >> 18));
    out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  }
  return out;
}

bool at_boundary(std::string_view text, std::size_t at) noexcept {
  return at == text.size() || (at < text.size() && !continuation(static_cast<unsigned char>(text[at])));
}

TextField::TextField(std::string_view text, std::size_t max_bytes, std::size_t max_chars)
    : max_(max_bytes), max_chars_(max_chars) {
  std::string cleaned = clean(text);
  if (max_chars_ != 0) {
    cleaned.resize(first_chars(cleaned, max_chars_));
  }
  text_ = cleaned.substr(0, fitting(cleaned, max_));
  caret_ = anchor_ = text_.size();
}

std::string_view TextField::selection() const noexcept {
  return std::string_view(text_).substr(selection_begin(), selection_end() - selection_begin());
}

std::size_t TextField::characters() const noexcept {
  return static_cast<std::size_t>(
      std::count_if(text_.begin(), text_.end(), [](char b) { return !continuation(static_cast<unsigned char>(b)); }));
}

std::size_t TextField::words() const noexcept {
  std::size_t count = 0;
  bool in_word = false;
  for (const char b : text_) {
    const bool space = b == ' ';
    if (!space && !in_word) {
      ++count;
    }
    in_word = !space;
  }
  return count;
}

std::size_t TextField::previous(std::size_t at) const noexcept {
  if (at == 0) {
    return 0;
  }
  --at;
  while (at > 0 && continuation(static_cast<unsigned char>(text_[at]))) {
    --at;
  }
  return at;
}

std::size_t TextField::next(std::size_t at) const noexcept {
  if (at >= text_.size()) {
    return text_.size();
  }
  ++at;
  while (at < text_.size() && continuation(static_cast<unsigned char>(text_[at]))) {
    ++at;
  }
  return at;
}

std::size_t TextField::word_left(std::size_t at) const noexcept {
  while (at > 0 && text_[previous(at)] == ' ') {
    at = previous(at);  // the spaces before the caret
  }
  while (at > 0 && text_[previous(at)] != ' ') {
    at = previous(at);  // then the word, to its start
  }
  return at;
}

std::size_t TextField::word_right(std::size_t at) const noexcept {
  while (at < text_.size() && text_[at] != ' ') {
    at = next(at);  // the rest of the word
  }
  while (at < text_.size() && text_[at] == ' ') {
    at = next(at);  // then the spaces: the next word's start, as Windows' fields go
  }
  return at;
}

void TextField::remove(std::size_t begin, std::size_t end) {
  text_.erase(begin, end - begin);
  caret_ = anchor_ = begin;
}

bool TextField::insert(std::string_view utf8) {
  const std::size_t begin = selection_begin();
  const std::size_t end = selection_end();
  const std::size_t room = max_ - (text_.size() - (end - begin));
  if (max_chars_ != 0) {
    // As many characters as the cap leaves, the selection's given back first.
    const std::size_t kept = characters() - static_cast<std::size_t>(std::count_if(
        text_.begin() + static_cast<std::ptrdiff_t>(begin), text_.begin() + static_cast<std::ptrdiff_t>(end),
        [](char b) { return !continuation(static_cast<unsigned char>(b)); }));
    utf8 = utf8.substr(0, first_chars(utf8, max_chars_ > kept ? max_chars_ - kept : 0));
  }
  const std::size_t take = fitting(utf8, room);
  if (take == 0 && begin == end) {
    return false;
  }
  text_.replace(begin, end - begin, utf8.substr(0, take));
  caret_ = anchor_ = begin + take;
  return true;  // inserted, or at least the selection replaced
}

bool TextField::type(char32_t c) {
  if (control(c) || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) {
    return false;
  }
  const std::string bytes = utf8_of(c);
  if (text_.size() - (selection_end() - selection_begin()) + bytes.size() > max_) {
    return false;  // never half a character, and never past the field's room
  }
  return insert(bytes);
}

bool TextField::paste(std::string_view utf8) {
  const std::string cleaned = clean(utf8);
  return !cleaned.empty() && insert(cleaned);
}

bool TextField::backspace(bool word) {
  if (selected()) {
    remove(selection_begin(), selection_end());
    return true;
  }
  if (caret_ == 0) {
    return false;
  }
  remove(word ? word_left(caret_) : previous(caret_), caret_);
  return true;
}

bool TextField::erase(bool word) {
  if (selected()) {
    remove(selection_begin(), selection_end());
    return true;
  }
  if (caret_ >= text_.size()) {
    return false;
  }
  remove(caret_, word ? word_right(caret_) : next(caret_));
  return true;
}

std::string TextField::cut() {
  std::string out(selection());
  if (!out.empty()) {
    remove(selection_begin(), selection_end());
  }
  return out;
}

void TextField::move(Move how, bool select) {
  if (!select && selected() && (how == Move::left || how == Move::right)) {
    caret_ = anchor_ = how == Move::left ? selection_begin() : selection_end();
    return;
  }
  switch (how) {
    case Move::left: caret_ = previous(caret_); break;
    case Move::right: caret_ = next(caret_); break;
    case Move::word_left: caret_ = word_left(caret_); break;
    case Move::word_right: caret_ = word_right(caret_); break;
    case Move::home: caret_ = 0; break;
    case Move::end: caret_ = text_.size(); break;
  }
  if (!select) {
    anchor_ = caret_;
  }
}

void TextField::select_all() noexcept {
  anchor_ = 0;
  caret_ = text_.size();
}

void TextField::wipe() noexcept {
  volatile char* bytes = text_.data();
  for (std::size_t i = 0; i < text_.size(); ++i) {
    bytes[i] = '\0';
  }
  text_.clear();
  caret_ = anchor_ = 0;
}

void TextField::place(std::size_t at, bool select) noexcept {
  at = std::min(at, text_.size());
  while (at > 0 && !at_boundary(text_, at)) {
    --at;
  }
  caret_ = at;
  if (!select) {
    anchor_ = caret_;
  }
}

std::size_t TextField::line_of(const std::vector<Line>& lines, std::size_t at) noexcept {
  std::size_t line = 0;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].begin <= at) {
      line = i;
    }
  }
  return line;
}

}  // namespace grace::view
