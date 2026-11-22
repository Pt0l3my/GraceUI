// The mods' pad chords (chords.h, D204), by timelines of pad states and times: what the game's XInputGetState returns
// read by read, and the hotkey's presses.
#include <doctest/doctest.h>

#include "grace/input/chords.h"

namespace {

using namespace grace::input;

constexpr std::uint16_t kUp = 0x0001;    // XINPUT_GAMEPAD_DPAD_UP
constexpr std::uint16_t kDown = 0x0002;  // XINPUT_GAMEPAD_DPAD_DOWN
constexpr std::uint16_t kView = 0x0020;  // XINPUT_GAMEPAD_BACK
constexpr std::uint16_t kA = 0x1000;     // XINPUT_GAMEPAD_A
constexpr std::uint32_t kChord = kView | (std::uint32_t{kUp} << 16);  // EldenNet's: D-pad Up, then View
constexpr std::uint32_t kPacket = 7;     // the pad's own packet number, which these timelines never change
constexpr std::int64_t kStart = 100000;

struct Read {
  std::uint16_t buttons = 0;
  std::uint32_t packet = 0;
  ChordStep step = ChordStep::none;
};

struct Pads {
  Hotkeys hotkeys;
  PadChords chords;
  std::uint32_t id = 0;

  Pads() {
    Binding binding;
    REQUIRE(hotkeys.add(1, "talk", kNoKey, 0, kChord, kF5, id, &binding) == Added::bound);
    CHECK_FALSE(binding.key);
    CHECK(binding.chord);
  }

  // One read of pad `index` at kStart + `at` ms: the real buttons in, what the game sees out.
  Read read(std::uint16_t real, std::int64_t at, Aside aside = Aside::none, unsigned index = 0) {
    Read out;
    out.buttons = real;
    out.packet = kPacket;
    out.step = chords.filter(index, real, out.buttons, out.packet, aside, hotkeys, kStart + at);
    return out;
  }

  std::uint32_t presses() {
    Press press;
    return hotkeys.take(1, press) ? press.count : 0;
  }
};

}  // namespace

TEST_CASE("a chord's anchor always reaches the game, and a key pressed within 150 ms of it too") {
  Pads pads;
  CHECK(pads.read(kUp, 0).buttons == kUp);
  CHECK(pads.read(kUp, 50).buttons == kUp);
  const Read early = pads.read(kUp | kView, 100);
  CHECK(early.buttons == (kUp | kView));
  CHECK(early.step == ChordStep::none);
  CHECK(pads.read(kUp | kView, 600).buttons == (kUp | kView));  // not a chord: nothing resolves later either
  CHECK(pads.read(0, 700).buttons == 0);
  CHECK(pads.presses() == 0);
}

TEST_CASE("both pressed at once, or the key first, is not the chord") {
  Pads pads;
  CHECK(pads.read(kUp | kView, 0).buttons == (kUp | kView));
  CHECK(pads.read(kUp | kView, 500).buttons == (kUp | kView));
  CHECK(pads.read(0, 600).buttons == 0);
  CHECK(pads.read(kView, 700).buttons == kView);
  CHECK(pads.read(kView | kUp, 1000).buttons == (kView | kUp));
  CHECK(pads.read(kView | kUp, 1500).buttons == (kView | kUp));
  CHECK(pads.presses() == 0);
}

TEST_CASE("a key 200 ms after the anchor is held back; at 0.4 s the hotkey fires once and both are hidden until "
          "released") {
  Pads pads;
  CHECK(pads.read(kUp, 0).buttons == kUp);
  const Read held = pads.read(kUp | kView, 200);
  CHECK(held.step == ChordStep::held_back);
  CHECK(held.buttons == kUp);  // the game sees the anchor alone
  CHECK(held.packet == kPacket);
  CHECK(pads.read(kUp | kView, 400).buttons == kUp);
  CHECK(pads.read(kUp | kView, 599).buttons == kUp);
  CHECK(pads.presses() == 0);
  const Read fired = pads.read(kUp | kView, 600);
  CHECK(fired.step == ChordStep::fired);
  CHECK(fired.buttons == 0);              // the anchor too, from now on
  CHECK(fired.packet == kPacket + 1);     // the change is read
  CHECK(pads.presses() == 1);
  CHECK(pads.read(kUp | kView, 2000).buttons == 0);  // a long hold reads as nothing, not a second spell switch
  CHECK(pads.read(kUp | kView, 2000).step == ChordStep::none);
  CHECK(pads.read(kUp, 2100).buttons == 0);  // View released: Up still hidden until it is released
  CHECK(pads.read(0, 2200).buttons == 0);
  CHECK(pads.read(kUp, 2300).buttons == kUp);  // a fresh press is the game's
  CHECK(pads.presses() == 0);                  // fired once
}

