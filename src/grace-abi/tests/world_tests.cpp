// The world-state providers' claims (world.h): every provider's claims combined, a provider's own replaced, a provider's
// gone with it, unknown flags refused, and reads that never wait while providers write.
#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <thread>

#include "grace/abi/world.h"

using namespace grace::abi;

namespace {
constexpr GraceUiConsumer kEldenNetLike = (1ull << 8) | 1;
constexpr GraceUiConsumer kOtherProvider = (1ull << 8) | 2;
}  // namespace

TEST_CASE("two providers' claims are combined: either's danger counts") {
  auto world = std::make_unique<World>();
  std::uint32_t combined = 0;
  CHECK(world->combined() == 0);
  CHECK(world->set(kEldenNetLike, GRACEUI_WORLD_GAME_MENU, combined));
  CHECK(combined == GRACEUI_WORLD_GAME_MENU);
  CHECK(world->set(kOtherProvider, GRACEUI_WORLD_DANGER, combined));
  CHECK(combined == (GRACEUI_WORLD_GAME_MENU | GRACEUI_WORLD_DANGER));
  CHECK(world->providers() == 2);

  // A provider's new claims replace its own earlier ones, never another's.
  CHECK(world->set(kEldenNetLike, 0, combined));
  CHECK(combined == GRACEUI_WORLD_DANGER);
  CHECK(world->set(kOtherProvider, 0, combined));
  CHECK(combined == 0);
  CHECK(world->providers() == 2);  // providers still, claiming nothing

  GraceUiConsumer handle = 0;
  std::uint32_t flags = 99;
  REQUIRE(world->provider(0, handle, flags));
  CHECK(handle == kEldenNetLike);
  CHECK(flags == 0);
  CHECK_FALSE(world->provider(2, handle, flags));
}

TEST_CASE("a provider's claims go when it disconnects; unknown flags change nothing") {
  auto world = std::make_unique<World>();
  std::uint32_t combined = 0;
  CHECK(world->set(kEldenNetLike, GRACEUI_WORLD_DANGER | GRACEUI_WORLD_ANTI_CHEAT, combined));
  CHECK_FALSE(world->set(kOtherProvider, 64, combined));
  CHECK_FALSE(world->set(0, GRACEUI_WORLD_DANGER, combined));
  CHECK(combined == (GRACEUI_WORLD_DANGER | GRACEUI_WORLD_ANTI_CHEAT));
  CHECK(world->remove(kOtherProvider) == combined);  // not a provider: nothing changes
  CHECK(world->remove(kEldenNetLike) == 0);
  CHECK(world->providers() == 0);
  // A handle from the slot's earlier mod no longer removes the new one's claims.
  const GraceUiConsumer again = (2ull << 8) | 1;
  CHECK(world->set(again, GRACEUI_WORLD_LOADING, combined));
  CHECK(world->remove(kEldenNetLike) == GRACEUI_WORLD_LOADING);
}

TEST_CASE("the combined flags are read without waiting while providers write") {
  auto world = std::make_unique<World>();
  std::atomic<bool> stop{false};
  std::thread provider([&] {
    std::uint32_t combined = 0;
    for (std::uint32_t n = 0; !stop.load(std::memory_order_relaxed); ++n) {
      world->set(kEldenNetLike, (n & 1u) != 0 ? GRACEUI_WORLD_DANGER : 0u, combined);
      world->set(kOtherProvider, GRACEUI_WORLD_TITLE, combined);
    }
  });
  bool only_known = true;
  for (int i = 0; i < 100000; ++i) {
    only_known = only_known && (world->combined() & ~(GRACEUI_WORLD_DANGER | GRACEUI_WORLD_TITLE)) == 0;
  }
  stop.store(true);
  provider.join();
  CHECK(only_known);
}
