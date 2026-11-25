// The opt-in on_present callbacks (present.h, D259): a hook's bounds, the mod's and the session's counts, a panel's
// hook only for that panel, the budget (0.5 ms a frame for a mod's callbacks together, 120 frames running), suspension
// by the budget or an exception, named once with the mod's id kept, the wait for the frame being drawn before a hook is
// freed (for as long as the present thread lives), and the present thread's side making no allocation (invariant 10).
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>

#include "alloc_counter.h"
#include "grace/abi/present.h"

using namespace grace::abi;

namespace {

constexpr GraceUiConsumer kModA = (1ull << 8) | 1;
constexpr GraceUiConsumer kModB = (1ull << 8) | 2;
constexpr std::uint32_t kNumberA = 1;
constexpr std::uint32_t kNumberB = 2;

std::uint32_t draw_nothing(void*, const GraceUiPresentFrame*, GraceUiDrawList*) { return GRACEUI_PRESENT_KEEP; }
std::uint32_t draw_other(void*, const GraceUiPresentFrame*, GraceUiDrawList*) { return GRACEUI_PRESENT_DRAW; }

PresentHookDesc hook_desc(GraceUiPresentCallback callback = &draw_nothing, void* user = nullptr) {
  PresentHookDesc d;
  d.callback = callback;
  d.user = user;
  d.vertex_capacity = 64;
  d.index_capacity = 96;
  d.command_capacity = 4;
  return d;
}

// A panel's id at slot `index`, as panels.h makes them.
std::uint64_t panel_at(std::uint32_t index, std::uint32_t generation = 1) { return panel_id(index, generation); }

// One frame on the present thread in which the hook at slot `index` (if it is called) took `us` microseconds.
std::uint32_t frame_of(PresentHooks& hooks, std::uint32_t index, std::int64_t us) {
  hooks.begin_frame();
  if (const PresentHook* hook = hooks.hook(index, panel_at(index)); hook != nullptr) {
    hooks.spent(*hook, us * 1000);
  }
  return hooks.end_frame();
}

std::atomic<bool> g_present_alive{true};
bool present_alive() noexcept { return g_present_alive.load(); }

}  // namespace

TEST_CASE("a hook GraceUI takes: a callback, and capacities from 1 to D76's bounds of one submission") {
  CHECK(valid_present_hook(hook_desc()));
  PresentHookDesc d = hook_desc(nullptr);
  CHECK_FALSE(valid_present_hook(d));
  d = hook_desc();
  d.vertex_capacity = 0;
  CHECK_FALSE(valid_present_hook(d));
  d.vertex_capacity = kDrawVerticesMax;
  CHECK(valid_present_hook(d));
  d.vertex_capacity = kDrawVerticesMax + 1;
  CHECK_FALSE(valid_present_hook(d));
  d = hook_desc();
  d.index_capacity = kDrawIndicesMax + 1;
  CHECK_FALSE(valid_present_hook(d));
  d = hook_desc();
  d.command_capacity = 0;
  CHECK_FALSE(valid_present_hook(d));
  d.command_capacity = kDrawCommandsMax + 1;
  CHECK_FALSE(valid_present_hook(d));
}

TEST_CASE("a list a callback gave fits its hook only within the hook's capacities and a stride GraceUI reads") {
  PresentHook hook;
  hook.vertex_capacity = 64;
  hook.index_capacity = 96;
  hook.command_capacity = 4;
  CHECK(present_list_fits(hook, 64, 96, 4, sizeof(GraceUiDrawCommand)));
  CHECK(present_list_fits(hook, 0, 0, 0, sizeof(GraceUiDrawCommand)));
  CHECK_FALSE(present_list_fits(hook, 65, 96, 4, sizeof(GraceUiDrawCommand)));
  CHECK_FALSE(present_list_fits(hook, 64, 97, 4, sizeof(GraceUiDrawCommand)));
  CHECK_FALSE(present_list_fits(hook, 64, 96, 5, sizeof(GraceUiDrawCommand)));
  CHECK_FALSE(present_list_fits(hook, 64, 96, 4, 8));       // below ABI 1.0's command
  CHECK_FALSE(present_list_fits(hook, 64, 96, 4, 100000));  // a stride that is not one
}

