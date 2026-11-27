// input.h: the input half of the host (D69, D77, D125). grace-input holds the rules (pure, tested on Linux); this is
// their Win32 and Dear ImGui side: the one holder and its capture, which the window procedure's filter and the input
// detours (hooks_input.cpp) feed and consult, and which the render path (draw.cpp) hands to Dear ImGui on the present
// thread. Nothing here waits or allocates once warm (invariant 10); every function is noexcept.
#pragma once

#include <windows.h>

#include <cstdint>

#include "grace/input/devices.h"
#include "grace/input/holder.h"
#include "grace/input/holder_mail.h"
#include "grace/input/hotkeys.h"
#include "grace/input/kept_bindings.h"
#include "grace/input/chords.h"
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
// The player moved it (hotkeys.menu, D208): the window procedure and DirectInput's view use the new key at once.
void set_menu_key(grace::input::Key key) noexcept;
// The player's hotkey bindings, kept between sessions and applied at each registration (G37, D208).
grace::input::KeptBindings& kept_bindings() noexcept;
grace::input::Hotkeys& hotkeys() noexcept;  // the mods' hotkeys (D155)
grace::input::PadChords& chords() noexcept;  // their pad chords, in XInput's reads (D204)
// Whether chords stand aside now: a holder has input, or request_input would answer not_now (D204).
grace::input::Aside chord_aside() noexcept;
// What DirectInput's readers hide from the game besides a holder's capture: the menu key, every key bound with no
// modifier, and a hotkey's press with modifiers until its release.
grace::input::KeyBits hidden_keys() noexcept;

// ---- What the window procedure hands the holder, in order: pushed on the window's thread, popped on the present thread.
struct HolderEvent {
  enum class Type : std::uint8_t { none = 0, key = 1, character = 2, mouse_move = 3, mouse_button = 4, wheel = 5 };
  Type type = Type::none;
  std::uint8_t key = 0;   // a key's DIK code (grace::input::Key); a mouse button's index
  std::uint8_t down = 0;
  std::uint8_t extra = 0;        // a key's: its modifiers (GRACEUI_MOD_*) in the low bits, kExtraRepeat for a repeat
  std::uint32_t generation = 0;  // the holder it is for
  std::uint32_t vk = 0;          // a key's virtual key
  std::int32_t a = 0;            // a character's code point; x in client pixels; the vertical wheel's delta
  std::int32_t b = 0;            // y in client pixels; the horizontal wheel's delta
};
inline constexpr std::uint8_t kExtraRepeat = 0x80;
// Pushed for GraceUI's own Dear ImGui (its cursor, and its own holders) and, while the holder asked for them, posted to
// its mod as well (D176).
void push_event(HolderEvent event) noexcept;

// ---- A holder's events for its mod (D176): made once by session::allocate, before any mod has a table; null before.
void make_holder_mail();
grace::input::HolderMail* holder_mail() noexcept;
// Its events enabled for the holder `generation` (HolderMail::enable, after the grant): if another thread ended that
// holder in between, its close routine found nothing to end, so its end is posted here, once.
void holder_mail_enabled(std::uint32_t generation) noexcept;
// The back buffer's size as the input half last saw it (0 before the first frame).
void buffer_size(std::uint32_t& width, std::uint32_t& height) noexcept;
// A mod's caret for the IME, in the back buffer's pixels: it counts only while `owner` holds input. Height 0 clears it.
void set_mod_caret(std::uint16_t owner, std::int32_t x, std::int32_t y, std::int32_t height) noexcept;

// ---- The game's window, filtered once at install (hooks_input.cpp).
void set_window(HWND window, bool unicode) noexcept;
HWND window() noexcept;
bool window_filtered() noexcept;
bool window_unicode() noexcept;
void set_client_size(std::int32_t width, std::int32_t height) noexcept;
void window_moved() noexcept;  // WM_SIZE or WM_MOVE: the cursor's confinement follows the window

