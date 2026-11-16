// text.cpp: the string checks of text.h.
#include "grace/abi/text.h"

#include <cstdint>

#include "graceui.h"

namespace grace::abi {
namespace {

bool continuation(std::string_view text, std::size_t at) noexcept {
  return at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u;
}

// No C0 control character, no DEL and no C1 control character; the line feed only where `line_feed` allows it.
bool printable(char32_t point, bool line_feed) noexcept {
  if (point == U'\n') {
    return line_feed;
  }
  return point >= 0x20 && !(point >= 0x7F && point <= 0x9F);
}

bool printable_text(std::string_view text, bool line_feed) noexcept {
  std::size_t at = 0;
  char32_t point = 0;
  while (at < text.size()) {
    if (!next_code_point(text, at, point) || !printable(point, line_feed)) {
      return false;
    }
  }
  return true;
}

bool id_char(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; }

}  // namespace

bool next_code_point(std::string_view text, std::size_t& at, char32_t& point) noexcept {
  if (at >= text.size()) {
    return false;
  }
  const auto lead = static_cast<unsigned char>(text[at]);
  std::size_t length = 0;
  char32_t value = 0;
  char32_t least = 0;
  if (lead < 0x80u) {
    point = lead;
    ++at;
    return true;
  }
  if ((lead & 0xE0u) == 0xC0u) {
    length = 2;
    value = lead & 0x1Fu;
    least = 0x80;
  } else if ((lead & 0xF0u) == 0xE0u) {
    length = 3;
    value = lead & 0x0Fu;
    least = 0x800;
  } else if ((lead & 0xF8u) == 0xF0u) {
    length = 4;
    value = lead & 0x07u;
    least = 0x10000;
  } else {
    return false;  // a continuation byte where a character should start, or 0xF8 and above
  }
  for (std::size_t i = 1; i < length; ++i) {
    if (!continuation(text, at + i)) {
      return false;
    }
    value = (value << 6) | (static_cast<unsigned char>(text[at + i]) & 0x3Fu);
  }
  if (value < least || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    return false;  // overlong, beyond Unicode, or a surrogate
  }
  point = value;
  at += length;
  return true;
}

bool valid_utf8(std::string_view text) noexcept {
  std::size_t at = 0;
  char32_t point = 0;
  while (at < text.size()) {
    if (!next_code_point(text, at, point)) {
      return false;
    }
  }
  return true;
}

bool valid_id(std::string_view id) noexcept {
  if (id.size() < 3 || id.size() > GRACEUI_ID_MAX) {
    return false;
  }
  for (const char c : id) {
    if (!id_char(c)) {
      return false;
    }
  }
  return true;
}

bool valid_name(std::string_view name) noexcept {
  return !name.empty() && name.size() <= GRACEUI_NAME_MAX && printable_text(name, false);
}

bool valid_caps(std::string_view caps) noexcept {
  if (caps.size() > GRACEUI_CAPS_MAX) {
    return false;
  }
  std::size_t word = 0;
  for (std::size_t i = 0; i < caps.size(); ++i) {
    if (caps[i] == ' ') {
      if (word == 0) {
        return false;  // a leading space, or two in a row
      }
      word = 0;
    } else if (!id_char(caps[i]) || ++word > GRACEUI_ID_MAX) {
      return false;
    }
  }
  return caps.empty() || word != 0;  // no trailing space
}

bool valid_notice_text(std::string_view text) noexcept {
  return !text.empty() && text.size() <= GRACEUI_NOTICE_TEXT_MAX && printable_text(text, true);
}

std::size_t utf8_tail_start(std::string_view text, std::size_t room) noexcept {
  if (text.size() <= room) {
    return 0;
  }
  std::size_t start = text.size() - room;
  while (start < text.size() && (static_cast<unsigned char>(text[start]) & 0xC0u) == 0x80u) {
    ++start;  // a continuation byte: the character it belongs to began before the cut
  }
  return start;
}

}  // namespace grace::abi
