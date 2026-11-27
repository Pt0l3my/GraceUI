// hooks_input.cpp: the input half's hooks (D69, D102, D125), installed by hooks.cpp's one table and never removed.
//
// The filter on the game's window procedure: a subclass made once through SetWindowLongPtr with the window's own
// character set (an ANSI window stays ANSI, so later subclasses see what they would without GraceUI), which routes each
// message by grace-input's rules: to the game's procedure, to GraceUI alone, or to DefWindowProc alone; and hands the
// holder its keys, characters (surrogates paired, an ANSI window's bytes decoded) and mouse, and places the IME's
// windows at the holder's caret. DirectInput's GetDeviceState and XInput's XInputGetState: the original first, then
// what the game reads rewritten in place by the rules (at rest while a holder takes the device, a closing press hidden
// until released), and the pads' real state kept for the holder's own navigation.
//
// SAFETY: every function here that the game reaches is a filter or a detour: noexcept, with its own work in a
// catch-all that turns input off for the session, and the original called with the arguments it was given, exactly
// once (the window procedure: at most once, for a message GraceUI keeps). The pointers the game hands the detours (a
// device, the state buffer it asked to be filled, an XINPUT_STATE) are used only within the call, within the size the
// call itself gives, and only after the original has filled them. The casts turn function addresses into the values
// MinHook and SetWindowLongPtr take, and back into the type CallWindowProc takes.
#include "hooks_input.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <imm.h>
#include <xinput.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "counters.h"
#include "grace/input/chars.h"
#include "grace/input/keys.h"
#include "hooks_vtable.h"
#include "input.h"
#include "log.h"
#include "menu.h"
#include "wake.h"

namespace grace::host::hooks_input {
namespace {

using grace::input::Decision;
using grace::input::Key;
using grace::input::Route;
using input::HolderEvent;

constexpr std::uint32_t kCostWindow = 0;
constexpr std::uint32_t kCostDirectInput = 1;
constexpr std::uint32_t kCostXInput = 2;

std::int64_t ticks() noexcept {
#if defined(GRACEUI_TEST_EXPORTS)
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return now.QuadPart;
#else
  return 0;
#endif
}

void record([[maybe_unused]] std::uint32_t kind, [[maybe_unused]] std::int64_t start) noexcept {
#if defined(GRACEUI_TEST_EXPORTS)
  LARGE_INTEGER now{};
  LARGE_INTEGER frequency{};
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&frequency);
  const double ns = static_cast<double>(now.QuadPart - start) * 1e9 / static_cast<double>(frequency.QuadPart);
  counters::record_input_cost(kind, ns > 0 ? static_cast<std::uint64_t>(ns) : 0);
#endif
}

// ---- The window procedure's filter (D125).

// Per window filtered (kWindowFilters): the window, and the procedure its messages go on to. Only the newest is active
// (input::window()); one the filter has left passes every message on, untouched. GraceUI's thread writes a slot before
// SetWindowLongPtr makes its filter reachable, and never again.
constinit std::atomic<HWND> g_filtered[kWindowFilters] = {};
constinit std::atomic<LONG_PTR> g_previous[kWindowFilters] = {};
constinit std::atomic<int> g_filter_count{0};

// The window thread's: a character's halves and bytes, joined for the holder they arrive for.
constinit grace::input::Utf16Pairer g_pairer;
constinit grace::input::DbcsJoiner g_joiner;
constinit grace::input::Utf8Joiner g_utf8;  // an ANSI window whose code page is UTF-8 (L13)
constinit std::uint32_t g_char_generation = 0;

struct Outcome {
  Route route = Route::game;
  LRESULT result = 0;  // what GraceUI answers for a message it keeps
  // For the spikes' counters (D175): the device an input message belongs to (0 for another message), and what kind of
  // input it is (counters::Passed; -1 for one that may always reach the game, Alt+F4's press).
  std::uint8_t device = 0;
  int passed = -1;
};

std::uint32_t keyboard_code_page(HWND window) noexcept {
  // SAFETY: an HKL is a value, not a pointer to anything: its low word is the input language.
  const auto layout = reinterpret_cast<std::uintptr_t>(GetKeyboardLayout(GetWindowThreadProcessId(window, nullptr)));
  const LCID locale = static_cast<LCID>(layout & 0xFFFFu);  // the language's own sort order (SORT_DEFAULT)
  UINT page = 0;
  // SAFETY: with LOCALE_RETURN_NUMBER, GetLocaleInfoA writes one number into the 4 bytes it is given.
  if (GetLocaleInfoA(locale, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, reinterpret_cast<LPSTR>(&page),
                     sizeof page) == 0 ||
      page == 0) {
    page = GetACP();
  }
  return page;
}

// The window procedure's own, while it hands on a character message: bit 30 of its lParam, the key already down (an
// auto-repeat's character), for the menu (D270).
bool g_char_repeat = false;

void push_code_points(const char32_t* points, int count, std::uint32_t generation) noexcept {
  if (const grace::input::Holder holder = input::capture().holder();
      holder.kind == grace::input::Kind::menu && holder.owner == 0) {
    for (int i = 0; i < count; ++i) {
      menu::character(points[i], g_char_repeat);  // the menu's own field, or its search (G38 (a))
    }
    return;
  }
  for (int i = 0; i < count; ++i) {
    HolderEvent event;
    event.type = HolderEvent::Type::character;
    event.generation = generation;
    event.a = static_cast<std::int32_t>(points[i]);
    input::push_event(event);
  }
}

void follow_characters(std::uint32_t generation) noexcept {
  if (generation != g_char_generation) {
    char32_t dropped[2];
    g_pairer.flush(dropped);  // a half left from the last holder is not this one's
    g_joiner.reset();
    g_utf8.reset();
    g_char_generation = generation;
  }
}

void push_utf16(std::uint16_t unit, std::uint32_t generation) noexcept {
  char32_t points[2];
  push_code_points(points, g_pairer.push(unit, points), generation);
}

void push_byte(std::uint8_t byte, std::uint32_t generation) noexcept {
  const UINT page = input::code_page();
  if (page == CP_UTF8) {
    char32_t points[2];
    push_code_points(points, g_utf8.push(byte, points), generation);
    return;
  }
  std::uint8_t bytes[2];
  const int count = g_joiner.push(byte, IsDBCSLeadByteEx(page, byte) != FALSE, bytes);
  if (count == 0) {
    return;
  }
  wchar_t units[2] = {};
  // SAFETY: MultiByteToWideChar reads the `count` bytes (1 or 2) of `bytes` and writes at most 2 units.
  const int decoded = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(bytes), count,
                                          units, 2);
  if (decoded <= 0) {
    const char32_t replacement = grace::input::kReplacement;
    push_code_points(&replacement, 1, generation);
    return;
  }
  for (int i = 0; i < decoded; ++i) {
    push_utf16(static_cast<std::uint16_t>(units[i]), generation);
  }
}

