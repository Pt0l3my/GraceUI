// text.h: the checks a mod's strings pass before GraceUI keeps or shows them (D73): UTF-8 that decodes, and the shapes of
// an id, a display name, a list of caps and a notice's text. Pure: no Win32, no allocation.
#pragma once

#include <cstddef>
#include <string_view>

namespace grace::abi {

// Decodes the code point at `text[at]` and moves `at` past it; false for a byte sequence that is not UTF-8 (a stray or
// missing continuation byte, an overlong form, a surrogate, or beyond U+10FFFF), leaving `at` where it was.
bool next_code_point(std::string_view text, std::size_t& at, char32_t& point) noexcept;

// Every byte of `text` is UTF-8.
bool valid_utf8(std::string_view text) noexcept;

// A mod's id (D75): 3 to 32 of a-z, 0-9 and '-'.
bool valid_id(std::string_view id) noexcept;

// A display name: 1 to 64 bytes of UTF-8 with no control character.
bool valid_name(std::string_view name) noexcept;

// A mod's caps: empty, or cap names (1 to 32 of a-z, 0-9 and '-') separated by single spaces, 256 bytes at most.
bool valid_caps(std::string_view caps) noexcept;

// A notice's text: 1 to 512 bytes of UTF-8 with no control character but the line feed.
bool valid_notice_text(std::string_view text) noexcept;

}  // namespace grace::abi
