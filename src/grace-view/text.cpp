// SPDX-License-Identifier: MIT
// text.cpp: text layout from the glyph tables (text.h).
#include "grace/view/text.h"

#include <algorithm>

namespace grace::view {
namespace {

constexpr std::uint32_t kEllipsis = 0x2026;
constexpr std::uint32_t kReplacement = 0xFFFD;

std::uint32_t upper(std::uint32_t c) noexcept {
  if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) {
    return c - 0x20;
  }
  return c;
}

// The size a role is drawn at, and the factor from its face's bake.
struct Sizing {
  float size = 0;
  float factor = 1;
  float tracking = 0;
  bool upper = false;
};

Sizing sizing(const Face& face, tokens::TypeRole role, float scale) noexcept {
  const tokens::Type& type = tokens::kTypes[static_cast<std::size_t>(role)];
  Sizing out;
  out.size = type.size * scale;
  out.factor = face.pixel_height > 0 ? out.size / face.pixel_height : 1.0f;
  out.tracking = type.tracking * out.size;
  out.upper = type.upper;
  return out;
}

const Glyph* glyph_for(const Face& face, std::uint32_t c) noexcept {
  const Glyph* glyph = face.find(c);
  return glyph != nullptr ? glyph : face.find(kReplacement);
}

}  // namespace

const Glyph* Face::find(std::uint32_t code_point) const noexcept {
  const auto it = std::lower_bound(glyphs.begin(), glyphs.end(), code_point,
                                   [](const Glyph& g, std::uint32_t c) { return g.code_point < c; });
  return it != glyphs.end() && it->code_point == code_point ? &*it : nullptr;
}

std::uint32_t next_code_point(std::string_view text, std::size_t& at) noexcept {
  const auto c = static_cast<unsigned char>(text[at]);
  std::size_t extra = 0;
  std::uint32_t point = 0;
  if (c < 0x80) {
    ++at;
    return c;
  }
  if ((c & 0xE0u) == 0xC0u) {
    extra = 1;
    point = c & 0x1Fu;
  } else if ((c & 0xF0u) == 0xE0u) {
    extra = 2;
    point = c & 0x0Fu;
  } else if ((c & 0xF8u) == 0xF0u) {
    extra = 3;
    point = c & 0x07u;
  } else {
    ++at;
    return kReplacement;
  }
  if (text.size() - at <= extra) {
    at = text.size();
    return kReplacement;
  }
  for (std::size_t k = 1; k <= extra; ++k) {
    const auto next = static_cast<unsigned char>(text[at + k]);
    if ((next & 0xC0u) != 0x80u) {
      ++at;
      return kReplacement;
    }
    point = (point << 6) | (next & 0x3Fu);
  }
  at += extra + 1;
  return point;
}

float line_height(tokens::TypeRole role, float scale) noexcept {
  return tokens::kTypes[static_cast<std::size_t>(role)].line * scale;
}

float measure(const Fonts& fonts, tokens::TypeRole role, float scale, std::string_view text) noexcept {
  const Face* face = fonts.face(role);
  if (face == nullptr) {
    return 0;
  }
  const Sizing s = sizing(*face, role, scale);
  float width = 0;
  for (std::size_t at = 0; at < text.size();) {
    std::uint32_t c = next_code_point(text, at);
    if (s.upper) {
      c = upper(c);
    }
    if (const Glyph* glyph = glyph_for(*face, c)) {
      width += glyph->advance * s.factor + s.tracking;
    }
  }
  return width > 0 ? width - s.tracking : 0;  // no tracking after the last glyph
}

float place_text(DisplayList& out, const Fonts& fonts, tokens::TypeRole role, float scale, std::string_view text, float x,
                 float y, float max_width, tokens::Colour colour, std::uint8_t alpha, std::uint16_t clip) {
  const Face* face = fonts.face(role);
  if (face == nullptr || text.empty()) {
    return 0;
  }
  const Sizing s = sizing(*face, role, scale);
  // The pen sits at the top of the line; the glyph quads are measured from it as the face was baked. A line taller than
  // the face's own (the token's leading) centres it.
  const float baked_line = (face->ascent - face->descent) * s.factor;
  const float top = y + std::max(0.0f, (line_height(role, scale) - baked_line) * 0.5f);
  const bool fits = measure(fonts, role, scale, text) <= max_width;
  const Glyph* ellipsis = glyph_for(*face, kEllipsis);
  const float ellipsis_width = ellipsis != nullptr ? ellipsis->advance * s.factor : 0;
  Item item;
  item.shape = Shape::text;
  item.colour = colour;
  item.alpha = alpha;
  item.clip = clip;
  item.texture = face->texture;
  item.glyph_first = static_cast<std::uint32_t>(out.glyphs.size());
  float pen = x;
  const auto put = [&](const Glyph& g) {
    if (g.x1 > g.x0) {
      out.glyphs.push_back({pen + g.x0 * s.factor, top + g.y0 * s.factor, pen + g.x1 * s.factor, top + g.y1 * s.factor,
                            g.u0, g.v0, g.u1, g.v1});
    }
    pen += g.advance * s.factor + s.tracking;
  };
  for (std::size_t at = 0; at < text.size();) {
    std::uint32_t c = next_code_point(text, at);
    if (s.upper) {
      c = upper(c);
    }
    const Glyph* glyph = glyph_for(*face, c);
    if (glyph == nullptr) {
      continue;
    }
    if (!fits && pen + glyph->advance * s.factor + ellipsis_width > x + max_width) {
      if (ellipsis != nullptr) {
        put(*ellipsis);
      }
      break;
    }
    put(*glyph);
  }
  item.glyph_count = static_cast<std::uint32_t>(out.glyphs.size()) - item.glyph_first;
  item.text_first = static_cast<std::uint32_t>(out.text.size());
  item.text_size = static_cast<std::uint32_t>(text.size());
  out.text.append(text);
  item.rect = Rect{x, y, std::min(pen - x, max_width), line_height(role, scale)};
  if (item.glyph_count != 0) {
    out.items.push_back(item);
  }
  return item.rect.w;
}

}  // namespace grace::view
