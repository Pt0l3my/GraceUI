// The game's view of DirectInput and XInput (devices.h): untouched with no holder, at rest while a holder takes the
// device, a closing press hidden until released; and the holder's own view of a pad.
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include "grace/input/devices.h"

namespace {

using namespace grace::input;

constexpr Conditions kReady{true, false, false, false};
constexpr std::int64_t kStart = 100000;
constexpr Key kA = 0x1E;
constexpr Key kW = 0x11;
constexpr std::uint16_t kButtonA = 0x1000;  // XINPUT_GAMEPAD_A
constexpr std::uint16_t kButtonB = 0x2000;  // XINPUT_GAMEPAD_B

struct Keyboard {
  std::uint8_t bytes[256] = {};
  Keyboard& down(Key key) {
    bytes[key] = 0x80;
    return *this;
  }
};

// One DirectInput read: the real state in, what the game sees out.
Keyboard read(KeyboardLedger& ledger, const Capture& capture, Keyboard real, std::int64_t now) {
  ledger.filter(real.bytes, capture, kF5, now);
  return real;
}

bool none_down(const Keyboard& keyboard) {
  return std::all_of(std::begin(keyboard.bytes), std::end(keyboard.bytes), [](std::uint8_t b) { return b == 0; });
}

}  // namespace

TEST_CASE("DirectInput keyboard: untouched with no holder, but for the menu key") {
  Capture capture;
  KeyboardLedger ledger;
  Keyboard real;
  real.down(kA).down(kW).down(kEscape);
  real.bytes[0x20] = 0x01;  // the bits below bit 7 pass as they are
  const Keyboard seen = read(ledger, capture, real, kStart);
  CHECK(std::memcmp(seen.bytes, real.bytes, 256) == 0);
  Keyboard with_menu_key = real;
  with_menu_key.down(kF5);
  const Keyboard seen2 = read(ledger, capture, with_menu_key, kStart);
  CHECK(seen2.bytes[kF5] == 0);
  CHECK(seen2.bytes[kA] == 0x80);
}

TEST_CASE("DirectInput keyboard: nothing while a holder has it, and the closing press hidden until released") {
  Capture capture;
  KeyboardLedger ledger;
  REQUIRE(capture.request(1, Kind::panel, want_all, kReady, kStart).answer == Answer::granted);
  CHECK(none_down(read(ledger, capture, Keyboard{}.down(kA).down(kW), kStart + 16)));
  // Escape pressed to close; the holder ends while it is down.
  CHECK(none_down(read(ledger, capture, Keyboard{}.down(kEscape), kStart + 32)));
  capture.close(Closed::released, kStart + 40);
  Keyboard seen = read(ledger, capture, Keyboard{}.down(kEscape), kStart + 48);
  CHECK(seen.bytes[kEscape] == 0);  // invariant 8: Escape never reaches the game, so its menu does not open
  seen = read(ledger, capture, Keyboard{}.down(kEscape).down(kA), kStart + 2000);
  CHECK(seen.bytes[kEscape] == 0);  // still held: still hidden
  CHECK(seen.bytes[kA] == 0x80);    // pressed after the close: the game's
  seen = read(ledger, capture, Keyboard{}, kStart + 2016);
  CHECK(none_down(seen));
  seen = read(ledger, capture, Keyboard{}.down(kEscape), kStart + 2032);
  CHECK(seen.bytes[kEscape] == 0x80);  // released, then pressed again: the game's
  CHECK_FALSE(ledger.masked().any());
}

TEST_CASE("DirectInput keyboard: a closing press not yet read at the close is hidden too") {
  Capture capture;
  KeyboardLedger ledger;
  REQUIRE(capture.request(1, Kind::menu, want_all, kReady, kStart).answer == Answer::granted);
  CHECK(none_down(read(ledger, capture, Keyboard{}, kStart + 16)));  // the last read before the press
  capture.close(Closed::menu_key, kStart + 20);                      // closed by a press no read has seen
  const Keyboard seen = read(ledger, capture, Keyboard{}.down(kF5).down(kEscape), kStart + 33);
  CHECK(none_down(seen));
}

