// The input half's hand-overs between threads (queue.h), single-threaded and under ThreadSanitizer.
#include <doctest/doctest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "grace/input/queue.h"

namespace {

struct Item {
  std::uint32_t value = 0;
  std::uint32_t check = 0;  // ~value: a torn copy would show
  std::uint64_t padding = 0;
};

}  // namespace

TEST_CASE("the ring keeps order, refuses when full and counts what it dropped") {
  grace::input::SpscRing<std::uint32_t, 4> ring;
  std::uint32_t out = 0;
  CHECK_FALSE(ring.pop(out));
  for (std::uint32_t i = 1; i <= 4; ++i) {
    CHECK(ring.push(i));
  }
  CHECK_FALSE(ring.push(5));
  CHECK(ring.dropped() == 1);
  for (std::uint32_t i = 1; i <= 4; ++i) {
    REQUIRE(ring.pop(out));
    CHECK(out == i);
  }
  CHECK_FALSE(ring.pop(out));
  CHECK(ring.push(6));
  REQUIRE(ring.pop(out));
  CHECK(out == 6);
}

TEST_CASE("the ring between two threads: everything arrives once, in order") {
  static grace::input::SpscRing<Item, 64> ring;
  constexpr std::uint32_t kCount = 200000;
  std::thread producer([] {
    for (std::uint32_t i = 1; i <= kCount;) {
      if (ring.push(Item{i, ~i, 0})) {
        ++i;
      }
    }
  });
  std::uint32_t expected = 1;
  bool in_order = true;
  while (expected <= kCount) {
    Item item;
    if (ring.pop(item)) {
      in_order = in_order && item.value == expected && item.check == ~expected;
      ++expected;
    }
  }
  producer.join();
  CHECK(in_order);
}

TEST_CASE("the newest value: nothing until written, then the last write") {
  grace::input::Latest<Item> latest;
  Item out;
  CHECK_FALSE(latest.read(out));
  CHECK(latest.publish(Item{7, ~7u, 0}));
  REQUIRE(latest.read(out));
  CHECK(out.value == 7);
  CHECK(latest.publish(Item{8, ~8u, 0}));
  REQUIRE(latest.read(out));
  CHECK(out.value == 8);
}

TEST_CASE("the newest value read while two threads write: never torn") {
  static grace::input::Latest<Item> latest;
  std::atomic<bool> stop{false};
  auto writer = [&](std::uint32_t start) {
    for (std::uint32_t i = start; !stop.load(); i += 2) {
      latest.publish(Item{i, ~i, i});
    }
  };
  std::thread first(writer, 1u);
  std::thread second(writer, 2u);
  int torn = 0;
  int read = 0;
  // The reads race the writes, not the threads' start: on a loaded machine the loop below can end before either writer
  // has published anything.
  for (Item out; !latest.read(out);) {
    std::this_thread::yield();
  }
  for (int i = 0; i < 200000; ++i) {
    Item out;
    if (latest.read(out)) {
      ++read;
      torn += out.check == ~out.value && out.padding == out.value ? 0 : 1;
    }
  }
  stop.store(true);
  first.join();
  second.join();
  CHECK(torn == 0);
  CHECK(read > 0);
}

TEST_CASE("the many-to-many ring keeps order for one producer, drops the newest when full and counts them (D176)") {
  grace::input::MpmcRing<std::uint32_t, 4> ring;
  std::uint32_t got = 0;
  CHECK_FALSE(ring.pop(got));
  for (std::uint32_t i = 1; i <= 6; ++i) {
    CHECK(ring.push(i) == (i <= 4));
  }
  CHECK(ring.take_dropped() == 2);
  CHECK(ring.take_dropped() == 0);
  for (std::uint32_t want = 1; want <= 4; ++want) {
    REQUIRE(ring.pop(got));
    CHECK(got == want);
  }
  CHECK_FALSE(ring.pop(got));
  CHECK(ring.push(7));  // room again, the slots reused
  REQUIRE(ring.pop(got));
  CHECK(got == 7);
}

TEST_CASE("the many-to-many ring between three producers and two consumers: each item arrives once") {
  grace::input::MpmcRing<std::uint32_t, 64> ring;
  constexpr std::uint32_t kEach = 20000;
  std::atomic<std::uint32_t> taken{0};
  std::atomic<std::uint64_t> sum{0};
  std::atomic<bool> producing{true};
  std::vector<std::thread> producers;
  for (std::uint32_t p = 0; p < 3; ++p) {
    producers.emplace_back([&ring, p] {
      for (std::uint32_t i = 1; i <= kEach; ++i) {
        while (!ring.push(p * kEach + i)) {
          std::this_thread::yield();  // full: the test waits, GraceUI never does
        }
      }
    });
  }
  std::vector<std::thread> consumers;
  for (int c = 0; c < 2; ++c) {
    consumers.emplace_back([&] {
      std::uint32_t item = 0;
      for (;;) {
        if (ring.pop(item)) {
          taken.fetch_add(1);
          sum.fetch_add(item);
        } else if (!producing.load()) {
          return;
        } else {
          std::this_thread::yield();
        }
      }
    });
  }
  for (std::thread& t : producers) {
    t.join();
  }
  producing.store(false);
  for (std::thread& t : consumers) {
    t.join();
  }
  std::uint32_t rest = 0;
  while (ring.pop(rest)) {
    taken.fetch_add(1);
    sum.fetch_add(rest);
  }
  const std::uint64_t n = 3ull * kEach;
  CHECK(taken.load() == n);
  CHECK(sum.load() == n * (n + 1) / 2);
}
