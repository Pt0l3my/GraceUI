// world.cpp: the world-state providers' claims (world.h).
#include "grace/abi/world.h"

namespace grace::abi {

bool World::set(GraceUiConsumer provider, std::uint32_t flags, std::uint32_t& combined) noexcept {
  if (provider == 0 || (flags & ~kWorldFlags) != 0) {
    combined = combined_.load(std::memory_order_acquire);
    return false;
  }
  const SpinGuard guard(lock_);
  Claim& claim = claims_[(provider & 0xFFu) % kMaxConsumers];
  claim.provider = provider;  // a handle from a slot's earlier mod is replaced: that mod has gone
  claim.flags = flags;
  combined = recombine();
  return true;
}

std::uint32_t World::remove(GraceUiConsumer provider) noexcept {
  const SpinGuard guard(lock_);
  Claim& claim = claims_[(provider & 0xFFu) % kMaxConsumers];
  if (claim.provider == provider) {
    claim = Claim{};
  }
  return recombine();
}

std::uint32_t World::providers() const noexcept {
  const SpinGuard guard(lock_);
  std::uint32_t count = 0;
  for (const Claim& claim : claims_) {
    count += claim.provider != 0 ? 1u : 0u;
  }
  return count;
}

bool World::provider(std::uint32_t index, GraceUiConsumer& handle, std::uint32_t& flags) const noexcept {
  const SpinGuard guard(lock_);
  for (const Claim& claim : claims_) {
    if (claim.provider != 0 && index-- == 0) {
      handle = claim.provider;
      flags = claim.flags;
      return true;
    }
  }
  return false;
}

std::uint32_t World::recombine() noexcept {
  std::uint32_t flags = 0;
  for (const Claim& claim : claims_) {
    flags |= claim.flags;
  }
  combined_.store(flags, std::memory_order_release);
  return flags;
}

}  // namespace grace::abi
