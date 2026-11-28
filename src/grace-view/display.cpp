// SPDX-License-Identifier: MIT
// display.cpp: the display list's structure dump (display.h).
#include "grace/view/display.h"

#include <cmath>
#include <cstdio>

namespace grace::view {
namespace {

// A value rounded to a whole pixel before it is printed, half to even: printf's own rounding of a half differs between
// C runtimes (glibc's and MinGW's to even, the Microsoft CRT's away from zero), and the dump must read the same on all.
double whole(float value) noexcept { return std::nearbyint(static_cast<double>(value)); }

}  // namespace

std::string DisplayList::dump() const {
  static constexpr const char* kShape[] = {"fill", "stroke", "line", "circle", "text", "diamond", "tri", "segment"};
  static constexpr const char* kFacing[] = {">", "v", "<", "^"};
  std::string out;
  char line[160];
  for (const Item& item : items) {
    std::snprintf(line, sizeof line, "%-7s %-12s %4.0f,%4.0f %4.0fx%-4.0f", kShape[static_cast<int>(item.shape)],
                  tokens::kColourName[static_cast<int>(item.colour)], whole(item.rect.x), whole(item.rect.y),
                  whole(item.rect.w), whole(item.rect.h));
    out += line;
    if (item.radius > 0) {
      std::snprintf(line, sizeof line, " r%.0f", whole(item.radius));
      out += line;
    }
    if (item.width > 0) {
      std::snprintf(line, sizeof line, " w%.0f", whole(item.width));
      out += line;
    }
    if (item.alpha != 255) {
      std::snprintf(line, sizeof line, " a%u", item.alpha);
      out += line;
    }
    if (item.clip != 0) {
      std::snprintf(line, sizeof line, " clip%u", item.clip);
      out += line;
    }
    if (item.shape == Shape::triangle || item.shape == Shape::segment) {
      out += ' ';
      out += kFacing[static_cast<int>(item.facing)];
    }
    if (item.shape == Shape::text) {
      std::snprintf(line, sizeof line, " glyphs %u \"", item.glyph_count);
      out += line;
      out.append(text, item.text_first, item.text_size);
      out += '"';
    }
    out += '\n';
  }
  return out;
}

}  // namespace grace::view
