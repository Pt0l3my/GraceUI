// holder_mail.h: a holder's events for its mod (D176), pure: no Win32, unit-tested on Linux. When a mod is granted input
// with GRACEUI_INPUT_EVENTS, what the player does while it holds (keys, characters, the pointer, a pad's buttons) is
// posted here by the threads that see it (the window procedure's, the XInput detour's), and the reason its holder ended
// by whichever thread ends it; the mod takes them through poll, on any of its threads. Nothing here ever waits for
// another thread but the mod's own takes for each other.
//
//   - Only the active holder's events are kept: one posted for another generation, or after the holder ended, is
//     dropped; one the mod has not taken when its holder ends is discarded, and the end comes after the rest, last.
//   - A pointer's moves are coalesced as they are taken: only the newest position since the last take.
//   - Each box holds at most kMailPerHolder events; past that the newest are dropped and counted, never reordered.
//   - A box belongs to one connection of one mod (its token): a mod connecting again starts with an empty one.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "grace/input/queue.h"

namespace grace::input {

// GRACEUI_EVENT_*'s values for a holder's kinds.
enum class MailKind : std::uint8_t { none = 0, key = 5, character = 6, pointer = 7, ended = 8, pad = 9 };

struct Mail {
  MailKind kind = MailKind::none;
  std::uint8_t pressed = 0;
  std::uint8_t repeat = 0;
  std::uint8_t button_event = 0;  // a pointer's: 1 for a button's press or release
  std::uint32_t generation = 0;   // the holder it is for
  std::uint32_t key = 0;          // a key's virtual key
  std::uint32_t modifiers = 0;    // GRACEUI_MOD_*
  std::uint32_t code_point = 0;
  std::uint32_t button = 0;       // a pointer's GRACEUI_BUTTON_*; a pad's button mask
  std::uint32_t pad = 0;
  std::uint32_t why = 0;          // ended: GRACEUI_CLOSED_*
  float x = 0.0f;                 // the pointer, in the back buffer's pixels (the host makes them the panel's)
  float y = 0.0f;
  float wheel_x = 0.0f;           // notches
  float wheel_y = 0.0f;
};

inline constexpr std::uint32_t kMailPerHolder = 256;
inline constexpr std::uint32_t kMailBoxes = 64;
inline constexpr std::uint32_t kMailPads = 4;

class HolderMail {
 public:
  HolderMail() = default;
  ~HolderMail();
  HolderMail(const HolderMail&) = delete;
  HolderMail& operator=(const HolderMail&) = delete;

  // The mod's thread, when its request is granted with GRACEUI_INPUT_EVENTS: the holder `generation` is mod `owner`'s,
  // on its connection `token`. Its box is emptied first if it held another connection's mail.
  void enable(std::uint16_t owner, std::uint64_t token, std::uint32_t generation);

  // Whether the holder `generation` asked for its events, and whether any holder now has.
  bool active(std::uint32_t generation) const noexcept;
  bool any() const noexcept;

  // Any thread: an event of the holder `mail.generation`, kept only while that holder is the active one.
  void post(const Mail& mail) noexcept;

  // Called with the owner whenever an event is kept for it, on the thread that kept it: the host sets the mod's wait
  // handle (D212). It must never wait. Set once, before any holder.
  using Notify = void (*)(std::uint16_t owner) noexcept;
  void set_notify(Notify notify) noexcept { notify_.store(notify, std::memory_order_release); }

  // Any thread, at each XInput read: pad `user`'s buttons as the pad gave them. A change is posted (one event a button)
  // while the active holder is `generation` and takes the pad.
  void pad(std::uint32_t user, std::uint16_t buttons, std::uint32_t generation, bool takes_pad) noexcept;

  // The close routine: the holder `generation` ended. If it had asked for its events, its mod gets `ended` with `why`,
  // after its other events, and nothing more is kept for it.
  void end(std::uint32_t generation, std::uint32_t why) noexcept;

  // The mod's poll, on any of its threads: its next event and how many were dropped before it; false if none waits.
  bool take(std::uint16_t owner, std::uint64_t token, Mail& out, std::uint32_t& dropped);

 private:
  struct Box {
    std::atomic<std::uint64_t> token{0};
    std::atomic<std::uint16_t> owner{0};
    MpmcRing<Mail, kMailPerHolder> ring;
    std::mutex taking;  // the mod's takes, against each other only
    bool held = false;  // under `taking`: an event read past a coalesced move, to give next
    Mail next{};
    // An end the ring could not take even once emptied (another thread's pop still held its slot): its generation and
    // why, 0 for none, given before anything in the ring (D207).
    std::atomic<std::uint64_t> pending_end{0};
  };

  static std::uint64_t pack(std::uint16_t owner, std::uint32_t generation) noexcept {
    return (std::uint64_t{owner} << 32) | generation;
  }
  Box* box_of(std::uint16_t owner) const noexcept;
  bool pop(Box& box, Mail& out);

  std::array<std::atomic<Box*>, kMailBoxes> boxes_{};
  std::mutex making_;                      // a box's creation (the mods' threads)
  std::atomic<std::uint64_t> active_{0};   // the active holder: its owner and generation, 0 for none
  std::array<std::atomic<std::uint16_t>, kMailPads> pad_raw_{};
  std::atomic<Notify> notify_{nullptr};
};

}  // namespace grace::input
