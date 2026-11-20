// fake_face.h: a face of our own for the layout tests, every glyph 8 px wide at a bake of 16 px, so a run's width is
// its character count times 8 (scaled with the size its role asks). Shared by grace-view's and grace-draw's tests.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "grace/pages/builtin.h"
#include "grace/protocol/messages.h"
#include "grace/view/text.h"

namespace grace::view::test {

inline constexpr std::uint64_t kAtlas = 1;

inline const std::vector<Glyph>& glyphs() {
  static const std::vector<Glyph> table = [] {
    std::vector<Glyph> out;
    const auto add = [&](std::uint32_t c) {
      Glyph g;
      g.code_point = c;
      g.advance = 8;
      if (c != ' ') {
        g.x0 = 1;
        g.y0 = 3;
        g.x1 = 7;
        g.y1 = 15;
        g.u0 = static_cast<float>(c % 64) / 64;
        g.v0 = 0;
        g.u1 = g.u0 + 1.0f / 64;
        g.v1 = 1.0f / 64;
      }
      out.push_back(g);
    };
    for (std::uint32_t c = 0x20; c < 0x7F; ++c) {
      add(c);
    }
    for (std::uint32_t c : {0xD7u, 0x2026u, 0x2190u, 0x2191u, 0x2192u, 0x2193u, 0xFFFDu}) {
      add(c);
    }
    return out;
  }();
  return table;
}

inline Fonts fonts() {
  static const Face face{16, 13, -3, kAtlas, glyphs()};
  Fonts out;
  for (auto& slot : out.by_role) {
    slot = &face;
  }
  out.generation = 1;
  return out;
}

template <class T>
T decoded(std::string_view text) {
  T out;
  protocol::DecodeError error;
  const protocol::Json json = protocol::Json::parse(text.begin(), text.end());
  protocol::decode(json, out, "", error);
  return out;
}

inline const pages::BuiltinSpace& space(std::string_view id) {
  for (const auto& s : pages::builtin_spaces()) {
    if (s.id == id) {
      return s;
    }
  }
  return pages::builtin_spaces()[0];
}

}  // namespace grace::view::test
