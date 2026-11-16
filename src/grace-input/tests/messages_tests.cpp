// The window procedure's filter rules (messages.h): what reaches the game, what the holder gets, and what the menu key,
// Escape and focus loss do.
#include <doctest/doctest.h>

#include "grace/input/messages.h"

namespace {

using namespace grace::input;

constexpr Conditions kReady{true, false, false, false};
constexpr std::int64_t kStart = 100000;
constexpr Key kA = 0x1E;
constexpr Key kW = 0x11;
constexpr Key kAlt = 0x38;
constexpr Key kF4 = 0x3E;
constexpr std::uint32_t kVkA = 'A';
constexpr std::uint32_t kVkW = 'W';
constexpr std::uint32_t kVkEscape = 0x1B;
constexpr std::uint32_t kVkF5 = 0x74;

struct World {
  Capture capture;
  KeyboardLedger keyboard;
  MouseLedger mouse;
  MessageRules rules{capture, keyboard, mouse};

  Decision press(Key key, std::uint32_t vk, std::int64_t now = kStart) {
    return rules.key(key, vk, true, false, false, now);
  }
  Decision repeat(Key key, std::uint32_t vk, std::int64_t now = kStart) {
    return rules.key(key, vk, true, false, true, now);
  }
  Decision release(Key key, std::uint32_t vk, std::int64_t now = kStart) {
    return rules.key(key, vk, false, false, false, now);
  }
  Request take(Kind kind = Kind::panel, std::uint8_t wants = want_all, std::int64_t now = kStart) {
    return capture.request(1, kind, wants, kReady, now);
  }
};

}  // namespace

TEST_CASE("with no holder, every message reaches the game") {
  World world;
  CHECK(world.press(kA, kVkA).route == Route::game);
  CHECK(world.rules.character(kA).route == Route::game);
  CHECK(world.release(kA, kVkA).route == Route::game);
  CHECK(world.rules.mouse_button(0, true).route == Route::game);
  CHECK(world.rules.mouse_motion().route == Route::game);
  CHECK(world.rules.mouse_button(0, false).route == Route::game);
  CHECK(world.rules.ime().route == Route::game);
  CHECK(world.rules.raw_input(want_mouse).route == Route::game);
  CHECK(world.rules.character(kNoKey).route == Route::game);
  CHECK_FALSE(world.rules.character(kA).to_holder);
}

TEST_CASE("the menu key never reaches the game, and is never a holder's") {
  World world;
  Decision decision = world.press(kF5, kVkF5);
  CHECK(decision.route == Route::consume);
  CHECK(decision.menu == MenuKey::open_menu);
  CHECK(world.repeat(kF5, kVkF5).route == Route::consume);
  CHECK(world.release(kF5, kVkF5).route == Route::consume);
  REQUIRE(world.take().answer == Answer::granted);
  decision = world.press(kF5, kVkF5);
  CHECK(decision.route == Route::consume);
  CHECK_FALSE(decision.to_holder);
  CHECK(decision.menu == MenuKey::notice);
  CHECK(world.release(kF5, kVkF5).route == Route::consume);
}

TEST_CASE("while a holder has the keyboard, its messages are the holder's, Alt+F4 excepted") {
  World world;
  REQUIRE(world.take().answer == Answer::granted);
  Decision decision = world.press(kA, kVkA);
  CHECK(decision.route == Route::consume);
  CHECK(decision.to_holder);
  decision = world.rules.character(kA);
  CHECK(decision.route == Route::consume);
  CHECK(decision.to_holder);
  CHECK(world.repeat(kA, kVkA).to_holder);
  decision = world.release(kA, kVkA);
  CHECK(decision.route == Route::consume);
  CHECK(decision.to_holder);
  // A character no key made (an IME's, or a posted one) is the holder's too.
  decision = world.rules.character(kNoKey);
  CHECK(decision.route == Route::consume);
  CHECK(decision.to_holder);
  // System keys are consumed too, but Alt+F4 still closes the game.
  CHECK(world.rules.key(kAlt, 0x12, true, true, false, kStart).route == Route::consume);
  CHECK(world.rules.key(kF4, kVkF4, true, true, false, kStart).route == Route::game);
  CHECK(world.rules.key(kF4, kVkF4, false, true, false, kStart).route == Route::game);
  // Dead keys and system characters are never a holder's: the composed character comes with WM_CHAR.
  decision = world.rules.other_character(kA);
  CHECK(decision.route == Route::consume);
  CHECK_FALSE(decision.to_holder);
}

