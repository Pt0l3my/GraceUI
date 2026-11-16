// The mods connected to GraceUI (consumers.h): connect's answers across versions, ids and the session's states, the
// handles, and two or more mods connecting at once.
#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "grace/abi/consumers.h"

using namespace grace::abi;

namespace {

constexpr std::uint32_t kGraceUi = GRACEUI_VERSION_PACK(0, 1, 0);

ConnectRequest request(std::string_view id, std::uint32_t floor = GRACEUI_VERSION_PACK(0, 1, 0)) {
  ConnectRequest out;
  out.abi_minor = GRACEUI_ABI1_MINOR;
  out.version = GRACEUI_VERSION_PACK(1, 0, 0);
  out.floor = floor;
  out.id = id;
  out.name = "An invented mod";
  out.caps = "notices";
  return out;
}

}  // namespace

TEST_CASE("connect: ok, and a handle every later call takes") {
  Consumers consumers;
  const ConnectResult result = consumers.connect(request("alpha-mod"), kGraceUi, Inactive::none);
  CHECK(result.status == GRACEUI_OK);
  CHECK(result.reason == 0);
  CHECK(result.handle != 0);
  CHECK(result.owner == 1);
  CHECK(consumers.owner_of(result.handle) == 1);
  CHECK(consumers.count() == 1);

  ConsumerInfo info;
  REQUIRE(consumers.info(0, info));
  CHECK(std::string_view(info.id) == "alpha-mod");
  CHECK(std::string_view(info.name) == "An invented mod");
  CHECK(std::string_view(info.caps) == "notices");
  CHECK(info.version == GRACEUI_VERSION_PACK(1, 0, 0));
  CHECK(info.floor == GRACEUI_VERSION_PACK(0, 1, 0));
  CHECK_FALSE(consumers.info(1, info));
}

TEST_CASE("connect: too_old across versions, the floor against GraceUI's version") {
  Consumers consumers;
  // GraceUI 0.1.0 against floors below, at and above it.
  CHECK(consumers.connect(request("floor-any", 0), kGraceUi, Inactive::none).status == GRACEUI_OK);
  CHECK(consumers.connect(request("floor-older", GRACEUI_VERSION_PACK(0, 0, 9)), kGraceUi, Inactive::none).status ==
        GRACEUI_OK);
  CHECK(consumers.connect(request("floor-same", kGraceUi), kGraceUi, Inactive::none).status == GRACEUI_OK);
  CHECK(consumers.connect(request("floor-patch", GRACEUI_VERSION_PACK(0, 1, 1)), kGraceUi, Inactive::none).status ==
        GRACEUI_TOO_OLD);
  CHECK(consumers.connect(request("floor-minor", GRACEUI_VERSION_PACK(0, 2, 0)), kGraceUi, Inactive::none).status ==
        GRACEUI_TOO_OLD);
  CHECK(consumers.connect(request("floor-major", GRACEUI_VERSION_PACK(1, 0, 0)), kGraceUi, Inactive::none).status ==
        GRACEUI_TOO_OLD);
  // A newer GraceUI serves an older mod.
  CHECK(consumers.connect(request("newer-host", GRACEUI_VERSION_PACK(0, 2, 0)), GRACEUI_VERSION_PACK(1, 4, 2),
                          Inactive::none)
            .status == GRACEUI_OK);
  CHECK(consumers.count() == 4);  // a too-old mod is not connected
}

