// The mods' hotkeys (hotkeys.h) and what the window procedure's filter and DirectInput's view do with them (D155).
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <string_view>
#include <thread>
#include <vector>

#include "grace/input/devices.h"
#include "grace/input/hotkeys.h"
#include "grace/input/messages.h"

namespace {

using namespace grace::input;

constexpr Conditions kReady{true, false, false, false};
constexpr std::int64_t kStart = 100000;
constexpr Key kA = 0x1E;
constexpr Key kK = 0x25;
constexpr Key kF4 = 0x3E;
constexpr Key kF11 = 0x57;
constexpr Key kLeftCtrl = 0x1D;
constexpr std::uint32_t kVkA = 'A';
constexpr std::uint32_t kVkK = 'K';
constexpr std::uint32_t kVkF11 = 0x7A;

struct World {
  Capture capture;
  KeyboardLedger keyboard;
  MouseLedger mouse;
  Hotkeys hotkeys;
  MessageRules rules{capture, keyboard, mouse};

  World() { rules.set_hotkeys(&hotkeys); }

  Decision press(Key key, std::uint32_t vk, std::uint8_t modifiers = 0, bool repeat = false) {
    return rules.key(key, vk, true, false, repeat, kStart, modifiers);
  }
  Decision release(Key key, std::uint32_t vk, std::uint8_t modifiers = 0) {
    return rules.key(key, vk, false, false, false, kStart, modifiers);
  }
  // DirectInput's view of a state with `keys` down.
  bool game_reads(Key key) {
    std::uint8_t state[256] = {};
    state[key] = 0x80;
    keyboard.filter(state, capture, rules.menu_key(), kStart);
    KeyBits hidden;
    hidden.set(rules.menu_key());
    std::uint8_t again[256] = {};
    again[key] = 0x80;
    keyboard.filter(again, capture, hidden | hotkeys.bare() | hotkeys.held(), kStart);
    return (again[key] & 0x80u) != 0;
  }
};

}  // namespace

TEST_CASE("a hotkey is bound, unbound when its default is taken, and refused for a reserved key") {
  Hotkeys hotkeys;
  std::uint32_t id = 0;
  CHECK(hotkeys.add(1, "debug-view", kF11, 0, kF5, id) == Added::bound);
  CHECK(id != 0);
  std::uint32_t other = 0;
  CHECK(hotkeys.add(2, "other-view", kF11, 0, kF5, other) == Added::unbound);  // taken by mod 1
  CHECK(other != 0);
  CHECK(other != id);
  CHECK(hotkeys.add(2, "with-ctrl", kF11, kModCtrl, kF5, other) == Added::bound);  // other modifiers: free
  CHECK(hotkeys.add(2, "menu", kF5, kModShift, kF5, other) == Added::unbound);     // the menu key is the host's
  CHECK(hotkeys.add(2, "none", kNoKey, 0, kF5, other) == Added::unbound);          // no default
  CHECK(hotkeys.add(2, "escape", kEscape, 0, kF5, other) == Added::invalid);
  CHECK(hotkeys.add(2, "ctrl", kLeftCtrl, 0, kF5, other) == Added::invalid);
  CHECK(hotkeys.add(2, "alt-f4", kF4, kModAlt, kF5, other) == Added::invalid);
  CHECK(hotkeys.add(2, "f4", kF4, 0, kF5, other) == Added::bound);
  CHECK(hotkeys.add(2, "bad-mods", kA, 8, kF5, other) == Added::invalid);
  CHECK(hotkeys.add(1, "debug-view", kA, 0, kF5, other) == Added::invalid);  // the mod's own action again
  CHECK(hotkeys.add(1, "", kA, 0, kF5, other) == Added::invalid);
  CHECK(hotkeys.match(kF11, 0) == id);
  CHECK(hotkeys.match(kF11, kModShift) == 0);
  CHECK(hotkeys.match(kF5, kModShift) == 0);
  CHECK(hotkeys.bare().test(kF11));
  CHECK(hotkeys.bare().test(kF4));
  CHECK_FALSE(hotkeys.bare().test(kF5));
  CHECK(hotkeys.bound_keys().test(kF11));
  CHECK(hotkeys.bound_keys().test(kF4));
  CHECK_FALSE(hotkeys.bound_keys().test(kF5));  // unbound
  CHECK_FALSE(hotkeys.bound_keys().test(kA));
}

