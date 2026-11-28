// devices.h: what the game reads through the input detours (§5.2, D69): DirectInput's keyboard and mouse states and
// XInput's pads, each rewritten in place to what the game may see. While a holder takes a device, the game sees it at
// rest: no key, no movement or button, a pad untouched. When the holder ends, whatever was held stays hidden from the
// game until it is released (invariant 8: the closing press never reaches the game). The pads also feed the holder's
// own view (Dear ImGui's pad navigation), in which the press that was held when the holder took input does nothing
// until it is released (the opening press, invariant 8). Every function here may run on any thread, two at once.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "grace/input/holder.h"
#include "grace/input/keys.h"
#include "grace/input/queue.h"

namespace grace::input {

// After a close, a key held at the first read is masked if it was held at the read before too (held through the
// close), or if the close was this recent, since the closing press may not have been read yet (EST). The same window
// after a grant marks the opening press on a pad.
inline constexpr std::int64_t kTransitionWindowMs = 250;

// DirectInput's keyboard as c_dfDIKeyboard lays it out: 256 bytes, bit 7 set while the key is down.
class KeyboardLedger {
 public:
  constexpr KeyboardLedger() = default;
  KeyboardLedger(const KeyboardLedger&) = delete;
  KeyboardLedger& operator=(const KeyboardLedger&) = delete;

  // Rewrites `state` (what GetDeviceState returned) to what the game may see: nothing while a holder takes the
  // keyboard; after it ends, nothing of a key held at the close until it is released; never the menu key (§5.1).
  void filter(std::uint8_t (&state)[256], const Capture& capture, Key menu_key, std::int64_t now_ms) noexcept;
  // The same, hiding every key of `hidden` as the menu key is hidden: the menu key and the mods' hotkeys' (D155).
  void filter(std::uint8_t (&state)[256], const Capture& capture, const KeyBits& hidden, std::int64_t now_ms) noexcept;
  // The same, also hiding each key of `fresh` but one the game was shown held at the last read and holds still, until
  // it is released: the keys bound with no modifier, so that a hotkey resumed while the game holds its key leaves that
  // press whole (D280).
  void filter(std::uint8_t (&state)[256], const Capture& capture, const KeyBits& hidden, const KeyBits& fresh,
              std::int64_t now_ms) noexcept;

  KeyBits latest() const noexcept { return latest_.load(); }   // the newest real state read
  KeyBits masked() const noexcept { return masked_.load(); }

 private:
  AtomicKeyBits latest_;
  AtomicKeyBits masked_;
  AtomicKeyBits shown_;  // the keys the game was shown held at the last read (D280)
  std::atomic<std::uint64_t> armed_for_{0};  // the close count the masks were last armed for
};

// DirectInput's mouse as c_dfDIMouse or c_dfDIMouse2 lays it out: relative axes and 4 or 8 buttons.
struct MouseState {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::array<std::uint8_t, 8> buttons{};
  std::uint8_t button_count = 4;
};

class MouseLedger {
 public:
  constexpr MouseLedger() = default;
  MouseLedger(const MouseLedger&) = delete;
  MouseLedger& operator=(const MouseLedger&) = delete;

  // While a holder takes the mouse: no movement, wheel or button. After it ends, a button held at the close stays
  // hidden until it is released; movement reaches the game at once.
  void filter(MouseState& state, const Capture& capture, std::int64_t now_ms) noexcept;

  std::uint8_t latest() const noexcept { return latest_.load(std::memory_order_acquire); }  // buttons, one bit each
  std::uint8_t masked() const noexcept { return masked_.load(std::memory_order_acquire); }

 private:
  std::atomic<std::uint8_t> latest_{0};
  std::atomic<std::uint8_t> masked_{0};
  std::atomic<std::uint64_t> armed_for_{0};
};

// XINPUT_GAMEPAD's fields.
struct Pad {
  std::uint16_t buttons = 0;
  std::uint8_t left_trigger = 0;
  std::uint8_t right_trigger = 0;
  std::int16_t left_x = 0;
  std::int16_t left_y = 0;
  std::int16_t right_x = 0;
  std::int16_t right_y = 0;

  constexpr bool operator==(const Pad&) const noexcept = default;
};

// A pad's controls as a ledger counts them: bits 0 to 15 its buttons (XINPUT_GAMEPAD_* masks), then its two triggers
// and its two sticks, each held once past XInput's own thresholds.
using PadControls = std::uint32_t;
inline constexpr PadControls kLeftTrigger = 1u << 16;
inline constexpr PadControls kRightTrigger = 1u << 17;
inline constexpr PadControls kLeftStick = 1u << 18;
inline constexpr PadControls kRightStick = 1u << 19;
inline constexpr std::uint8_t kTriggerThreshold = 30;        // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
inline constexpr std::int32_t kLeftStickDeadzone = 7849;     // XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
inline constexpr std::int32_t kRightStickDeadzone = 8689;    // XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE

PadControls held_controls(const Pad& pad) noexcept;
Pad without(const Pad& pad, PadControls hidden) noexcept;  // the hidden controls at rest

// The holder's view of one pad, for the present thread.
struct HolderPad {
  std::uint32_t generation = 0;  // the holder it was made for
  std::uint32_t stamp = 0;       // when this pad last changed, against the others: the newest feeds the holder
  Pad pad{};
  std::uint8_t index = 0;
  std::uint8_t connected = 0;
};

inline constexpr unsigned kPads = 4;  // XUSER_MAX_COUNT

class PadLedger {
 public:
  constexpr PadLedger() = default;
  PadLedger(const PadLedger&) = delete;
  PadLedger& operator=(const PadLedger&) = delete;

  // Rewrites one pad's state (XInputGetState's answer for user `index`) to what the game may see: at rest while a holder
  // takes the pad, then a held control hidden until it is released. The packet number changes at every grant and
  // close, so that the game reads the change (EldenNet NPC §5.5), and otherwise follows the pad's own. Publishes the
  // holder's view as well.
  void filter(unsigned index, std::uint32_t& packet, Pad& pad, const Capture& capture, std::int64_t now_ms) noexcept;

  // XInputGetState answered that user `index` has no pad.
  void disconnected(unsigned index) noexcept;

  // The holder's view of its newest pad: false if no pad has reported for this holder.
  bool holder_view(std::uint32_t generation, HolderPad& out) const noexcept;

  PadControls masked(unsigned index) const noexcept { return pads_[index].masked.load(std::memory_order_acquire); }
  PadControls inert(unsigned index) const noexcept { return pads_[index].inert.load(std::memory_order_acquire); }

 private:
  struct One {
    std::atomic<PadControls> latest{0};
    std::atomic<PadControls> masked{0};
    std::atomic<PadControls> inert{0};
    std::atomic<std::uint64_t> armed_for{0};    // the close count the game's masks were armed for
    std::atomic<std::uint32_t> inert_for{0};    // the holder the opening press was marked for
    std::atomic<std::uint32_t> last_packet{0};
    std::atomic<std::uint32_t> stamp{0};
    Latest<HolderPad> view;
  };

  std::array<One, kPads> pads_{};
  std::atomic<std::uint32_t> stamps_{0};
};

}  // namespace grace::input
