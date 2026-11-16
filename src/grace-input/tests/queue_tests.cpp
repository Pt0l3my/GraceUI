// The input half's hand-overs between threads (queue.h), single-threaded and under ThreadSanitizer.
#include <doctest/doctest.h>

#include <atomic>
#include <thread>

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
