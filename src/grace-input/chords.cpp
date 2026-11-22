// chords.cpp: the mods' pad chords (chords.h).
#include "grace/input/chords.h"

namespace grace::input {
namespace {

// The lowest button of `bits`.
std::uint16_t lowest(std::uint16_t bits) noexcept {
  return static_cast<std::uint16_t>(bits & static_cast<std::uint16_t>(~bits + 1u));
}

int bit_index(std::uint16_t bit) noexcept {
  int index = 0;
  while (bit > 1u) {
    bit = static_cast<std::uint16_t>(bit >> 1);
    ++index;
  }
  return index;
}

}  // namespace

void PadChords::begin_tap(One& one, std::int64_t now_ms) noexcept {
  one.phase = Phase::tap;
  one.at = now_ms;
  one.reads = 1;  // this read shows the key
  ++one.steps;
}

ChordStep PadChords::step(One& one, std::uint16_t real, Aside aside, Hotkeys& hotkeys, std::int64_t now_ms) noexcept {
  const auto pressed = static_cast<std::uint16_t>(real & ~one.before);
  for (std::uint16_t rest = pressed; rest != 0; rest = static_cast<std::uint16_t>(rest & (rest - 1))) {
    one.since[static_cast<std::size_t>(bit_index(lowest(rest)))] = now_ms;
  }
  one.before = real;
  one.hidden = static_cast<std::uint16_t>(one.hidden & real);  // a hidden button seen up is the game's again
  ChordStep result = ChordStep::none;

  if (one.phase == Phase::tap) {
    if (aside == Aside::holder ||
        (one.reads >= kChordTapReads && now_ms - one.at >= kChordTapMs)) {
      // The tap's release: a key still held stays hidden until it is released, so the game sees one press.
      one.phase = Phase::idle;
      ++one.steps;
      one.hidden = static_cast<std::uint16_t>(one.hidden | (real & one.key));
    } else {
      ++one.reads;
    }
  } else if (one.phase == Phase::pending) {
    if (aside == Aside::holder) {
      // The holder has the pad; the key it held back stays hidden until released, so no late press reaches the game.
      one.phase = Phase::idle;
      one.hidden = static_cast<std::uint16_t>(one.hidden | (real & one.key));
      result = ChordStep::dropped;
    } else if ((real & one.key) == 0 || (real & one.anchor) == 0 || aside == Aside::not_now) {
      begin_tap(one, now_ms);
      result = ChordStep::failed;
    } else if (now_ms - one.at >= kChordCompleteMs) {
      if (hotkeys.pressed(one.id)) {
        one.phase = Phase::idle;
        one.hidden = static_cast<std::uint16_t>(one.hidden | one.key | one.anchor);
        ++one.steps;  // the anchor the game saw held is hidden from now on
        result = ChordStep::fired;
      } else {
        begin_tap(one, now_ms);  // the hotkey went while its chord resolved
        result = ChordStep::failed;
      }
    }
  }

  if (one.phase == Phase::idle && aside == Aside::none && result == ChordStep::none) {
    // A fresh press of a chord's key, while its anchor has been held long enough and the game sees it.
    const auto fresh = static_cast<std::uint16_t>(pressed & hotkeys.chord_keys() & ~one.hidden);
    for (std::uint16_t keys = fresh; keys != 0 && one.phase == Phase::idle;
         keys = static_cast<std::uint16_t>(keys & (keys - 1))) {
      const std::uint16_t key = lowest(keys);
      const auto anchors = static_cast<std::uint16_t>(real & ~pressed & ~one.hidden & kChordButtons);
      for (std::uint16_t rest = anchors; rest != 0; rest = static_cast<std::uint16_t>(rest & (rest - 1))) {
        const std::uint16_t anchor = lowest(rest);
        if (now_ms - one.since[static_cast<std::size_t>(bit_index(anchor))] < kChordHoldBackMs) {
          continue;
        }
        if (const std::uint32_t id = hotkeys.match_chord(anchor, key); id != 0) {
          one.phase = Phase::pending;
          one.key = key;
          one.anchor = anchor;
          one.id = id;
          one.at = now_ms;
          result = ChordStep::held_back;
          break;
        }
      }
    }
  }

  ChordView view;
  view.hide = static_cast<std::uint16_t>(one.hidden | (one.phase == Phase::pending ? one.key : 0u));
  view.show = one.phase == Phase::tap ? one.key : std::uint16_t{0};
  view.packet = one.steps;
  one.view.store(pack(view), std::memory_order_release);
  return result;
}

ChordStep PadChords::filter(unsigned index, std::uint16_t real, std::uint16_t& buttons, std::uint32_t& packet,
                            Aside aside, Hotkeys& hotkeys, std::int64_t now_ms) noexcept {
  if (index >= kPads) {
    return ChordStep::none;
  }
  One& one = pads_[index];
  ChordStep result = ChordStep::none;
  if (!one.busy.test_and_set(std::memory_order_acquire)) {
    result = step(one, real, aside, hotkeys, now_ms);
    one.busy.clear(std::memory_order_release);
  }
  const ChordView view = unpack(one.view.load(std::memory_order_acquire));
  buttons = static_cast<std::uint16_t>((buttons & ~view.hide) | (aside == Aside::holder ? 0u : view.show));
  packet += view.packet;
  return result;
}

void PadChords::disconnected(unsigned index) noexcept {
  if (index >= kPads) {
    return;
  }
  One& one = pads_[index];
  if (one.busy.test_and_set(std::memory_order_acquire)) {
    return;  // another reader has it: its next read sees the pad gone too
  }
  one.before = 0;
  one.hidden = 0;
  one.phase = Phase::idle;
  ChordView view;
  view.packet = one.steps;
  one.view.store(pack(view), std::memory_order_release);
  one.busy.clear(std::memory_order_release);
}

ChordView PadChords::view(unsigned index) const noexcept {
  return index < kPads ? unpack(pads_[index].view.load(std::memory_order_acquire)) : ChordView{};
}

}  // namespace grace::input
