// display.h: what a mod learns of the display (D180, proposal 0004): the back buffer's size, ui.scale and its default,
// and the scale its scaled faces were baked at. Each value is written by the thread that knows it (the present thread
// the size, GraceUI's thread ui.scale, warm-up's bake the faces' scale), with no lock: a revision rises after each
// change, and a mod's poll gives one GRACEUI_EVENT_DISPLAY for every revision it has not seen, once warm-up has baked.
#pragma once

#include <cstdint>

namespace grace::host::display {

struct Snapshot {
  std::uint32_t revision = 0;  // 0 before warm-up: no event yet
  std::uint32_t width = 0;     // 0 until a swap chain is bound
  std::uint32_t height = 0;
  float ui_scale = 0.0f;
  float ui_scale_default = 0.0f;
  float scale = 0.0f;          // (height / 1080) x ui.scale, §3.13; 0 until the size is known
  float content_scale = 0.0f;  // scale / ui_scale_default: a mod's design size at 1080 lines and the default
  float faces_scale = 0.0f;    // the content scale its scaled faces were baked at; 0 before warm-up
};

// GraceUI's own settings, as loaded (the last session's ui.scale, D179) and after each change (GraceUI's thread).
void set_ui_scale(float value, float default_value) noexcept;
// The back buffer's size, at a bind or a resize (the present thread).
void set_size(std::uint32_t width, std::uint32_t height) noexcept;
// Warm-up's bake: the content scale the mods' scaled faces were baked at. From then on, changes raise the revision.
void set_faces_scale(float content_scale) noexcept;

// The values now, whole (any thread; a write in between is read again).
Snapshot read() noexcept;
// The content scale for a bake at `height` lines: (height / 1080) x ui.scale / its default.
float content_scale(std::uint32_t height) noexcept;

// A mod connected under `owner`: it has seen nothing yet.
void connected(std::uint32_t owner) noexcept;
// A mod's poll: true with the values if a revision waits that it has not seen; it is then seen.
bool take(std::uint32_t owner, Snapshot& out) noexcept;

}  // namespace grace::host::display
