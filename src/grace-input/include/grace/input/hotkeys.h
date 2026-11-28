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
//
// A hotkey may also have a default pad chord (D204, proposal 0005): an anchor button and a key button, which chords.h
// watches for in XInput's reads. A chord another hotkey has bound registers unbound alike; the key and the chord are
// bound or unbound each on its own, and either fires the hotkey.
#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "grace/input/keys.h"

namespace grace::input {

inline constexpr std::uint32_t kMaxHotkeys = 64;     // the session's (EST; GRACEUI_HOTKEYS_MAX)
inline constexpr std::uint32_t kHotkeysPerMod = 8;   // a mod's (EST; GRACEUI_HOTKEYS_PER_MOD)
inline constexpr std::size_t kActionMax = 32;        // bytes of an action's id
inline constexpr std::size_t kHotkeyNameMax = 64;    // bytes of what the Hotkeys page calls it

// The modifiers a hotkey names, as graceui.h's GRACEUI_MOD_* give them.
inline constexpr std::uint8_t kModCtrl = 1;
inline constexpr std::uint8_t kModShift = 2;
inline constexpr std::uint8_t kModAlt = 4;
inline constexpr std::uint8_t kModAll = 7;

// The keys a hotkey can never take (DIK codes): Escape (the player's exit) and the modifiers themselves.
bool reserved_key(Key key) noexcept;

enum class Added : std::uint8_t {
  bound = 0,      // every default it names is bound: a press fires it
  unbound = 1,    // no default, or a default it names is taken (by another hotkey, or the menu key)
  invalid = 2,    // a reserved key, an unknown modifier, Alt+F4, a chord that is not one (valid_chord), or an action
                  // already the mod's
  full = 3,       // kHotkeysPerMod of the mod's, or kMaxHotkeys in the session
};

// A pad chord as graceui.h's GraceUiHotkeyDesc.pad_chord gives it (D204): the key's button in the low 16 bits and the
// anchor's in the high 16, each exactly one of XInput's fourteen buttons (no triggers, no sticks), the two different.
inline constexpr std::uint16_t kChordButtons = 0xF3FFu;  // the XINPUT_GAMEPAD_* buttons; 0x0400 and 0x0800 are none
constexpr std::uint16_t chord_key(std::uint32_t chord) noexcept { return static_cast<std::uint16_t>(chord & 0xFFFFu); }
constexpr std::uint16_t chord_anchor(std::uint32_t chord) noexcept { return static_cast<std::uint16_t>(chord >> 16); }
constexpr bool one_button(std::uint16_t bits) noexcept {
  return bits != 0 && (bits & (bits - 1)) == 0 && (bits & ~kChordButtons) == 0;
}
constexpr bool valid_chord(std::uint32_t chord) noexcept {
  return one_button(chord_key(chord)) && one_button(chord_anchor(chord)) && chord_key(chord) != chord_anchor(chord);
}

// Which of a hotkey's defaults are bound, as add() found them.
struct Binding {
  bool key = false;
  bool chord = false;
};

// The player's binding of a hotkey, over its mod's defaults (G37, D208): its key with modifiers (kNoKey for none) and
// its chord (0 for none).
struct Chosen {
  Key key = kNoKey;
  std::uint8_t modifiers = 0;
  std::uint32_t chord = 0;
};

// One hotkey as the Hotkeys page lists it (D206).
struct Listed {
  std::uint32_t id = 0;
  std::uint16_t owner = 0;
  Key key = kNoKey;
  std::uint8_t modifiers = 0;
  std::uint32_t chord = 0;
  bool key_bound = false;
  bool chord_bound = false;
  bool paused = false;  // its mod has paused it for now (ABI 1.11, D280)
  Chosen defaults{};  // its mod's, which a reset restores
  std::string_view action() const noexcept { return {action_bytes, action_length}; }
  std::string_view name() const noexcept { return {name_bytes, name_length}; }
  std::uint8_t action_length = 0;
  std::uint8_t name_length = 0;
  char action_bytes[kActionMax] = {};
  char name_bytes[kHotkeyNameMax] = {};
};

// What pressed a hotkey (D243): its key, its pad chord, or both among the presses one event counts.
inline constexpr std::uint8_t kByKey = 1;
inline constexpr std::uint8_t kByChord = 2;

struct Press {
  std::uint32_t id = 0;     // the hotkey's id: never 0
  std::uint32_t count = 0;  // presses since its last one was taken
  std::uint8_t by = 0;      // kByKey and kByChord: what made those presses
};

// What a change of binding did (D208).
enum class Rebound : std::uint8_t {
  done = 0,
  taken = 1,    // another bound hotkey has it (`Taken::id`), or it is the menu's key (`Taken::menu`); nothing changed
  invalid = 2,  // a reserved key, an unknown modifier, Alt+F4, or a chord that is not one
  gone = 3,     // no hotkey has this id now
};

struct Taken {
  std::uint32_t id = 0;  // the hotkey that has it, or that lost it to a move
  bool menu = false;     // it is the menu's key
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
            std::uint32_t& id) noexcept {
    return add(owner, action, key, modifiers, 0, menu_key, id, nullptr);
  }
  // The same with a default pad `chord` (0 for none, else valid_chord), and which defaults are bound in `binding`.
  // `name` is what the Hotkeys page calls it (checked by the caller; at most kHotkeyNameMax bytes are kept).
  // With `chosen`, the hotkey takes the player's binding instead (G37), and keeps its defaults for a reset.
  Added add(std::uint16_t owner, std::string_view action, Key key, std::uint8_t modifiers, std::uint32_t chord,
            Key menu_key, std::uint32_t& id, Binding* binding, std::string_view name = {},
            const Chosen* chosen = nullptr) noexcept;

