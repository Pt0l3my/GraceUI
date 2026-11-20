// SPDX-License-Identifier: MIT
// pointer.h: the Grace pointer (G31, D178), GraceUI's own, which the host draws in Dear ImGui's place for every holder
// that takes the mouse (§5.5): a notched arrowhead with rounded corners, a gold edge on a dark fill over a soft dark
// halo, still (no animation: invariant 14), its tip at the hotspot; and a text beam over a text field. Our own geometry.
//
// Pure geometry, built at compile time, in the pointer's own units: 1 unit is 1 px at an effective scale of 1 (§3.13),
// y downwards, the hotspot at 0,0. The host scales it and draws it in the active variant's colours; nothing here
// allocates or runs at start-up.
#pragma once

#include <array>
#include <cstddef>

namespace grace::draw {

struct PointerPoint {
  float x;
  float y;
};

// The arrowhead's corners before rounding, from the tip, and each corner's radius.
struct PointerCorner {
  PointerPoint at;
  float radius;
};
inline constexpr std::array<PointerCorner, 4> kPointerCorners{{
    {{0.0f, 0.0f}, 1.1f},    // the tip, the hotspot
    {{0.0f, 19.0f}, 1.8f},   // the left wing
    {{5.4f, 13.4f}, 1.6f},   // the notch
    {{14.2f, 14.6f}, 1.8f},  // the right wing
}};
inline constexpr std::size_t kPointerArcPoints = 9;  // points along each rounded corner, both ends included
inline constexpr std::size_t kPointerPoints = kPointerCorners.size() * kPointerArcPoints;

// Sizes in units: the edge (Grace; Grace Clear's is thicker, as its borders are) and the halo under it.
inline constexpr float kPointerEdge = 1.5f;
inline constexpr float kPointerEdgeClear = 2.0f;
inline constexpr float kPointerHalo = 3.6f;
inline constexpr float kPointerHaloAlpha = 0.6f;  // the halo's opacity over the scene
inline constexpr float kPointerFillAlpha = 0.88f; // the fill's

// The text beam: a stem centred on the hotspot with a bar at each end, drawn as rounded rectangles.
inline constexpr float kBeamHeight = 18.0f;
inline constexpr float kBeamStem = 1.5f;
inline constexpr float kBeamBar = 6.0f;  // the end bars' width

namespace detail {

constexpr float root(float v) noexcept {  // Newton's method: std::sqrt is not constexpr in C++20
  if (v <= 0.0f) {
    return 0.0f;
  }
  float x = v > 1.0f ? v : 1.0f;
  for (int i = 0; i < 32; ++i) {
    x = 0.5f * (x + v / x);
  }
  return x;
}

constexpr PointerPoint toward(PointerPoint from, PointerPoint to, float distance) noexcept {
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  const float length = root(dx * dx + dy * dy);
  return PointerPoint{from.x + dx * distance / length, from.y + dy * distance / length};
}

// Each corner replaced by a quadratic curve from `radius` before it to `radius` after it, the corner its control point.
constexpr std::array<PointerPoint, kPointerPoints> outline() noexcept {
  std::array<PointerPoint, kPointerPoints> out{};
  std::size_t n = 0;
  for (std::size_t i = 0; i < kPointerCorners.size(); ++i) {
    const PointerCorner& corner = kPointerCorners[i];
    const PointerPoint before = kPointerCorners[(i + kPointerCorners.size() - 1) % kPointerCorners.size()].at;
    const PointerPoint after = kPointerCorners[(i + 1) % kPointerCorners.size()].at;
    const PointerPoint a = toward(corner.at, before, corner.radius);
    const PointerPoint b = toward(corner.at, after, corner.radius);
    for (std::size_t k = 0; k < kPointerArcPoints; ++k) {
      const float t = static_cast<float>(k) / static_cast<float>(kPointerArcPoints - 1);
      const float u = 1.0f - t;
      out[n++] = PointerPoint{u * u * a.x + 2.0f * u * t * corner.at.x + t * t * b.x,
                              u * u * a.y + 2.0f * u * t * corner.at.y + t * t * b.y};
    }
  }
  return out;
}

}  // namespace detail

// The arrowhead's outline, from the tip's curve, round the left wing, the notch and the right wing.
inline constexpr std::array<PointerPoint, kPointerPoints> kPointerOutline = detail::outline();

}  // namespace grace::draw
