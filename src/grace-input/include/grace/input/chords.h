// chords.h: the mods' pad chords (D204, proposal 0005), watched for in XInput's reads. A chord is two buttons in order:
// an anchor, which always reaches the game, then a key, pressed while the anchor has been held for kChordHoldBackMs.
// Such a key is held back from the game while the chord resolves. Both held for kChordCompleteMs from the key's press
// complete it: the hotkey fires once, the key never reaches the game, and both buttons are hidden from it until each is
// released. Either released sooner fails it: the held-back key is replayed as one tap, pressed for kChordTapReads of the
// game's reads and kChordTapMs, then released, the packet number changing at each step so that the game reads both.
// A key the tap leaves held stays hidden until it is released, so that the game sees one press.
//
// Chords stand aside (D204): while a holder has input the holder gets the pad, and a chord resolving then is dropped
// with no tap (the game sees the pad at rest anyway); while a request for input would be answered not_now, or in the
// notice-only mode, a chord neither starts nor holds anything back, and one resolving then fails at once, with its tap.
//
// Pure: no Win32, so the rules are tested on Linux by timelines of pad states. Each pad's state is written by one reader
// at a time, under a try-lock: a second reader of the same pad meanwhile never waits, and gives the game what the last
// read decided.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "grace/input/devices.h"
#include "grace/input/hotkeys.h"

namespace grace::input {

inline constexpr std::int64_t kChordHoldBackMs = 150;  // the anchor held this long before the key (EST, S4 checks)
inline constexpr std::int64_t kChordCompleteMs = 400;  // both held this long from the key's press (EST, S4 checks)
inline constexpr std::int64_t kChordTapMs = 50;        // a failed chord's tap, at least (EST, S4 checks)
inline constexpr std::uint32_t kChordTapReads = 2;     // and for at least this many of the game's reads

// Why chords stand aside now, if they do.
enum class Aside : std::uint8_t {
  none = 0,
  holder = 1,  // a holder has input: it gets the pad's buttons itself (D176)
  not_now = 2, // request_input would answer not_now (danger, a game menu, loading, not ready), or notice-only mode
};

// What chords make of one read, for the game.
struct ChordView {
  std::uint16_t hide = 0;    // buttons the game does not see now
  std::uint16_t show = 0;    // a button the game sees pressed now (a failed chord's tap)
  std::uint32_t packet = 0;  // added to the packet number: it changes at each step chords take
};

// What one read did, for its counters and its log.
enum class ChordStep : std::uint8_t {
  none = 0,
  held_back = 1,  // a chord's key was pressed in time: held back while it resolves
  fired = 2,      // a chord completed and its hotkey's press was counted
  failed = 3,     // a chord failed: its key's tap begins
  dropped = 4,    // a holder took input while a chord resolved
};

class PadChords {
 public:
  constexpr PadChords() = default;
  PadChords(const PadChords&) = delete;
  PadChords& operator=(const PadChords&) = delete;

  // One read of user `index`'s pad: `real` its buttons as XInput gave them. Rewrites `buttons` (what the pad ledger lets
  // the game see) and `packet` to what chords let it see, and counts a completed chord's press in `hotkeys`.
  ChordStep filter(unsigned index, std::uint16_t real, std::uint16_t& buttons, std::uint32_t& packet, Aside aside,
                   Hotkeys& hotkeys, std::int64_t now_ms) noexcept;

  // XInputGetState answered that user `index` has no pad: whatever was resolving is forgotten, with no tap.
  void disconnected(unsigned index) noexcept;

  // What the game was last given for user `index` (the second reader's answer; tests).
  ChordView view(unsigned index) const noexcept;

 private:
  enum class Phase : std::uint8_t { idle = 0, pending = 1, tap = 2 };

  struct One {
    std::atomic_flag busy{};                 // one reader at a time; another takes the view meanwhile
    std::atomic<std::uint64_t> view{0};      // the last ChordView: hide, show, packet
    // Under busy:
    std::uint16_t before = 0;                // the buttons at the last read
    std::array<std::int64_t, 16> since{};    // when each held button was pressed
    std::uint16_t hidden = 0;                // hidden until released: a completed chord's two, a tap's key still held
    Phase phase = Phase::idle;
    std::uint16_t key = 0;                   // the pending chord's, or the tap's
    std::uint16_t anchor = 0;
    std::uint32_t id = 0;                    // the pending chord's hotkey
    std::int64_t at = 0;                     // the key's press (pending), or the tap's start
    std::uint32_t reads = 0;                 // the tap's reads so far
    std::uint32_t steps = 0;                 // added to the packet number
  };

  static std::uint64_t pack(const ChordView& view) noexcept {
    return std::uint64_t{view.hide} | (std::uint64_t{view.show} << 16) | (std::uint64_t{view.packet} << 32);
  }
  static ChordView unpack(std::uint64_t word) noexcept {
    return {static_cast<std::uint16_t>(word), static_cast<std::uint16_t>(word >> 16),
            static_cast<std::uint32_t>(word >> 32)};
  }
  static ChordStep step(One& one, std::uint16_t real, Aside aside, Hotkeys& hotkeys, std::int64_t now_ms) noexcept;
  static void begin_tap(One& one, std::int64_t now_ms) noexcept;

  std::array<One, kPads> pads_{};
};

}  // namespace grace::input
