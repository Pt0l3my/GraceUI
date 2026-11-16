// alloc_counter.h: the test binary's own global operator new and delete (alloc_counter.cpp) count, on a thread that asks
// them to, every allocation and free: the present thread's side of the triple buffers must make none (invariant 10).
#pragma once

#include <cstdint>

namespace grace::abi::test {

struct Counts {
  std::uint64_t allocations = 0;
  std::uint64_t frees = 0;
};

// While one lives on a thread, that thread's allocations and frees are counted.
class Counting {
 public:
  Counting() noexcept;
  ~Counting();
  Counting(const Counting&) = delete;
  Counting& operator=(const Counting&) = delete;
  Counts counts() const noexcept;
};

}  // namespace grace::abi::test