// A character message's wParam, for the holder: a UTF-16 unit on a Unicode window; on an ANSI window a byte, or both
// bytes of a double-byte character (lead byte high) as WM_IME_CHAR carries them.
void push_character(WPARAM wparam, bool unicode) noexcept {
  const std::uint32_t generation = input::capture().holder().generation;
  follow_characters(generation);
  if (unicode) {
    push_utf16(static_cast<std::uint16_t>(wparam & 0xFFFFu), generation);
    return;
  }
  const auto high = static_cast<std::uint8_t>((wparam >> 8) & 0xFFu);
  if (high != 0) {
    push_byte(high, generation);
  }
  push_byte(static_cast<std::uint8_t>(wparam & 0xFFu), generation);
}

void push_whole_character(WPARAM wparam) noexcept {
  const std::uint32_t generation = input::capture().holder().generation;
  follow_characters(generation);
  char32_t points[2];
  const char32_t point = grace::input::from_utf32(static_cast<std::uint32_t>(wparam));
  push_code_points(points, g_pairer.push_whole(point, points), generation);
}

std::uint8_t modifiers_now() noexcept;

void push_key(Key key, std::uint32_t vk, bool down, bool repeat) noexcept {
  if (const grace::input::Holder holder = input::capture().holder();
      holder.kind == grace::input::Kind::menu && holder.owner == 0) {
    // The menu's keys go to GraceUI's thread as the page view's inputs (D167), never to Dear ImGui; its modifiers are
    // read only for its own presses.
    if (down) {
      menu::key_down(vk, key, (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                     (GetKeyState(VK_MENU) & 0x8000) != 0, repeat);
    }
    return;
  }
  HolderEvent event;
  event.type = HolderEvent::Type::key;
  event.key = key;
  event.vk = vk;
  event.down = down ? 1 : 0;
  event.generation = input::capture().holder().generation;
  // Its modifiers and whether it repeats, read only while a mod takes its holder's events (D176): GetKeyState can cost
  // a call into Wine's server.
  if (const grace::input::HolderMail* mail = input::holder_mail(); mail != nullptr && mail->active(event.generation)) {
    event.extra = static_cast<std::uint8_t>(modifiers_now() | (repeat ? input::kExtraRepeat : 0));
  }
  input::push_event(event);
}

void push_mouse(HolderEvent::Type type, std::uint8_t button, bool down, std::int32_t a, std::int32_t b) noexcept {
  HolderEvent event;
  event.type = type;
  event.key = button;
  event.down = down ? 1 : 0;
  event.a = a;
  event.b = b;
  event.generation = input::capture().holder().generation;
  input::push_event(event);
}

std::int32_t low_signed(LPARAM value) noexcept { return static_cast<std::int16_t>(value & 0xFFFF); }
std::int32_t high_signed(LPARAM value) noexcept { return static_cast<std::int16_t>((value >> 16) & 0xFFFF); }

// The IME's composition window at the holder's caret, and its candidate list below the caret's line (§5.6, D57).
void place_ime(HWND window) noexcept {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t height = 0;
  if (!input::caret_in_client(x, y, height)) {
    return;
  }
  const HIMC context = ImmGetContext(window);
  if (context == nullptr) {
    return;  // the game has taken the window's input context away: no IME here (S4)
  }
  COMPOSITIONFORM composition{};
  composition.dwStyle = CFS_FORCE_POSITION;
  composition.ptCurrentPos = POINT{x, y};
  ImmSetCompositionWindow(context, &composition);
  CANDIDATEFORM candidate{};
  candidate.dwIndex = 0;
  candidate.dwStyle = CFS_CANDIDATEPOS;
  candidate.ptCurrentPos = POINT{x, y + height};
  ImmSetCandidateWindow(context, &candidate);
  ImmReleaseContext(window, context);
}

void lost_focus(std::int64_t now) noexcept {
  input::focus_gone();
  const grace::input::Holder before = input::capture().holder();
  if (before.kind == grace::input::Kind::menu && before.owner == 0 && before.phase == grace::input::Phase::active) {
    input::set_menu_away();  // G40: the menu lets go of input but stays, and asks again when the focus comes back
  }
  input::closed(input::messages().focus_lost(now));
  char32_t dropped[2];
  g_pairer.flush(dropped);
  g_joiner.reset();
  g_utf8.reset();
}

// The Ctrl, Shift and Alt keys as the window's thread sees them for the message in hand (its message queue's key
// state, which GetKeyState reads), for the hotkeys' modifiers (D155).
std::uint8_t modifiers_now() noexcept {
  std::uint8_t modifiers = 0;
  if (GetKeyState(VK_CONTROL) < 0) {
    modifiers |= grace::input::kModCtrl;
  }
  if (GetKeyState(VK_SHIFT) < 0) {
    modifiers |= grace::input::kModShift;
  }
  if (GetKeyState(VK_MENU) < 0) {
    modifiers |= grace::input::kModAlt;
  }
  return modifiers;
}

void handled(const Decision& decision) noexcept {
  if (decision.menu != grace::input::MenuKey::nothing) {
    input::note_menu_key(decision.menu);
  }
  input::closed(decision.closed);
}

int mouse_button_of(UINT message, WPARAM wparam) noexcept {
  switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP: return 0;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP: return 1;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP: return 2;
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP: return ((wparam >> 16) & 0xFFFFu) == XBUTTON1 ? 3 : 4;
    default: return -1;
  }
}

