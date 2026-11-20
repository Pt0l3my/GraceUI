// SPDX-License-Identifier: MIT
// text.h: GraceUI's text layout from the shared atlas's glyph tables (§3.16): a face baked at one size is drawn at the
// size its type token and the scale ask, the nearest bake scaled; the token's tracking is added to every advance; a run
// too wide ends in an ellipsis. Pure: no ImGui.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "grace/tokens.h"
#include "grace/view/display.h"

namespace grace::view {

// A glyph as GraceUI's glyph_table gives it (GraceUiGlyph's layout): the quad from the pen at the top of the line.
struct Glyph {
  std::uint32_t code_point = 0;
  float advance = 0;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};

struct Face {
  float pixel_height = 0;  // the size it was baked at
  float ascent = 0;
  float descent = 0;
  std::uint64_t texture = 0;
  std::span<const Glyph> glyphs;  // sorted by code point

  const Glyph* find(std::uint32_t code_point) const noexcept;
};

// The face each type role is drawn in (§3.16's bakes): the host fills it from the faces it baked.
struct Fonts {
  std::array<const Face*, tokens::kTypeCount> by_role{};
  std::uint32_t generation = 0;
  const Face* face(tokens::TypeRole role) const noexcept { return by_role[static_cast<std::size_t>(role)]; }
};

// The next code point of UTF-8 `text` from `at`, moving it on; U+FFFD for a byte that starts no character.
std::uint32_t next_code_point(std::string_view text, std::size_t& at) noexcept;

// The width of `text` in a role at a scale, its tracking included; upper-case roles measure their capitals.
float measure(const Fonts& fonts, tokens::TypeRole role, float scale, std::string_view text) noexcept;

// Lays `text` out from (x, y), the top of its line, into `out` as one text item, at most `max_width` wide (an ellipsis
// ends a run cut short). Answers the width drawn.
float place_text(DisplayList& out, const Fonts& fonts, tokens::TypeRole role, float scale, std::string_view text, float x,
                 float y, float max_width, tokens::Colour colour, std::uint8_t alpha, std::uint16_t clip);

// The height of a line of a role at a scale.
float line_height(tokens::TypeRole role, float scale) noexcept;

}  // namespace grace::view