TEST_CASE("a panel's hook is set, replaced and cleared, and is the present thread's only for that panel") {
  PresentHooks hooks;
  const std::uint64_t textures[] = {77, 78};
  int user = 5;
  REQUIRE(hooks.set(kModA, kNumberA, panel_at(3), 3, hook_desc(&draw_nothing, &user), textures, 2, "lantern") ==
          PresentSet::ok);
  CHECK(hooks.count() == 1);
  CHECK(hooks.wants_frame(3, panel_at(3)));
  CHECK_FALSE(hooks.wants_frame(3, panel_at(3, 2)));  // the slot's panel since: not this hook's
  CHECK_FALSE(hooks.wants_frame(4, panel_at(4)));
  CHECK_FALSE(hooks.wants_frame(kMaxPanels, panel_at(3)));
  hooks.begin_frame();
  PresentHook* hook = hooks.hook(3, panel_at(3));
  REQUIRE(hook != nullptr);
  CHECK(hook->callback == &draw_nothing);
  CHECK(hook->user == &user);
  CHECK(hook->owner == kModA);
  CHECK(hook->mod == kNumberA);
  CHECK(std::string_view(hook->id) == "lantern");
  CHECK(hook->texture_count == 2);
  CHECK(hook->textures[1] == 78);
  CHECK(hook->list.vertices.capacity() >= 64);
  CHECK(hook->list.indices.capacity() >= 96);
  CHECK(hook->list.commands.capacity() >= 4);
  CHECK(hooks.hook(3, panel_at(3, 2)) == nullptr);
  CHECK(hooks.hook(4, panel_at(4)) == nullptr);
  CHECK(hooks.hook(kMaxPanels, panel_at(3)) == nullptr);
  hooks.end_frame();

  REQUIRE(hooks.set(kModA, kNumberA, panel_at(3), 3, hook_desc(&draw_other), nullptr, 0) == PresentSet::ok);
  CHECK(hooks.count() == 1);  // replaced, not added
  hooks.begin_frame();
  REQUIRE(hooks.hook(3, panel_at(3)) != nullptr);
  CHECK(hooks.hook(3, panel_at(3))->callback == &draw_other);
  hooks.end_frame();

  REQUIRE(hooks.set(kModA, kNumberA, panel_at(3), 3, PresentHookDesc{}, nullptr, 0) == PresentSet::ok);
  CHECK(hooks.count() == 0);
  CHECK_FALSE(hooks.wants_frame(3, panel_at(3)));
  hooks.begin_frame();
  CHECK(hooks.hook(3, panel_at(3)) == nullptr);
  hooks.end_frame();
  // Clearing a panel with no hook is no error.
  CHECK(hooks.set(kModA, kNumberA, panel_at(3), 3, PresentHookDesc{}, nullptr, 0) == PresentSet::ok);
}

TEST_CASE("a hook is refused out of bounds, and past the mod's four or the session's eight") {
  PresentHooks hooks;
  PresentHookDesc bad = hook_desc();
  bad.command_capacity = 0;
  CHECK(hooks.set(kModA, kNumberA, panel_at(0), 0, bad, nullptr, 0) == PresentSet::invalid);
  CHECK(hooks.set(kModA, kNumberA, panel_at(0), kMaxPanels, hook_desc(), nullptr, 0) == PresentSet::invalid);
  CHECK(hooks.set(kModA, 0, panel_at(0), 0, hook_desc(), nullptr, 0) == PresentSet::invalid);
  CHECK(hooks.set(kModA, kMaxConsumers + 1, panel_at(0), 0, hook_desc(), nullptr, 0) == PresentSet::invalid);
  for (std::uint32_t i = 0; i < kPresentHooksPerMod; ++i) {
    CHECK(hooks.set(kModA, kNumberA, panel_at(i), i, hook_desc(), nullptr, 0) == PresentSet::ok);
  }
  CHECK(hooks.set(kModA, kNumberA, panel_at(9), 9, hook_desc(), nullptr, 0) == PresentSet::full);
  CHECK(hooks.set(kModA, kNumberA, panel_at(0), 0, hook_desc(&draw_other), nullptr, 0) == PresentSet::ok);  // replaced
  for (std::uint32_t i = 10; i < 10 + kPresentHooksPerMod; ++i) {
    CHECK(hooks.set(kModB, kNumberB, panel_at(i), i, hook_desc(), nullptr, 0) == PresentSet::ok);
  }
  CHECK(hooks.count() == kPresentHooksMax);
  constexpr GraceUiConsumer kModC = (1ull << 8) | 3;
  CHECK(hooks.set(kModC, 3, panel_at(20), 20, hook_desc(), nullptr, 0) == PresentSet::full);
  const PresentHooks::Removed removed = hooks.remove_owner(kModA, kNumberA);
  CHECK(removed.count == kPresentHooksPerMod);
  CHECK_FALSE(removed.suspended);
  CHECK(hooks.count() == kPresentHooksPerMod);
  CHECK(hooks.set(kModC, 3, panel_at(20), 20, hook_desc(), nullptr, 0) == PresentSet::ok);
  hooks.begin_frame();
  CHECK(hooks.hook(0, panel_at(0)) == nullptr);
  CHECK(hooks.hook(10, panel_at(10)) != nullptr);
  hooks.end_frame();
  // A connection's hooks alone go, with no number named: the number's state is left as it is.
  CHECK(hooks.remove_owner(kModC, 0).count == 1);
  CHECK(hooks.count() == kPresentHooksPerMod);
}