Outcome route(HWND window, UINT message, WPARAM wparam, LPARAM lparam, bool unicode) noexcept {
  grace::input::MessageRules& rules = input::messages();
  const std::int64_t now = input::now_ms();
  const auto bits = static_cast<std::uint32_t>(lparam);
  Outcome out;
  switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
      const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
      const bool system = message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
      const auto vk = static_cast<std::uint32_t>(wparam);
      const Key key = grace::input::key_from_message(bits, vk);
      const bool repeat = down && (bits & (1u << 30)) != 0;
      // The modifiers are read only for a fresh press of a key some hotkey binds, with no holder: GetKeyState can cost
      // a call into Wine's server (100 us and more under Xvfb), and no other message has a use for them.
      const std::uint8_t modifiers = down && !repeat && key != grace::input::kNoKey &&
                                             input::hotkeys().bound_keys().test(key) &&
                                             !input::capture().holder().holds()
                                         ? modifiers_now()
                                         : 0;
      const Decision decision = rules.key(key, vk, down, system, repeat, now, modifiers);
      handled(decision);
      if (decision.hotkey != 0) {
        wake::signal(input::hotkeys().owner_of(decision.hotkey));  // its mod's poll has a press (D212)
      }
      if (decision.to_holder) {
        push_key(key, vk, down, repeat);
      }
      out.route = decision.route;
      out.device = grace::input::want_keyboard;
      out.passed = !down ? 1 : system && vk == VK_F4 ? -1 : 0;  // release, press; Alt+F4 always reaches the game
      return out;
    }
    case WM_CHAR: {
      const Decision decision = rules.character(grace::input::key_from_message(bits, 0));
      if (decision.to_holder) {
        g_char_repeat = (bits & (1u << 30)) != 0;
        push_character(wparam, unicode);
        g_char_repeat = false;
      }
      out.route = decision.route;
      out.device = grace::input::want_keyboard;
      out.passed = 2;
      return out;
    }
    case WM_DEADCHAR:
    case WM_SYSCHAR:
    case WM_SYSDEADCHAR:
      out.route = rules.other_character(grace::input::key_from_message(bits, 0)).route;
      out.device = grace::input::want_keyboard;
      out.passed = 2;
      return out;
    case WM_UNICHAR: {
      if (wparam == UNICODE_NOCHAR) {
        // Whether the window takes WM_UNICHAR: GraceUI does while a holder has the keyboard.
        if (input::capture().holder().takes(grace::input::want_keyboard)) {
          out.route = Route::consume;
          out.result = TRUE;
        }
        return out;
      }
      const Decision decision = rules.character(grace::input::kNoKey);
      if (decision.to_holder) {
        push_whole_character(wparam);
      }
      out.route = decision.route;
      return out;
    }
    case WM_IME_CHAR: {
      if (unicode) {
        out.route = rules.ime().route;  // DefWindowProcW sends it back as WM_CHAR, which comes here again
        return out;
      }
      // An ANSI window's double-byte character arrives whole here: GraceUI decodes it rather than letting
      // DefWindowProcA split it into two WM_CHARs.
      const Decision decision = rules.character(grace::input::kNoKey);
      if (decision.to_holder) {
        push_character(wparam, false);
      }
      out.route = decision.route;
      return out;
    }
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
      out.route = rules.ime().route;
      if (out.route == Route::default_proc) {
        place_ime(window);
      }
      return out;
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_NOTIFY:
    case WM_IME_SETCONTEXT:
    case WM_IME_REQUEST:
    case WM_IME_SELECT:
    case WM_IME_CONTROL:
    case WM_IME_COMPOSITIONFULL:
    case WM_IME_KEYDOWN:
    case WM_IME_KEYUP:
      out.route = rules.ime().route;
      return out;
    case WM_MOUSEMOVE: {
      const Decision decision = rules.mouse_motion();
      // While the mouse's own motion drives the pointer (D232), the cursor's position is the game's to set and not
      // where the player points: a game that recentres its cursor would pull the pointer back with it.
      if (decision.to_holder && !input::pointer_by_motion()) {
        push_mouse(HolderEvent::Type::mouse_move, 0, false, low_signed(lparam), high_signed(lparam));
      }
      out.route = decision.route;
      out.device = grace::input::want_mouse;
      out.passed = 3;
      return out;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
      const Decision decision = rules.mouse_motion();
      if (decision.to_holder) {
        const auto delta = static_cast<std::int16_t>((wparam >> 16) & 0xFFFFu);
        push_mouse(HolderEvent::Type::wheel, 0, false, message == WM_MOUSEWHEEL ? delta : 0,
                   message == WM_MOUSEHWHEEL ? delta : 0);
      }
      out.route = decision.route;
      out.device = grace::input::want_mouse;
      out.passed = 3;
      return out;
    }
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP: {
      const int button = mouse_button_of(message, wparam);
      const bool down = message != WM_LBUTTONUP && message != WM_RBUTTONUP && message != WM_MBUTTONUP &&
                        message != WM_XBUTTONUP;
      const Decision decision = rules.mouse_button(static_cast<unsigned>(button), down);
      if (decision.to_holder) {
        push_mouse(HolderEvent::Type::mouse_button, static_cast<std::uint8_t>(button), down, 0, 0);
      }
      out.route = decision.route;
      out.result = message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK || message == WM_XBUTTONUP ? TRUE : 0;
      out.device = grace::input::want_mouse;
      out.passed = down ? 3 : -1;  // a button's release reaches the game as a key's does
      return out;
    }
    case WM_INPUT: {
      if (!input::capture().holder().holds()) {
        return out;  // the game's, as every message is with no holder: its header is read only while one holds input
      }
      RAWINPUTHEADER header{};
      UINT size = sizeof header;
      // SAFETY: WM_INPUT's lParam is the handle of the raw input it announces; only its header is copied, into ours.
      if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_HEADER, &header, &size, sizeof header) ==
          sizeof header) {
        const std::uint8_t device = header.dwType == RIM_TYPEMOUSE      ? grace::input::want_mouse
                                    : header.dwType == RIM_TYPEKEYBOARD ? grace::input::want_keyboard
                                                                        : 0;
        if (device != 0) {
          out.route = rules.raw_input(device).route;
          out.device = device;
          out.passed = 4;
        }
        if (device == grace::input::want_mouse) {
          // The mouse's relative motion, for the pointer while a holder has the mouse (D232); an absolute device's
          // (a tablet, a remote desktop) moves the cursor, which WM_MOUSEMOVE reports.
          RAWINPUT raw{};
          UINT raw_size = sizeof raw;
          // SAFETY: as above; the input is copied into ours, at most sizeof raw bytes.
          if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &raw_size, sizeof header) !=
              static_cast<UINT>(-1)) {
            if ((raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
              input::relative_motion(input::MotionSource::raw_input, raw.data.mouse.lLastX, raw.data.mouse.lLastY);
            } else {
              input::absolute_motion();  // a tablet or a remote desktop: WM_MOUSEMOVE's position is where it points
            }
          }
        }
      }
      return out;
    }
    case WM_KILLFOCUS:
      lost_focus(now);
      return out;
    case WM_SETFOCUS:
      input::focus_back();  // the menu, if it was away, takes input again (G40)
      return out;
    case WM_ACTIVATEAPP:
      if (wparam == FALSE) {
        lost_focus(now);
      }
      return out;
    case WM_SIZE:
      input::set_client_size(static_cast<std::int32_t>(lparam & 0xFFFF), static_cast<std::int32_t>((lparam >> 16) & 0xFFFF));
      input::window_moved();
      return out;
    case WM_MOVE:
      input::window_moved();
      return out;
    case WM_INPUTLANGCHANGE:
      input::set_keyboard_code_page(keyboard_code_page(window));
      g_joiner.reset();
      g_utf8.reset();
      return out;
    default:
      return out;
  }
}

