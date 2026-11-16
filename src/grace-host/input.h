// input.h: the input half of the host (D69, D77, D125). grace-input holds the rules (pure, tested on Linux); this is
// their Win32 and Dear ImGui side: the one holder and its capture, which the window procedure's filter and the input
// detours (hooks_input.cpp) feed and consult, and which the render path (draw.cpp) hands to Dear ImGui on the present
// thread. Nothing here waits or allocates once warm (invariant 10); every function is noexcept.
#pragma once

#include <windows.h>

#include <cstdint>

#include "grace/input/devices.h"
#include "grace/input/holder.h"
#include "grace/input/messages.h"

namespace grace::host::input {

// ---- The rules, one each in the process.
grace::input::Capture& capture() noexcept;
grace::input::KeyboardLedger& keyboard() noexcept;
grace::input::MouseLedger& mouse() noexcept;
grace::input::PadLedger& pads() noexcept;
grace::input::MessageRules& messages() noexcept;  // the game's window thread only

std::int64_t now_ms() noexcept;  // the rules' clock: GetTickCount64

// The menu key (§5.1): F5 until GraceUI's own settings can rebind it (D115). Always masked from the game.
grace::input::Key menu_key() noexcept;

// ---- What the window procedure hands the holder, in order: pushed on the window's thread, popped on the present thread.
struct HolderEvent {
  enum class Type : std::uint8_t { none = 0, key = 1, character = 2, mouse_move = 3, mouse_button = 4, wheel = 5 };
  Type type = Type::none;
  std::uint8_t key = 0;   // a key's DIK code (grace::input::Key); a mouse button's index
  std::uint8_t down = 0;
  std::uint8_t unused = 0;
  std::uint32_t generation = 0;  // the holder it is for
  std::uint32_t vk = 0;          // a key's virtual key
  std::int32_t a = 0;            // a character's code point; x in client pixels; the vertical wheel's delta
  std::int32_t b = 0;            // y in client pixels; the horizontal wheel's delta
};
void push_event(HolderEvent event) noexcept;

// ---- The game's window, filtered once at install (hooks_input.cpp).
void set_window(HWND window, bool unicode) noexcept;
HWND window() noexcept;
bool window_filtered() noexcept;
bool window_unicode() noexcept;
void set_client_size(std::int32_t width, std::int32_t height) noexcept;
void window_moved() noexcept;  // WM_SIZE or WM_MOVE: the cursor's confinement follows the window
// The code page an ANSI window's characters are read in: its keyboard layout's (WM_INPUTLANGCHANGE), unless a test set one.
void set_keyboard_code_page(std::uint32_t code_page) noexcept;
std::uint32_t code_page() noexcept;
// Where the holder's text caret is, in client pixels, for the IME's composition window; false if no holder reported one.
bool caret_in_client(std::int32_t& x, std::int32_t& y, std::int32_t& height) noexcept;

// ---- The holder's side (S10.4.b's ABI; GraceUI 0.1 reaches it through test exports). Any thread.
grace::input::Request request(std::uint16_t owner, grace::input::Kind kind, std::uint8_t wants) noexcept;
bool release(std::uint16_t owner) noexcept;
void set_world(bool danger, bool loading, bool game_menu) noexcept;  // the providers' claims, combined (D78)
// The one close routine for holder `generation` only (a later holder is left alone), with `why`; answers whether it
// ended one.
bool close_holder(grace::input::Closed why, std::uint32_t generation) noexcept;
void set_caret(std::int32_t x, std::int32_t y, std::int32_t height) noexcept;  // in back-buffer pixels

// ---- The close routine's Win32 side, after any close: the cursor's confinement put back, one log line.
void closed(const grace::input::Holder& holder) noexcept;
// The player's exit and the watchdog: called on every message, input read and frame.
void tick() noexcept;
// A fault in the input half: input is off for the session (every detour and the filter pass through) and any holder ends.
void fault(const char* why) noexcept;
// Input off for the session from the start, before any hook exists: the anti-cheat's notice-only mode (D88), in which
// capture is never on and every detour and the filter are pure pass-through, the menu key included.
void turn_off(const char* why) noexcept;
bool off() noexcept;
void note_menu_key(grace::input::MenuKey what) noexcept;  // "‹Mod› has the controls", or the menu GraceUI 0.1 lacks

// ---- The present thread (draw.cpp).
bool wants_frames() noexcept;  // a holder has input: its frames run, its cursor drawn
void rendered() noexcept;      // the overlay drew, or had nothing to draw: the watchdog's heartbeat
void render_off() noexcept;    // the render path turned itself off: any holder ends (fault)
void reserve_imgui_queues() noexcept;  // at warm-up, so that a holder's input allocates nothing during play
// Just before Dear ImGui's NewFrame: the holder's input, the pad included, in back-buffer pixels; or nothing.
void before_imgui_frame(std::uint32_t width, std::uint32_t height) noexcept;
void after_new_frame() noexcept;  // test builds: what Dear ImGui received

#if defined(GRACEUI_TEST_EXPORTS)
void test_set_code_page(std::uint32_t code_page) noexcept;  // 0: the keyboard layout's again
std::uint32_t test_events_dropped() noexcept;
#endif

}  // namespace grace::host::input