  // The first of `owner`'s hotkeys whose binding the player changed since its mod last asked, its mark cleared; false
  // if none (GRACEUI_EVENT_HOTKEY_CHANGED, D208).
  bool take_changed(std::uint16_t owner, std::uint32_t& id) noexcept;
  // The owners with a change waiting, one bit each (bit `owner`, owners 1 to 63): their wait handles are set (D212).
  std::uint64_t changed_owners() const noexcept;
  // The mod's own hotkey `id` paused (`enabled` false) or resumed (ABI 1.11, D280), such as while the game's own menu
  // is open. Paused, it neither fires nor keeps its key or chord from the game; its binding stays, held against other
  // hotkeys as before, and presses already counted stay to be taken. A press under way is kept whole by the side that
  // saw it begin: a press the hotkey took stays hidden from the game until released, and a press the game saw begin
  // reaches the game until released and fires nothing. Not the player's change: no HOTKEY_CHANGED. False if `id`
  // names no hotkey of `owner`'s now.
  bool set_enabled(std::uint16_t owner, std::uint32_t id, bool enabled) noexcept;

  // ---- GraceUI's thread.

  // Every hotkey added and not yet removed, in the table's order, at most `capacity`: how many were written.
  std::size_t list(Listed* out, std::size_t capacity) const noexcept;
  // One hotkey, as list() gives it; false if no hotkey has this id now.
  bool find(std::uint32_t id, Listed& out) const noexcept;

  // The player's changes (G37, D208). A key or chord another bound hotkey has answers `taken`, naming it, and changes
  // nothing, unless `move`: then the other loses it (its key, or its chord, becomes none) and `other` names it. The
  // menu's key is never moved from it (`taken`, `menu`). kNoKey, or chord 0, removes the hotkey's own.
  Rebound rebind_key(std::uint32_t id, Key key, std::uint8_t modifiers, Key menu_key, bool move, Taken& other) noexcept;
  Rebound rebind_chord(std::uint32_t id, std::uint32_t chord, bool move, Taken& other) noexcept;
  // Back to its mod's defaults, each bound unless another hotkey has it, as at registration.
  Rebound reset(std::uint32_t id, Key menu_key) noexcept;
  // The menu's key is now `key`: every hotkey bound to it, whatever its modifiers, loses its key. Their ids are written
  // to `out`, at most `capacity`; answers how many lost it.
  std::size_t menu_key_moved(Key key, std::uint32_t* out, std::size_t capacity) noexcept;

  // The mod has gone: its hotkeys and their presses go with it.
  void remove_owner(std::uint16_t owner) noexcept;
  // One hotkey, if `id` still names it.
  void remove(std::uint32_t id) noexcept;

  // The first of `owner`'s hotkeys, in the table's order, with presses waiting, its presses taken; false if none has
  // any.
  bool take(std::uint16_t owner, Press& out) noexcept;

  // ---- The game's window thread.

  // The bound hotkey whose key and modifiers these are exactly, or 0; a paused one is none (D280).
  std::uint32_t match(Key key, std::uint8_t modifiers) const noexcept;
  // The owner of hotkey `id`, or 0 if no hotkey has it now: read without the lock, to set its mod's wait handle (D212).
  std::uint16_t owner_of(std::uint32_t id) const noexcept;
  // Counts a press of `id`, by its key or its chord (kByKey, kByChord); false if it was removed or paused meanwhile.
  bool pressed(std::uint32_t id, std::uint8_t by = kByKey) noexcept;