TEST_CASE("DirectInput keyboard: a key pressed well after the close, and not held through it, is the game's") {
  Capture capture;
  KeyboardLedger ledger;
  REQUIRE(capture.request(1, Kind::menu, want_all, kReady, kStart).answer == Answer::granted);
  read(ledger, capture, Keyboard{}.down(kEscape), kStart + 16);
  capture.close(Closed::released, kStart + 20);
  // The game reads nothing for a while (a loading screen), then: Escape still held, W freshly pressed.
  const Keyboard seen = read(ledger, capture, Keyboard{}.down(kEscape).down(kW), kStart + 5000);
  CHECK(seen.bytes[kEscape] == 0);
  CHECK(seen.bytes[kW] == 0x80);
}

TEST_CASE("DirectInput keyboard: a holder that did not take the keyboard leaves no mask") {
  Capture capture;
  KeyboardLedger ledger;
  REQUIRE(capture.request(1, Kind::panel, want_mouse, kReady, kStart).answer == Answer::granted);
  CHECK(read(ledger, capture, Keyboard{}.down(kW), kStart + 16).bytes[kW] == 0x80);
  capture.close(Closed::released, kStart + 20);
  CHECK(read(ledger, capture, Keyboard{}.down(kW), kStart + 32).bytes[kW] == 0x80);
}

TEST_CASE("DirectInput mouse: at rest while a holder has it; a held button hidden until released") {
  Capture capture;
  MouseLedger ledger;
  MouseState real;
  real.x = 12;
  real.y = -7;
  real.z = 120;
  real.buttons[0] = 0x80;
  real.button_count = 8;
  MouseState seen = real;
  ledger.filter(seen, capture, kStart);
  CHECK(seen.x == 12);
  CHECK(seen.y == -7);
  CHECK(seen.z == 120);
  CHECK(seen.buttons[0] == 0x80);

  REQUIRE(capture.request(1, Kind::menu, want_all, kReady, kStart).answer == Answer::granted);
  seen = real;
  seen.buttons[1] = 0x80;
  ledger.filter(seen, capture, kStart + 16);
  CHECK(seen.x == 0);
  CHECK(seen.y == 0);
  CHECK(seen.z == 0);
  CHECK(std::all_of(seen.buttons.begin(), seen.buttons.end(), [](std::uint8_t b) { return b == 0; }));

  capture.close(Closed::released, kStart + 20);  // the right button still held
  seen = MouseState{};
  seen.x = 5;
  seen.buttons[1] = 0x80;
  seen.button_count = 4;
  ledger.filter(seen, capture, kStart + 32);
  CHECK(seen.x == 5);  // movement is the game's at once
  CHECK(seen.buttons[1] == 0);
  seen = MouseState{};
  ledger.filter(seen, capture, kStart + 48);
  seen.buttons[1] = 0x80;
  ledger.filter(seen, capture, kStart + 64);
  CHECK(seen.buttons[1] == 0x80);
}

TEST_CASE("XInput: untouched, packet number included, while no holder has ever had input") {
  Capture capture;
  PadLedger pads;
  Pad real;
  real.buttons = kButtonA;
  real.left_x = 20000;
  real.right_trigger = 200;
  Pad seen = real;
  std::uint32_t packet = 41;
  pads.filter(0, packet, seen, capture, kStart);
  CHECK(seen == real);
  CHECK(packet == 41);
}

TEST_CASE("XInput: a pad at rest while a holder has it, a changed packet at each grant and close") {
  Capture capture;
  PadLedger pads;
  Pad real;
  real.buttons = kButtonA;
  std::uint32_t packet = 10;
  Pad seen = real;
  pads.filter(0, packet, seen, capture, kStart);
  const std::uint32_t before = packet;

  REQUIRE(capture.request(1, Kind::menu, want_all, kReady, kStart + 1).answer == Answer::granted);
  packet = 10;  // the pad itself has not changed
  seen = real;
  pads.filter(0, packet, seen, capture, kStart + 16);
  CHECK(seen == Pad{});
  CHECK(packet != before);  // the game reads a change, to the pad at rest
  const std::uint32_t during = packet;

  capture.close(Closed::released, kStart + 30);
  packet = 10;
  seen = Pad{};
  pads.filter(0, packet, seen, capture, kStart + 32);
  CHECK(packet != during);
}

