// SPDX-License-Identifier: MIT
// tessellate.h: grace-view's display list as vertices, indices and commands in ABI 1's draw-list layout (D76, §3.16,
// D162), so GraceUI's own drawing passes the same checker as a mod's panel. Rectangles with rounded corners, strokes,
// discs and the lozenge get a half-pixel fringe that fades to nothing, as Dear ImGui's own anti-aliased shapes do;
// hairlines are snapped to device pixels; text is one quad a glyph. Colours come from a variant's palette by role, each
// item's own alpha over its colour's. Pure; it allocates nothing once its vectors have grown to a frame's size
// (invariant 10).
#pragma once

#include <cstdint>

#include "grace/abi/drawlist.h"
#include "grace/tokens.h"
#include "grace/view/display.h"

namespace grace::draw {

struct Atlas {
  std::uint64_t texture = 0;  // the shared atlas: solid shapes are drawn with its white pixel, so a page is one texture
  float white_u = 0;
  float white_v = 0;
};

struct Stats {
  std::uint32_t items = 0;
  std::uint32_t vertices = 0;
  std::uint32_t indices = 0;
  std::uint32_t commands = 0;
};

// Writes `list` drawn in `palette` into `out` (cleared first, its vectors' room kept), its vertices in the display's
// pixels. `offset_x` and `offset_y` move everything (a panel's list is in its own pixels).
Stats tessellate(const view::DisplayList& list, const tokens::Palette& palette, const Atlas& atlas,
                 abi::DrawListData& out, float offset_x = 0, float offset_y = 0);

// RGBA bytes, R first, as GraceUiDrawVert holds a colour.
constexpr std::uint32_t pack(tokens::Rgba c, std::uint8_t alpha) noexcept {
  const std::uint32_t a = (std::uint32_t{c.a} * alpha + 127) / 255;
  return std::uint32_t{c.r} | (std::uint32_t{c.g} << 8) | (std::uint32_t{c.b} << 16) | (a << 24);
}

}  // namespace grace::draw
