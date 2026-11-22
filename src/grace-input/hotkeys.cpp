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

void Hotkeys::lock() const noexcept {
  while (lock_.test_and_set(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

void Hotkeys::update_bare() noexcept {
  KeyBits bare;
  KeyBits keys;
  std::uint16_t chord_keys = 0;
  for (const Slot& slot : slots_) {
    const std::uint64_t chord = slot.chord.load(std::memory_order_relaxed);
    if ((chord >> 32 & 1u) != 0) {
      chord_keys = static_cast<std::uint16_t>(chord_keys | chord_key(static_cast<std::uint32_t>(chord)));
    }
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
  chord_keys_.store(chord_keys, std::memory_order_release);
}

Added Hotkeys::add(std::uint16_t owner, std::string_view action, Key key, std::uint8_t modifiers, std::uint32_t chord,
                   Key menu_key, std::uint32_t& id, Binding* binding, std::string_view name) noexcept {
  if (action.empty() || action.size() > kActionMax || (modifiers & ~kModAll) != 0 || reserved_key(key) ||
      (key == kF4 && (modifiers & kModAlt) != 0) || (chord != 0 && !valid_chord(chord))) {
    return Added::invalid;
  }
  lock();
  std::uint32_t mine = 0;
  std::uint32_t used = 0;
  std::uint32_t free_index = kMaxHotkeys;
  bool taken = key == kNoKey || key == menu_key;  // the menu key is the host's, whatever the modifiers (§5.1)
  bool chord_taken = chord == 0;                  // GraceUI's menu has no chord (D33)
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
    const std::uint64_t other = slot.chord.load(std::memory_order_relaxed);
    if (chord != 0 && (other >> 32 & 1u) != 0 && static_cast<std::uint32_t>(other) == chord) {
      chord_taken = true;
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
  const std::size_t name_size = name.size() < kHotkeyNameMax ? name.size() : kHotkeyNameMax;
  slot.name_length = static_cast<std::uint8_t>(name_size);
  if (name_size != 0) {
    std::memcpy(slot.name, name.data(), name_size);
  }
  generation_ = (generation_ + 1) & 0x7FFFFu;
  if (generation_ == 0) {
    generation_ = 1;
  }
  // The chord word first: a reader that finds the hotkey's word finds its chord too.
  slot.chord.store(pack_chord(chord, !chord_taken, generation_), std::memory_order_release);
  slot.word.store(pack(taken ? unbound : bound, key, modifiers, generation_), std::memory_order_release);
  update_bare();
  id = id_of(free_index, generation_);
  unlock();
  if (binding != nullptr) {
    binding->key = !taken;
    binding->chord = !chord_taken;
  }
  // Unbound when it names no default, or when a default it names is taken (D164, D204): the mod learns of it.
  const bool none = key == kNoKey && chord == 0;
  const bool lost = (key != kNoKey && taken) || (chord != 0 && chord_taken);
  return none || lost ? Added::unbound : Added::bound;
}

void Hotkeys::remove_owner(std::uint16_t owner) noexcept {
  lock();
  for (Slot& slot : slots_) {
    if (state_of(slot.word.load(std::memory_order_relaxed)) != free && slot.owner == owner) {
      slot.word.store(0, std::memory_order_release);
      slot.chord.store(0, std::memory_order_release);
      slot.owner = 0;
      slot.action_length = 0;
      slot.name_length = 0;
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
    slot.chord.store(0, std::memory_order_release);
    slot.owner = 0;
    slot.action_length = 0;
    slot.name_length = 0;
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
    // Removed, or replaced by another hotkey, since it matched. A hotkey whose key is unbound may be pressed by its
    // chord (D204), so any hotkey of this generation counts; match() and match_chord() find bound defaults alone.
    if (state_of(word) == free || generation_of(word) != (id >> 8)) {
      return false;
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

std::size_t Hotkeys::list(Listed* out, std::size_t capacity) const noexcept {
  std::size_t written = 0;
  lock();
  for (std::uint32_t i = 0; i < kMaxHotkeys && written < capacity; ++i) {
    const Slot& slot = slots_[i];
    const std::uint64_t word = slot.word.load(std::memory_order_relaxed);
    if (state_of(word) == free) {
      continue;
    }
    const std::uint64_t chord = slot.chord.load(std::memory_order_relaxed);
    Listed& listed = out[written++];
    listed = Listed{};
    listed.id = id_of(i, generation_of(word));
    listed.owner = slot.owner;
    listed.key = key_of(word);
    listed.modifiers = modifiers_of(word);
    listed.chord = static_cast<std::uint32_t>(chord);
    listed.key_bound = state_of(word) == bound;
    listed.chord_bound = (chord >> 32 & 1u) != 0;
    listed.action_length = slot.action_length;
    std::memcpy(listed.action_bytes, slot.action, slot.action_length);
    listed.name_length = slot.name_length;
    std::memcpy(listed.name_bytes, slot.name, slot.name_length);
  }
  unlock();
  return written;
}

std::uint32_t Hotkeys::match_chord(std::uint16_t anchor, std::uint16_t key) const noexcept {
  if ((chord_keys() & key) == 0) {
    return 0;
  }
  const std::uint32_t chord = (std::uint32_t{anchor} << 16) | key;
  for (std::uint32_t i = 0; i < kMaxHotkeys; ++i) {
    const std::uint64_t word = slots_[i].chord.load(std::memory_order_acquire);
    if ((word >> 32 & 1u) != 0 && static_cast<std::uint32_t>(word) == chord) {
      return id_of(i, static_cast<std::uint32_t>(word >> 33) & 0x7FFFFu);
    }
  }
  return 0;
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
