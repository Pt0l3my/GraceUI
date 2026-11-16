// chars.h: characters as the window procedure receives them, made whole before any holder sees them (§5.6, D69).
// A Unicode window's WM_CHAR carries UTF-16 code units, so a character outside the Basic Multilingual Plane arrives as
// two messages; an ANSI window's carries bytes in the keyboard's code page, so a double-byte character arrives as two;
// WM_UNICHAR carries a whole code point. A holder takes code points only. Pure: one of each per window thread.
#pragma once

#include <cstdint>

namespace grace::input {

inline constexpr char32_t kReplacement = 0xFFFD;  // what a lone or misplaced half becomes

constexpr bool is_high_surrogate(std::uint32_t unit) noexcept { return unit >= 0xD800 && unit <= 0xDBFF; }
constexpr bool is_low_surrogate(std::uint32_t unit) noexcept { return unit >= 0xDC00 && unit <= 0xDFFF; }

// WM_UNICHAR's value as a code point: itself when it is one, U+FFFD otherwise (a surrogate, or past U+10FFFF).
constexpr char32_t from_utf32(std::uint32_t value) noexcept {
  return value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF) ? kReplacement : static_cast<char32_t>(value);
}

// UTF-16 code units, one at a time, into whole code points, in order.
class Utf16Pairer {
 public:
  constexpr Utf16Pairer() = default;

  // One unit in; answers how many code points `out` received (0, 1 or 2). A high surrogate waits for the unit after it:
  // a low surrogate completes it; anything else makes it U+FFFD and then counts on its own. A low surrogate with no
  // high one before it is U+FFFD. NUL is no character and gives nothing, though it ends a waiting half.
  int push(std::uint16_t unit, char32_t (&out)[2]) noexcept {
    int count = 0;
    if (high_ != 0) {
      if (is_low_surrogate(unit)) {
        out[0] = 0x10000 + ((static_cast<char32_t>(high_) - 0xD800) << 10) + (static_cast<char32_t>(unit) - 0xDC00);
        high_ = 0;
        return 1;
      }
      out[count++] = kReplacement;
      high_ = 0;
    }
    if (is_high_surrogate(unit)) {
      high_ = unit;
    } else if (is_low_surrogate(unit)) {
      out[count++] = kReplacement;
    } else if (unit != 0) {
      out[count++] = unit;
    }
    return count;
  }

  // A code point that arrives whole (WM_UNICHAR) after a waiting high surrogate ends that half first.
  int push_whole(char32_t code_point, char32_t (&out)[2]) noexcept {
    int count = flush(out);
    if (code_point != 0) {
      out[count++] = code_point;
    }
    return count;
  }

  // Ends a waiting high surrogate as U+FFFD: when the holder changes, or the window loses focus.
  int flush(char32_t (&out)[2]) noexcept {
    if (high_ == 0) {
      return 0;
    }
    high_ = 0;
    out[0] = kReplacement;
    return 1;
  }

  constexpr bool waiting() const noexcept { return high_ != 0; }

 private:
  std::uint16_t high_ = 0;
};

// The bytes of an ANSI window's character messages: a lead byte waits for the byte after it, whatever that is (the code
// page decides whether the pair is valid). Which bytes lead is the caller's to say (IsDBCSLeadByteEx), and the caller
// decodes the one or two bytes (MultiByteToWideChar) into UTF-16 units for a Utf16Pairer.
class DbcsJoiner {
 public:
  constexpr DbcsJoiner() = default;

  // Answers how many bytes are ready in `out`: 0 while a lead byte waits, else 1 or 2.
  int push(std::uint8_t byte, bool lead, std::uint8_t (&out)[2]) noexcept {
    if (waiting_) {
      out[0] = lead_;
      out[1] = byte;
      waiting_ = false;
      return 2;
    }
    if (lead) {
      lead_ = byte;
      waiting_ = true;
      return 0;
    }
    out[0] = byte;
    return 1;
  }

  // A waiting lead byte is dropped: when the holder changes, or the keyboard's code page does.
  void reset() noexcept { waiting_ = false; }

  constexpr bool waiting() const noexcept { return waiting_; }

 private:
  std::uint8_t lead_ = 0;
  bool waiting_ = false;
};

}  // namespace grace::input
