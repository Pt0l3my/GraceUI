// The in-game menu (menu.cpp; §5.1, D79, D115, D167): GraceUI's own space drawn by the host as the input holder of kind
// menu. The menu key opens it; the window procedure hands it keys, and the present thread the pad's buttons, each as
// one of grace-view's inputs; GraceUI's own thread runs the page view and the space's settings store (its one entry
// point, invariant 3), lays the page out and tessellates it, and publishes the list; the present thread lends the
// newest list to Dear ImGui's renderer in GraceUI's own layer, so that the present thread allocates nothing and never
// waits for the layout (invariant 10).
#pragma once

#include "grace/input/keys.h"

#include <cstdint>

namespace grace::abi {
struct DrawListData;
}

namespace grace::host::menu {

// The effective scale the menu's faces are baked at (§3.13): (height / 1080) times ui.scale.
float bake_scale(std::uint32_t height) noexcept;
// Whether the bake adds Plain text's faces (D267): ui.menu.plain_text as GraceUI's own store holds it, the last
// session's if it kept one (D179), else its default.
bool bake_plain() noexcept;
// Whether GraceUI's theme is Grace Clear (ui.menu.theme), for what the present thread draws in it: the pointer.
bool clear_theme() noexcept;

// The window procedure, on a fresh press of the menu key with no holder: asks for input as the menu; granted, GraceUI's
// thread lays out the first frame; not now, one of GraceUI's notices says why (§5.1).
void open_requested() noexcept;

// The window procedure, for a key that went down (or repeated) while the menu holds input: true if it is one of the
// menu's keys (§5.3), which goes to GraceUI's thread. `shift` and `ctrl` held; `repeat`, the key was already down
// (a repeat moves along or steps a number only, D270).
bool key_down(std::uint32_t vk, grace::input::Key code, bool shift, bool ctrl, bool alt, bool repeat) noexcept;

// The window procedure, for a character typed (a surrogate pair joined, an ANSI window's bytes decoded) while the menu
// holds input: to the edited field, or `/` to open search (G38 (a), §4.3); dropped otherwise.
// `repeat`: a held key's auto-repeat, which types only into the field its key went down in (D270).
void character(char32_t code_point, bool repeat = false) noexcept;

// The present thread, for the menu holder's mouse (§5.5): a move (0), a left button's press (1) or release (2), the wheel
// (3, in notches, positive away from the player) or the pointer gone (4), at `x`, `y` in the display's pixels as Dear
// ImGui has them, so that a press lands where the Grace pointer is drawn; to GraceUI's thread as the page view's input.
void pointer(std::uint8_t kind, float x, float y, float wheel, std::uint32_t generation) noexcept;

// The present thread, each frame: the menu's newest list for the holder of this generation if the menu holds input and
// has published one for it, with the alpha of its fade in (§3.10, D271); after a close by the player, its last list
// while it fades out; else none. Also turns the pad's new presses into the menu's inputs, and notes the display's size,
// which a change of makes GraceUI's thread lay the page out again.
struct Frame {
  grace::abi::DrawListData* list = nullptr;  // null: nothing of the menu's drawn this frame
  float alpha = 1.0f;                        // its fade (§3.10): below 1 while it fades in or out
  bool closing = false;                      // it fades out after a close by the player, holding no input
};
Frame frame(std::uint32_t width, std::uint32_t height) noexcept;
// The present thread: whether the menu's list was drawn in its last frame, open or fading out (D271), so that the next
// frame is built and sees the menu close, and draws its fade out.
bool drawing() noexcept;

// GraceUI's own thread, when woken.
void serve() noexcept;
// Whether the menu wants GraceUI's thread to wake once a second by itself: Live updates' Every 5 s holds bodies (D252).
bool wants_timer() noexcept;
// Whether GraceUI's menu holds input now (any thread; lock-free): what an on_present callback is told (D259).
bool open_now() noexcept;

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
  std::uint32_t focus_shown;    // 1 while the focus ring is drawn (G42)
  std::uint32_t pad_mode;       // 1 while the key hints are the pad's (D242)
  std::uint32_t hints_shown;    // 1 while they are drawn
  char footer[160];             // the footer's words as drawn
  std::uint32_t plain_text;     // 1 while the menu draws its serif and display roles in Noto Sans (D267)
  std::uint32_t plain_baked;    // 1 if Plain text's own faces were baked at warm-up
  std::uint32_t fade_in_frames;   // frames the present thread drew while the menu faded in (D271)
  std::uint32_t fade_out_frames;  // and while it faded out after a close by the player
  std::uint32_t alpha_milli;      // the last frame's alpha, in thousandths
};
void test_view(TestView& out) noexcept;
// A setting of GraceUI's own space as JSON text (a secret's as its status); answers its length, 0 for none.
std::uint32_t test_setting(const char* key, char* out, std::uint32_t capacity) noexcept;
// Where the menu's last layout drew an element, named as grace-view's focused() names it: its row and its control
// (x, y, width, height each, in the display's pixels); false if it was not drawn.
bool test_bounds(const char* name, float (&out)[8]) noexcept;
// Whether the menu's last layout draws `text` (within one of its texts).
bool test_shows(const char* text) noexcept;
// Whether the menu's last layout had a space's page present (1: in its rail and Related, its settings in search, the one
// rule of PageView::present), absent (0), or no such space or page (2), D269.
std::uint32_t test_page_present(const char* space, const char* page) noexcept;
#endif

}  // namespace grace::host::menu