#if defined(GRACEUI_TEST_EXPORTS)
// D238: the window's focus messages, and with no holder the presses that reach the game.
void count_window(UINT message, WPARAM wparam, LPARAM lparam, const Outcome& outcome) noexcept {
  switch (message) {
    case WM_SETFOCUS: counters::count_focus(counters::focus_gained); return;
    case WM_KILLFOCUS: counters::count_focus(counters::focus_lost); return;
    case WM_ACTIVATEAPP: counters::count_focus(wparam != FALSE ? counters::app_active : counters::app_inactive); return;
    default: break;
  }
  if (outcome.route != Route::game || input::capture().holder().holds()) {
    return;
  }
  if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && (lparam & (LPARAM{1} << 30)) == 0) {
    counters::count_free_down(counters::window_key);
  } else if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
             message == WM_XBUTTONDOWN) {
    counters::count_free_down(counters::window_button);
  }
}

// D238: what a thread's previous DirectInput read gave the game, to count the downs that follow.
constinit thread_local std::uint8_t t_keys_given[256] = {};
constinit thread_local std::uint8_t t_buttons_given[8] = {};
// D239: when this thread last read each kind of device, in QueryPerformanceCounter's ticks (0 before its first read).
constinit thread_local std::int64_t t_last_read[counters::kPollKinds] = {};