TEST_CASE("the budget: a mod over 0.5 ms in 120 frames running is suspended, named once, with its id kept") {
  PresentHooks hooks;
  REQUIRE(hooks.set(kModA, kNumberA, panel_at(1), 1, hook_desc(), nullptr, 0, "slow-one") == PresentSet::ok);
  REQUIRE(hooks.set(kModA, kNumberA, panel_at(3), 3, hook_desc(), nullptr, 0, "slow-one") == PresentSet::ok);
  REQUIRE(hooks.set(kModB, kNumberB, panel_at(2), 2, hook_desc(), nullptr, 0, "quick-one") == PresentSet::ok);
  // 119 frames over, then one at the budget, which is within it: the run starts again.
  for (std::uint32_t i = 0; i + 1 < kPresentOverFrames; ++i) {
    CHECK(frame_of(hooks, 1, kPresentBudgetUs + 1) == 0);
  }
  CHECK(frame_of(hooks, 1, kPresentBudgetUs) == 0);
  // 119 over, then a frame it did not run in (its panels hidden): the run starts again too.
  for (std::uint32_t i = 0; i + 1 < kPresentOverFrames; ++i) {
    CHECK(frame_of(hooks, 1, kPresentBudgetUs + 1) == 0);
  }
  CHECK(hooks.spent_before(kNumberA) == (kPresentBudgetUs + 1) * 1000);
  CHECK(frame_of(hooks, 2, 10) == 0);
  CHECK_FALSE(hooks.suspended(kNumberA));
  CHECK(hooks.spent_before(kNumberA) == 0);  // it did not run in the frame before
  CHECK(hooks.spent_before(kNumberB) == 10000);
  CHECK(hooks.spent_before(0) == 0);
  // The mod's callbacks together: two of 300 µs are over 500 (the store counts nanoseconds).
  std::uint32_t named = 0;
  for (std::uint32_t i = 0; i < kPresentOverFrames; ++i) {
    hooks.begin_frame();
    hooks.spent(*hooks.hook(1, panel_at(1)), 300000);
    hooks.spent(*hooks.hook(3, panel_at(3)), 300000);
    hooks.spent(*hooks.hook(2, panel_at(2)), 100000);
    const std::uint32_t newly = hooks.end_frame();
    named += newly != 0 ? 1 : 0;
    if (i + 1 < kPresentOverFrames) {
      CHECK(newly == 0);
    } else {
      CHECK(newly == (1u << (kNumberA - 1)));
    }
  }
  CHECK(named == 1);
  CHECK(hooks.suspended(kNumberA));
  CHECK_FALSE(hooks.suspended_by_fault(kNumberA));
  CHECK(hooks.suspended_owner(kNumberA) == kModA);
  char id[GRACEUI_ID_MAX + 1] = {};
  CHECK(hooks.suspended_id(kNumberA, id));
  CHECK(std::string_view(id) == "slow-one");
  CHECK_FALSE(hooks.suspended(kNumberB));
  CHECK(hooks.suspended_owner(kNumberB) == 0);
  CHECK_FALSE(hooks.suspended_id(kNumberB, id));
  // Suspended: its hooks are no longer the present thread's, nor wanted for a frame, and it is not named again.
  CHECK_FALSE(hooks.wants_frame(1, panel_at(1)));
  CHECK(hooks.wants_frame(2, panel_at(2)));
  hooks.begin_frame();
  CHECK(hooks.hook(1, panel_at(1)) == nullptr);
  CHECK(hooks.hook(2, panel_at(2)) != nullptr);
  CHECK(hooks.end_frame() == 0);
  // It can set no hook, but may clear one, which is then freed.
  CHECK(hooks.set(kModA, kNumberA, panel_at(5), 5, hook_desc(), nullptr, 0) == PresentSet::suspended);
  CHECK(hooks.set(kModA, kNumberA, panel_at(1), 1, hook_desc(&draw_other), nullptr, 0) == PresentSet::suspended);
  CHECK(hooks.set(kModA, kNumberA, panel_at(1), 1, PresentHookDesc{}, nullptr, 0) == PresentSet::ok);
  CHECK(hooks.count() == 2);
  // Its connection goes: removed, said to have been suspended, with its id; its number's next mod starts afresh.
  const PresentHooks::Removed removed = hooks.remove_owner(kModA, kNumberA);
  CHECK(removed.count == 1);
  CHECK(removed.suspended);
  CHECK(std::string_view(removed.id) == "slow-one");
  CHECK_FALSE(hooks.suspended(kNumberA));
  CHECK(hooks.suspended_owner(kNumberA) == 0);
  constexpr GraceUiConsumer kModA2 = (2ull << 8) | 1;
  CHECK(hooks.set(kModA2, kNumberA, panel_at(5), 5, hook_desc(), nullptr, 0) == PresentSet::ok);
  for (std::uint32_t i = 0; i + 1 < kPresentOverFrames; ++i) {
    CHECK(frame_of(hooks, 5, kPresentBudgetUs + 1) == 0);
  }
  CHECK_FALSE(hooks.remove_owner(kModA2, kNumberA).suspended);
}

