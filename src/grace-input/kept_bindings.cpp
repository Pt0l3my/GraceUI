// kept_bindings.cpp: the player's hotkey bindings kept between sessions (kept_bindings.h).
#include "grace/input/kept_bindings.h"

#include <cstring>
#include <thread>

namespace grace::input {

void KeptBindings::lock() const noexcept {
  while (lock_.test_and_set(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

std::size_t KeptBindings::index_of(std::string_view mod, std::string_view action) const noexcept {
  for (std::size_t i = 0; i < count_; ++i) {
    if (entries_[i].mod_id() == mod && entries_[i].action_id() == action) {
      return i;
    }
  }
  return count_;
}

bool KeptBindings::find(std::string_view mod, std::string_view action, Chosen& out) const noexcept {
  lock();
  const std::size_t i = index_of(mod, action);
  const bool found = i < count_;
  if (found) {
    out = entries_[i].chosen;
  }
  unlock();
  return found;
}

bool KeptBindings::keep(std::string_view mod, std::string_view action, const Chosen& chosen) noexcept {
  if (mod.empty() || mod.size() > kModIdMax || action.empty() || action.size() > kActionMax) {
    return false;
  }
  lock();
  const std::size_t i = index_of(mod, action);
  const bool made_now = i == count_;
  if (made_now) {
    if (count_ == kKeptBindings) {
      unlock();
      return false;
    }
    Entry& made = entries_[count_++];
    made = Entry{};
    made.mod_length = static_cast<std::uint8_t>(mod.size());
    made.action_length = static_cast<std::uint8_t>(action.size());
    std::memcpy(made.mod, mod.data(), mod.size());
    std::memcpy(made.action, action.data(), action.size());
  }
  Entry& entry = entries_[i];
  const bool same = entry.chosen.key == chosen.key && entry.chosen.modifiers == chosen.modifiers &&
                    entry.chosen.chord == chosen.chord;
  entry.chosen = chosen;
  if (made_now || !same) {
    revision_.fetch_add(1, std::memory_order_acq_rel);
  }
  unlock();
  return true;
}

void KeptBindings::forget(std::string_view mod, std::string_view action) noexcept {
  lock();
  const std::size_t i = index_of(mod, action);
  if (i < count_) {
    entries_[i] = entries_[--count_];  // the order is no one's concern
    entries_[count_] = Entry{};
    revision_.fetch_add(1, std::memory_order_acq_rel);
  }
  unlock();
}

void KeptBindings::forget_all() noexcept {
  lock();
  if (count_ != 0) {
    for (std::size_t i = 0; i < count_; ++i) {
      entries_[i] = Entry{};
    }
    count_ = 0;
    revision_.fetch_add(1, std::memory_order_acq_rel);
  }
  unlock();
}

std::size_t KeptBindings::all(Entry* out, std::size_t capacity) const noexcept {
  lock();
  const std::size_t n = count_ < capacity ? count_ : capacity;
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = entries_[i];
  }
  unlock();
  return n;
}

}  // namespace grace::input