void note_read(counters::Poll kind, std::int64_t now_ticks) noexcept {
  const std::int64_t last = t_last_read[kind];
  t_last_read[kind] = now_ticks;
  if (last == 0) {
    return;
  }
  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  if (frequency.QuadPart > 0 && now_ticks > last) {
    counters::record_poll_gap(kind, static_cast<std::uint64_t>((now_ticks - last) * 1'000'000 / frequency.QuadPart));
  }
}
#endif

template <bool kUnicode, int kSlot>
LRESULT CALLBACK window_filter(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
  Outcome outcome;
  // A window the filter has left (the bound swap chain's window turned out to be another, M2) passes untouched; so
  // does every window once input is off.
  if (!input::off() && window == input::window()) {
    const std::int64_t start = ticks();
    {
      const counters::CountingScope counting;
      try {
        outcome = route(window, message, wparam, lparam, kUnicode);
#if defined(GRACEUI_TEST_EXPORTS)
        if (outcome.route == Route::game && outcome.passed >= 0 && input::capture().holder().takes(outcome.device)) {
          counters::count_passed(static_cast<counters::Passed>(outcome.passed));
        }
        count_window(message, wparam, lparam, outcome);
#endif
        input::tick();
      } catch (...) {
        input::fault("an exception in the window procedure's filter");
        outcome = Outcome{};
      }
    }
    record(kCostWindow, start);
  }
  switch (outcome.route) {
    case Route::consume: return outcome.result;
    case Route::default_proc:
      return kUnicode ? DefWindowProcW(window, message, wparam, lparam) : DefWindowProcA(window, message, wparam, lparam);
    case Route::game: break;
  }
  // SAFETY: the value SetWindowLongPtr answered at install: the game's procedure, or a handle only CallWindowProc reads.
  const auto previous = reinterpret_cast<WNDPROC>(g_previous[kSlot].load(std::memory_order_acquire));
  return kUnicode ? CallWindowProcW(previous, window, message, wparam, lparam)
                  : CallWindowProcA(previous, window, message, wparam, lparam);
}

// ---- DirectInput's GetDeviceState.

using GetDeviceStateFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPVOID);
constinit GetDeviceStateFn g_get_device_state[kDirectInputTargets] = {};
constinit thread_local int t_dinput_depth = 0;
constinit std::atomic<bool> g_told_other_read{false};