  // ---- XInput's readers.

  // The hotkey whose bound chord is `anchor` then `key` (one button each), or 0; a paused one is none (D280).
  std::uint32_t match_chord(std::uint16_t anchor, std::uint16_t key) const noexcept;
  // The key buttons of every bound chord: a fresh press of any other starts no chord, so no table need be searched.
  std::uint16_t chord_keys() const noexcept { return chord_keys_.load(std::memory_order_acquire); }
  // A press with modifiers that a hotkey took: hidden from DirectInput's readers until released (held()).
  void hold(Key key) noexcept;
  void let_go(Key key) noexcept;
  void let_go_all() noexcept;

  // ---- Any thread.

  // The keys bound with no modifier to a hotkey not paused: never the game's while bound, but for a press the game saw
  // begin (the window's messages and DirectInput).
  KeyBits bare() const noexcept { return bare_.load(); }
  // Every bound key, whatever its modifiers: a press of any other fires nothing, so its modifiers need not be read.
  KeyBits bound_keys() const noexcept { return bound_.load(); }
  // The keys a hotkey's press holds now (hold()).
  KeyBits held() const noexcept { return held_.load(); }
  std::uint32_t count() const noexcept;  // hotkeys added and not yet removed, bound or not

 private:
  // A hotkey's word: bits 0-1 its state, 2-9 its key, 10-12 its modifiers, 13-31 its generation, 32-61 its presses, 62-63
  // what made them (kByKey, kByChord; D243), taken with them in one compare-and-swap.
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
  static constexpr std::uint32_t kMaxPresses = 0x3FFFFFFFu;
  static constexpr std::uint32_t presses_of(std::uint64_t word) noexcept {
    return static_cast<std::uint32_t>(word >> 32) & kMaxPresses;
  }
  static constexpr std::uint8_t by_of(std::uint64_t word) noexcept { return static_cast<std::uint8_t>(word >> 62); }
  static constexpr std::uint32_t id_of(std::uint32_t index, std::uint32_t generation) noexcept {
    return (generation << 8) | (index + 1);
  }

  void lock() const noexcept;
  void unlock() const noexcept { lock_.clear(std::memory_order_release); }
  struct Slot;
  void update_bare(const Slot* pausing = nullptr) noexcept;  // under the lock; `pausing` counted as paused (D280)

  // A hotkey's chord word: bits 0-31 its chord, bit 32 set while bound, bits 33-51 the hotkey's generation.
  static constexpr std::uint64_t pack_chord(std::uint32_t chord, bool bound, std::uint32_t generation) noexcept {
    return std::uint64_t{chord} | (std::uint64_t{bound ? 1u : 0u} << 32) | (std::uint64_t{generation & 0x7FFFFu} << 33);
  }

  struct Slot {
    std::atomic<std::uint64_t> word{0};
    std::atomic<std::uint64_t> chord{0};
    std::uint16_t owner = 0;            // under the lock
    std::uint8_t action_length = 0;     // under the lock
    char action[kActionMax] = {};       // under the lock
    std::uint8_t name_length = 0;       // under the lock
    char name[kHotkeyNameMax] = {};     // under the lock
    Chosen defaults{};                  // under the lock: its mod's, for a reset
    std::atomic<std::uint16_t> owner_now{0};  // `owner`, readable without the lock (D212)
    std::atomic<bool> changed{false};   // the player changed it since its mod last asked
    std::atomic<bool> paused{false};    // its mod paused it (D280): set under the lock, read without it
  };

  // Under the lock: the slot's key and modifiers, bound or not, its generation and presses kept.
  void set_key(Slot& slot, Key key, std::uint8_t modifiers, bool bound) noexcept;
  void set_chord(Slot& slot, std::uint32_t chord, bool bound) noexcept;
  Slot* slot_of(std::uint32_t id) noexcept;  // under the lock: the slot `id` names now, or null
  static Listed listed_of(const Slot& slot, std::uint32_t id) noexcept;  // under the lock
  std::uint32_t key_holder(Key key, std::uint8_t modifiers, const Slot* except) const noexcept;  // under the lock
  std::uint32_t chord_holder(std::uint32_t chord, const Slot* except) const noexcept;            // under the lock

  mutable std::atomic_flag lock_{};
  std::uint32_t generation_ = 0;  // under the lock
  Slot slots_[kMaxHotkeys];
  AtomicKeyBits bare_;
  AtomicKeyBits bound_;
  AtomicKeyBits held_;
  std::atomic<std::uint16_t> chord_keys_{0};
};

}  // namespace grace::input