TEST_CASE("the key released before 0.4 s is replayed as one tap of two reads and 50 ms") {
  Pads pads;
  pads.read(kUp, 0);
  CHECK(pads.read(kUp | kView, 200).step == ChordStep::held_back);
  const Read failed = pads.read(kUp, 300);
  CHECK(failed.step == ChordStep::failed);
  CHECK(failed.buttons == (kUp | kView));  // the tap's press, though View is up
  CHECK(failed.packet == kPacket + 1);
  const Read second = pads.read(kUp, 310);  // two reads, but only 10 ms
  CHECK(second.buttons == (kUp | kView));
  CHECK(second.packet == kPacket + 1);
  const Read third = pads.read(kUp, 349);
  CHECK(third.buttons == (kUp | kView));
  const Read released = pads.read(kUp, 350);  // 50 ms and three reads
  CHECK(released.buttons == kUp);
  CHECK(released.packet == kPacket + 2);
  CHECK(pads.read(kUp, 400).buttons == kUp);
  CHECK(pads.read(kUp, 800).buttons == kUp);  // nothing more comes, and nothing fires
  CHECK(pads.presses() == 0);
}

TEST_CASE("a tap needs two reads however long it waits") {
  Pads pads;
  pads.read(kUp, 0);
  pads.read(kUp | kView, 200);
  CHECK(pads.read(0, 300).buttons == kView);  // the anchor released first: the tap
  CHECK(pads.read(0, 900).buttons == kView);  // the game's next read, long after, still sees it
  CHECK(pads.read(0, 901).buttons == 0);
}

TEST_CASE("the anchor released first replays the tap, and a key still held then stays hidden until released") {
  Pads pads;
  pads.read(kUp, 0);
  pads.read(kUp | kView, 200);
  const Read failed = pads.read(kView, 300);
  CHECK(failed.step == ChordStep::failed);
  CHECK(failed.buttons == kView);
  CHECK(pads.read(kView, 320).buttons == kView);
  const Read released = pads.read(kView, 360);
  CHECK(released.buttons == 0);  // the tap ends; View is still held, and hidden, so the game sees one press
  CHECK(released.packet == kPacket + 2);
  CHECK(pads.read(kView, 900).buttons == 0);
  CHECK(pads.read(0, 950).buttons == 0);
  CHECK(pads.read(kView, 1000).buttons == kView);  // a fresh press is the game's
  CHECK(pads.presses() == 0);
}

TEST_CASE("while a holder has input, chords stand aside, and one resolving is dropped with no tap") {
  Pads pads;
  pads.read(kUp, 0, Aside::holder);
  CHECK(pads.read(kUp | kView, 200, Aside::holder).step == ChordStep::none);
  CHECK(pads.read(kUp | kView, 700, Aside::holder).step == ChordStep::none);
  CHECK(pads.presses() == 0);

  Pads other;
  other.read(kUp, 0);
  CHECK(other.read(kUp | kView, 200).step == ChordStep::held_back);
  const Read dropped = other.read(0, 300, Aside::holder);
  CHECK(dropped.step == ChordStep::dropped);
  CHECK(dropped.buttons == 0);
  CHECK(other.read(0, 310).buttons == 0);  // no tap afterwards
  CHECK(other.presses() == 0);
}

TEST_CASE("while input would be refused, a chord neither starts nor holds anything back, and one resolving fails "
          "with its tap") {
  Pads pads;
  pads.read(kUp, 0, Aside::not_now);
  const Read passes = pads.read(kUp | kView, 200, Aside::not_now);
  CHECK(passes.step == ChordStep::none);
  CHECK(passes.buttons == (kUp | kView));  // View reaches the game at once in a fight or a game menu
  CHECK(pads.read(kUp | kView, 700, Aside::not_now).buttons == (kUp | kView));
  CHECK(pads.presses() == 0);

  Pads other;
  other.read(kUp, 0);
  CHECK(other.read(kUp | kView, 200).step == ChordStep::held_back);
  const Read failed = other.read(kUp | kView, 300, Aside::not_now);  // danger, while it resolves
  CHECK(failed.step == ChordStep::failed);
  CHECK(failed.buttons == (kUp | kView));
  other.read(kUp | kView, 320, Aside::not_now);
  CHECK(other.read(kUp | kView, 360, Aside::not_now).buttons == kUp);  // tapped once, then hidden until released
  CHECK(other.presses() == 0);
}

