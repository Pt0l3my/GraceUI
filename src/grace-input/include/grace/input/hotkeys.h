// hotkeys.h: the mods' hotkeys (D77, D155): a named action with a default key and modifiers, which GraceUI watches for
// in the game's window procedure and hides from the game. Pure: no Win32 and no Dear ImGui, so the rules are tested on
// Linux.
//
// A mod's thread adds and removes hotkeys and takes its presses (poll); the game's window thread matches a fresh press
// against them and counts it; DirectInput's readers read which keys to hide. Each hotkey is one 64-bit word (its state,
// key, modifiers, generation and the presses not yet taken), so the window thread and DirectInput's readers never wait
// and never meet a hotkey half written: they load the word, and a press is counted by a compare-and-swap that fails if
// the hotkey was removed or replaced meanwhile. The mods' threads take a short spin lock among themselves.
//
// A default that another hotkey has taken (the same key and modifiers), or the menu key, registers unbound (D77): the
// hotkey exists, and fires nothing until the player binds it on GraceUI's Hotkeys page (GraceUI 0.6). A key bound with
// no modifier is never the game's while bound, as the menu key is not; a press with modifiers is the hotkey's from its
// press to its release. Hotkeys stand aside while any holder has input (§5.2).
#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "grace/input/keys.h"

namespace grace::input {

inline constexpr std::uint32_t kMaxHotkeys = 64;     // the session's (EST; GRACEUI_HOTKEYS_MAX)
inline constexpr std::uint32_t kHotkeysPerMod = 8;   // a mod's (EST; GRACEUI_HOTKEYS_PER_MOD)
inline constexpr std::size_t kActionMax = 32;        // bytes of an action's id

// The modifiers a hotkey names, as graceui.h's GRACEUI_MOD_* give them.
inline constexpr std::uint8_t kModCtrl = 1;
inline constexpr std::uint8_t kModShift = 2;
inline constexpr std::uint8_t kModAlt = 4;
inline constexpr std::uint8_t kModAll = 7;

// The keys a hotkey can never take (DIK codes): Escape (the player's exit) and the modifiers themselves.
bool reserved_key(Key key) noexcept;

enum class Added : std::uint8_t {
  bound = 0,      // its default is bound: a press fires it
  unbound = 1,    // no default, or its default is taken (by another hotkey, or the menu key): it fires nothing yet
  invalid = 2,    // a reserved key, an unknown modifier, Alt+F4, or an action already the mod's
  full = 3,       // kHotkeysPerMod of the mod's, or kMaxHotkeys in the session
};

struct Press {
  std::uint32_t id = 0;     // the hotkey's id: never 0
  std::uint32_t count = 0;  // presses since its last one was taken
};

class Hotkeys {
 public:
  constexpr Hotkeys() = default;
  Hotkeys(const Hotkeys&) = delete;
  Hotkeys& operator=(const Hotkeys&) = delete;

  // ---- Mods' threads.

  // A hotkey of `owner`'s for `action` (checked by the caller: 1 to kActionMax bytes), its default `key` (kNoKey for
  // none) with `modifiers`. `menu_key` is the host's, which no hotkey takes. `id` is set with bound and unbound.
  Added add(std::uint16_t owner, std::string_view action, Key key, std::uint8_t modifiers, Key menu_key,
            std::uint32_t& id) noexcept;

  // The mod has gone: its hotkeys and their presses go with it.
  void remove_owner(std::uint16_t owner) noexcept;
  // One hotkey, if `id` still names it.
  void remove(std::uint32_t id) noexcept;

  // The first of `owner`'s hotkeys, in the table's order, with presses waiting, its presses taken; false if none has
  // any.
  bool take(std::uint16_t owner, Press& out) noexcept;

  // ---- The game's window thread.

  // The bound hotkey whose key and modifiers these are exactly, or 0.
  std::uint32_t match(Key key, std::uint8_t modifiers) const noexcept;
  // Counts a press of `id`; false if it was removed meanwhile.
  bool pressed(std::uint32_t id) noexcept;
  // A press with modifiers that a hotkey took: hidden from DirectInput's readers until released (held()).
  void hold(Key key) noexcept;
  void let_go(Key key) noexcept;
  void let_go_all() noexcept;

  // ---- Any thread.

  // The keys bound with no modifier: never the game's while bound (the window's messages and DirectInput).
  KeyBits bare() const noexcept { return bare_.load(); }
  // Every bound key, whatever its modifiers: a press of any other fires nothing, so its modifiers need not be read.
  KeyBits bound_keys() const noexcept { return bound_.load(); }
  // The keys a hotkey's press holds now (hold()).
  KeyBits held() const noexcept { return held_.load(); }
  std::uint32_t count() const noexcept;  // hotkeys added and not yet removed, bound or not

 private:
  // A hotkey's word: bits 0-1 its state, 2-9 its key, 10-12 its modifiers, 13-31 its generation, 32-63 its presses.
  enum State : std::uint64_t { free = 0, bound = 1, unbound = 2 };
  static constexpr std::uint64_t pack(State state, Key key, std::uint8_t modifiers, std::uint32_t generation) noexcept {
    return std::uint64_t{state} | (std::uint64_t{key} << 2) | (std::uint64_t{static_cast<std::uint8_t>(modifiers & kModAll)} << 10) |
           (std::uint64_t{generation & 0x7FFFFu} << 13);
  }
  static constexpr State state_of(std::uint64_t word) noexcept { return static_cast<State>(word & 3u); }
  static constexpr Key key_of(std::uint64_t word) noexcept { return static_cast<Key>((word >> 2) & 0xFFu); }
  static constexpr std::uint8_t modifiers_of(std::uint64_t word) noexcept {
    return static_cast<std::uint8_t>((word >> 10) & kModAll);
  }
  static constexpr std::uint32_t generation_of(std::uint64_t word) noexcept {
    return static_cast<std::uint32_t>((word >> 13) & 0x7FFFFu);
  }
  static constexpr std::uint32_t presses_of(std::uint64_t word) noexcept { return static_cast<std::uint32_t>(word >> 32); }
  static constexpr std::uint32_t id_of(std::uint32_t index, std::uint32_t generation) noexcept {
    return (generation << 8) | (index + 1);
  }

  void lock() noexcept;
  void unlock() noexcept { lock_.clear(std::memory_order_release); }
  void update_bare() noexcept;  // under the lock

  struct Slot {
    std::atomic<std::uint64_t> word{0};
    std::uint16_t owner = 0;            // under the lock
    std::uint8_t action_length = 0;     // under the lock
    char action[kActionMax] = {};       // under the lock
  };

  std::atomic_flag lock_{};
  std::uint32_t generation_ = 0;  // under the lock
  Slot slots_[kMaxHotkeys];
  AtomicKeyBits bare_;
  AtomicKeyBits bound_;
  AtomicKeyBits held_;
};

}  // namespace grace::input
