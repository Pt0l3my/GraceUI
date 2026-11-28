// SPDX-License-Identifier: MIT
// tessellate.cpp: the display list as ABI 1's draw list (tessellate.h).
#include "grace/draw/tessellate.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace grace::draw {
namespace {

using view::Item;
using view::Rect;
using view::Shape;

constexpr float kFringe = 1.0f;          // the anti-aliasing fringe's width, in pixels
constexpr std::uint32_t kMaxVertices = 65536;  // a command's vertices (D76)
constexpr int kMaxArc = 12;              // segments a quarter circle takes at most
constexpr float kPi = 3.14159265358979f;

struct Point {
  float x;
  float y;
};

class Writer {
 public:
  Writer(abi::DrawListData& out, const Atlas& atlas, float dx, float dy) : out_(out), atlas_(atlas), dx_(dx), dy_(dy) {}

  // Starts the vertices of a shape of `count` vertices: a new command if the texture or clip changes, or if the
  // command would pass its vertex bound.
  std::uint32_t begin(std::uint32_t count, std::uint64_t texture, const Rect& clip) {
    const bool same = !out_.commands.empty() && out_.commands.back().texture == texture &&
                      out_.commands.back().clip_x0 == clip.x + dx_ && out_.commands.back().clip_y0 == clip.y + dy_ &&
                      out_.commands.back().clip_x1 == clip.right() + dx_ &&
                      out_.commands.back().clip_y1 == clip.bottom() + dy_ &&
                      out_.commands.back().vertex_count + count <= kMaxVertices;
    if (!same) {
      GraceUiDrawCommand command{};
      command.clip_x0 = clip.x + dx_;
      command.clip_y0 = clip.y + dy_;
      command.clip_x1 = clip.right() + dx_;
      command.clip_y1 = clip.bottom() + dy_;
      command.texture = texture;
      command.vertex_offset = static_cast<std::uint32_t>(out_.vertices.size());
      command.index_offset = static_cast<std::uint32_t>(out_.indices.size());
      out_.commands.push_back(command);
    }
    GraceUiDrawCommand& command = out_.commands.back();
    const std::uint32_t base = command.vertex_count;
    command.vertex_count += count;
    return base;  // the shape's first vertex, relative to its command
  }
  void vertex(float x, float y, std::uint32_t colour, float u, float v) {
    out_.vertices.push_back(GraceUiDrawVert{x + dx_, y + dy_, u, v, colour});
  }
  void solid(float x, float y, std::uint32_t colour) { vertex(x, y, colour, atlas_.white_u, atlas_.white_v); }
  void triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    out_.indices.push_back(static_cast<std::uint16_t>(a));
    out_.indices.push_back(static_cast<std::uint16_t>(b));
    out_.indices.push_back(static_cast<std::uint16_t>(c));
    out_.commands.back().index_count += 3;
  }
  std::uint64_t solid_texture() const { return atlas_.texture; }

 private:
  abi::DrawListData& out_;
  const Atlas& atlas_;
  float dx_;
  float dy_;
};

int arc_segments(float radius) {
  return std::clamp(static_cast<int>(std::ceil(radius / 2.0f)), 2, kMaxArc);
}

// A rectangle's outline as a convex polygon, clockwise on the display, its corners rounded by `radius`.
int outline(const Rect& r, float radius, std::array<Point, 4 * (kMaxArc + 1)>& points) {
  radius = std::min({radius, r.w / 2, r.h / 2});
  if (radius < 0.5f) {
    points[0] = {r.x, r.y};
    points[1] = {r.right(), r.y};
    points[2] = {r.right(), r.bottom()};
    points[3] = {r.x, r.bottom()};
    return 4;
  }
  const int n = arc_segments(radius);
  const Point centres[4] = {{r.right() - radius, r.y + radius},
                            {r.right() - radius, r.bottom() - radius},
                            {r.x + radius, r.bottom() - radius},
                            {r.x + radius, r.y + radius}};
  int count = 0;
  for (int corner = 0; corner < 4; ++corner) {
    const float start = -kPi / 2 + static_cast<float>(corner) * kPi / 2;
    for (int i = 0; i <= n; ++i) {
      const float a = start + (kPi / 2) * static_cast<float>(i) / static_cast<float>(n);
      points[static_cast<std::size_t>(count++)] = {centres[corner].x + radius * std::cos(a),
                                                   centres[corner].y + radius * std::sin(a)};
    }
  }
  return count;
}