TEST_CASE("connect: refused, with the reason, for every argument that fails its check") {
  Consumers consumers;
  ConnectRequest bad = request("ab");
  ConnectResult result = consumers.connect(bad, kGraceUi, Inactive::none);
  CHECK(result.status == GRACEUI_REFUSED);
  CHECK(result.reason == GRACEUI_REFUSED_ID);
  CHECK(result.handle == 0);

  bad = request("Capital-Mod");
  CHECK(consumers.connect(bad, kGraceUi, Inactive::none).reason == GRACEUI_REFUSED_ID);

  bad = request("good-id");
  bad.name = "";
  result = consumers.connect(bad, kGraceUi, Inactive::none);
  CHECK(result.status == GRACEUI_REFUSED);
  CHECK(result.reason == GRACEUI_REFUSED_NAME);
  bad.name = "bad\xC3";
  CHECK(consumers.connect(bad, kGraceUi, Inactive::none).reason == GRACEUI_REFUSED_NAME);

  bad = request("good-id");
  bad.caps = "notices  panels";
  result = consumers.connect(bad, kGraceUi, Inactive::none);
  CHECK(result.status == GRACEUI_REFUSED);
  CHECK(result.reason == GRACEUI_REFUSED_CAPS);

  // The checks come before the floor: a bad request that is also too old is refused.
  bad = request("x", GRACEUI_VERSION_PACK(9, 0, 0));
  CHECK(consumers.connect(bad, kGraceUi, Inactive::none).status == GRACEUI_REFUSED);
  CHECK(consumers.count() == 0);
}

TEST_CASE("connect: duplicate_id while the id is connected, and free again after a disconnect") {
  Consumers consumers;
  const ConnectResult first = consumers.connect(request("same-mod"), kGraceUi, Inactive::none);
  REQUIRE(first.status == GRACEUI_OK);
  const ConnectResult second = consumers.connect(request("same-mod"), kGraceUi, Inactive::none);
  CHECK(second.status == GRACEUI_DUPLICATE_ID);
  CHECK(second.handle == 0);

  std::uint32_t owner = 0;
  CHECK(consumers.disconnect(first.handle, owner) == GRACEUI_OK);
  CHECK(owner == first.owner);
  CHECK(consumers.owner_of(first.handle) == 0);
  CHECK(consumers.disconnect(first.handle, owner) == GRACEUI_NOT_CONNECTED);  // a handle disconnects once
  consumers.left(first.owner);

  const ConnectResult again = consumers.connect(request("same-mod"), kGraceUi, Inactive::none);
  CHECK(again.status == GRACEUI_OK);
  CHECK(again.owner == first.owner);    // the same slot ...
  CHECK(again.handle != first.handle);  // ... under a new handle: the old one never names the new mod
  CHECK(consumers.owner_of(first.handle) == 0);
  CHECK(consumers.owner_of(again.handle) == again.owner);
}

TEST_CASE("connect: inactive, with its reason, still connects the mod") {
  for (const Inactive inactive : {Inactive::anti_cheat, Inactive::host_failed, Inactive::render_dead}) {
    Consumers consumers;
    const ConnectResult result = consumers.connect(request("quiet-mod"), kGraceUi, inactive);
    CHECK(result.status == GRACEUI_INACTIVE);
    CHECK(result.reason == static_cast<std::uint32_t>(inactive));
    CHECK(result.handle != 0);  // notice takes it in the anti-cheat's notice-only mode (D88)
    CHECK(consumers.owner_of(result.handle) == result.owner);
    // A second mod with the same id is still a duplicate, and a too-old one still too old.
    CHECK(consumers.connect(request("quiet-mod"), kGraceUi, inactive).status == GRACEUI_DUPLICATE_ID);
    CHECK(consumers.connect(request("other-mod", GRACEUI_VERSION_PACK(0, 3, 0)), kGraceUi, inactive).status ==
          GRACEUI_TOO_OLD);
  }
}

TEST_CASE("two mods connect in either order, each with its own handle") {
  for (const bool alpha_first : {true, false}) {
    Consumers consumers;
    const char* first_id = alpha_first ? "alpha-mod" : "beta-mod";
    const char* second_id = alpha_first ? "beta-mod" : "alpha-mod";
    const ConnectResult first = consumers.connect(request(first_id), kGraceUi, Inactive::none);
    const ConnectResult second = consumers.connect(request(second_id), kGraceUi, Inactive::none);
    REQUIRE(first.status == GRACEUI_OK);
    REQUIRE(second.status == GRACEUI_OK);
    CHECK(first.handle != second.handle);
    CHECK(first.owner != second.owner);
    ConsumerInfo info;
    REQUIRE(consumers.info_of(second.owner, info));
    CHECK(std::string_view(info.id) == second_id);
    std::uint32_t owner = 0;
    CHECK(consumers.disconnect(first.handle, owner) == GRACEUI_OK);
    CHECK(consumers.owner_of(second.handle) == second.owner);  // the other stays connected
  }
}