void filter_device_state(IDirectInputDevice8W* device, DWORD size, void* data) noexcept {
  const std::int64_t start = ticks();
  {
    const counters::CountingScope counting;
    try {
      DIDEVCAPS caps{};
      caps.dwSize = sizeof caps;
      if (FAILED(device->GetCapabilities(&caps))) {
        return;
      }
      const auto type = static_cast<std::uint8_t>(caps.dwDevType & 0xFFu);
      const std::int64_t now = input::now_ms();
      if (type == DI8DEVTYPE_KEYBOARD && size == 256) {
#if defined(GRACEUI_TEST_EXPORTS)
        note_read(counters::poll_keyboard, start);
#endif
        // SAFETY: DirectInput has just written the caller's 256 bytes at `data`, c_dfDIKeyboard's layout.
        auto& keys = *static_cast<std::uint8_t(*)[256]>(data);
        input::capture().escape(grace::input::Source::direct_input, (keys[grace::input::kEscape] & 0x80u) != 0, now);
#if defined(GRACEUI_TEST_EXPORTS)
        std::uint8_t before[256];
        std::memcpy(before, keys, sizeof before);
#endif
        input::keyboard().filter(keys, input::capture(), input::hidden_keys(), now);
#if defined(GRACEUI_TEST_EXPORTS)
        if (std::memcmp(before, keys, sizeof before) != 0) {
          counters::count_masked(counters::dinput_keyboard, input::capture().holder().holds());
        }
        if (!input::capture().holder().holds()) {
          std::uint32_t downs = 0;
          for (unsigned k = 0; k < 256; ++k) {
            downs += (keys[k] & 0x80u) != 0 && (t_keys_given[k] & 0x80u) == 0 ? 1u : 0u;
          }
          if (downs != 0) {
            counters::count_free_down(counters::dinput_key, downs);
          }
        }
        std::memcpy(t_keys_given, keys, sizeof t_keys_given);
#endif
      } else if (type == DI8DEVTYPE_MOUSE && (size == sizeof(DIMOUSESTATE) || size == sizeof(DIMOUSESTATE2))) {
#if defined(GRACEUI_TEST_EXPORTS)
        note_read(counters::poll_mouse, start);
#endif
        DIMOUSESTATE2 mouse{};
        // SAFETY: DirectInput has just written `size` bytes at `data`: a DIMOUSESTATE or a DIMOUSESTATE2, whose first
        // `size` bytes DIMOUSESTATE2 shares.
        std::memcpy(&mouse, data, size);
        input::relative_motion(input::MotionSource::direct_input, mouse.lX, mouse.lY);  // before it is blanked (D232)
        grace::input::MouseState view;
        view.x = mouse.lX;
        view.y = mouse.lY;
        view.z = mouse.lZ;
        view.button_count = size == sizeof(DIMOUSESTATE2) ? 8 : 4;
        for (unsigned i = 0; i < 8; ++i) {
          view.buttons[i] = mouse.rgbButtons[i];
        }
        input::mouse().filter(view, input::capture(), now);
#if defined(GRACEUI_TEST_EXPORTS)
        bool changed = view.x != mouse.lX || view.y != mouse.lY || view.z != mouse.lZ;
        for (unsigned i = 0; i < 8; ++i) {
          changed = changed || view.buttons[i] != mouse.rgbButtons[i];
        }
        if (changed) {
          counters::count_masked(counters::dinput_mouse, input::capture().holder().holds());
        }
        if (!input::capture().holder().holds()) {
          std::uint32_t downs = 0;
          for (unsigned i = 0; i < 8; ++i) {
            downs += (view.buttons[i] & 0x80u) != 0 && (t_buttons_given[i] & 0x80u) == 0 ? 1u : 0u;
          }
          if (downs != 0) {
            counters::count_free_down(counters::dinput_button, downs);
          }
        }
        for (unsigned i = 0; i < 8; ++i) {
          t_buttons_given[i] = view.buttons[i];
        }
#endif
        mouse.lX = view.x;
        mouse.lY = view.y;
        mouse.lZ = view.z;
        for (unsigned i = 0; i < 8; ++i) {
          mouse.rgbButtons[i] = view.buttons[i];
        }
        std::memcpy(data, &mouse, size);  // SAFETY: back into the same `size` bytes
      } else if (!g_told_other_read.exchange(true, std::memory_order_relaxed)) {
        log_line("a DirectInput read of another device or format passes untouched (device type %u, %lu bytes)",
                 static_cast<unsigned>(type), static_cast<unsigned long>(size));
      }
      input::tick();
    } catch (...) {
      input::fault("an exception in the DirectInput detour");
    }
  }
  record(kCostDirectInput, start);
}

template <int kSlot>
HRESULT STDMETHODCALLTYPE device_state_detour(IDirectInputDevice8W* device, DWORD size, LPVOID data) noexcept {
  ++t_dinput_depth;
  const HRESULT result = g_get_device_state[kSlot](device, size, data);
  // Only the outermost read is rewritten: an implementation's own call into another hooked function passes as it is.
  if (--t_dinput_depth == 0 && SUCCEEDED(result) && data != nullptr && device != nullptr && !input::off()) {
    filter_device_state(device, size, data);
  }
  return result;
}

// ---- XInput's XInputGetState.

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
constinit XInputGetStateFn g_xinput_get_state[kXInputDlls] = {};
constinit thread_local int t_xinput_depth = 0;

void filter_pad(DWORD user, XINPUT_STATE* state, DWORD result) noexcept {
  if (user >= grace::input::kPads) {
    return;
  }
  const std::int64_t start = ticks();
  {
    const counters::CountingScope counting;
    try {
      if (result == ERROR_SUCCESS && state != nullptr) {
        // SAFETY: XInputGetState has just filled the caller's XINPUT_STATE at `state`.
        XINPUT_GAMEPAD& gamepad = state->Gamepad;
        grace::input::Pad pad;
        pad.buttons = gamepad.wButtons;
        pad.left_trigger = gamepad.bLeftTrigger;
        pad.right_trigger = gamepad.bRightTrigger;
        pad.left_x = gamepad.sThumbLX;
        pad.left_y = gamepad.sThumbLY;
        pad.right_x = gamepad.sThumbRX;
        pad.right_y = gamepad.sThumbRY;
        std::uint32_t packet = state->dwPacketNumber;
        if (grace::input::HolderMail* mail = input::holder_mail(); mail != nullptr) {
          // The pad's own buttons, before the ledger masks them: their changes go to a holder's mod that takes the pad
          // and asked for its events (D176).
          const grace::input::Holder holder = input::capture().holder();
          mail->pad(user, pad.buttons, holder.generation,
                    holder.phase == grace::input::Phase::active && holder.takes(grace::input::want_pad));
        }
#if defined(GRACEUI_TEST_EXPORTS)
        const grace::input::Pad before = pad;
#endif
        const std::int64_t now = input::now_ms();
        const std::uint16_t real = pad.buttons;
        input::pads().filter(user, packet, pad, input::capture(), now);
        // The mods' chords (D204): a chord's key held back while it resolves, its tap, both hidden after it fires.
        std::uint32_t fired = 0;
        if (input::chords().filter(user, real, pad.buttons, packet, input::chord_aside(), input::hotkeys(), now,
                                   &fired) == grace::input::ChordStep::fired) {
          wake::signal(input::hotkeys().owner_of(fired));  // its mod's poll has a press (D212)
        }
#if defined(GRACEUI_TEST_EXPORTS)
        if (!(pad == before)) {
          counters::count_masked(counters::xinput, input::capture().holder().holds());
        }
#endif
        state->dwPacketNumber = packet;
        gamepad.wButtons = pad.buttons;
        gamepad.bLeftTrigger = pad.left_trigger;
        gamepad.bRightTrigger = pad.right_trigger;
        gamepad.sThumbLX = pad.left_x;
        gamepad.sThumbLY = pad.left_y;
        gamepad.sThumbRX = pad.right_x;
        gamepad.sThumbRY = pad.right_y;
      } else if (result == ERROR_DEVICE_NOT_CONNECTED) {
        input::pads().disconnected(user);
        input::chords().disconnected(user);
      }
      input::tick();
    } catch (...) {
      input::fault("an exception in the XInput detour");
    }
  }
  record(kCostXInput, start);
}