TEST_CASE("the opening press does nothing in the holder until it is released") {
  World world;
  CHECK(world.press(kW, kVkW).route == Route::game);  // the game sees W go down: the player runs
  REQUIRE(world.take().answer == Answer::granted);
  Decision decision = world.repeat(kW, kVkW);
  CHECK(decision.route == Route::consume);  // the game has its own copy of W through DirectInput, masked
  CHECK_FALSE(decision.to_holder);
  decision = world.rules.character(kW);
  CHECK(decision.route == Route::consume);
  CHECK_FALSE(decision.to_holder);  // no stray "w" in the holder's text
  decision = world.release(kW, kVkW);
  CHECK(decision.route == Route::game);  // a release the game saw begin always reaches it
  CHECK_FALSE(decision.to_holder);
  decision = world.press(kW, kVkW);  // pressed again: the holder's now
  CHECK(decision.to_holder);
  CHECK(decision.route == Route::consume);
}

TEST_CASE("a key DirectInput saw down at the grant is the opening press, even if its message comes late") {
  World world;
  std::uint8_t state[256] = {};
  state[kA] = 0x80;
  world.keyboard.filter(state, world.capture, kF5, kStart);  // the game's read sees A down (a hotkey, say)
  REQUIRE(world.take().answer == Answer::granted);
  Decision decision = world.press(kA, kVkA);  // its WM_KEYDOWN arrives after the grant
  CHECK(decision.route == Route::consume);
  CHECK_FALSE(decision.to_holder);
  CHECK_FALSE(world.rules.character(kA).to_holder);
  CHECK_FALSE(world.release(kA, kVkA).to_holder);
  CHECK(world.press(kA, kVkA).to_holder);
}

TEST_CASE("the closing press never reaches the game: its repeats and release stay GraceUI's") {
  World world;
  REQUIRE(world.take(Kind::modal).answer == Answer::granted);
  Decision decision = world.press(kEscape, kVkEscape);
  CHECK(decision.route == Route::consume);
  CHECK(decision.to_holder);  // the holder sees Escape and closes itself
  REQUIRE(world.capture.release(1, kStart + 5));
  CHECK(world.repeat(kEscape, kVkEscape, kStart + 40).route == Route::consume);
  CHECK(world.rules.character(kEscape).route == Route::consume);
  CHECK(world.release(kEscape, kVkEscape, kStart + 80).route == Route::consume);
  // Released: the next press is the game's.
  CHECK(world.press(kEscape, kVkEscape, kStart + 400).route == Route::game);
  CHECK(world.release(kEscape, kVkEscape, kStart + 450).route == Route::game);
}

TEST_CASE("a key held from before the holder: its release during the holder still reaches the game") {
  World world;
  CHECK(world.press(kW, kVkW).route == Route::game);
  REQUIRE(world.take().answer == Answer::granted);
  CHECK(world.release(kW, kVkW).route == Route::game);
  REQUIRE(world.capture.release(1, kStart + 5));
  CHECK(world.press(kW, kVkW).route == Route::game);
}

TEST_CASE("a fresh press after the holder ends is the game's, even if an older release was lost") {
  World world;
  REQUIRE(world.take().answer == Answer::granted);
  world.press(kA, kVkA);
  REQUIRE(world.capture.release(1, kStart + 5));
  CHECK(world.press(kA, kVkA, kStart + 900).route == Route::game);
  CHECK(world.release(kA, kVkA, kStart + 950).route == Route::game);
}

TEST_CASE("the player's exit: a second menu-key press within 2 s") {
  World world;
  REQUIRE(world.take(Kind::panel).answer == Answer::granted);
  CHECK(world.press(kF5, kVkF5, kStart + 100).menu == MenuKey::notice);
  world.release(kF5, kVkF5, kStart + 150);
  const Decision decision = world.press(kF5, kVkF5, kStart + 700);
  CHECK(decision.menu == MenuKey::player_exit);
  CHECK(decision.closed.owner == 1);
  CHECK(decision.closed.kind == Kind::panel);
  CHECK_FALSE(world.capture.holder().holds());
  CHECK(world.capture.last_close().why == Closed::player);
  CHECK(world.release(kF5, kVkF5, kStart + 750).route == Route::consume);
}

TEST_CASE("the menu key closes the menu when the menu holds input") {
  World world;
  REQUIRE(world.take(Kind::menu).answer == Answer::granted);
  const Decision decision = world.press(kF5, kVkF5, kStart + 100);
  CHECK(decision.menu == MenuKey::closed_menu);
  CHECK(decision.closed.kind == Kind::menu);
  CHECK(world.capture.last_close().why == Closed::menu_key);
}

