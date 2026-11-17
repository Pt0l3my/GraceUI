// messages.cpp: the window procedure's filter rules (messages.h).
#include "grace/input/messages.h"

namespace grace::input {

Holder MessageRules::follow() noexcept {
  const Holder holder = capture_->holder();
  if (holder.generation != followed_) {
    // A new holder took input since the last message: whatever is down now, by any account, is its opening press.
    followed_ = holder.generation;
    holder_down_.clear_all();
    holder_buttons_ = 0;
    holder_inert_ = game_down_ | captured_down_ | keyboard_->latest();
    holder_inert_buttons_ = static_cast<std::uint8_t>(game_buttons_ | captured_buttons_ | mouse_->latest());
  }
  return holder;
}

bool MessageRules::hotkey_key(Key key, bool down, bool repeat, const Holder& holder, std::uint8_t modifiers,
                              Decision& decision) noexcept {
  if (hotkeys_ == nullptr || key == kNoKey) {
    return false;
  }
  if (hotkey_down_.test(key)) {
    if (!down || repeat) {
      decision.route = Route::consume;  // the rest of a hotkey's press: its repeats and its release
      if (!down) {
        hotkey_down_.clear(key);
        hotkeys_->let_go(key);
      }
      return true;
    }
    hotkey_down_.clear(key);  // a fresh press: the last one's release was lost
    hotkeys_->let_go(key);
  }
  if (game_down_.test(key)) {
    return false;  // a press the game saw begin reaches it to its release
  }
  if (down && !repeat && !holder.holds()) {
    const std::uint32_t id = hotkeys_->match(key, modifiers);
    if (id != 0 && hotkeys_->pressed(id)) {
      hotkey_down_.set(key);
      hotkeys_->hold(key);  // hidden from DirectInput's readers too, until its release
      decision.route = Route::consume;
      decision.hotkey = id;
      return true;
    }
  }
  if (hotkeys_->bare().test(key) && !holder.takes(want_keyboard)) {
    decision.route = Route::consume;  // a key bound with no modifier is never the game's while bound
    return true;
  }
  return false;
}

Decision MessageRules::key(Key key, std::uint32_t vk, bool down, bool system, bool repeat, std::int64_t now_ms,
                           std::uint8_t modifiers) noexcept {
  const Holder holder = follow();
  Decision decision;
  if (key == kEscape) {
    capture_->escape(Source::messages, down, now_ms);
  }
  if (key != kNoKey && key == menu_key_) {
    decision.route = Route::consume;  // the menu key never reaches the game, and is the host's, never a holder's
    if (down && !repeat) {
      decision.menu = capture_->menu_key(now_ms);
      if (decision.menu == MenuKey::closed_menu || decision.menu == MenuKey::player_exit) {
        decision.closed = capture_->last_close().holder;
      }
    }
    return decision;
  }
  if (hotkey_key(key, down, repeat, holder, modifiers, decision)) {
    return decision;
  }
  const bool capturing = holder.takes(want_keyboard);
  if (down) {
    if (capturing && !(system && vk == kVkF4)) {
      decision.route = Route::consume;
      if (key == kNoKey) {
        decision.to_holder = !repeat;
      } else if (!repeat) {
        captured_down_.set(key);
        if (!holder_inert_.test(key)) {
          holder_down_.set(key);
          decision.to_holder = true;
        }
      } else {
        decision.to_holder = holder_down_.test(key);
      }
    } else if (key != kNoKey && captured_down_.test(key) && repeat) {
      decision.route = Route::consume;  // the rest of a press the game never saw begin
    } else {
      if (key != kNoKey) {
        captured_down_.clear(key);  // a fresh press: an older one's release was lost
        game_down_.set(key);
      }
      decision.route = Route::game;
    }
    return decision;
  }
  decision.to_holder = capturing && (key == kNoKey || holder_down_.test(key));
  if (key == kNoKey) {
    decision.route = capturing ? Route::consume : Route::game;
    return decision;
  }
  holder_down_.clear(key);
  holder_inert_.clear(key);
  if (game_down_.test(key)) {
    game_down_.clear(key);
    captured_down_.clear(key);
    decision.route = Route::game;  // a release the game saw begin always reaches it
  } else if (captured_down_.test(key)) {
    captured_down_.clear(key);
    decision.route = Route::consume;
  } else {
    decision.route = capturing ? Route::consume : Route::game;
  }
  return decision;
}

Decision MessageRules::character(Key key) noexcept {
  const Holder holder = follow();
  Decision decision;
  const bool menu = key != kNoKey && key == menu_key_;
  if (holder.takes(want_keyboard)) {
    decision.route = Route::consume;
    decision.to_holder = !menu && (key == kNoKey || holder_down_.test(key));
  } else if (menu || (key != kNoKey && captured_down_.test(key))) {
    decision.route = Route::consume;
  } else if (hotkeys_ != nullptr && key != kNoKey && !game_down_.test(key) &&
             (hotkey_down_.test(key) || hotkeys_->bare().test(key))) {
    decision.route = Route::consume;  // a hotkey's character, or a bound key's
  }
  return decision;
}

Decision MessageRules::other_character(Key key) noexcept {
  Decision decision = character(key);
  decision.to_holder = false;
  return decision;
}

Decision MessageRules::mouse_button(unsigned button, bool down) noexcept {
  const Holder holder = follow();
  Decision decision;
  if (button >= 8) {
    return decision;
  }
  const auto bit = static_cast<std::uint8_t>(1u << button);
  const auto clear = static_cast<std::uint8_t>(~bit);
  const bool capturing = holder.takes(want_mouse);
  if (down) {
    if (capturing) {
      decision.route = Route::consume;
      captured_buttons_ |= bit;
      if ((holder_inert_buttons_ & bit) == 0) {
        holder_buttons_ |= bit;
        decision.to_holder = true;
      }
    } else {
      captured_buttons_ &= clear;  // buttons do not repeat: a press is always a new one
      game_buttons_ |= bit;
    }
    return decision;
  }
  decision.to_holder = capturing && (holder_buttons_ & bit) != 0;
  holder_buttons_ &= clear;
  holder_inert_buttons_ &= clear;
  if ((game_buttons_ & bit) != 0) {
    game_buttons_ &= clear;
    decision.route = Route::game;
  } else if ((captured_buttons_ & bit) != 0) {
    captured_buttons_ &= clear;
    decision.route = Route::consume;
  } else {
    decision.route = capturing ? Route::consume : Route::game;
  }
  return decision;
}

Decision MessageRules::mouse_motion() noexcept {
  Decision decision;
  if (follow().takes(want_mouse)) {
    decision.route = Route::consume;
    decision.to_holder = true;
  }
  return decision;
}

Decision MessageRules::ime() noexcept {
  Decision decision;
  if (follow().takes(want_keyboard)) {
    decision.route = Route::default_proc;
  }
  return decision;
}

Decision MessageRules::raw_input(std::uint8_t device) noexcept {
  Decision decision;
  if (follow().takes(device)) {
    decision.route = Route::default_proc;
  }
  return decision;
}

Holder MessageRules::focus_lost(std::int64_t now_ms) noexcept {
  const Holder closed = capture_->close(Closed::focus_lost, now_ms);
  // Both of Escape's timers: an unfocused game's DirectInput reads may fail, and none would report the release.
  capture_->escape(Source::messages, false, now_ms);
  capture_->escape(Source::direct_input, false, now_ms);
  game_down_.clear_all();
  captured_down_.clear_all();
  holder_down_.clear_all();
  holder_inert_.clear_all();
  hotkey_down_.clear_all();
  if (hotkeys_ != nullptr) {
    hotkeys_->let_go_all();
  }
  game_buttons_ = 0;
  captured_buttons_ = 0;
  holder_buttons_ = 0;
  holder_inert_buttons_ = 0;
  return closed;
}

}  // namespace grace::input