TEST_CASE("a mod's hotkeys are bounded, and the session's") {
  Hotkeys hotkeys;
  std::uint32_t id = 0;
  char action[] = "action-0";
  for (std::uint32_t i = 0; i < kHotkeysPerMod; ++i) {
    action[7] = static_cast<char>('0' + i);
    CHECK(hotkeys.add(1, action, kNoKey, 0, kF5, id) == Added::unbound);
  }
  CHECK(hotkeys.add(1, "one-more", kNoKey, 0, kF5, id) == Added::full);
  for (std::uint16_t owner = 2; owner <= kMaxHotkeys / kHotkeysPerMod; ++owner) {
    for (std::uint32_t i = 0; i < kHotkeysPerMod; ++i) {
      action[7] = static_cast<char>('0' + i);
      CHECK(hotkeys.add(owner, action, kNoKey, 0, kF5, id) == Added::unbound);
    }
  }
  CHECK(hotkeys.count() == kMaxHotkeys);
  CHECK(hotkeys.add(99, "late", kNoKey, 0, kF5, id) == Added::full);
  hotkeys.remove_owner(1);
  CHECK(hotkeys.count() == kMaxHotkeys - kHotkeysPerMod);
  CHECK(hotkeys.add(99, "late", kNoKey, 0, kF5, id) == Added::unbound);
}

TEST_CASE("presses are counted for their hotkey and taken by its mod alone") {
  Hotkeys hotkeys;
  std::uint32_t f11 = 0;
  std::uint32_t k = 0;
  REQUIRE(hotkeys.add(1, "debug-view", kF11, 0, kF5, f11) == Added::bound);
  REQUIRE(hotkeys.add(2, "chat", kK, kModCtrl, kF5, k) == Added::bound);
  Press press;
  CHECK_FALSE(hotkeys.take(1, press));
  CHECK(hotkeys.pressed(f11));
  CHECK(hotkeys.pressed(f11));
  CHECK(hotkeys.pressed(k));
  CHECK_FALSE(hotkeys.take(3, press));
  REQUIRE(hotkeys.take(1, press));
  CHECK(press.id == f11);
  CHECK(press.count == 2);
  CHECK_FALSE(hotkeys.take(1, press));
  REQUIRE(hotkeys.take(2, press));
  CHECK(press.id == k);
  CHECK(press.count == 1);
  CHECK_FALSE(hotkeys.pressed(0));
  CHECK_FALSE(hotkeys.pressed(0xFFFFu));
}

TEST_CASE("a hotkey removed, or its slot given to another, counts no press of the old one") {
  Hotkeys hotkeys;
  std::uint32_t old_id = 0;
  REQUIRE(hotkeys.add(1, "debug-view", kF11, 0, kF5, old_id) == Added::bound);
  hotkeys.remove_owner(1);
  CHECK_FALSE(hotkeys.pressed(old_id));
  CHECK(hotkeys.match(kF11, 0) == 0);
  CHECK_FALSE(hotkeys.bare().test(kF11));
  std::uint32_t new_id = 0;
  REQUIRE(hotkeys.add(2, "debug-view", kF11, 0, kF5, new_id) == Added::bound);
  CHECK(new_id != old_id);
  CHECK_FALSE(hotkeys.pressed(old_id));
  Press press;
  CHECK_FALSE(hotkeys.take(2, press));
}

TEST_CASE("a hotkey's press fires it once and never reaches the game, its repeats, characters and release neither") {
  World world;
  std::uint32_t id = 0;
  REQUIRE(world.hotkeys.add(1, "debug-view", kF11, 0, kF5, id) == Added::bound);
  Decision decision = world.press(kF11, kVkF11);
  CHECK(decision.route == Route::consume);
  CHECK(decision.hotkey == id);
  CHECK_FALSE(decision.to_holder);
  decision = world.press(kF11, kVkF11, 0, true);
  CHECK(decision.route == Route::consume);
  CHECK(decision.hotkey == 0);
  CHECK(world.rules.character(kF11).route == Route::consume);
  CHECK_FALSE(world.game_reads(kF11));
  CHECK(world.release(kF11, kVkF11).route == Route::consume);
  CHECK_FALSE(world.game_reads(kF11));  // a bare key stays hidden while bound
  Press press;
  REQUIRE(world.hotkeys.take(1, press));
  CHECK(press.count == 1);
  // With other modifiers a bare-bound key fires nothing and is still not the game's.
  decision = world.press(kF11, kVkF11, kModShift);
  CHECK(decision.route == Route::consume);
  CHECK(decision.hotkey == 0);
  CHECK(world.release(kF11, kVkF11, kModShift).route == Route::consume);
  CHECK_FALSE(world.hotkeys.take(1, press));
}

