// SPDX-License-Identifier: MIT
// display.h: what grace-view lays out and grace-draw tessellates (§3.16, D162): a flat list of shapes in
// the display's pixels, each naming its colour by role (so a theme change redraws without a new layout), its glyphs
// placed from the atlas's glyph tables. Pure. Its vectors keep their room from frame to frame (invariant 10).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "grace/tokens.h"

namespace grace::view {

struct Rect {
  float x = 0;
  float y = 0;
  float w = 0;
  float h = 0;
  constexpr float right() const noexcept { return x + w; }
  constexpr float bottom() const noexcept { return y + h; }
  constexpr bool operator==(const Rect&) const = default;
};

enum class Shape : std::uint8_t {
  fill,    // a rectangle, its corners rounded by `radius`
  stroke,  // its outline, `width` wide, inside the rectangle
  line,    // a hairline along the rectangle's top (h is its width), snapped to device pixels
  circle,  // a disc filling the rectangle's square
  text,    // glyphs[glyph_first, +glyph_count), from the atlas `texture`
  diamond, // the lozenge: a hollow diamond filling the rectangle, `width` wide
  triangle,  // filled, in the rectangle: its base along one side, its point at the middle of the side `facing` names
};

// Where a triangle points (D247).
enum class Facing : std::uint8_t { right, down, left, up };

struct Item {
  Shape shape = Shape::fill;
  tokens::Colour colour = tokens::Colour::ink_hi;
  std::uint8_t alpha = 255;       // the element's own opacity (fades, dimmed rows), over the colour's own
  std::uint16_t clip = 0;         // its clip rectangle, an index into DisplayList::clips
  Facing facing = Facing::right;  // a triangle's point
  Rect rect{};
  float radius = 0;
  float width = 0;
  std::uint32_t glyph_first = 0;
  std::uint32_t glyph_count = 0;
  std::uint64_t texture = 0;      // a text item's atlas
  std::uint32_t text_first = 0;   // a text item's words, in DisplayList::text, for the structure dump
  std::uint32_t text_size = 0;
};

// A glyph's quad on the display and in the atlas.
struct PlacedGlyph {
  float x0, y0, x1, y1;
  float u0, v0, u1, v1;
};

struct DisplayList {
  std::vector<Item> items;
  std::vector<PlacedGlyph> glyphs;
  std::vector<Rect> clips;  // clips[0] is the whole display
  std::string text;         // every text item's words, one after another
  std::uint32_t generation = 0;  // the shared atlas's generation its glyphs come from
  // The page's body, items[body_first, body_end): what fades in when the page changes in game (§3.10, D272), the rail,
  // the top bar and the key hints staying still. Empty (both 0) for a list with no page.
  std::uint32_t body_first = 0;
  std::uint32_t body_end = 0;
  // The focus ring while it is in the page, items[ring_first, ring_end): drawn after the key hints, it fades with the
  // body. Empty when the ring is on the rail or the top bar, or not drawn.
  std::uint32_t ring_first = 0;
  std::uint32_t ring_end = 0;

  void clear() noexcept {
    items.clear();
    glyphs.clear();
    clips.clear();
    text.clear();
    body_first = 0;
    body_end = 0;
    ring_first = 0;
    ring_end = 0;
  }
  // One line per item, with its colour's role and the text it draws: the structure dumps of §9.3's parity tests.
  std::string dump() const;
};

}  // namespace grace::view