TEST_CASE("a number's next mod starts its run afresh, even with no frame between the two") {
  PresentHooks hooks;
  REQUIRE(hooks.set(kModA, kNumberA, panel_at(1), 1, hook_desc(), nullptr, 0) == PresentSet::ok);
  for (std::uint32_t i = 0; i + 1 < kPresentOverFrames; ++i) {
    CHECK(frame_of(hooks, 1, kPresentBudgetUs + 1) == 0);
  }
  CHECK(hooks.remove_owner(kModA, kNumberA).count == 1);
  constexpr GraceUiConsumer kModA2 = (2ull << 8) | 1;
  REQUIRE(hooks.set(kModA2, kNumberA, panel_at(1), 1, hook_desc(), nullptr, 0) == PresentSet::ok);
  CHECK(frame_of(hooks, 1, kPresentBudgetUs + 1) == 0);  // its first frame over, not the 120th
  CHECK_FALSE(hooks.suspended(kNumberA));
}

TEST_CASE("an exception out of a callback suspends its mod at once, named once") {
  PresentHooks hooks;
  REQUIRE(hooks.set(kModA, kNumberA, panel_at(1), 1, hook_desc(), nullptr, 0, "thrower") == PresentSet::ok);
  REQUIRE(hooks.set(kModA, kNumberA, panel_at(2), 2, hook_desc(), nullptr, 0, "thrower") == PresentSet::ok);
  hooks.begin_frame();
  const PresentHook* first = hooks.hook(1, panel_at(1));
  REQUIRE(first != nullptr);
  hooks.faulted(*first);
  CHECK(hooks.hook(2, panel_at(2)) == nullptr);  // its other callbacks in the same frame are not called
  hooks.faulted(*first);
  CHECK(hooks.end_frame() == (1u << (kNumberA - 1)));
  CHECK(hooks.suspended(kNumberA));
  CHECK(hooks.suspended_by_fault(kNumberA));
  CHECK(hooks.suspended_owner(kNumberA) == kModA);
  char id[GRACEUI_ID_MAX + 1] = {};
  CHECK(hooks.suspended_id(kNumberA, id));
  CHECK(std::string_view(id) == "thrower");
  CHECK(frame_of(hooks, 1, 0) == 0);
}

