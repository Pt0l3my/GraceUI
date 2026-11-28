// grace-draw (D162): the Menu page's display list tessellated passes the same checker as a mod's panel; a theme changes
// colours and nothing else; shapes have the vertices and fringes they should; hairlines are whole pixels; a second
// frame allocates nothing (invariant 10).
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "alloc_counter.h"
#include "fake_face.h"
#include "grace/draw/pointer.h"
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

TEST_CASE("the page's body: its items' vertices, between the rail and top bar's and the key hints' (D272)") {
  const view::DisplayList list = menu();
  REQUIRE(list.body_first > 0);
  REQUIRE(list.body_end > list.body_first);
  REQUIRE(list.body_end <= list.items.size());
  const auto text_of = [&](const Item& item) { return list.text.substr(item.text_first, item.text_size); };
  // A setting's label lies within the body; the rail's page names and the key hints' words outside it.
  std::size_t label = list.items.size();
  std::size_t rail = list.items.size();
  std::size_t hint = list.items.size();
  for (std::size_t i = 0; i < list.items.size(); ++i) {
    if (list.items[i].shape != Shape::text) {
      continue;
    }
    const std::string words = text_of(list.items[i]);
    if (words == "Menu size" && label == list.items.size()) {
      label = i;
    } else if (words == "Hotkeys" && rail == list.items.size()) {
      rail = i;
    } else if (words == "Back") {
      hint = i;
    }
  }
  REQUIRE(label < list.items.size());
  REQUIRE(rail < list.items.size());
  REQUIRE(hint < list.items.size());
  CHECK(label >= list.body_first);
  CHECK(label < list.body_end);
  CHECK(rail < list.body_first);
  CHECK(hint >= list.body_end);
  // Tessellated, the body's vertices are the run its items made, within the list.
  abi::DrawListData out;
  const draw::Stats stats = draw::tessellate(list, tokens::kGrace.colour, kAtlas, out);
  CHECK(out.body_first > 0);
  CHECK(out.body_end > out.body_first);
  CHECK(out.body_end < stats.vertices);
  view::DisplayList none = one(Item{});
  none.items[0].rect = Rect{0, 0, 10, 10};
  CHECK(draw::tessellate(none, tokens::kGrace.colour, kAtlas, out).vertices > 0);
  CHECK(out.body_first == 0);  // a list with no page, as a panel's: no body
  CHECK(out.body_end == 0);
  // The focus ring on the page, drawn after the key hints, is its own run, which fades with the body.
  REQUIRE(list.ring_end > list.ring_first);
  CHECK(list.ring_first >= list.body_end);
  CHECK(list.items[list.ring_first].shape == Shape::stroke);
  CHECK(list.items[list.ring_first].colour == tokens::Colour::gold_hi);
  abi::DrawListData again;
  draw::tessellate(list, tokens::kGrace.colour, kAtlas, again);
  CHECK(again.ring_first >= again.body_end);
  CHECK(again.ring_end > again.ring_first);
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

TEST_CASE("a triangle: three points and a fringe outside them, its point where it faces (D247)") {
  const Rect r{10, 20, 12, 10};
  const struct {
    view::Facing facing;
    float x, y;  // the point
  } cases[] = {{view::Facing::right, 22, 25}, {view::Facing::down, 16, 30}, {view::Facing::left, 10, 25},
               {view::Facing::up, 16, 20}};
  for (const auto& at : cases) {
    Item triangle;
    triangle.shape = Shape::triangle;
    triangle.colour = tokens::Colour::ink_body;
    triangle.rect = r;
    triangle.facing = at.facing;
    abi::DrawListData out;
    draw::tessellate(one(triangle), tokens::kGrace.colour, kAtlas, out);
    REQUIRE(out.vertices.size() == 6);  // three solid, three clear
    CHECK(out.indices.size() == 3 * (1 + 6));
    // The solid points sit inside the rectangle, half a fringe in; the clear ones half a fringe out, never inward.
    float cx = 0, cy = 0;
    for (int i = 0; i < 3; ++i) {
      cx += out.vertices[i].x / 3;
      cy += out.vertices[i].y / 3;
    }
    bool at_point = false;
    for (int i = 0; i < 3; ++i) {
      const GraceUiDrawVert& solid = out.vertices[i];
      const GraceUiDrawVert& clear = out.vertices[i + 3];
      CHECK((solid.colour >> 24) == 255);
      CHECK((clear.colour >> 24) == 0);
      CHECK(solid.x >= r.x - 0.01f);
      CHECK(solid.x <= r.right() + 0.01f);
      CHECK(solid.y >= r.y - 0.01f);
      CHECK(solid.y <= r.bottom() + 0.01f);
      const float in = std::hypot(solid.x - cx, solid.y - cy);
      const float out_ = std::hypot(clear.x - cx, clear.y - cy);
      CHECK(out_ > in);  // clockwise: the normals point outward
      at_point = at_point || (std::abs(solid.x - at.x) < 1.5f && std::abs(solid.y - at.y) < 1.5f);
    }
    CHECK(at_point);
  }
}

TEST_CASE("a segment: a quad along it and a disc at each end, level, upright or slanted, either way (D277)") {
  const struct {
    Rect rect;
    view::Facing facing;
    float x0, y0, x1, y1;  // its ends
  } cases[] = {{{10, 20, 30, 0}, view::Facing::down, 10, 20, 40, 20},   // level: no height, still drawn
               {{10, 20, 0, 30}, view::Facing::down, 10, 20, 10, 50},   // upright: no width
               {{10, 20, 30, 40}, view::Facing::down, 10, 20, 40, 60},  // top-left to bottom-right
               {{10, 20, 30, 40}, view::Facing::up, 10, 60, 40, 20}};   // bottom-left to top-right
  for (const auto& c : cases) {
    Item segment;
    segment.shape = Shape::segment;
    segment.colour = tokens::Colour::gold_hi;
    segment.rect = c.rect;
    segment.facing = c.facing;
    segment.width = 2;
    abi::DrawListData out;
    draw::tessellate(one(segment), tokens::kGrace.colour, kAtlas, out);
    REQUIRE(out.vertices.size() == 2 * 4 + 2 * 2 * 12);  // the quad and two discs, each with its clear fringe
    // Every solid vertex lies within half the width of the line between its ends: nothing reaches past it.
    const float dx = c.x1 - c.x0;
    const float dy = c.y1 - c.y0;
    const float length = std::hypot(dx, dy);
    bool near_start = false;
    bool near_end = false;
    for (const GraceUiDrawVert& v : out.vertices) {
      if ((v.colour >> 24) == 0) {
        continue;
      }
      const float t = std::clamp(((v.x - c.x0) * dx + (v.y - c.y0) * dy) / (length * length), 0.0f, 1.0f);
      CHECK(std::hypot(v.x - (c.x0 + t * dx), v.y - (c.y0 + t * dy)) <= 1.0f + 0.01f);
      near_start = near_start || std::hypot(v.x - c.x0, v.y - c.y0) <= 1.01f;
      near_end = near_end || std::hypot(v.x - c.x1, v.y - c.y1) <= 1.01f;
    }
    CHECK(near_start);
    CHECK(near_end);
  }
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

// ---- The Grace pointer (G31, D178).

namespace {

float cross(draw::PointerPoint o, draw::PointerPoint a, draw::PointerPoint b) {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

bool segments_cross(draw::PointerPoint a, draw::PointerPoint b, draw::PointerPoint c, draw::PointerPoint d) {
  const float d1 = cross(c, d, a);
  const float d2 = cross(c, d, b);
  const float d3 = cross(a, b, c);
  const float d4 = cross(a, b, d);
  return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

}  // namespace

TEST_CASE("the pointer: its tip at the hotspot, its size, and an outline that never crosses itself") {
  const auto& outline = draw::kPointerOutline;
  REQUIRE(outline.size() == draw::kPointerPoints);
  float nearest = 1e9f;
  float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
  for (const draw::PointerPoint& p : outline) {
    nearest = std::min(nearest, std::hypot(p.x, p.y));
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_y = std::min(min_y, p.y);
    max_y = std::max(max_y, p.y);
  }
  CHECK(nearest < 0.6f);  // the rounded tip stays within about half a pixel of the hotspot
  CHECK(min_x >= 0.0f);
  CHECK(min_y >= 0.0f);
  CHECK(max_x < 14.2f);
  CHECK(max_y < 19.0f);
  CHECK(max_y > 17.5f);  // about 24 px tall at the default 1.25 at 1080 lines
  // A simple polygon: no edge crosses another that it does not share a point with, so Dear ImGui's concave fill takes it.
  const std::size_t n = outline.size();
  int crossings = 0;
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = i + 2; j < n; ++j) {
      if (i == 0 && j == n - 1) {
        continue;  // the closing edge meets the first
      }
      crossings += segments_cross(outline[i], outline[(i + 1) % n], outline[j], outline[(j + 1) % n]) ? 1 : 0;
    }
  }
  CHECK(crossings == 0);
  // One winding all round but at the notch, which turns the other way: the arrowhead's back.
  int left_turns = 0;
  int right_turns = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const float turn = cross(outline[i], outline[(i + 1) % n], outline[(i + 2) % n]);
    left_turns += turn > 1e-6f ? 1 : 0;
    right_turns += turn < -1e-6f ? 1 : 0;
  }
  CHECK(std::min(left_turns, right_turns) > 0);
  CHECK(std::min(left_turns, right_turns) <= static_cast<int>(draw::kPointerArcPoints) + 1);
}


TEST_CASE("an item wholly outside its clip costs no vertex; one that crosses its edge is drawn (D256)") {
  Item below;
  below.shape = Shape::fill;
  below.colour = tokens::Colour::ground_2;
  below.rect = Rect{10, 900, 100, 40};  // a long page's row past the view's 400 px
  abi::DrawListData out;
  draw::tessellate(one(below), tokens::kGrace.colour, kAtlas, out);
  CHECK(out.vertices.empty());
  CHECK(out.commands.empty());
  Item above = below;
  above.rect = Rect{10, -60, 100, 40};
  draw::tessellate(one(above), tokens::kGrace.colour, kAtlas, out);
  CHECK(out.vertices.empty());
  Item crossing = below;
  crossing.rect = Rect{10, 380, 100, 40};  // half in view
  draw::tessellate(one(crossing), tokens::kGrace.colour, kAtlas, out);
  CHECK(out.vertices.size() == 4);
}

TEST_CASE("a page far longer than the view tessellates only its rows in view, within a mod's limits (D256)") {
  view::DisplayList list;
  list.clips.push_back(Rect{0, 0, 1920, 1080});
  for (int row = 0; row < 4000; ++row) {
    Item item;
    item.shape = Shape::fill;
    item.colour = tokens::Colour::ground_2;
    item.radius = 4;
    item.rect = Rect{10, static_cast<float>(row * 30), 600, 24};
    list.items.push_back(item);
  }
  abi::DrawListData out;
  draw::tessellate(list, tokens::kGrace.colour, kAtlas, out);
  CHECK(out.vertices.size() <= 37 * 24);  // the 36 or 37 rows that reach the view
  abi::DrawContext context;
  context.width = 1920;
  context.height = 1080;
  context.atlas = kAtlas.texture;
  context.atlas_generation = 1;
  out.atlas_generation = 1;
  CHECK(abi::check_draw_list(out, context) == abi::Refusal::none);
}