TEST_CASE("a hotkey with modifiers takes its own press alone, and hides it from DirectInput until released") {
  World world;
  std::uint32_t id = 0;
  REQUIRE(world.hotkeys.add(1, "chat", kK, kModCtrl, kF5, id) == Added::bound);
  // K alone is the game's.
  CHECK(world.press(kK, kVkK).route == Route::game);
  CHECK(world.game_reads(kK));
  CHECK(world.release(kK, kVkK).route == Route::game);
  // Ctrl+K is the hotkey's, from its press to its release, whatever happens to Ctrl meanwhile.
  Decision decision = world.press(kK, kVkK, kModCtrl);
  CHECK(decision.route == Route::consume);
  CHECK(decision.hotkey == id);
  CHECK_FALSE(world.game_reads(kK));
  CHECK(world.press(kK, kVkK, 0, true).route == Route::consume);
  CHECK(world.rules.character(kK).route == Route::consume);
  CHECK(world.release(kK, kVkK).route == Route::consume);
  CHECK(world.game_reads(kK));
  CHECK_FALSE(world.rules.hotkey_down().test(kK));
}

TEST_CASE("a press the game saw begin reaches it to its release, though a hotkey binds its key meanwhile") {
  World world;
  CHECK(world.press(kF11, kVkF11).route == Route::game);
  std::uint32_t id = 0;
  REQUIRE(world.hotkeys.add(1, "debug-view", kF11, 0, kF5, id) == Added::bound);
  CHECK(world.press(kF11, kVkF11, 0, true).route == Route::game);
  CHECK(world.release(kF11, kVkF11).route == Route::game);
  const Decision decision = world.press(kF11, kVkF11);  // the next press is the hotkey's
  CHECK(decision.route == Route::consume);
  CHECK(decision.hotkey == id);
}

TEST_CASE("hotkeys stand aside while a holder has input") {
  World world;
  std::uint32_t id = 0;
  REQUIRE(world.hotkeys.add(1, "debug-view", kF11, 0, kF5, id) == Added::bound);
  REQUIRE(world.capture.request(2, Kind::panel, want_all, kReady, kStart).answer == Answer::granted);
  Decision decision = world.press(kF11, kVkF11);
  CHECK(decision.hotkey == 0);
  CHECK(decision.route == Route::consume);
  CHECK(decision.to_holder);  // a holder that takes the keyboard gets it as any key
  CHECK(world.release(kF11, kVkF11).route == Route::consume);
  world.capture.close(Closed::released, kStart);
  // A holder that takes only the mouse: the key fires nothing, and is still not the game's.
  REQUIRE(world.capture.request(2, Kind::panel, want_mouse, kReady, kStart).answer == Answer::granted);
  decision = world.press(kF11, kVkF11);
  CHECK(decision.hotkey == 0);
  CHECK(decision.route == Route::consume);
  CHECK_FALSE(decision.to_holder);
  CHECK(world.release(kF11, kVkF11).route == Route::consume);
  Press press;
  CHECK_FALSE(world.hotkeys.take(1, press));
}

TEST_CASE("a hotkey's press that a holder's grant interrupts stays the hotkey's to its release") {
  World world;
  std::uint32_t id = 0;
  REQUIRE(world.hotkeys.add(1, "chat", kK, kModCtrl, kF5, id) == Added::bound);
  REQUIRE(world.press(kK, kVkK, kModCtrl).hotkey == id);
  REQUIRE(world.capture.request(2, Kind::panel, want_all, kReady, kStart).answer == Answer::granted);
  const Decision decision = world.release(kK, kVkK);
  CHECK(decision.route == Route::consume);
  CHECK_FALSE(decision.to_holder);
}

TEST_CASE("focus loss forgets a hotkey's press") {
  World world;
  std::uint32_t id = 0;
  REQUIRE(world.hotkeys.add(1, "chat", kK, kModCtrl, kF5, id) == Added::bound);
  REQUIRE(world.press(kK, kVkK, kModCtrl).hotkey == id);
  CHECK(world.hotkeys.held().test(kK));
  world.rules.focus_lost(kStart);
  CHECK_FALSE(world.hotkeys.held().test(kK));
  CHECK(world.press(kK, kVkK).route == Route::game);
}

TEST_CASE("with no hotkey table, keys route as before") {
  Capture capture;
  KeyboardLedger keyboard;
  MouseLedger mouse;
  MessageRules rules{capture, keyboard, mouse};
  CHECK(rules.key(kF11, kVkF11, true, false, false, kStart, 0).route == Route::game);
  CHECK(rules.key(kA, kVkA, true, false, false, kStart, kModCtrl).route == Route::game);
}