// ---- The pointer by the mouse's own motion while a holder has the mouse (D232). A game that turns its camera by the
// mouse puts the system cursor back at its centre (SetCursorPos, ClipCursor) whatever holds input, so the cursor's
// position, which WM_MOUSEMOVE reports, is pulled back too. While raw input or DirectInput gives the mouse's relative
// motion, GraceUI moves its own pointer by it, within the client area, and leaves the system cursor to the game.
enum class MotionSource : std::uint8_t { raw_input, direct_input };

// ---- The menu across the game's loss of focus (G40). At an Alt+Tab the menu lets go of input as any holder does
// (capture released, every key forgotten), but it is kept open, away: its page, row, confirmation, field and sheet
// as they were. When the game's window has the focus again it asks for input again; granted, it goes on where it was
// (the keys held then do nothing until released, invariant 8); refused, or if danger or loading came meanwhile
// (invariant 9), it closes as any close would.
void set_menu_away() noexcept;   // the window's thread, at the focus's loss, before the menu's close
bool menu_away() noexcept;
void focus_back() noexcept;      // the window's thread, at WM_SETFOCUS: the next frames ask again, if it was away
void focus_gone() noexcept;      // the window's thread, at any loss of the focus: a return not yet taken is off
// Any thread (the window's for raw input, the game's reading thread for DirectInput), while a holder has the mouse:
// motion of the mouse, in counts. Raw input is preferred where both come, so that nothing counts twice.
void relative_motion(MotionSource source, std::int32_t dx, std::int32_t dy) noexcept;
// Whether the mouse's motion drives the pointer: from the first motion a holder of the mouse is given until it ends, or
// until an absolute device (a tablet, a remote desktop) reports through raw input (D232, D236). WM_MOUSEMOVE's position
// is then left out: a game that keeps its cursor at its centre sends it at the centre, moving or still.
bool pointer_by_motion() noexcept;
// The window's thread: raw input from an absolute device, whose position WM_MOUSEMOVE reports; the pointer follows it.
void absolute_motion() noexcept;
// The code page an ANSI window's characters are read in: its keyboard layout's (WM_INPUTLANGCHANGE), unless a test set one.
void set_keyboard_code_page(std::uint32_t code_page) noexcept;
std::uint32_t code_page() noexcept;
// Where the holder's text caret is, in client pixels, for the IME's composition window; false if no holder reported one.
bool caret_in_client(std::int32_t& x, std::int32_t& y, std::int32_t& height) noexcept;

// ---- The holder's side (S10.4.b's ABI; GraceUI 0.1 reaches it through test exports). Any thread.
grace::input::Request request(std::uint16_t owner, grace::input::Kind kind, std::uint8_t wants) noexcept;
bool release(std::uint16_t owner) noexcept;
// The providers' claims changed (D78): the session's World holds them, combined, and was written first. Danger or
// loading ends the holder (invariant 9); request() reads the claims afresh, before and after its grant.
void world_changed() noexcept;
// Whether a provider (or the test build) claims danger or a load now (D78): the menu's fade out ends at once (D271).
bool danger_or_loading() noexcept;
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
void note_menu_key(grace::input::MenuKey what) noexcept;  // "‹Mod› has the controls", or the menu opens (D167)

// ---- The present thread (draw.cpp).
bool wants_frames() noexcept;  // a holder has input: its frames run, its cursor drawn
void rendered() noexcept;      // the overlay drew, or had nothing to draw: the watchdog's heartbeat
void reserve_imgui_queues() noexcept;  // at warm-up, so that a holder's input allocates nothing during play
// Just before Dear ImGui's NewFrame: the holder's input, the pad included, in back-buffer pixels; or nothing.
void before_imgui_frame(std::uint32_t width, std::uint32_t height) noexcept;
void after_new_frame() noexcept;  // test builds: what Dear ImGui received

#if defined(GRACEUI_TEST_EXPORTS)
void test_set_world(std::uint32_t flags) noexcept;  // a test build's own claims (GRACEUI_WORLD_*), or-ed with the mods'
void test_set_code_page(std::uint32_t code_page) noexcept;  // 0: the keyboard layout's again
std::uint32_t test_events_dropped() noexcept;
#endif

}  // namespace grace::host::input