TEST_CASE("XInput: B held at the close stays hidden until released (it would dodge)") {
  Capture capture;
  PadLedger pads;
  REQUIRE(capture.request(1, Kind::menu, want_all, kReady, kStart).answer == Answer::granted);
  Pad real;
  real.buttons = kButtonB;
  real.left_trigger = 255;
  real.left_y = 30000;
  std::uint32_t packet = 1;
  Pad seen = real;
  pads.filter(0, packet, seen, capture, kStart + 16);
  capture.close(Closed::released, kStart + 20);

  seen = real;
  seen.buttons |= kButtonA;  // A pressed after the close: the game's
  packet = 2;
  pads.filter(0, packet, seen, capture, kStart + 3000);
  CHECK(seen.buttons == kButtonA);
  CHECK(seen.left_trigger == 0);
  CHECK(seen.left_y == 0);
  CHECK(pads.masked(0) == (kButtonB | kLeftTrigger | kLeftStick));

  Pad released;
  packet = 3;
  seen = released;
  pads.filter(0, packet, seen, capture, kStart + 3016);
  CHECK(pads.masked(0) == 0);
  seen = real;
  packet = 4;
  pads.filter(0, packet, seen, capture, kStart + 3032);
  CHECK(seen == real);
}

TEST_CASE("XInput: the holder's view, with the opening press inert until released") {
  Capture capture;
  PadLedger pads;
  Pad real;
  real.buttons = kButtonA;  // held as the holder takes input (a chord that opened it)
  std::uint32_t packet = 5;
  Pad seen = real;
  pads.filter(1, packet, seen, capture, kStart);
  REQUIRE(capture.request(1, Kind::menu, want_all, kReady, kStart + 1).answer == Answer::granted);
  const std::uint32_t generation = capture.holder().generation;
  seen = real;
  packet = 5;
  pads.filter(1, packet, seen, capture, kStart + 16);
  HolderPad view;
  REQUIRE(pads.holder_view(generation, view));
  CHECK(view.index == 1);
  CHECK(view.pad.buttons == 0);  // A does nothing in the holder yet
  CHECK_FALSE(pads.holder_view(generation + 1, view));

  real.buttons = kButtonA | kButtonB;
  seen = real;
  packet = 6;
  pads.filter(1, packet, seen, capture, kStart + 32);
  REQUIRE(pads.holder_view(generation, view));
  CHECK(view.pad.buttons == kButtonB);  // B is a new press: the holder's

  real.buttons = kButtonB;
  seen = real;
  packet = 7;
  pads.filter(1, packet, seen, capture, kStart + 48);  // A released
  real.buttons = kButtonA | kButtonB;
  seen = real;
  packet = 8;
  pads.filter(1, packet, seen, capture, kStart + 64);
  REQUIRE(pads.holder_view(generation, view));
  CHECK(view.pad.buttons == (kButtonA | kButtonB));
}

TEST_CASE("XInput: a press made just before the grant, first read after it, is the opening press") {
  Capture capture;
  PadLedger pads;
  REQUIRE(capture.request(1, Kind::menu, want_all, kReady, kStart).answer == Answer::granted);
  Pad real;
  real.buttons = kButtonA;
  std::uint32_t packet = 1;
  Pad seen = real;
  pads.filter(0, packet, seen, capture, kStart + 10);  // within the window: inert
  HolderPad view;
  REQUIRE(pads.holder_view(capture.holder().generation, view));
  CHECK(view.pad.buttons == 0);
}

TEST_CASE("XInput: the holder follows the pad that changed last; a disconnected pad drops out") {
  Capture capture;
  PadLedger pads;
  REQUIRE(capture.request(1, Kind::menu, want_all, kReady, kStart).answer == Answer::granted);
  const std::uint32_t generation = capture.holder().generation;
  std::uint32_t packet = 1;
  Pad seen;
  pads.filter(0, packet, seen, capture, kStart + 1000);
  packet = 1;
  seen = Pad{};
  pads.filter(2, packet, seen, capture, kStart + 1000);
  HolderPad view;
  REQUIRE(pads.holder_view(generation, view));
  CHECK(view.index == 2);
  packet = 2;
  seen = Pad{};
  seen.buttons = kButtonA;
  pads.filter(0, packet, seen, capture, kStart + 1016);
  REQUIRE(pads.holder_view(generation, view));
  CHECK(view.index == 0);
  CHECK(view.pad.buttons == kButtonA);
  pads.disconnected(0);
  REQUIRE(pads.holder_view(generation, view));
  CHECK(view.index == 2);
  pads.disconnected(2);
  CHECK_FALSE(pads.holder_view(generation, view));
}