TEST_CASE("two pads at once, each its own") {
  Pads pads;
  pads.read(kUp, 0, Aside::none, 0);
  pads.read(kA, 0, Aside::none, 1);
  CHECK(pads.read(kUp | kView, 200, Aside::none, 0).step == ChordStep::held_back);
  CHECK(pads.read(kA | kView, 200, Aside::none, 1).buttons == (kA | kView));  // pad 1 holds no anchor
  CHECK(pads.read(kUp, 250, Aside::none, 1).buttons == kUp);
  CHECK(pads.read(kUp | kView, 600, Aside::none, 0).step == ChordStep::fired);
  CHECK(pads.read(kUp, 600, Aside::none, 1).buttons == kUp);  // pad 1's Up is its own, unhidden
  CHECK(pads.presses() == 1);
}

TEST_CASE("a chord's hotkey removed while it resolves fires nothing, and its key is tapped") {
  Pads pads;
  pads.read(kUp, 0);
  pads.read(kUp | kView, 200);
  pads.hotkeys.remove(pads.id);
  const Read failed = pads.read(kUp | kView, 600);
  CHECK(failed.step == ChordStep::failed);
  CHECK(failed.buttons == (kUp | kView));
  CHECK(pads.presses() == 0);
}

TEST_CASE("a pad that goes forgets its chord") {
  Pads pads;
  pads.read(kUp, 0);
  pads.read(kUp | kView, 200);
  pads.chords.disconnected(0);
  CHECK(pads.read(kUp | kView, 600).buttons == (kUp | kView));  // a new pad: both its own presses
  CHECK(pads.presses() == 0);
}

TEST_CASE("a chord is two different buttons, one each, of XInput's fourteen") {
  CHECK(valid_chord(kChord));
  CHECK(valid_chord(kA | (std::uint32_t{kDown} << 16)));
  CHECK_FALSE(valid_chord(0));
  CHECK_FALSE(valid_chord(kView | kUp));                             // a mask: no order
  CHECK_FALSE(valid_chord(kUp | (std::uint32_t{kUp} << 16)));        // the same button twice
  CHECK_FALSE(valid_chord(0x0400u | (std::uint32_t{kUp} << 16)));    // not a button
  CHECK_FALSE(valid_chord(kView | (std::uint32_t{kUp | kDown} << 16)));
  CHECK_FALSE(valid_chord(std::uint32_t{kUp} << 16));                // no key
}

TEST_CASE("a chord another hotkey has is registered unbound; the key and the chord are bound each on its own") {
  Hotkeys hotkeys;
  std::uint32_t first = 0;
  std::uint32_t second = 0;
  std::uint32_t third = 0;
  Binding binding;
  CHECK(hotkeys.add(1, "talk", kNoKey, 0, kChord, kF5, first, &binding) == Added::bound);
  CHECK(hotkeys.add(2, "shout", 0x21, 0, kChord, kF5, second, &binding) == Added::unbound);  // F, and the chord taken
  CHECK(binding.key);
  CHECK_FALSE(binding.chord);
  CHECK(hotkeys.match_chord(kUp, kView) == first);
  CHECK(hotkeys.match(0x21, 0) == second);  // its key still fires it
  CHECK(hotkeys.add(3, "wave", kNoKey, 0, 0x0400u | (std::uint32_t{kUp} << 16), kF5, third, &binding) ==
        Added::invalid);
  CHECK(hotkeys.chord_keys() == kView);
  hotkeys.remove(first);
  CHECK(hotkeys.match_chord(kUp, kView) == 0);  // the second keeps its chord unbound (the player binds it, 0.6)
  CHECK(hotkeys.chord_keys() == 0);
  CHECK(hotkeys.add(3, "wave", kNoKey, 0, kChord, kF5, third, &binding) == Added::bound);
  CHECK(hotkeys.pressed(third));  // a press through its chord, its key unbound
}