TEST_CASE("handles: nothing but connect's answer names a mod") {
  Consumers consumers;
  const ConnectResult result = consumers.connect(request("only-mod"), kGraceUi, Inactive::none);
  REQUIRE(result.status == GRACEUI_OK);
  CHECK(consumers.owner_of(0) == 0);
  CHECK(consumers.owner_of(result.handle + 256) == 0);  // the next generation of the same slot
  CHECK(consumers.owner_of(result.handle + 1) == 0);    // the next slot
  CHECK(consumers.owner_of(0xFFFFFFFFFFFFFFFFull) == 0);
  CHECK(consumers.owner_of(result.handle & 0xFFu) == 0);  // generation 0 never connects
}

TEST_CASE("connect: refused once GRACEUI_MODS_MAX mods are connected") {
  auto consumers = std::make_unique<Consumers>();
  std::vector<std::string> ids;
  for (std::uint32_t i = 0; i < kMaxConsumers; ++i) {
    ids.push_back("mod-" + std::to_string(i));
  }
  for (const std::string& id : ids) {
    REQUIRE(consumers->connect(request(id), kGraceUi, Inactive::none).status == GRACEUI_OK);
  }
  const ConnectResult one_more = consumers->connect(request("one-more"), kGraceUi, Inactive::none);
  CHECK(one_more.status == GRACEUI_REFUSED);
  CHECK(one_more.reason == GRACEUI_REFUSED_TOO_MANY);
  CHECK(consumers->count() == kMaxConsumers);
}

TEST_CASE("mods connecting at once from many threads: one id connects once") {
  auto consumers = std::make_unique<Consumers>();
  constexpr int kThreads = 8;
  std::atomic<int> ok{0};
  std::atomic<int> duplicate{0};
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!go.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      // Every thread asks for the shared id, then one of its own.
      const ConnectResult shared = consumers->connect(request("shared-mod"), kGraceUi, Inactive::none);
      (shared.status == GRACEUI_OK ? ok : duplicate).fetch_add(1);
      const std::string own = "own-" + std::to_string(t);
      CHECK(consumers->connect(request(own), kGraceUi, Inactive::none).status == GRACEUI_OK);
    });
  }
  go.store(true, std::memory_order_release);
  for (std::thread& thread : threads) {
    thread.join();
  }
  CHECK(ok.load() == 1);
  CHECK(duplicate.load() == kThreads - 1);
  CHECK(consumers->count() == kThreads + 1);
}

TEST_CASE("a slot being left is given to nobody until its clean-up is done") {
  // The host withdraws a disconnected mod's notices and holder by its number after disconnect(); a mod connecting on
  // another thread meanwhile must not have that number, or it would lose what it had just made (the review's L14).
  auto consumers = std::make_unique<Consumers>();
  std::vector<ConnectResult> connected;
  for (std::uint32_t i = 0; i < kMaxConsumers; ++i) {
    connected.push_back(consumers->connect(request(("mod-" + std::to_string(i)).c_str()), kGraceUi, Inactive::none));
    REQUIRE(connected.back().status == GRACEUI_OK);
  }
  std::uint32_t owner = 0;
  REQUIRE(consumers->disconnect(connected[5].handle, owner) == GRACEUI_OK);
  CHECK(consumers->owner_of(connected[5].handle) == 0);  // its handle names nobody at once
  CHECK(consumers->count() == kMaxConsumers - 1);
  const ConnectResult meanwhile = consumers->connect(request("late-mod"), kGraceUi, Inactive::none);
  CHECK(meanwhile.status == GRACEUI_REFUSED);  // its slot is not free yet, and no other is
  CHECK(meanwhile.reason == GRACEUI_REFUSED_TOO_MANY);
  consumers->left(owner);
  const ConnectResult after = consumers->connect(request("late-mod"), kGraceUi, Inactive::none);
  CHECK(after.status == GRACEUI_OK);
  CHECK(after.owner == owner);
}
