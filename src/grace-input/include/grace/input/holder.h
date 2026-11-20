// holder.h: the input holder (D77): one in the process at a time, answered at once, ended by one close routine for
// every exit path (invariants 7, 8 and 17). Lock-free: a consumer's thread asks and releases, the window procedure
// watches the menu key and Escape, the input detours and the present thread call tick(), and any of them may run the
// close routine; nothing here ever waits. Pure: times are milliseconds from the caller's monotonic clock.
#pragma once

#include <atomic>
#include <cstdint>

namespace grace::input {

enum class Kind : std::uint8_t { none = 0, menu = 1, panel = 2, modal = 3 };

// What a holder takes from the game while it holds input (one capture state for every holder, §5.2).
enum Want : std::uint8_t { want_keyboard = 1, want_mouse = 2, want_pad = 4, want_all = 7 };

enum class Phase : std::uint8_t {
  none = 0,     // nobody holds input
  active = 1,   // a holder has input
  closing = 2,  // the close routine is ending it: input is still captured until it completes
};

enum class Answer : std::uint8_t { granted = 0, refused = 1, not_now = 2 };

enum class NotNow : std::uint8_t {
  none = 0,
  not_ready = 1,  // the overlay cannot show the holder's view yet (or has not drawn for 2 s), or input is off
  danger = 2,     // a world-state provider reports danger (D78)
  loading = 3,    // a provider reports a loading screen, death, quitting to the title, travel or a multiplayer gate
  game_menu = 4,  // a provider reports a game menu or the game's own text entry open
  invalid = 5,    // no kind, or nothing wanted
};

// Why a holder's input ended: what the holder is told (D77).
enum class Closed : std::uint8_t {
  none = 0,
  released = 1,    // the holder gave it back
  menu_key = 2,    // the menu key while the menu holds input
  danger = 3,      // invariant 9
  loading = 4,
  focus_lost = 5,  // the game's window lost the keyboard focus (Alt+Tab)
  no_render = 6,   // 2 s without an overlay frame
  fault = 7,       // a fault in GraceUI's frame or in its input handling
  player = 8,      // the player's exit: a second menu-key press within 2 s, or Escape held 1 s
};

struct Holder {
  std::uint32_t generation = 0;  // grants so far: each holder has its own number
  std::uint16_t owner = 0;       // the consumer that asked (S10.4.b's ids; the test exports' in GraceUI 0.1)
  Kind kind = Kind::none;
  std::uint8_t wants = 0;
  Phase phase = Phase::none;

  constexpr bool holds() const noexcept { return phase != Phase::none; }
  constexpr bool takes(std::uint8_t want) const noexcept { return holds() && (wants & want) != 0; }
  constexpr bool operator==(const Holder&) const noexcept = default;
};

struct Request {
  Answer answer = Answer::not_now;
  NotNow why = NotNow::none;  // for not_now
  Holder holder{};            // granted: the new holder; refused: the one that holds input
};

// What request() weighs besides the holder: whether the overlay is live, and the world-state providers' claims (D78).
struct Conditions {
  bool ready = false;
  bool danger = false;
  bool loading = false;
  bool game_menu = false;
  bool off = false;  // input or the overlay is off for the session: never ready again (a holder found then ends: fault)
};

// The conditions as they are now, read from the words other threads write (the providers' combined claims, the
// overlay's state). Called with the context the caller gave; never waits.
using ReadConditions = Conditions (*)(const void* context) noexcept;

// What a fresh press of the menu key did.
enum class MenuKey : std::uint8_t {
  nothing = 0,
  open_menu = 1,    // no holder: the menu opens (D167)
  closed_menu = 2,  // the menu held input: it closed
  notice = 3,       // another holder has input: "‹Mod› has the controls", and the 2 s for the player's exit start
  player_exit = 4,  // the second press within 2 s: the player's exit
};

// Where an Escape press was seen: each source keeps its own timer for the player's exit.
enum class Source : std::uint8_t { messages = 0, direct_input = 1 };

struct LastClose {
  Holder holder{};
  Closed why = Closed::none;
};

inline constexpr std::int64_t kExitPressWindowMs = 2000;  // the second menu-key press (EST, S4 checks)
inline constexpr std::int64_t kEscapeHoldMs = 1000;       // Escape held (EST, S4 checks)
inline constexpr std::int64_t kNoRenderMs = 2000;         // the watchdog (EldenNet DESIGN §8.1)

class Capture {
 public:
  constexpr Capture() = default;
  Capture(const Capture&) = delete;
  Capture& operator=(const Capture&) = delete;