TEST_CASE("pad controls past XInput's thresholds") {
  Pad pad;
  CHECK(held_controls(pad) == 0);
  pad.left_trigger = kTriggerThreshold;
  pad.right_trigger = static_cast<std::uint8_t>(kTriggerThreshold + 1);
  pad.left_x = static_cast<std::int16_t>(-kLeftStickDeadzone);
  pad.right_y = static_cast<std::int16_t>(-32768);
  CHECK(held_controls(pad) == (kRightTrigger | kRightStick));
  pad.left_x = static_cast<std::int16_t>(-kLeftStickDeadzone - 1);
  CHECK(held_controls(pad) == (kRightTrigger | kRightStick | kLeftStick));
  const Pad rest = without(pad, kRightTrigger | kRightStick | kLeftStick);
  CHECK(rest.right_trigger == 0);
  CHECK(rest.right_y == 0);
  CHECK(rest.left_x == 0);
  CHECK(rest.left_trigger == kTriggerThreshold);
}

TEST_CASE("devices read on two threads while holders come and go (ThreadSanitizer)") {
  Capture capture;
  KeyboardLedger keyboard;
  PadLedger pads;
  std::atomic<bool> stop{false};
  std::thread reader([&] {
    std::int64_t now = kStart;
    while (!stop.load()) {
      Keyboard real;
      real.down(kA);
      keyboard.filter(real.bytes, capture, kF5, ++now);
      std::uint32_t packet = 1;
      Pad pad;
      pad.buttons = kButtonA;
      pads.filter(0, packet, pad, capture, now);
    }
  });
  std::thread viewer([&] {
    HolderPad view;
    while (!stop.load()) {
      const std::uint32_t generation = capture.holder().generation;
      if (pads.holder_view(generation, view)) {
        CHECK(view.generation == generation);
      }
    }
  });
  for (int i = 0; i < 2000; ++i) {
    capture.request(1, Kind::panel, want_all, kReady, kStart + i);
    capture.close(Closed::released, kStart + i);
  }
  stop.store(true);
  reader.join();
  viewer.join();
  CHECK(capture.closes() == 2000);
}

TEST_CASE("DirectInput keyboard: a fresh-only key is hidden from a fresh press, never from one the game was shown "
          "(D280)") {
  Capture capture;
  KeyboardLedger ledger;
  KeyBits hidden;
  hidden.set(kF5);
  KeyBits fresh;
  const auto read_with = [&](Keyboard real) {
    ledger.filter(real.bytes, capture, hidden, fresh, kStart);
    return real;
  };
  // W is the game's while nothing binds it, and stays the game's while held once something does.
  CHECK(read_with(Keyboard{}.down(kW)).bytes[kW] == 0x80);
  fresh.set(kW);
  CHECK(read_with(Keyboard{}.down(kW)).bytes[kW] == 0x80);
  CHECK(read_with(Keyboard{}.down(kW).down(kA)).bytes[kW] == 0x80);
  // Released, then pressed afresh: hidden, and hidden while held.
  CHECK(none_down(read_with(Keyboard{})));
  CHECK(read_with(Keyboard{}.down(kW)).bytes[kW] == 0);
  CHECK(read_with(Keyboard{}.down(kW)).bytes[kW] == 0);
  // No longer fresh-only while held: shown from the next read, as it is the game's now.
  fresh.clear(kW);
  CHECK(read_with(Keyboard{}.down(kW)).bytes[kW] == 0x80);
  // `hidden` always wins, whatever the game was shown: the menu key, and a hotkey's press under way.
  hidden.set(kW);
  CHECK(read_with(Keyboard{}.down(kW)).bytes[kW] == 0);
  // A holder's capture hides everything and leaves nothing shown: after it, a fresh-only key is hidden from its press.
  hidden.clear(kW);
  fresh.set(kA);
  CHECK(read_with(Keyboard{}.down(kA)).bytes[kA] == 0);
}
