// grace-draw (D162): the Menu page's display list tessellated passes the same checker as a mod's panel; a theme changes
// colours and nothing else; shapes have the vertices and fringes they should; hairlines are whole pixels; a second
// frame allocates nothing (invariant 10).
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "alloc_counter.h"
#include "fake_face.h"
#include "grace/draw/tessellate.h"
#include "grace/view/page_view.h"

namespace {

using namespace grace;
using view::Item;
using view::Rect;
using view::Shape;

constexpr draw::Atlas kAtlas{view::test::kAtlas, 0.5f / 64, 0.5f / 64};

view::DisplayList menu(float width = 1920, float height = 1080, float scale = 1) {
  view::PageView page;
  std::string why;
  const auto& s = view::test::space("graceui");
  REQUIRE(page.load(view::test::decoded<protocol::SpacePages>(s.pages),
                    view::test::decoded<protocol::SettingsDescription>(s.settings), why));
  REQUIRE(page.open("graceui.menu"));
  view::DisplayList list;
  page.layout(view::Viewport{width, height, scale}, view::test::fonts(), list);
  return list;
}

view::DisplayList one(Item item) {
  view::DisplayList list;
  list.clips.push_back(Rect{0, 0, 400, 400});
  list.items.push_back(item);
  return list;
}

}  // namespace

TEST_CASE("the Menu page passes the checker a mod's panel passes") {
  const view::DisplayList list = menu();
  abi::DrawListData out;
  const draw::Stats stats = draw::tessellate(list, tokens::kGrace.colour, kAtlas, out);
  CHECK(stats.commands >= 1);
  CHECK(stats.vertices > 100);
  abi::DrawContext context;
  context.width = 1920;
  context.height = 1080;
  context.atlas = kAtlas.texture;
  context.atlas_generation = 1;
  CHECK(abi::check_draw_list(out, context) == abi::Refusal::none);
  MESSAGE("the Menu page at 1920x1080: ", stats.items, " items, ", stats.vertices, " vertices, ", stats.indices,
          " indices, ", stats.commands, " commands");
  // Within one submission's bounds (D76), as a mod's would be.
  CHECK(stats.vertices * sizeof(GraceUiDrawVert) <= GRACEUI_DRAW_VERTEX_BYTES_MAX);
  CHECK(stats.commands <= GRACEUI_DRAW_COMMANDS_MAX);
}

TEST_CASE("Grace Clear changes colours and nothing else") {
  const view::DisplayList list = menu();
  abi::DrawListData grace_out;
  abi::DrawListData clear_out;
  draw::tessellate(list, tokens::kGrace.colour, kAtlas, grace_out);
  draw::tessellate(list, tokens::kClear.colour, kAtlas, clear_out);
  REQUIRE(grace_out.vertices.size() == clear_out.vertices.size());
  bool some_colour_differs = false;
  for (std::size_t i = 0; i < grace_out.vertices.size(); ++i) {
    CHECK(grace_out.vertices[i].x == clear_out.vertices[i].x);
    CHECK(grace_out.vertices[i].y == clear_out.vertices[i].y);
    some_colour_differs = some_colour_differs || grace_out.vertices[i].colour != clear_out.vertices[i].colour;
  }
  CHECK(some_colour_differs);
  CHECK(grace_out.indices == clear_out.indices);
}

TEST_CASE("a square fill is one quad; a rounded one its outline with a fringe") {
  Item square;
  square.shape = Shape::fill;
  square.colour = tokens::Colour::ground_2;
  square.rect = Rect{10, 10, 100, 40};
  abi::DrawListData out;
  draw::tessellate(one(square), tokens::kGrace.colour, kAtlas, out);
  CHECK(out.vertices.size() == 4);
  CHECK(out.indices.size() == 6);
  CHECK(out.vertices[0].colour == draw::pack(tokens::kGrace.colour.ground_2, 255));
  Item rounded = square;
  rounded.radius = 4;
  draw::tessellate(one(rounded), tokens::kGrace.colour, kAtlas, out);
  CHECK(out.vertices.size() == 2 * 4 * 3);  // 4 corners of 2 segments (3 points), body and fringe
  const std::size_t clear = static_cast<std::size_t>(std::count_if(
      out.vertices.begin(), out.vertices.end(), [](const GraceUiDrawVert& v) { return (v.colour >> 24) == 0; }));
  CHECK(clear == out.vertices.size() / 2);  // the fringe fades to nothing
}

TEST_CASE("a hairline is whole pixels at any position") {
  Item line;
  line.shape = Shape::line;
  line.colour = tokens::Colour::rule;
  line.rect = Rect{10.4f, 20.6f, 99.7f, 1};
  abi::DrawListData out;
  draw::tessellate(one(line), tokens::kGrace.colour, kAtlas, out);
  REQUIRE(out.vertices.size() == 4);
  for (const GraceUiDrawVert& v : out.vertices) {
    CHECK(v.x == std::round(v.x));
    CHECK(v.y == std::round(v.y));
  }
  CHECK(out.vertices[2].y - out.vertices[0].y == 1.0f);
  // rule is gold at 14%: its alpha is drawn, not composited in advance.
  CHECK((out.vertices[0].colour >> 24) == tokens::kGrace.colour.rule.a);
}

TEST_CASE("an item's own alpha multiplies its colour's") {
  Item dimmed;
  dimmed.shape = Shape::fill;
  dimmed.colour = tokens::Colour::gold;
  dimmed.alpha = 128;
  dimmed.rect = Rect{0, 0, 10, 10};
  abi::DrawListData out;
  draw::tessellate(one(dimmed), tokens::kGrace.colour, kAtlas, out);
  CHECK((out.vertices[0].colour >> 24) == 128);
  CHECK((out.vertices[0].colour & 0xFFFFFFu) == (draw::pack(tokens::kGrace.colour.gold, 255) & 0xFFFFFFu));
}

TEST_CASE("text: one quad a glyph, from the atlas, in its clip") {
  const view::DisplayList list = menu();
  abi::DrawListData out;
  draw::tessellate(list, tokens::kGrace.colour, kAtlas, out);
  std::size_t glyphs = 0;
  for (const Item& item : list.items) {
    glyphs += item.shape == Shape::text ? item.glyph_count : 0;
  }
  CHECK(glyphs > 100);
  for (const GraceUiDrawCommand& command : out.commands) {
    CHECK(command.texture == kAtlas.texture);  // solid shapes use the atlas's white pixel: one texture for the page
  }
}

TEST_CASE("a second frame allocates nothing (invariant 10)") {
  const view::DisplayList list = menu();
  abi::DrawListData out;
  draw::tessellate(list, tokens::kGrace.colour, kAtlas, out);
  abi::test::Counts counts;
  {
    abi::test::Counting counting;
    draw::tessellate(list, tokens::kClear.colour, kAtlas, out);
    counts = counting.counts();
  }
  CHECK(counts.allocations == 0);
  CHECK(counts.frees == 0);
}

TEST_CASE("at 150%, on a smaller display, the page still passes the checker") {
  const view::DisplayList list = menu(1280, 800, 1.5f);
  abi::DrawListData out;
  draw::tessellate(list, tokens::kGrace.colour, kAtlas, out);
  abi::DrawContext context;
  context.width = 1280;
  context.height = 800;
  context.atlas = kAtlas.texture;
  context.atlas_generation = 1;
  CHECK(abi::check_draw_list(out, context) == abi::Refusal::none);
}
