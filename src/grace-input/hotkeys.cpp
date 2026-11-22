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
                   Key menu_key, std::uint32_t& id, Binding* binding, std::string_view name,
                   const Chosen* chosen) noexcept {
  if (action.empty() || action.size() > kActionMax || (modifiers & ~kModAll) != 0 || reserved_key(key) ||
      (key == kF4 && (modifiers & kModAlt) != 0) || (chord != 0 && !valid_chord(chord))) {
    return Added::invalid;
  }
  const Chosen defaults{key, modifiers, chord};
  if (chosen != nullptr && (chosen->modifiers & ~kModAll) == 0 && !reserved_key(chosen->key) &&
      !(chosen->key == kF4 && (chosen->modifiers & kModAlt) != 0) &&
      (chosen->chord == 0 || valid_chord(chosen->chord))) {
    key = chosen->key;  // the player's binding over the mod's defaults (D208); a kept one that is no longer valid is not
    modifiers = chosen->modifiers;
    chord = chosen->chord;
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
  slot.owner_now.store(owner, std::memory_order_release);
  slot.action_length = static_cast<std::uint8_t>(action.size());
  std::memcpy(slot.action, action.data(), action.size());
  slot.defaults = defaults;
  slot.changed.store(false, std::memory_order_relaxed);
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
      slot.owner_now.store(0, std::memory_order_release);
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
    slot.owner_now.store(0, std::memory_order_release);
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

// ---- The player's changes (D208).

Hotkeys::Slot* Hotkeys::slot_of(std::uint32_t id) noexcept {
  const std::uint32_t index = (id & 0xFFu) - 1;
  if ((id & 0xFFu) == 0 || index >= kMaxHotkeys) {
    return nullptr;
  }
  Slot& slot = slots_[index];
  const std::uint64_t word = slot.word.load(std::memory_order_relaxed);
  return state_of(word) != free && generation_of(word) == (id >> 8) ? &slot : nullptr;
}

std::uint32_t Hotkeys::key_holder(Key key, std::uint8_t modifiers, const Slot* except) const noexcept {
  for (std::uint32_t i = 0; i < kMaxHotkeys; ++i) {
    const std::uint64_t word = slots_[i].word.load(std::memory_order_relaxed);
    if (&slots_[i] != except && state_of(word) == bound && key_of(word) == key && modifiers_of(word) == modifiers) {
      return id_of(i, generation_of(word));
    }
  }
  return 0;
}

std::uint32_t Hotkeys::chord_holder(std::uint32_t chord, const Slot* except) const noexcept {
  for (std::uint32_t i = 0; i < kMaxHotkeys; ++i) {
    const std::uint64_t word = slots_[i].chord.load(std::memory_order_relaxed);
    if (&slots_[i] != except && (word >> 32 & 1u) != 0 && static_cast<std::uint32_t>(word) == chord) {
      return id_of(i, static_cast<std::uint32_t>(word >> 33) & 0x7FFFFu);
    }
  }
  return 0;
}

void Hotkeys::set_key(Slot& slot, Key key, std::uint8_t modifiers, bool is_bound) noexcept {
  // The window thread counts presses into this word by compare-and-swap: keep them, and the generation.
  std::uint64_t word = slot.word.load(std::memory_order_relaxed);
  for (;;) {
    const std::uint64_t next = pack(is_bound && key != kNoKey ? bound : unbound, key, modifiers, generation_of(word)) |
                               (word & 0xFFFFFFFF00000000ull);
    if (slot.word.compare_exchange_weak(word, next, std::memory_order_acq_rel, std::memory_order_relaxed)) {
      return;
    }
  }
}

void Hotkeys::set_chord(Slot& slot, std::uint32_t chord, bool is_bound) noexcept {
  const std::uint64_t word = slot.word.load(std::memory_order_relaxed);
  slot.chord.store(pack_chord(chord, is_bound && chord != 0, generation_of(word)), std::memory_order_release);
}

Rebound Hotkeys::rebind_key(std::uint32_t id, Key key, std::uint8_t modifiers, Key menu_key, bool move,
                            Taken& other) noexcept {
  other = Taken{};
  if ((modifiers & ~kModAll) != 0 || reserved_key(key) || (key == kF4 && (modifiers & kModAlt) != 0)) {
    return Rebound::invalid;
  }
  if (key == kNoKey) {
    modifiers = 0;
  }
  lock();
  Slot* slot = slot_of(id);
  if (slot == nullptr) {
    unlock();
    return Rebound::gone;
  }
  if (key != kNoKey && key == menu_key) {
    other.menu = true;
    unlock();
    return Rebound::taken;
  }
  if (key != kNoKey) {
    if (const std::uint32_t holder = key_holder(key, modifiers, slot); holder != 0) {
      other.id = holder;
      if (!move) {
        unlock();
        return Rebound::taken;
      }
      Slot* lost = slot_of(holder);
      set_key(*lost, kNoKey, 0, false);
      lost->changed.store(true, std::memory_order_release);
    }
  }
  set_key(*slot, key, modifiers, true);
  slot->changed.store(true, std::memory_order_release);
  update_bare();
  unlock();
  return Rebound::done;
}

Rebound Hotkeys::rebind_chord(std::uint32_t id, std::uint32_t chord, bool move, Taken& other) noexcept {
  other = Taken{};
  if (chord != 0 && !valid_chord(chord)) {
    return Rebound::invalid;
  }
  lock();
  Slot* slot = slot_of(id);
  if (slot == nullptr) {
    unlock();
    return Rebound::gone;
  }
  if (chord != 0) {
    if (const std::uint32_t holder = chord_holder(chord, slot); holder != 0) {
      other.id = holder;
      if (!move) {
        unlock();
        return Rebound::taken;
      }
      Slot* lost = slot_of(holder);
      set_chord(*lost, 0, false);
      lost->changed.store(true, std::memory_order_release);
    }
  }
  set_chord(*slot, chord, true);
  slot->changed.store(true, std::memory_order_release);
  update_bare();
  unlock();
  return Rebound::done;
}

Rebound Hotkeys::reset(std::uint32_t id, Key menu_key) noexcept {
  lock();
  Slot* slot = slot_of(id);
  if (slot == nullptr) {
    unlock();
    return Rebound::gone;
  }
  const Chosen defaults = slot->defaults;
  const bool key_free = defaults.key != kNoKey && defaults.key != menu_key &&
                        key_holder(defaults.key, defaults.modifiers, slot) == 0;
  const bool chord_free = defaults.chord != 0 && chord_holder(defaults.chord, slot) == 0;
  set_key(*slot, defaults.key, defaults.modifiers, key_free);
  set_chord(*slot, defaults.chord, chord_free);
  slot->changed.store(true, std::memory_order_release);
  update_bare();
  unlock();
  return Rebound::done;
}

std::size_t Hotkeys::menu_key_moved(Key key, std::uint32_t* out, std::size_t capacity) noexcept {
  std::size_t lost = 0;
  lock();
  for (std::uint32_t i = 0; i < kMaxHotkeys; ++i) {
    Slot& slot = slots_[i];
    const std::uint64_t word = slot.word.load(std::memory_order_relaxed);
    if (state_of(word) == bound && key_of(word) == key) {  // the menu's key, whatever the modifiers (§5.1)
      set_key(slot, kNoKey, 0, false);
      slot.changed.store(true, std::memory_order_release);
      if (lost < capacity) {
        out[lost] = id_of(i, generation_of(word));
      }
      ++lost;
    }
  }
  update_bare();
  unlock();
  return lost < capacity ? lost : capacity;
}

bool Hotkeys::take_changed(std::uint16_t owner, std::uint32_t& id) noexcept {
  lock();
  for (std::uint32_t i = 0; i < kMaxHotkeys; ++i) {
    Slot& slot = slots_[i];
    const std::uint64_t word = slot.word.load(std::memory_order_relaxed);
    if (state_of(word) != free && slot.owner == owner && slot.changed.exchange(false, std::memory_order_acq_rel)) {
      id = id_of(i, generation_of(word));
      unlock();
      return true;
    }
  }
  unlock();
  return false;
}

bool Hotkeys::find(std::uint32_t id, Listed& out) const noexcept {
  const std::uint32_t index = (id & 0xFFu) - 1;
  if ((id & 0xFFu) == 0 || index >= kMaxHotkeys) {
    return false;
  }
  lock();
  const Slot& slot = slots_[index];
  const std::uint64_t word = slot.word.load(std::memory_order_relaxed);
  const bool found = state_of(word) != free && generation_of(word) == (id >> 8);
  if (found) {
    out = listed_of(slot, id);
  }
  unlock();
  return found;
}

Listed Hotkeys::listed_of(const Slot& slot, std::uint32_t id) noexcept {
  const std::uint64_t word = slot.word.load(std::memory_order_relaxed);
  const std::uint64_t chord = slot.chord.load(std::memory_order_relaxed);
  Listed listed;
  listed.id = id;
  listed.owner = slot.owner;
  listed.key = key_of(word);
  listed.modifiers = modifiers_of(word);
  listed.chord = static_cast<std::uint32_t>(chord);
  listed.key_bound = state_of(word) == bound;
  listed.chord_bound = (chord >> 32 & 1u) != 0;
  listed.defaults = slot.defaults;
  listed.action_length = slot.action_length;
  std::memcpy(listed.action_bytes, slot.action, slot.action_length);
  listed.name_length = slot.name_length;
  std::memcpy(listed.name_bytes, slot.name, slot.name_length);
  return listed;
}

std::uint16_t Hotkeys::owner_of(std::uint32_t id) const noexcept {
  const std::uint32_t index = (id & 0xFFu) - 1;
  if ((id & 0xFFu) == 0 || index >= kMaxHotkeys) {
    return 0;
  }
  const Slot& slot = slots_[index];
  const std::uint16_t owner = slot.owner_now.load(std::memory_order_acquire);
  const std::uint64_t word = slot.word.load(std::memory_order_acquire);
  return state_of(word) != free && generation_of(word) == (id >> 8) ? owner : 0;
}

std::uint64_t Hotkeys::changed_owners() const noexcept {
  std::uint64_t owners = 0;
  lock();
  for (const Slot& slot : slots_) {
    if (state_of(slot.word.load(std::memory_order_relaxed)) != free && slot.changed.load(std::memory_order_acquire) &&
        slot.owner > 0 && slot.owner < 64) {
      owners |= std::uint64_t{1} << slot.owner;
    }
  }
  unlock();
  return owners;
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
    out[written++] = listed_of(slot, id_of(i, generation_of(word)));
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