template <int kSlot>
DWORD WINAPI pad_detour(DWORD user, XINPUT_STATE* state) noexcept {
  ++t_xinput_depth;
  const DWORD result = g_xinput_get_state[kSlot](user, state);
  if (--t_xinput_depth == 0 && !input::off()) {  // as for DirectInput: one XInput DLL may call another
    filter_pad(user, state, result);
  }
  return result;
}

// The filters, by slot and character set: SetWindowLongPtr takes one of these.
template <bool kUnicode>
LONG_PTR filter_for(int slot) noexcept {
  // SAFETY: each filter has WNDPROC's exact signature; the window keeps it for the life of the process (D107).
  const LONG_PTR filters[kWindowFilters] = {reinterpret_cast<LONG_PTR>(&window_filter<kUnicode, 0>),
                                            reinterpret_cast<LONG_PTR>(&window_filter<kUnicode, 1>),
                                            reinterpret_cast<LONG_PTR>(&window_filter<kUnicode, 2>),
                                            reinterpret_cast<LONG_PTR>(&window_filter<kUnicode, 3>)};
  return filters[slot];
}
static_assert(kWindowFilters == 4, "filter_for lists one filter per slot");

}  // namespace

#if defined(GRACEUI_TEST_EXPORTS)
void test_pad(std::uint32_t user, std::uint16_t buttons) noexcept {
  XINPUT_STATE state{};
  state.dwPacketNumber = static_cast<DWORD>(input::now_ms());
  state.Gamepad.wButtons = buttons;
  filter_pad(user, &state, ERROR_SUCCESS);
}

void test_pad_state(std::uint32_t user, std::uint16_t buttons, std::uint8_t left_trigger, std::uint8_t right_trigger,
                    std::int16_t left_x, std::int16_t left_y, std::int16_t right_x, std::int16_t right_y) noexcept {
  XINPUT_STATE state{};
  state.dwPacketNumber = static_cast<DWORD>(input::now_ms());
  state.Gamepad.wButtons = buttons;
  state.Gamepad.bLeftTrigger = left_trigger;
  state.Gamepad.bRightTrigger = right_trigger;
  state.Gamepad.sThumbLX = left_x;
  state.Gamepad.sThumbLY = left_y;
  state.Gamepad.sThumbRX = right_x;
  state.Gamepad.sThumbRY = right_y;
  filter_pad(user, &state, ERROR_SUCCESS);
}

std::uint16_t test_pad_read(std::uint32_t user, std::uint16_t buttons, std::uint32_t& packet) noexcept {
  XINPUT_STATE state{};
  state.dwPacketNumber = packet;
  state.Gamepad.wButtons = buttons;
  filter_pad(user, &state, ERROR_SUCCESS);
  packet = state.dwPacketNumber;
  return state.Gamepad.wButtons;
}
#endif

void pin_module_of(void* address) noexcept {
  HMODULE module = nullptr;
  // SAFETY: the address of a function in a loaded module, read by GetModuleHandleExW only to find the module.
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                     reinterpret_cast<LPCWSTR>(address), &module);
}

int windows_filtered() noexcept { return g_filter_count.load(std::memory_order_acquire); }