TEST_CASE("a suspension in the frame being drawn while its mod leaves is still reported with its id") {
  PresentHooks hooks;
  REQUIRE(hooks.set(kModA, kNumberA, panel_at(1), 1, hook_desc(), nullptr, 0, "leaver") == PresentSet::ok);
  std::atomic<int> step{0};
  std::thread present([&] {
    hooks.begin_frame();
    const PresentHook* hook = hooks.hook(1, panel_at(1));
    step.store(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));  // the mod's thread takes its hook out meanwhile
    if (hook != nullptr) {
      hooks.faulted(*hook);
    }
    hooks.end_frame();
  });
  while (step.load() == 0) {
    std::this_thread::yield();
  }
  const PresentHooks::Removed removed = hooks.remove_owner(kModA, kNumberA);
  present.join();
  CHECK(removed.count == 1);
  CHECK(removed.suspended);
  CHECK(std::string_view(removed.id) == "leaver");
  CHECK_FALSE(hooks.suspended(kNumberA));
}

TEST_CASE("a hook taken out while its frame is drawn is freed only once that frame ends, however long it takes") {
  PresentHooks hooks;
  hooks.set_alive(&present_alive);
  g_present_alive.store(true);
  REQUIRE(hooks.set(kModA, kNumberA, panel_at(1), 1, hook_desc(), nullptr, 0) == PresentSet::ok);
  std::atomic<int> step{0};
  std::atomic<bool> cleared{false};
  std::atomic<bool> ended_first{false};
  std::thread present([&] {
    hooks.begin_frame();
    PresentHook* hook = hooks.hook(1, panel_at(1));
    CHECK(hook != nullptr);
    step.store(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));  // longer than any bound a wait might have had
    CHECK_FALSE(cleared.load());  // the mod's thread still waits
    CHECK((hook != nullptr && hook->callback == &draw_nothing));  // not freed under the present thread
    ended_first.store(true);
    hooks.end_frame();
    // The next frame finds no hook.
    hooks.begin_frame();
    CHECK(hooks.hook(1, panel_at(1)) == nullptr);
    hooks.end_frame();
  });
  while (step.load() == 0) {
    std::this_thread::yield();
  }
  CHECK(hooks.set(kModA, kNumberA, panel_at(1), 1, PresentHookDesc{}, nullptr, 0) == PresentSet::ok);
  cleared.store(true);
  CHECK(ended_first.load());
  present.join();
}

TEST_CASE("a mod's thread stops waiting once the present thread has gone mid-frame, and frees nothing then") {
  PresentHooks hooks;
  hooks.set_alive(&present_alive);
  g_present_alive.store(true);
  REQUIRE(hooks.set(kModA, kNumberA, panel_at(1), 1, hook_desc(), nullptr, 0) == PresentSet::ok);
  hooks.begin_frame();  // a present thread that stopped mid-frame, as at the process's exit
  PresentHook* held = hooks.hook(1, panel_at(1));
  REQUIRE(held != nullptr);
  std::atomic<bool> returned{false};
  std::thread mod([&] {
    CHECK(hooks.remove_owner(kModA, kNumberA).count == 1);
    returned.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK_FALSE(returned.load());  // it waits while the present thread lives
  g_present_alive.store(false);
  mod.join();
  CHECK(returned.load());
  CHECK(held->callback == &draw_nothing);  // left allocated
  g_present_alive.store(true);
  hooks.end_frame();
  delete held;  // the test's own: GraceUI leaves it, as it leaves its stores, to the process's end
}

TEST_CASE("the present thread's side allocates nothing and frees nothing (invariant 10)") {
  PresentHooks hooks;
  REQUIRE(hooks.set(kModA, kNumberA, panel_at(1), 1, hook_desc(), nullptr, 0) == PresentSet::ok);
  const test::Counting counting;
  for (int i = 0; i < 300; ++i) {
    CHECK(hooks.wants_frame(1, panel_at(1)));
    hooks.begin_frame();
    PresentHook* hook = hooks.hook(1, panel_at(1));
    REQUIRE(hook != nullptr);
    // A copy of a callback's list into the room reserved for it, as grace-host makes.
    hook->list.vertices.resize(64);
    hook->list.indices.resize(96);
    hook->list.commands.resize(4);
    hooks.spent(*hook, i % 150 < 100 ? (kPresentBudgetUs + 1) * 1000 : 0);  // over, but never 120 running
    hooks.end_frame();
  }
  hooks.begin_frame();
  hooks.faulted(*hooks.hook(1, panel_at(1)));
  hooks.end_frame();
  CHECK(counting.counts().allocations == 0);
  CHECK(counting.counts().frees == 0);
  CHECK(hooks.frames() == 301);
}
