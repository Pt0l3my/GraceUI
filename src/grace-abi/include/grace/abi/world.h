// world.h: the world-state providers' claims (D78): any connected mod may report danger, a loading screen, a game menu
// or text entry, the title screen, multiplayer or an anti-cheat; every claim of every provider is accepted and combined,
// so any provider's danger counts. A provider's claims go when it disconnects. Pure: mods' threads write under a spin
// lock; the combined flags are one atomic word that any thread reads without waiting.
#pragma once

#include <atomic>
#include <cstdint>

#include "grace/abi/consumers.h"
#include "grace/abi/spin.h"
#include "graceui.h"

namespace grace::abi {

inline constexpr std::uint32_t kWorldFlags = GRACEUI_WORLD_DANGER | GRACEUI_WORLD_LOADING | GRACEUI_WORLD_GAME_MENU |
                                             GRACEUI_WORLD_TITLE | GRACEUI_WORLD_MULTIPLAYER |
                                             GRACEUI_WORLD_ANTI_CHEAT;

class World {
 public:
  constexpr World() = default;
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  // A provider's claims, replacing its earlier ones; false (nothing changed) for an unknown flag or no handle. Answers
  // the combined flags in `combined`.
  bool set(GraceUiConsumer provider, std::uint32_t flags, std::uint32_t& combined) noexcept;
  // The provider has gone: its claims with it. Answers the combined flags.
  std::uint32_t remove(GraceUiConsumer provider) noexcept;

  std::uint32_t combined() const noexcept { return combined_.load(std::memory_order_acquire); }
  std::uint32_t providers() const noexcept;
  // The `index`-th provider and what it last reported, in the order of their slots; false past the last.
  bool provider(std::uint32_t index, GraceUiConsumer& handle, std::uint32_t& flags) const noexcept;

 private:
  struct Claim {
    GraceUiConsumer provider = 0;
    std::uint32_t flags = 0;
  };
  std::uint32_t recombine() noexcept;  // under lock_

  mutable SpinLock lock_;
  Claim claims_[kMaxConsumers]{};
  std::atomic<std::uint32_t> combined_{0};
};

}  // namespace grace::abi