TEST_CASE("presses counted on one thread while another adds, removes and takes are never lost or misplaced") {
  Hotkeys hotkeys;
  std::uint32_t id = 0;
  REQUIRE(hotkeys.add(1, "debug-view", kF11, 0, kF5, id) == Added::bound);
  constexpr int kPresses = 20000;
  std::atomic<bool> done{false};
  std::thread window([&] {
    for (int i = 0; i < kPresses; ++i) {
      const std::uint32_t matched = hotkeys.match(kF11, 0);
      CHECK(matched == id);
      hotkeys.pressed(matched);
    }
    done.store(true);
  });
  std::uint64_t taken = 0;
  std::uint32_t churn = 0;
  Press press;
  while (!done.load()) {
    if (hotkeys.take(1, press)) {
      CHECK(press.id == id);
      taken += press.count;
    }
    // Another mod's hotkeys come and go meanwhile.
    std::uint32_t other = 0;
    hotkeys.add(2, "other", static_cast<Key>(0x10 + (churn++ % 8)), 0, kF5, other);
    hotkeys.remove_owner(2);
  }
  window.join();
  while (hotkeys.take(1, press)) {
    taken += press.count;
  }
  CHECK(taken == kPresses);
}

TEST_CASE("the Hotkeys page's list: each hotkey's mod, name, defaults and which of them are bound (D206)") {
  Hotkeys hotkeys;
  std::uint32_t f11 = 0;
  std::uint32_t taken = 0;
  std::uint32_t talk = 0;
  Binding binding;
  REQUIRE(hotkeys.add(1, "debug-view", kF11, 0, 0, kF5, f11, &binding, "Debug view") == Added::bound);
  REQUIRE(hotkeys.add(2, "debug", kF11, 0, 0, kF5, taken, &binding, "Debug") == Added::unbound);
  const std::uint32_t chord = 0x0020u | (0x0001u << 16);
  REQUIRE(hotkeys.add(2, "talk", kNoKey, 0, chord, kF5, talk, &binding, "Talk") == Added::bound);
  Listed listed[kMaxHotkeys];
  REQUIRE(hotkeys.list(listed, kMaxHotkeys) == 3);
  CHECK(listed[0].id == f11);
  CHECK(listed[0].owner == 1);
  CHECK(listed[0].name() == "Debug view");
  CHECK(listed[0].action() == "debug-view");
  CHECK(listed[0].key == kF11);
  CHECK(listed[0].key_bound);
  CHECK(listed[1].name() == "Debug");
  CHECK_FALSE(listed[1].key_bound);
  CHECK(listed[2].chord == chord);
  CHECK(listed[2].chord_bound);
  CHECK(listed[2].key == kNoKey);
  CHECK(hotkeys.list(listed, 1) == 1);  // at most its capacity
  hotkeys.remove_owner(2);
  REQUIRE(hotkeys.list(listed, kMaxHotkeys) == 1);
  CHECK(listed[0].name() == "Debug view");
  std::uint32_t again = 0;
  REQUIRE(hotkeys.add(3, "again", kK, 0, 0, kF5, again, &binding, "") == Added::bound);
  REQUIRE(hotkeys.list(listed, kMaxHotkeys) == 2);
  CHECK(listed[1].name().empty());  // a freed slot keeps no name of its last hotkey's
}

TEST_CASE("keys' names: one per key, as the settings' key names allow, and modifiers before them (D206)") {
  CHECK(key_name(kF5) == "F5");
  CHECK(key_name(kF11) == "F11");
  CHECK(key_name(kA) == "A");
  CHECK(key_name(0x02) == "1");
  CHECK(key_name(0xC9) == "PageUp");
  CHECK(key_name(0x9C) == "NumpadEnter");
  CHECK(key_name(0xD3) == "Delete");
  CHECK(key_name(kNoKey).empty());
  CHECK(key_name(0xFF).empty());
  CHECK(key_text(kK, kModCtrl) == "Ctrl+K");
  CHECK(key_text(kK, kModCtrl | kModShift | kModAlt) == "Ctrl+Shift+Alt+K");
  std::vector<std::string_view> seen;
  for (int code = 0; code < 256; ++code) {
    const std::string_view name = key_name(static_cast<Key>(code));
    if (name.empty()) {
      continue;
    }
    CHECK(name.size() <= 12);
    for (const char c : name) {
      CHECK(((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')));
    }
    CHECK(std::find(seen.begin(), seen.end(), name) == seen.end());
    seen.push_back(name);
  }
  CHECK(seen.size() >= 100);
}
