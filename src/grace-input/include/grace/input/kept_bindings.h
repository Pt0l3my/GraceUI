// kept_bindings.h: the player's hotkey bindings kept between sessions (G37, D208): for each mod's action the player
// changed, its key with modifiers and its pad chord, applied over the mod's defaults whenever the mod registers it,
// whether or not the mod is installed now. Fixed in size and never allocating, so that graceui.dll can hold it as a
// static object (D71). Pure: no Win32, tested on Linux. Any thread: a short spin lock.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "grace/input/hotkeys.h"

namespace grace::input {

inline constexpr std::size_t kKeptBindings = 256;  // EST: more than every hotkey of every mod a player keeps
inline constexpr std::size_t kModIdMax = 32;       // GRACEUI_ID_MAX

class KeptBindings {
 public:
  struct Entry {
    std::uint8_t mod_length = 0;
    std::uint8_t action_length = 0;
    char mod[kModIdMax] = {};
    char action[kActionMax] = {};
    Chosen chosen{};
    std::string_view mod_id() const noexcept { return {mod, mod_length}; }
    std::string_view action_id() const noexcept { return {action, action_length}; }
  };

  constexpr KeptBindings() = default;
  KeptBindings(const KeptBindings&) = delete;
  KeptBindings& operator=(const KeptBindings&) = delete;

  bool find(std::string_view mod, std::string_view action, Chosen& out) const noexcept;
  // Keeps or replaces one; false for an id too long or a full store, which keeps nothing new.
  bool keep(std::string_view mod, std::string_view action, const Chosen& chosen) noexcept;
  void forget(std::string_view mod, std::string_view action) noexcept;
  void forget_all() noexcept;
  // Every one kept, at most `capacity`: how many were written.
  std::size_t all(Entry* out, std::size_t capacity) const noexcept;
  // Changes at each keep, forget and forget_all that changed something: the settings file is written again.
  std::uint64_t revision() const noexcept { return revision_.load(std::memory_order_acquire); }

 private:
  void lock() const noexcept;
  void unlock() const noexcept { lock_.clear(std::memory_order_release); }
  std::size_t index_of(std::string_view mod, std::string_view action) const noexcept;  // under the lock; count_ if none

  mutable std::atomic_flag lock_{};
  std::size_t count_ = 0;  // under the lock
  std::array<Entry, kKeptBindings> entries_{};
  std::atomic<std::uint64_t> revision_{0};
};

}  // namespace grace::input
