// messages.h: the rules of GraceUI's filter on the game's window procedure (§5.2, D69, D125): which window messages
// reach the game, which go to the holder, and what the menu key and Escape do. The window's own thread only.
//
// While a holder takes the keyboard, its messages are GraceUI's: keys, characters and IME messages never reach the
// game (Alt+F4 excepted, so the game can always be closed). A press that began while the holder had input stays the
// holder's to its end: its repeats, characters and release are kept from the game after the holder ends (invariant
// 8), and a release the game saw begin always reaches it. A holder gets a key's messages only from a press it saw
// begin, so the press held when it took input does nothing in it until released (the opening press). The menu key
// never reaches the game (§5.1) and never a holder: it is the host's. The mouse and raw input follow the same way.
//
// The mods' hotkeys (D77, D155), once set_hotkeys() names their table: with no holder, a fresh press of a hotkey's key
// with exactly its modifiers fires it (Decision::hotkey), and the press, its repeats, characters and release are the
// hotkey's, never the game's. A key bound with no modifier is never the game's while bound, whatever the modifiers and
// whether or not a holder has input; with a holder that takes the keyboard it goes to the holder as any key does, and
// fires nothing (hotkeys stand aside). A press the game saw begin keeps reaching the game to its release.
#pragma once

#include <cstdint>

#include "grace/input/devices.h"
#include "grace/input/holder.h"
#include "grace/input/hotkeys.h"
#include "grace/input/keys.h"

namespace grace::input {

enum class Route : std::uint8_t {
  game = 0,          // to the game's window procedure, as if GraceUI were not there
  consume = 1,       // GraceUI's: the game never sees it
  default_proc = 2,  // to DefWindowProc only, never to the game (IME messages and raw input while a holder has input)
};

struct Decision {
  Route route = Route::game;
  bool to_holder = false;
  MenuKey menu = MenuKey::nothing;  // what a fresh press of the menu key did
  Holder closed{};                  // a holder this message ended (the menu key, the player's exit, focus loss)
  std::uint32_t hotkey = 0;         // the hotkey this fresh press fired (counted already), or 0
};

class MessageRules {
 public:
  constexpr MessageRules(Capture& capture, const KeyboardLedger& keyboard, const MouseLedger& mouse,
                         Hotkeys* hotkeys = nullptr) noexcept
      : capture_(&capture), keyboard_(&keyboard), mouse_(&mouse), hotkeys_(hotkeys) {}
  MessageRules(const MessageRules&) = delete;
  MessageRules& operator=(const MessageRules&) = delete;

  void set_menu_key(Key key) noexcept { menu_key_ = key; }
  Key menu_key() const noexcept { return menu_key_; }
  void set_hotkeys(Hotkeys* hotkeys) noexcept { hotkeys_ = hotkeys; }

  // WM_KEYDOWN, WM_KEYUP and their system forms. `repeat` is bit 30 of a key-down's lParam (the key was already down);
  // `modifiers` the Ctrl, Shift and Alt keys held as the message came (kModCtrl and the rest).
  Decision key(Key key, std::uint32_t vk, bool down, bool system, bool repeat, std::int64_t now_ms,
               std::uint8_t modifiers = 0) noexcept;

  // A character for the holder (WM_CHAR, WM_UNICHAR, an ANSI window's WM_IME_CHAR), by the key that made it, or kNoKey.
  Decision character(Key key) noexcept;

  // WM_SYSCHAR, WM_DEADCHAR and WM_SYSDEADCHAR: routed as characters, but never a holder's (a dead key's character
  // arrives with the WM_CHAR after it).
  Decision other_character(Key key) noexcept;

  // WM_xBUTTONDOWN, WM_xBUTTONDBLCLK and WM_xBUTTONUP: 0 left, 1 right, 2 middle, 3 and 4 the X buttons.
  Decision mouse_button(unsigned button, bool down) noexcept;

  Decision mouse_motion() noexcept;               // WM_MOUSEMOVE, WM_MOUSEWHEEL, WM_MOUSEHWHEEL
  Decision ime() noexcept;                        // WM_IME_*
  Decision raw_input(std::uint8_t device) noexcept;  // WM_INPUT from a device of this kind (want_keyboard, want_mouse)

  // The window lost the keyboard focus: the holder ends (focus_lost), and every key and button seen held is forgotten,
  // since no release will come for it. Answers the holder it ended.
  Holder focus_lost(std::int64_t now_ms) noexcept;

  // For the tests: the keys each account holds as down.
  KeyBits game_down() const noexcept { return game_down_; }
  KeyBits captured_down() const noexcept { return captured_down_; }
  KeyBits holder_down() const noexcept { return holder_down_; }
  KeyBits holder_inert() const noexcept { return holder_inert_; }
  KeyBits hotkey_down() const noexcept { return hotkey_down_; }

 private:
  Holder follow() noexcept;  // the holder now; a new one starts its own view of what is down
  bool hotkey_key(Key key, bool down, bool repeat, const Holder& holder, std::uint8_t modifiers,
                  Decision& decision) noexcept;

  Capture* capture_;
  const KeyboardLedger* keyboard_;
  const MouseLedger* mouse_;
  Hotkeys* hotkeys_ = nullptr;
  KeyBits hotkey_down_;    // a hotkey's press, from its press to its release: never the game's
  Key menu_key_ = kF5;
  std::uint32_t followed_ = 0;  // the holder's generation the holder sets below belong to

  KeyBits game_down_;      // pressed with the game seeing it, not yet released
  KeyBits captured_down_;  // pressed while a holder had input, not yet released: never the game's
  KeyBits holder_down_;    // pressed with the holder seeing it
  KeyBits holder_inert_;   // down when the holder took input: nothing of it reaches the holder until released
  std::uint8_t game_buttons_ = 0;
  std::uint8_t captured_buttons_ = 0;
  std::uint8_t holder_buttons_ = 0;
  std::uint8_t holder_inert_buttons_ = 0;
};

}  // namespace grace::input