TEST_CASE("the player's exit: Escape held 1 s, seen through the window's messages") {
  World world;
  REQUIRE(world.take(Kind::modal).answer == Answer::granted);
  world.capture.rendered(kStart);
  world.press(kEscape, kVkEscape, kStart + 100);
  for (std::int64_t t = kStart + 600; t < kStart + 1100; t += 33) {
    CHECK(world.repeat(kEscape, kVkEscape, t).route == Route::consume);
    world.capture.rendered(t);
    CHECK_FALSE(world.capture.tick(t).holds());
  }
  world.capture.rendered(kStart + 1100);
  CHECK(world.capture.tick(kStart + 1100).holds());
  CHECK(world.capture.last_close().why == Closed::player);
  // Escape is still held after the exit: its repeats and release never reach the game (its menu stays shut).
  CHECK(world.repeat(kEscape, kVkEscape, kStart + 1133).route == Route::consume);
  CHECK(world.release(kEscape, kVkEscape, kStart + 1200).route == Route::consume);
}

TEST_CASE("focus loss ends the holder and forgets every key seen held") {
  World world;
  world.press(kW, kVkW);
  REQUIRE(world.take().answer == Answer::granted);
  world.press(kA, kVkA);
  world.rules.mouse_button(1, true);
  const Holder closed = world.rules.focus_lost(kStart + 10);
  CHECK(closed.owner == 1);
  CHECK(world.capture.last_close().why == Closed::focus_lost);
  CHECK_FALSE(world.rules.game_down().any());
  CHECK_FALSE(world.rules.captured_down().any());
  CHECK_FALSE(world.rules.holder_down().any());
  // Back in focus, every key is pressed afresh, and the game's.
  CHECK(world.press(kA, kVkA, kStart + 500).route == Route::game);
  CHECK(world.rules.mouse_button(1, true).route == Route::game);
  CHECK_FALSE(world.rules.focus_lost(kStart + 600).holds());  // no holder: nothing to close
}

TEST_CASE("mouse: the holder's while it has the mouse; a press it began never reaches the game") {
  World world;
  CHECK(world.rules.mouse_button(0, true).route == Route::game);  // held as the holder takes input
  REQUIRE(world.take().answer == Answer::granted);
  CHECK(world.rules.mouse_motion().to_holder);
  CHECK(world.rules.mouse_motion().route == Route::consume);
  Decision decision = world.rules.mouse_button(0, false);
  CHECK(decision.route == Route::game);  // the game saw it go down
  CHECK_FALSE(decision.to_holder);       // the holder did not
  decision = world.rules.mouse_button(1, true);
  CHECK(decision.route == Route::consume);
  CHECK(decision.to_holder);
  REQUIRE(world.capture.release(1, kStart + 5));
  decision = world.rules.mouse_button(1, false);
  CHECK(decision.route == Route::consume);  // released after the holder ended: still never the game's
  CHECK_FALSE(decision.to_holder);
  CHECK(world.rules.mouse_motion().route == Route::game);
}

TEST_CASE("a holder that takes only the mouse leaves the keyboard to the game") {
  World world;
  REQUIRE(world.take(Kind::panel, want_mouse).answer == Answer::granted);
  CHECK(world.press(kA, kVkA).route == Route::game);
  CHECK(world.rules.character(kA).route == Route::game);
  CHECK(world.rules.ime().route == Route::game);
  CHECK(world.rules.mouse_motion().route == Route::consume);
  CHECK(world.rules.raw_input(want_mouse).route == Route::default_proc);
  CHECK(world.rules.raw_input(want_keyboard).route == Route::game);
}

TEST_CASE("IME messages go to the default window procedure while a holder has the keyboard, never to the game") {
  World world;
  REQUIRE(world.take().answer == Answer::granted);
  CHECK(world.rules.ime().route == Route::default_proc);
  CHECK(world.rules.raw_input(want_keyboard).route == Route::default_proc);
}

TEST_CASE("a new holder starts its own view of what is down") {
  World world;
  REQUIRE(world.take(Kind::panel).answer == Answer::granted);
  CHECK(world.press(kA, kVkA).to_holder);
  REQUIRE(world.capture.release(1, kStart + 5));
  REQUIRE(world.capture.request(2, Kind::modal, want_all, kReady, kStart + 10).answer == Answer::granted);
  // A was pressed in the last holder and is still down: in this one it is the opening press.
  CHECK_FALSE(world.repeat(kA, kVkA).to_holder);
  CHECK_FALSE(world.release(kA, kVkA).to_holder);
  CHECK(world.press(kA, kVkA).to_holder);
}