void find_targets(Targets& out) noexcept {
  out = Targets{};
  // DirectInput: in the dinput8.dll the game has loaded. GraceUI never loads it itself: a mod loader may stand in its
  // place in the game's folder, and an early load from here would shadow it.
  const HMODULE dinput8 = GetModuleHandleW(L"dinput8.dll");
  const FARPROC create = dinput8 != nullptr ? GetProcAddress(dinput8, "DirectInput8Create") : nullptr;
  grace_host_fn found[GRACE_HOST_DINPUT_TARGETS] = {};
  // SAFETY: an exported function's address, handed to hooks_vtable.c, which calls it as DirectInput8Create.
  if (create != nullptr && grace_host_read_dinput_targets(reinterpret_cast<grace_host_fn>(create), found) != 0) {
    for (int i = 0; i < kDirectInputTargets; ++i) {
      // SAFETY: a function read from a DirectInput device's vtable, handed to MinHook only.
      out.get_device_state[i] = reinterpret_cast<void*>(found[i]);
      if (out.get_device_state[i] != nullptr) {
        pin_module_of(out.get_device_state[i]);
      }
    }
  } else if (dinput8 == nullptr) {
    log_line("DirectInput is not loaded: GetDeviceState is not hooked, and keys read through it are not masked");
  } else {
    log_line("DirectInput's GetDeviceState could not be read: it is not hooked, and keys read through it are not "
             "masked");
  }
  // XInput: in every DLL of these names already loaded, each function once.
  for (int slot = 0; slot < kXInputDlls; ++slot) {
    const HMODULE module = GetModuleHandleW(kXInputNames[slot]);
    const FARPROC function = module != nullptr ? GetProcAddress(module, "XInputGetState") : nullptr;
    // SAFETY: an exported function's address, handed to MinHook only.
    void* const target = reinterpret_cast<void*>(function);
    bool seen = target == nullptr;
    for (int earlier = 0; earlier < slot && !seen; ++earlier) {
      seen = out.xinput_get_state[earlier] == target;
    }
    if (!seen) {
      out.xinput_get_state[slot] = target;
      pin_module_of(target);
    }
  }
}

void* get_device_state_detour(int slot) noexcept {
  // SAFETY: each detour has GetDeviceState's exact signature.
  return slot == 0 ? reinterpret_cast<void*>(&device_state_detour<0>) : reinterpret_cast<void*>(&device_state_detour<1>);
}

void** get_device_state_original(int slot) noexcept {
  // SAFETY: MinHook writes a function address of GetDeviceState's type into this variable.
  return reinterpret_cast<void**>(&g_get_device_state[slot]);
}

void* xinput_detour(int slot) noexcept {
  // SAFETY: each detour has XInputGetState's exact signature.
  void* const detours[kXInputDlls] = {reinterpret_cast<void*>(&pad_detour<0>), reinterpret_cast<void*>(&pad_detour<1>),
                                      reinterpret_cast<void*>(&pad_detour<2>), reinterpret_cast<void*>(&pad_detour<3>),
                                      reinterpret_cast<void*>(&pad_detour<4>)};
  return detours[slot];
}

void** xinput_original(int slot) noexcept {
  // SAFETY: MinHook writes a function address of XInputGetState's type into this variable.
  return reinterpret_cast<void**>(&g_xinput_get_state[slot]);
}

bool filter_window(HWND window) noexcept {
  const bool unicode = IsWindowUnicode(window) != FALSE;
  const int count = g_filter_count.load(std::memory_order_acquire);
  for (int slot = 0; slot < count; ++slot) {
    if (g_filtered[slot].load(std::memory_order_acquire) == window) {
      // Filtered before: its filter becomes the active one again; a second subclass would route its messages twice.
      input::set_keyboard_code_page(keyboard_code_page(window));
      input::set_window(window, unicode);
      return true;
    }
  }
  if (count >= kWindowFilters) {
    log_line("the game's window changed more often than GraceUI follows (%d windows): input stays on the last",
             kWindowFilters);
    return false;
  }
  input::set_keyboard_code_page(keyboard_code_page(window));
  // The procedure the filter passes to is known before the filter can run; SetWindowLongPtr answers it again, and the
  // two differ only if the window's procedure changed in between.
  g_filtered[count].store(window, std::memory_order_release);
  g_previous[count].store(unicode ? GetWindowLongPtrW(window, GWLP_WNDPROC) : GetWindowLongPtrA(window, GWLP_WNDPROC),
                          std::memory_order_release);
  const LONG_PTR filter = unicode ? filter_for<true>(count) : filter_for<false>(count);
  SetLastError(0);
  const LONG_PTR previous = unicode ? SetWindowLongPtrW(window, GWLP_WNDPROC, filter)
                                    : SetWindowLongPtrA(window, GWLP_WNDPROC, filter);
  if (previous == 0 && GetLastError() != 0) {
    log_line("the game's window procedure could not be filtered (error %lu): no holder can have input",
             static_cast<unsigned long>(GetLastError()));
    g_filtered[count].store(nullptr, std::memory_order_release);
    return false;
  }
  g_previous[count].store(previous, std::memory_order_release);
  g_filter_count.store(count + 1, std::memory_order_release);
  input::set_window(window, unicode);
  return true;
}

#if defined(GRACEUI_TEST_EXPORTS)
bool window_lead(const void*& procedure, bool& ours) noexcept {
  const HWND window = input::window();
  const int count = g_filter_count.load(std::memory_order_acquire);
  for (int slot = 0; window != nullptr && slot < count; ++slot) {
    if (g_filtered[slot].load(std::memory_order_acquire) != window) {
      continue;
    }
    const bool unicode = IsWindowUnicode(window) != FALSE;
    const LONG_PTR now = unicode ? GetWindowLongPtrW(window, GWLP_WNDPROC) : GetWindowLongPtrA(window, GWLP_WNDPROC);
    ours = now == (unicode ? filter_for<true>(slot) : filter_for<false>(slot));
    // SAFETY: the window's procedure as an address, read only to find the module it lies in (D240).
    procedure = reinterpret_cast<const void*>(now);
    return true;
  }
  return false;
}
#endif

}  // namespace grace::host::hooks_input
