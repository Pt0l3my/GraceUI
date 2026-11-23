// The heap guard's rules (D235): each kind of breach found where it should be, a sound block never reported, the
// quarantine within its limits, and every count kept across threads. Each case has a guard of its own, a static one, so
// that the blocks it keeps (in its quarantine, or damaged for good) stay reachable and the leak checker counts none.
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "grace/guard/heap_guard.h"

using grace::guard::Breach;
using grace::guard::Found;
using grace::guard::Guard;
using grace::guard::kMakerFrames;

namespace {

const int kFrameA = 0;
const int kFrameB = 0;
const int kFrameC = 0;
constexpr const void* kMaker[kMakerFrames] = {&kFrameA, &kFrameB, &kFrameC};

unsigned char* make(Guard& guard, std::size_t size) {
  return static_cast<unsigned char*>(guard.allocate(size, kMaker));
}

void same_maker(const Found& found) {
  CHECK(found.maker[0] == &kFrameA);
  CHECK(found.maker[1] == &kFrameB);
  CHECK(found.maker[2] == &kFrameC);
}

}  // namespace

TEST_CASE("a sound block is made, used to its last byte and freed with no breach") {
  static Guard guard;
  unsigned char* block = make(guard, 40);
  REQUIRE(block != nullptr);
  void* aligned = block;
  std::size_t room = 64;
  CHECK(std::align(16, 1, aligned, room) == block);  // already on a 16-byte boundary, as malloc aligns
  std::memset(block, 0x5A, 40);
  CHECK(guard.stats().live_blocks == 1);
  CHECK(guard.stats().live_bytes == 40);
  CHECK(guard.serial_of(block) == 1);
  CHECK(guard.release(block).breach == Breach::none);
  CHECK(guard.stats().live_blocks == 0);
  CHECK(guard.stats().quarantined_blocks == 1);
  CHECK(guard.check_all().breach == Breach::none);
  CHECK(guard.stats().checks == 1);
  CHECK(guard.stats().breaches == 0);
  CHECK(guard.release(nullptr).breach == Breach::none);
  grace::guard::Stats copy;
  CHECK(guard.try_stats(copy));
  CHECK(copy.made == 1);
  unsigned char* empty = make(guard, 0);
  REQUIRE(empty != nullptr);
  CHECK(guard.release(empty).breach == Breach::none);
}

TEST_CASE("a byte written past a block's end is found when it is freed, with its size, number and maker") {
  static Guard guard;
  unsigned char* first = make(guard, 8);
  unsigned char* block = make(guard, 24);
  block[24] = 0;  // the tail guard's first byte
  const Found found = guard.release(block);
  CHECK(found.breach == Breach::past_end);
  CHECK(found.size == 24);
  CHECK(found.serial == 2);
  same_maker(found);
  CHECK(guard.stats().damaged_blocks == 1);
  CHECK(guard.stats().quarantined_blocks == 0);  // never kept as a sound block, nor given back to malloc
  CHECK(guard.release(first).breach == Breach::none);
}

TEST_CASE("a byte written before a block's start is found when it is freed") {
  static Guard guard;
  unsigned char* block = make(guard, 24);
  block[-1] = 0;  // the front guard's last byte
  const Found found = guard.release(block);
  CHECK(found.breach == Breach::before_start);
  CHECK(found.size == 24);
  same_maker(found);
}

TEST_CASE("a live block written past its end is found by check_all, once") {
  static Guard guard;
  unsigned char* sound = make(guard, 16);
  unsigned char* block = make(guard, 100);
  block[100 + 15] = 1;  // the tail guard's last byte
  const Found found = guard.check_all();
  CHECK(found.breach == Breach::past_end);
  CHECK(found.size == 100);
  CHECK(guard.stats().live_blocks == 1);
  CHECK(guard.stats().damaged_blocks == 1);
  CHECK(guard.check_all().breach == Breach::none);  // kept for good, and not reported again
  CHECK(guard.release(sound).breach == Breach::none);
}

TEST_CASE("a byte written to a freed block is found by check_all, once") {
  static Guard guard;
  unsigned char* block = make(guard, 32);
  REQUIRE(guard.release(block).breach == Breach::none);
  block[5] = 0x11;  // the block is still the guard's, in its quarantine
  const Found found = guard.check_all();
  CHECK(found.breach == Breach::after_free);
  CHECK(found.size == 32);
  CHECK(found.serial == 1);
  same_maker(found);
  CHECK(guard.stats().quarantined_blocks == 0);
  CHECK(guard.check_all().breach == Breach::none);
}

TEST_CASE("a block freed twice is found, and stays in the quarantine") {
  static Guard guard;
  unsigned char* block = make(guard, 48);
  REQUIRE(guard.release(block).breach == Breach::none);
  const Found found = guard.release(block);
  CHECK(found.breach == Breach::freed_twice);
  CHECK(found.size == 48);
  same_maker(found);
  CHECK(guard.stats().quarantined_blocks == 1);
  CHECK(guard.check_all().breach == Breach::none);
}

