// devices.cpp: the game's view of DirectInput's keyboard and mouse and of XInput's pads (devices.h).
#include "grace/input/devices.h"

#include <cstdlib>

namespace grace::input {
namespace {

// At a reader's first read after a close: what to hide from the game until it is released. Nothing if the holder that
// ended did not take this device; otherwise what is held now and was held at the read before, or everything held now if
// the close was within the window. Several readers may arm for one close: they arm alike, and the masks only grow.
template <class Bits>
bool arm_after_close(std::atomic<std::uint64_t>& armed_for, const Capture& capture, std::uint8_t want, Bits now_held,
                     Bits before, std::int64_t now_ms, Bits& arm) noexcept {
  std::uint64_t armed = armed_for.load(std::memory_order_acquire);
  const std::uint64_t closes = capture.closes();
  if (armed == closes) {
    return false;
  }
  const LastClose last = capture.last_close();
  const bool recent = now_ms - capture.closed_at() <= kTransitionWindowMs;
  arm = (last.holder.wants & want) == 0 ? Bits{} : recent ? now_held : static_cast<Bits>(now_held & before);
  armed_for.compare_exchange_strong(armed, closes, std::memory_order_acq_rel);
  return true;
}

bool beyond(std::int32_t value, std::int32_t deadzone) noexcept { return std::abs(value) > deadzone; }

}  // namespace

void KeyboardLedger::filter(std::uint8_t (&state)[256], const Capture& capture, Key menu_key,
                            std::int64_t now_ms) noexcept {
  KeyBits hidden;
  hidden.set(menu_key);
  filter(state, capture, hidden, now_ms);
}

void KeyboardLedger::filter(std::uint8_t (&state)[256], const Capture& capture, const KeyBits& hidden,
                            std::int64_t now_ms) noexcept {
  filter(state, capture, hidden, KeyBits{}, now_ms);
}

void KeyboardLedger::filter(std::uint8_t (&state)[256], const Capture& capture, const KeyBits& hidden,
                            const KeyBits& fresh, std::int64_t now_ms) noexcept {
  KeyBits real;
  for (int key = 0; key < 256; ++key) {
    if ((state[key] & 0x80u) != 0) {
      real.set(static_cast<Key>(key));
    }
  }
  const KeyBits before = latest_.load();
  latest_.store(real);
  const bool capturing = capture.holder().takes(want_keyboard);
  KeyBits arm;
  if (!capturing && arm_after_close(armed_for_, capture, want_keyboard, real, before, now_ms, arm)) {
    masked_.or_with(arm);
  }
  masked_.and_with(real);  // a masked key seen up is the game's again
  // A key the game was shown held, and holds still, stays the game's until released, whatever was bound meanwhile; as
  // long as reads went on (a read long ago tells nothing of what was released since).
  const bool recent = now_ms - shown_at_.load(std::memory_order_acquire) <= kShownForMs;
  const KeyBits game_holds = recent ? shown_.load() & real : KeyBits{};
  const KeyBits hide = capturing ? KeyBits::all() : (masked_.load() & real) | hidden | (fresh & ~game_holds);
  for (int key = 0; key < 256; ++key) {
    if (hide.test(static_cast<Key>(key))) {
      state[key] = 0;
    }
  }
  shown_.store(real & ~hide);
  shown_at_.store(now_ms, std::memory_order_release);
}

void MouseLedger::filter(MouseState& state, const Capture& capture, std::int64_t now_ms) noexcept {
  const unsigned count = state.button_count < 8 ? state.button_count : 8;
  std::uint8_t real = 0;
  for (unsigned i = 0; i < count; ++i) {
    if ((state.buttons[i] & 0x80u) != 0) {
      real = static_cast<std::uint8_t>(real | (1u << i));
    }
  }
  const std::uint8_t before = latest_.exchange(real, std::memory_order_acq_rel);
  const bool capturing = capture.holder().takes(want_mouse);
  std::uint8_t arm = 0;
  if (!capturing && arm_after_close<std::uint8_t>(armed_for_, capture, want_mouse, real, before, now_ms, arm)) {
    masked_.fetch_or(arm, std::memory_order_acq_rel);
  }
  const std::uint8_t masked = static_cast<std::uint8_t>(masked_.fetch_and(real, std::memory_order_acq_rel) & real);
  if (capturing) {
    state.x = 0;
    state.y = 0;
    state.z = 0;
  }
  for (unsigned i = 0; i < count; ++i) {
    if (capturing || (masked & (1u << i)) != 0) {
      state.buttons[i] = 0;
    }
  }
}

PadControls held_controls(const Pad& pad) noexcept {
  PadControls held = pad.buttons;
  if (pad.left_trigger > kTriggerThreshold) {
    held |= kLeftTrigger;
  }
  if (pad.right_trigger > kTriggerThreshold) {
    held |= kRightTrigger;
  }
  if (beyond(pad.left_x, kLeftStickDeadzone) || beyond(pad.left_y, kLeftStickDeadzone)) {
    held |= kLeftStick;
  }
  if (beyond(pad.right_x, kRightStickDeadzone) || beyond(pad.right_y, kRightStickDeadzone)) {
    held |= kRightStick;
  }
  return held;
}

Pad without(const Pad& pad, PadControls hidden) noexcept {
  Pad out = pad;
  out.buttons = static_cast<std::uint16_t>(out.buttons & ~(hidden & 0xFFFFu));
  if ((hidden & kLeftTrigger) != 0) {
    out.left_trigger = 0;
  }
  if ((hidden & kRightTrigger) != 0) {
    out.right_trigger = 0;
  }
  if ((hidden & kLeftStick) != 0) {
    out.left_x = 0;
    out.left_y = 0;
  }
  if ((hidden & kRightStick) != 0) {
    out.right_x = 0;
    out.right_y = 0;
  }
  return out;
}

void PadLedger::filter(unsigned index, std::uint32_t& packet, Pad& pad, const Capture& capture,
                       std::int64_t now_ms) noexcept {
  if (index >= kPads) {
    return;
  }
  One& one = pads_[index];
  const Pad real_pad = pad;
  const PadControls real = held_controls(real_pad);
  const PadControls before = one.latest.exchange(real, std::memory_order_acq_rel);
  if (one.last_packet.exchange(packet, std::memory_order_relaxed) != packet ||
      one.stamp.load(std::memory_order_relaxed) == 0) {
    one.stamp.store(stamps_.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_relaxed);
  }
  const Holder holder = capture.holder();
  const bool capturing = holder.takes(want_pad);

  // The game's side: a control held at the close stays hidden until it is released.
  PadControls arm = 0;
  if (!capturing && arm_after_close<PadControls>(one.armed_for, capture, want_pad, real, before, now_ms, arm)) {
    one.masked.fetch_or(arm, std::memory_order_acq_rel);
  }
  const PadControls masked = one.masked.fetch_and(real, std::memory_order_acq_rel) & real;

  // The holder's side: the press held when it took the pad does nothing until it is released.
  if (capturing) {
    if (one.inert_for.load(std::memory_order_acquire) != holder.generation) {
      const bool recent = now_ms - capture.granted_at() <= kTransitionWindowMs;
      one.inert.store(recent ? real : real & before, std::memory_order_release);
      one.inert_for.store(holder.generation, std::memory_order_release);
    }
    const PadControls inert = one.inert.fetch_and(real, std::memory_order_acq_rel) & real;
    HolderPad view;
    view.generation = holder.generation;
    view.stamp = one.stamp.load(std::memory_order_relaxed);
    view.pad = without(real_pad, inert);
    view.index = static_cast<std::uint8_t>(index);
    view.connected = 1;
    one.view.publish(view);
  }

  // Every grant and every close changes the packet number the game reads.
  packet += holder.generation + static_cast<std::uint32_t>(capture.closes());
  pad = capturing ? Pad{} : without(real_pad, masked);
}

void PadLedger::disconnected(unsigned index) noexcept {
  if (index >= kPads) {
    return;
  }
  One& one = pads_[index];
  one.latest.store(0, std::memory_order_release);
  one.masked.store(0, std::memory_order_release);
  one.inert.store(0, std::memory_order_release);
  HolderPad view;
  if (one.view.read(view) && view.connected != 0) {
    view.connected = 0;
    view.pad = Pad{};
    one.view.publish(view);
  }
}

bool PadLedger::holder_view(std::uint32_t generation, HolderPad& out) const noexcept {
  bool found = false;
  for (const One& one : pads_) {
    HolderPad view;
    if (one.view.read(view) && view.generation == generation && view.connected != 0 &&
        (!found || view.stamp > out.stamp)) {
      out = view;
      found = true;
    }
  }
  return found;
}

}  // namespace grace::input