// Each point's outward normal, averaged from its two edges, scaled to keep an offset of 1 along both.
template <std::size_t N>
void normals(const std::array<Point, N>& p, int count, std::array<Point, N>& out) {
  for (int i = 0; i < count; ++i) {
    const Point& prev = p[static_cast<std::size_t>((i + count - 1) % count)];
    const Point& here = p[static_cast<std::size_t>(i)];
    const Point& next = p[static_cast<std::size_t>((i + 1) % count)];
    const auto edge_normal = [](const Point& a, const Point& b) {
      const float dx = b.x - a.x;
      const float dy = b.y - a.y;
      const float len = std::sqrt(dx * dx + dy * dy);
      return len > 0 ? Point{dy / len, -dx / len} : Point{0, 0};
    };
    const Point n0 = edge_normal(prev, here);
    const Point n1 = edge_normal(here, next);
    Point n{(n0.x + n1.x) / 2, (n0.y + n1.y) / 2};
    const float d2 = n.x * n.x + n.y * n.y;
    if (d2 > 1e-6f) {
      const float k = std::min(1.0f / d2, 4.0f);
      n = {n.x * k, n.y * k};
    }
    out[static_cast<std::size_t>(i)] = n;
  }
}

// A filled convex polygon: its body opaque, and a fringe from its edge to nothing.
template <std::size_t N>
void fill_convex(Writer& w, const std::array<Point, N>& p, int count, std::uint32_t colour, const Rect& clip) {
  std::array<Point, N> n{};
  normals(p, count, n);
  const std::uint32_t clear = colour & 0x00FFFFFFu;
  const auto c = static_cast<std::uint32_t>(count);
  const std::uint32_t base = w.begin(2 * c, w.solid_texture(), clip);
  for (int i = 0; i < count; ++i) {
    const Point& q = p[static_cast<std::size_t>(i)];
    const Point& m = n[static_cast<std::size_t>(i)];
    w.solid(q.x - m.x * kFringe * 0.5f, q.y - m.y * kFringe * 0.5f, colour);
  }
  for (int i = 0; i < count; ++i) {
    const Point& q = p[static_cast<std::size_t>(i)];
    const Point& m = n[static_cast<std::size_t>(i)];
    w.solid(q.x + m.x * kFringe * 0.5f, q.y + m.y * kFringe * 0.5f, clear);
  }
  for (std::uint32_t i = 1; i + 1 < c; ++i) {
    w.triangle(base, base + i, base + i + 1);
  }
  for (std::uint32_t i = 0; i < c; ++i) {
    const std::uint32_t j = (i + 1) % c;
    w.triangle(base + i, base + c + i, base + c + j);
    w.triangle(base + i, base + c + j, base + j);
  }
}

// A closed outline `width` wide, inside the polygon's edge, with a fringe on both sides.
template <std::size_t N>
void stroke_convex(Writer& w, const std::array<Point, N>& p, int count, float width, std::uint32_t colour,
                   const Rect& clip) {
  std::array<Point, N> n{};
  normals(p, count, n);
  const std::uint32_t clear = colour & 0x00FFFFFFu;
  const auto c = static_cast<std::uint32_t>(count);
  const std::uint32_t base = w.begin(4 * c, w.solid_texture(), clip);
  const float body = std::max(width - kFringe, 0.0f);
  // Four rings, outermost first: clear at +half a fringe, solid at -half, solid at -(half + body), clear at -(width).
  const float offsets[4] = {kFringe * 0.5f, -kFringe * 0.5f, -kFringe * 0.5f - body, -width - kFringe * 0.5f};
  const std::uint32_t colours[4] = {clear, colour, colour, clear};
  for (int ring = 0; ring < 4; ++ring) {
    for (int i = 0; i < count; ++i) {
      const Point& q = p[static_cast<std::size_t>(i)];
      const Point& m = n[static_cast<std::size_t>(i)];
      w.solid(q.x + m.x * offsets[ring], q.y + m.y * offsets[ring], colours[ring]);
    }
  }
  for (std::uint32_t ring = 0; ring < 3; ++ring) {
    for (std::uint32_t i = 0; i < c; ++i) {
      const std::uint32_t j = (i + 1) % c;
      const std::uint32_t a = base + ring * c;
      const std::uint32_t b = base + (ring + 1) * c;
      w.triangle(a + i, b + i, b + j);
      w.triangle(a + i, b + j, a + j);
    }
  }
}

