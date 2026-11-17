// hotkeys.cpp: the mods' hotkeys (hotkeys.h).
#include "grace/input/hotkeys.h"

#include <cstring>
#include <thread>

namespace grace::input {
namespace {

constexpr Key kF4 = 0x3E;  // DIK_F4: Alt+F4 always closes the game

}  // namespace

bool reserved_key(Key key) noexcept {
  switch (key) {
    case kEscape:
    case 0x1D: case 0x9D:  // the Ctrl keys
    case 0x2A: case 0x36:  // the Shift keys
    case 0x38: case 0xB8:  // the Alt keys
    case 0xDB: case 0xDC:  // the Windows keys
      return true;
    default:
      return false;
  }
}

void Hotkeys::lock() noexcept {
  while (lock_.test_and_set(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

void Hotkeys::update_bare() noexcept {
  KeyBits bare;
  KeyBits keys;
  for (const Slot& slot : slots_) {
    const std::uint64_t word = slot.word.load(std::memory_order_relaxed);
    if (state_of(word) == bound) {
      keys.set(key_of(word));
      if (modifiers_of(word) == 0) {
        bare.set(key_of(word));
      }
    }
  }
  bare_.store(bare);
  bound_.store(keys);
}

Added Hotkeys::add(std::uint16_t owner, std::string_view action, Key key, std::uint8_t modifiers, Key menu_key,
                   std::uint32_t& id) noexcept {
  if (action.empty() || action.size() > kActionMax || (modifiers & ~kModAll) != 0 || reserved_key(key) ||
      (key == kF4 && (modifiers & kModAlt) != 0)) {
    return Added::invalid;
  }
  lock();
  std::uint32_t mine = 0;
  std::uint32_t used = 0;
  std::uint32_t free_index = kMaxHotkeys;
  bool taken = key == kNoKey || key == menu_key;  // the menu key is the host's, whatever the modifiers (§5.1)
  for (std::uint32_t i = 0; i < kMaxHotkeys; ++i) {
    const Slot& slot = slots_[i];
    const std::uint64_t word = slot.word.load(std::memory_order_relaxed);
    if (state_of(word) == free) {
      if (free_index == kMaxHotkeys) {
        free_index = i;
      }
      continue;
    }
    ++used;
    if (slot.owner == owner) {
      ++mine;
      if (std::string_view(slot.action, slot.action_length) == action) {
        unlock();
        return Added::invalid;  // an action already the mod's
      }
    }
    if (state_of(word) == bound && key_of(word) == key && modifiers_of(word) == modifiers) {
      taken = true;
    }
  }
  if (mine >= kHotkeysPerMod || used >= kMaxHotkeys || free_index == kMaxHotkeys) {
    unlock();
    return Added::full;
  }
  Slot& slot = slots_[free_index];
  slot.owner = owner;
  slot.action_length = static_cast<std::uint8_t>(action.size());
  std::memcpy(slot.action, action.data(), action.size());
  generation_ = (generation_ + 1) & 0x7FFFFu;
  if (generation_ == 0) {
    generation_ = 1;
  }
  slot.word.store(pack(taken ? unbound : bound, key, modifiers, generation_), std::memory_order_release);
  update_bare();
  id = id_of(free_index, generation_);
  unlock();
  return taken ? Added::unbound : Added::bound;
}

void Hotkeys::remove_owner(std::uint16_t owner) noexcept {
  lock();
  for (Slot& slot : slots_) {
    if (state_of(slot.word.load(std::memory_order_relaxed)) != free && slot.owner == owner) {
      slot.word.store(0, std::memory_order_release);
      slot.owner = 0;
      slot.action_length = 0;
    }
  }
  update_bare();
  unlock();
}

void Hotkeys::remove(std::uint32_t id) noexcept {
  const std::uint32_t index = (id & 0xFFu) - 1;
  if ((id & 0xFFu) == 0 || index >= kMaxHotkeys) {
    return;
  }
  lock();
  Slot& slot = slots_[index];
  const std::uint64_t word = slot.word.load(std::memory_order_relaxed);
  if (state_of(word) != free && generation_of(word) == (id >> 8)) {
    slot.word.store(0, std::memory_order_release);
    slot.owner = 0;
    slot.action_length = 0;
    update_bare();
  }
  unlock();
}

bool Hotkeys::take(std::uint16_t owner, Press& out) noexcept {
  lock();
  for (std::uint32_t i = 0; i < kMaxHotkeys; ++i) {
    Slot& slot = slots_[i];
    std::uint64_t word = slot.word.load(std::memory_order_acquire);
    if (state_of(word) == free || slot.owner != owner) {
      continue;
    }
    // The window thread may count a press meanwhile: the presses are taken by a compare-and-swap, which then retries.
    while (presses_of(word) != 0 &&
           !slot.word.compare_exchange_weak(word, word & 0xFFFFFFFFull, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
    }
    if (presses_of(word) != 0) {
      out.id = id_of(i, generation_of(word));
      out.count = presses_of(word);
      unlock();
      return true;
    }
  }
  unlock();
  return false;
}

std::uint32_t Hotkeys::match(Key key, std::uint8_t modifiers) const noexcept {
  if (key == kNoKey) {
    return 0;
  }
  for (std::uint32_t i = 0; i < kMaxHotkeys; ++i) {
    const std::uint64_t word = slots_[i].word.load(std::memory_order_acquire);
    if (state_of(word) == bound && key_of(word) == key && modifiers_of(word) == (modifiers & kModAll)) {
      return id_of(i, generation_of(word));
    }
  }
  return 0;
}

bool Hotkeys::pressed(std::uint32_t id) noexcept {
  const std::uint32_t index = (id & 0xFFu) - 1;
  if ((id & 0xFFu) == 0 || index >= kMaxHotkeys) {
    return false;
  }
  std::atomic<std::uint64_t>& slot = slots_[index].word;
  std::uint64_t word = slot.load(std::memory_order_acquire);
  for (;;) {
    if (state_of(word) != bound || generation_of(word) != (id >> 8)) {
      return false;  // removed, or replaced by another hotkey, since it matched
    }
    if (presses_of(word) == 0xFFFFFFFFu) {
      return true;  // as many as the count holds: more change nothing a mod does
    }
    if (slot.compare_exchange_weak(word, word + (std::uint64_t{1} << 32), std::memory_order_acq_rel,
                                   std::memory_order_acquire)) {
      return true;
    }
  }
}

void Hotkeys::hold(Key key) noexcept {
  KeyBits bit;
  bit.set(key);
  held_.or_with(bit);
}

void Hotkeys::let_go(Key key) noexcept {
  KeyBits keep = KeyBits::all();
  keep.clear(key);
  held_.and_with(keep);
}

void Hotkeys::let_go_all() noexcept { held_.store(KeyBits{}); }

std::uint32_t Hotkeys::count() const noexcept {
  std::uint32_t total = 0;
  for (const Slot& slot : slots_) {
    if (state_of(slot.word.load(std::memory_order_acquire)) != free) {
      ++total;
    }
  }
  return total;
}

}  // namespace grace::input
