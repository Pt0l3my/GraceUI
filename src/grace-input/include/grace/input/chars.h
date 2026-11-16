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

// The bytes of an ANSI window's character messages when its code page is UTF-8 (65001, which Windows' "use Unicode UTF-8
// for worldwide language support" makes every ANSI window's): a lead byte waits for its one to three continuation
// bytes, and the sequence is decoded here, since IsDBCSLeadByteEx knows no lead byte in UTF-8 and each byte decoded on
// its own is invalid (L13). A byte that cannot continue a waiting sequence ends it as U+FFFD and then counts on its
// own; a stray continuation byte, a byte that never leads, an overlong form, a surrogate and a point past U+10FFFF are
// U+FFFD. NUL gives nothing.
class Utf8Joiner {
 public:
  constexpr Utf8Joiner() = default;

  // One byte in; answers how many code points `out` received (0, 1 or 2).
  int push(std::uint8_t byte, char32_t (&out)[2]) noexcept {
    int count = 0;
    if (need_ != 0) {
      if ((byte & 0xC0u) == 0x80u) {
        point_ = (point_ << 6) | (byte & 0x3Fu);
        if (--need_ != 0) {
          return 0;
        }
        out[0] = finished();
        return 1;
      }
      out[count++] = kReplacement;  // cut short
      need_ = 0;
    }
    if (byte < 0x80u) {
      if (byte != 0) {
        out[count++] = byte;
      }
      return count;
    }
    if (byte >= 0xC2u && byte <= 0xDFu) {
      start(byte & 0x1Fu, 1);
    } else if (byte >= 0xE0u && byte <= 0xEFu) {
      start(byte & 0x0Fu, 2);
    } else if (byte >= 0xF0u && byte <= 0xF4u) {
      start(byte & 0x07u, 3);
    } else {
      out[count++] = kReplacement;
    }
    return count;
  }

  // A waiting sequence is dropped: when the holder changes, the keyboard's code page does, or the window loses focus.
  void reset() noexcept { need_ = 0; }

  constexpr bool waiting() const noexcept { return need_ != 0; }

 private:
  void start(std::uint32_t bits, std::uint8_t continuation) noexcept {
    point_ = bits;
    need_ = continuation;
    length_ = static_cast<std::uint8_t>(continuation + 1);
  }

  char32_t finished() const noexcept {
    const std::uint32_t least = length_ == 2 ? 0x80u : length_ == 3 ? 0x800u : 0x10000u;
    if (point_ < least || point_ > 0x10FFFFu || (point_ >= 0xD800u && point_ <= 0xDFFFu)) {
      return kReplacement;
    }
    return static_cast<char32_t>(point_);
  }

  std::uint32_t point_ = 0;
  std::uint8_t need_ = 0;    // continuation bytes still to come
  std::uint8_t length_ = 0;  // the whole sequence's
};

}  // namespace grace::input