TEST_CASE("a pointer the guard never made is refused and left untouched") {
  static Guard guard;
  // A buffer of malloc's, so that the bytes the guard reads before the pointer are a buffer's own, all zero.
  auto* buffer = static_cast<unsigned char*>(std::calloc(256, 1));
  REQUIRE(buffer != nullptr);
  const Found found = guard.release(buffer + 128);
  CHECK(found.breach == Breach::not_ours);
  CHECK(found.size == 0);
  CHECK(guard.serial_of(buffer + 128) == 0);
  for (int i = 0; i < 256; ++i) {
    REQUIRE(buffer[i] == 0);
  }
  std::free(buffer);
}

TEST_CASE("the quarantine keeps at most its blocks, and a write after free is found as its block leaves") {
  static Guard guard;
  unsigned char* first = make(guard, 16);
  REQUIRE(guard.release(first).breach == Breach::none);
  first[0] = 0;  // after its free, while it is kept
  Found found;
  for (std::size_t i = 1; i < grace::guard::kQuarantineBlocks; ++i) {
    found = guard.release(make(guard, 16));
    REQUIRE(found.breach == Breach::none);
  }
  CHECK(guard.stats().quarantined_blocks == grace::guard::kQuarantineBlocks);
  found = guard.release(make(guard, 16));  // the ring is full: the first block leaves, checked
  CHECK(found.breach == Breach::after_free);
  CHECK(found.serial == 1);
  CHECK(guard.stats().quarantined_blocks == grace::guard::kQuarantineBlocks);
  CHECK(guard.stats().damaged_blocks == 1);
}

TEST_CASE("the quarantine keeps at most its bytes, and a large block goes back to malloc at once") {
  static Guard guard;
  const std::size_t big = grace::guard::kQuarantineLargest;  // the largest it keeps
  const std::size_t count = grace::guard::kQuarantineBytes / big + 4;
  for (std::size_t i = 0; i < count; ++i) {
    REQUIRE(guard.release(make(guard, big)).breach == Breach::none);
  }
  CHECK(guard.stats().quarantined_bytes <= grace::guard::kQuarantineBytes);
  CHECK(guard.stats().quarantined_blocks == grace::guard::kQuarantineBytes / big);
  const std::uint64_t kept = guard.stats().quarantined_blocks;
  REQUIRE(guard.release(make(guard, big + 1)).breach == Breach::none);
  CHECK(guard.stats().quarantined_blocks == kept);
  CHECK(guard.check_all().breach == Breach::none);
}

TEST_CASE("blocks made and freed on four threads at once, checked meanwhile, keep every count") {
  static Guard guard;
  static std::atomic<int> breaches{0};  // counted here: the threads assert nothing themselves
  constexpr int kThreads = 4;
  constexpr int kEach = 5000;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([t] {
      std::vector<unsigned char*> held;
      for (int i = 0; i < kEach; ++i) {
        const std::size_t size = static_cast<std::size_t>((i * 7 + t) % 300);
        unsigned char* block = make(guard, size);
        if (block != nullptr) {
          std::memset(block, t, size);
          held.push_back(block);
        }
        if (held.size() > 50) {
          breaches += guard.release(held.front()).breach == Breach::none ? 0 : 1;
          held.erase(held.begin());
        }
      }
      for (unsigned char* block : held) {
        breaches += guard.release(block).breach == Breach::none ? 0 : 1;
      }
    });
  }
  std::thread checker([] {
    for (int i = 0; i < 50; ++i) {
      breaches += guard.check_all().breach == Breach::none ? 0 : 1;
      std::this_thread::yield();
    }
  });
  for (std::thread& thread : threads) {
    thread.join();
  }
  checker.join();
  CHECK(breaches.load() == 0);
  CHECK(guard.stats().live_blocks == 0);
  CHECK(guard.stats().live_bytes == 0);
  CHECK(guard.stats().made == static_cast<std::uint64_t>(kThreads * kEach));
  CHECK(guard.stats().breaches == 0);
  CHECK(guard.check_all().breach == Breach::none);
}

TEST_CASE("each breach has its name") {
  CHECK(std::strcmp(grace::guard::breach_name(Breach::past_end), "written past its end") == 0);
  CHECK(std::strcmp(grace::guard::breach_name(Breach::before_start), "written before its start") == 0);
  CHECK(std::strcmp(grace::guard::breach_name(Breach::after_free), "written after its free") == 0);
  CHECK(std::strcmp(grace::guard::breach_name(Breach::freed_twice), "freed twice") == 0);
  CHECK(std::strcmp(grace::guard::breach_name(Breach::not_ours), "not a block of the guard's") == 0);
}
