// The in-game menu (menu.cpp; §5.1, D79, D115, D167): GraceUI's own space drawn by the host as the input holder of kind
// menu. The menu key opens it; the window procedure hands it keys, and the present thread the pad's buttons, each as
// one of grace-view's inputs; GraceUI's own thread runs the page view and the space's settings store (its one entry
// point, invariant 3), lays the page out and tessellates it, and publishes the list; the present thread lends the
// newest list to Dear ImGui's renderer in GraceUI's own layer, so that the present thread allocates nothing and never
// waits for the layout (invariant 10).
#pragma once

#include <cstdint>

namespace grace::abi {
struct DrawListData;
}

namespace grace::host::menu {

// The effective scale the menu's faces are baked at (§3.13): (height / 1080) times ui.scale.
float bake_scale(std::uint32_t height) noexcept;

// The window procedure, on a fresh press of the menu key with no holder: asks for input as the menu; granted, GraceUI's
// thread lays out the first frame; not now, one of GraceUI's notices says why (§5.1).
void open_requested() noexcept;

// The window procedure, for a key that went down (or repeated) while the menu holds input: true if it is one of the
// menu's keys (§5.3), which goes to GraceUI's thread. `shift` and `ctrl` held.
bool key_down(std::uint32_t vk, bool shift, bool ctrl) noexcept;

// The present thread, each frame: the menu's newest list for the holder of this generation if the menu holds input and
// has published one for it, else null. Also turns the pad's new presses into the menu's inputs, and notes the display's
// size, which a change of makes GraceUI's thread lay the page out again.
grace::abi::DrawListData* frame(std::uint32_t width, std::uint32_t height) noexcept;

// GraceUI's own thread, when woken.
void serve() noexcept;

#if defined(GRACEUI_TEST_EXPORTS)
struct TestView {
  std::uint32_t open;  // 1 while the menu holds input
  std::uint32_t generation;
  std::uint64_t lists;          // lists published
  std::uint64_t frames;         // frames that drew the menu's list
  std::uint32_t inputs;         // inputs the page view took
  std::uint32_t refusals;       // commands its settings store refused
  std::uint32_t layout_us;      // the last layout and tessellation, on GraceUI's thread
  std::uint32_t vertices;
  std::uint32_t indices;
  std::uint32_t commands;
  std::uint32_t off;            // 1 once a fault turned the menu off for the session
  float scale;                  // the effective scale of the last layout
  std::uint32_t faces;          // the menu's faces in the atlas
  float baked_scale;            // the effective scale they were baked at (0 if left out)
  char page[64];
  char focused[96];
};
void test_view(TestView& out) noexcept;
// A setting of GraceUI's own space as JSON text (a secret's as its status); answers its length, 0 for none.
std::uint32_t test_setting(const char* key, char* out, std::uint32_t capacity) noexcept;
#endif

}  // namespace grace::host::menu