  Holder holder() const noexcept;

  // request_input (D77): granted, refused naming the holder, or not_now with the reason; never queued, never deposing.
  // The holder asking again for the same kind is told it holds input (granted, unchanged).
  Request request(std::uint16_t owner, Kind kind, std::uint8_t wants, const Conditions& conditions,
                  std::int64_t now_ms) noexcept;

  // The same, with the conditions read by `read`: once before the grant, and again after it behind a full fence. A
  // condition that ends a holder (danger, loading, input or the overlay off) found the second time ends the new holder
  // at once, and the answer is not_now with that reason, its `holder` the holder this ended (phase none if a writer's
  // settle() ended it first). With settle() on the writers' side, a writer that changed a condition and then looked
  // for a holder to end, and a request that read the condition before the change and then granted, cannot both miss
  // each other (the store-buffer pattern, both sides fenced), so no holder outlives a danger claim (invariant 9).
  Request request(std::uint16_t owner, Kind kind, std::uint8_t wants, ReadConditions read, const void* context,
                  std::int64_t now_ms) noexcept;

  // A writer's side, after it changed what `read` answers: a full fence, then the conditions read now; input or the
  // overlay off ends the holder (fault), danger or loading ends it with its reason. Answers the holder it ended, as
  // close() does. Each call reads the newest conditions, so the last writer's settle() decides, whatever order the
  // writers' changes and their settle() calls interleave in.
  Holder settle(ReadConditions read, const void* context, std::int64_t now_ms) noexcept;

  // The holder's own release: the close routine with `released`, if `owner` holds input. Answers whether it did.
  bool release(std::uint16_t owner, std::int64_t now_ms) noexcept;

  // The one close routine (invariant 7): ends the holder with `why` and answers it, or answers a Holder whose phase is
  // none if nobody held input, another close got there first, or `generation` (when not 0) names an earlier holder.
  // The devices then mask what was held at the close until it is released (invariant 8): they read closes() and
  // last_close() at their next read.
  Holder close(Closed why, std::int64_t now_ms, std::uint32_t generation = 0) noexcept;

  // The player's exit and the watchdog. menu_key() takes a fresh press of the menu key, from the window procedure.
  MenuKey menu_key(std::int64_t now_ms) noexcept;
  void escape(Source source, bool down, std::int64_t now_ms) noexcept;
  void rendered(std::int64_t now_ms) noexcept;
  // Closes on the player's exit (Escape held 1 s while the holder has input) or after 2 s without an overlay frame;
  // answers the holder it ended, as close() does. Any thread, as often as it likes.
  Holder tick(std::int64_t now_ms) noexcept;

  std::uint64_t closes() const noexcept { return closes_.load(std::memory_order_acquire); }
  std::int64_t closed_at() const noexcept { return closed_at_.load(std::memory_order_relaxed); }
  std::int64_t granted_at() const noexcept { return granted_at_.load(std::memory_order_relaxed); }
  std::int64_t last_render() const noexcept { return last_render_.load(std::memory_order_relaxed); }
  LastClose last_close() const noexcept;
  std::uint32_t notices() const noexcept { return notices_.load(std::memory_order_relaxed); }

 private:
  std::atomic<std::uint64_t> state_{0};
  std::atomic<std::uint64_t> closes_{0};
  std::atomic<std::uint64_t> last_close_{0};
  std::atomic<std::int64_t> closed_at_{0};
  std::atomic<std::int64_t> granted_at_{0};
  std::atomic<std::int64_t> last_render_{0};
  std::atomic<std::int64_t> escape_since_[2]{};  // per Source: when Escape went down, 0 while it is up
  std::atomic<std::int64_t> notice_at_{0};
  std::atomic<std::uint32_t> notice_generation_{0};
  std::atomic<std::uint32_t> notices_{0};
};

}  // namespace grace::input