void quad(Writer& w, float x0, float y0, float x1, float y1, std::uint32_t colour, const Rect& clip) {
  const std::uint32_t base = w.begin(4, w.solid_texture(), clip);
  w.solid(x0, y0, colour);
  w.solid(x1, y0, colour);
  w.solid(x1, y1, colour);
  w.solid(x0, y1, colour);
  w.triangle(base, base + 1, base + 2);
  w.triangle(base, base + 2, base + 3);
}

}  // namespace

Stats tessellate(const view::DisplayList& list, const tokens::Palette& palette, const Atlas& atlas,
                 abi::DrawListData& out, float offset_x, float offset_y) {
  out.vertices.clear();
  out.indices.clear();
  out.commands.clear();
  out.atlas_generation = list.generation;
  Writer w(out, atlas, offset_x, offset_y);
  static const Rect kNoClip{0, 0, 1e6f, 1e6f};
  std::array<Point, 4 * (kMaxArc + 1)> points{};
  Stats stats;
  out.body_first = 0;
  out.body_end = 0;
  out.ring_first = 0;
  out.ring_end = 0;
  const bool body = list.body_end > list.body_first;
  const bool ring = list.ring_end > list.ring_first;
  std::uint32_t index = 0;
  for (const Item& item : list.items) {
    // The page body's and the focus ring's first and end vertices, as their items begin and end (D272).
    const auto at = static_cast<std::uint32_t>(out.vertices.size());
    if (body && index == list.body_first) {
      out.body_first = at;
    }
    if (body && index == list.body_end) {
      out.body_end = at;
    }
    if (ring && index == list.ring_first) {
      out.ring_first = at;
    }
    if (ring && index == list.ring_end) {
      out.ring_end = at;
    }
    ++index;
    const Rect& clip = item.clip < list.clips.size() ? list.clips[item.clip] : kNoClip;
    const std::uint32_t colour = pack(tokens::colour(palette, item.colour), item.alpha);
    // A segment's rectangle is its bounding box, flat for a level or upright one (D277).
    if ((colour >> 24) == 0 || (item.shape != Shape::segment && (item.rect.w <= 0 || item.rect.h <= 0))) {
      continue;  // nothing to draw
    }
    // Wholly outside its clip (a long page's rows above or below the view): nothing of it would show, so no vertex is
    // spent on it. A stroke's width reaches past its rectangle, hence the margin.
    const float margin = std::max(item.width, 1.0f) + 1.0f;
    if (item.rect.right() + margin < clip.x || item.rect.x - margin > clip.right() ||
        item.rect.bottom() + margin < clip.y || item.rect.y - margin > clip.bottom()) {
      continue;
    }
    ++stats.items;
    const Rect& r = item.rect;
    switch (item.shape) {
      case Shape::fill: {
        if (item.radius < 0.5f) {
          quad(w, r.x, r.y, r.right(), r.bottom(), colour, clip);
        } else {
          const int count = outline(r, item.radius, points);
          fill_convex(w, points, count, colour, clip);
        }
        break;
      }
      case Shape::stroke: {
        const int count = outline(r, item.radius, points);
        stroke_convex(w, points, count, std::max(item.width, 1.0f), colour, clip);
        break;
      }
      case Shape::line: {
        // A hairline snapped to whole device pixels: crisp at any scale.
        const float x0 = std::round(r.x);
        const float y0 = std::floor(r.y);
        const float h = std::max(1.0f, std::round(r.h));
        quad(w, x0, y0, std::round(r.right()), y0 + h, colour, clip);
        break;
      }
      case Shape::circle: {
        const float radius = std::min(r.w, r.h) / 2;
        const Point centre{r.x + r.w / 2, r.y + r.h / 2};
        const int count = std::clamp(static_cast<int>(std::ceil(radius * 2)), 12, 4 * kMaxArc);
        for (int i = 0; i < count; ++i) {
          const float a = 2 * kPi * static_cast<float>(i) / static_cast<float>(count);
          points[static_cast<std::size_t>(i)] = {centre.x + radius * std::cos(a), centre.y + radius * std::sin(a)};
        }
        fill_convex(w, points, count, colour, clip);
        break;
      }
      case Shape::diamond: {
        points[0] = {r.x + r.w / 2, r.y};
        points[1] = {r.right(), r.y + r.h / 2};
        points[2] = {r.x + r.w / 2, r.bottom()};
        points[3] = {r.x, r.y + r.h / 2};
        stroke_convex(w, points, 4, std::max(item.width, 1.0f), colour, clip);
        break;
      }
      case Shape::triangle: {
        // Clockwise on the display, as fill_convex's normals need.
        switch (item.facing) {
          case view::Facing::right:
            points[0] = {r.x, r.y};
            points[1] = {r.right(), r.y + r.h / 2};
            points[2] = {r.x, r.bottom()};
            break;
          case view::Facing::down:
            points[0] = {r.x, r.y};
            points[1] = {r.right(), r.y};
            points[2] = {r.x + r.w / 2, r.bottom()};
            break;
          case view::Facing::left:
            points[0] = {r.right(), r.y};
            points[1] = {r.right(), r.bottom()};
            points[2] = {r.x, r.y + r.h / 2};
            break;
          case view::Facing::up:
            points[0] = {r.x + r.w / 2, r.y};
            points[1] = {r.right(), r.bottom()};
            points[2] = {r.x, r.bottom()};
            break;
        }
        fill_convex(w, points, 3, colour, clip);
        break;
      }
      case Shape::segment: {
        // A line `width` wide between two corners of its rectangle, with a disc at each end for its round ends and
        // joins (D277): the quad clockwise on the display, as fill_convex's normals need.
        const Point a = item.facing == view::Facing::up ? Point{r.x, r.bottom()} : Point{r.x, r.y};
        const Point b = item.facing == view::Facing::up ? Point{r.right(), r.y} : Point{r.right(), r.bottom()};
        const float half = std::max(item.width, 0.5f) / 2;
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 0.01f) {
          const float nx = -dy / length * half;
          const float ny = dx / length * half;
          points[0] = {a.x - nx, a.y - ny};
          points[1] = {b.x - nx, b.y - ny};
          points[2] = {b.x + nx, b.y + ny};
          points[3] = {a.x + nx, a.y + ny};
          fill_convex(w, points, 4, colour, clip);
        }
        for (const Point end : {a, b}) {
          constexpr int kEnd = 12;
          for (int i = 0; i < kEnd; ++i) {
            const float t = 2 * kPi * static_cast<float>(i) / static_cast<float>(kEnd);
            points[static_cast<std::size_t>(i)] = {end.x + half * std::cos(t), end.y + half * std::sin(t)};
          }
          fill_convex(w, points, kEnd, colour, clip);
        }
        break;
      }
      case Shape::text: {
        for (std::uint32_t g = item.glyph_first; g < item.glyph_first + item.glyph_count && g < list.glyphs.size(); ++g) {
          const view::PlacedGlyph& q = list.glyphs[g];
          const std::uint32_t base = w.begin(4, item.texture, clip);
          w.vertex(q.x0, q.y0, colour, q.u0, q.v0);
          w.vertex(q.x1, q.y0, colour, q.u1, q.v0);
          w.vertex(q.x1, q.y1, colour, q.u1, q.v1);
          w.vertex(q.x0, q.y1, colour, q.u0, q.v1);
          w.triangle(base, base + 1, base + 2);
          w.triangle(base, base + 2, base + 3);
        }
        break;
      }
    }
  }
  if (body && list.body_end >= list.items.size()) {
    out.body_end = static_cast<std::uint32_t>(out.vertices.size());  // the body ran to the list's end
  }
  if (ring && list.ring_end >= list.items.size()) {
    out.ring_end = static_cast<std::uint32_t>(out.vertices.size());
  }
  stats.vertices = static_cast<std::uint32_t>(out.vertices.size());
  stats.indices = static_cast<std::uint32_t>(out.indices.size());
  stats.commands = static_cast<std::uint32_t>(out.commands.size());
  return stats;
}

}  // namespace grace::draw
