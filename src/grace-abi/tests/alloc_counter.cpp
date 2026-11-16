// alloc_counter.cpp: the test binary's global operator new and delete, counting on threads that ask (alloc_counter.h).
#include "alloc_counter.h"

#include <cstdlib>
#include <new>

// ThreadSanitizer's runtime defines the global allocation functions itself: under it nothing is replaced, and the counts
// stay 0 (the ThreadSanitizer run checks the hand-overs; the AddressSanitizer and GCC runs check the counts).
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define GRACE_NO_ALLOC_COUNT 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define GRACE_NO_ALLOC_COUNT 1
#endif

namespace {

thread_local bool t_counting = false;
thread_local std::uint64_t t_allocations = 0;
thread_local std::uint64_t t_frees = 0;

[[maybe_unused]] void* counted(std::size_t size) {
  if (t_counting) {
    ++t_allocations;
  }
  return std::malloc(size == 0 ? 1 : size);
}

[[maybe_unused]] void counted_free(void* block) noexcept {
  if (block != nullptr && t_counting) {
    ++t_frees;
  }
  std::free(block);
}

}  // namespace

namespace grace::abi::test {

Counting::Counting() noexcept {
  t_counting = true;
  t_allocations = 0;
  t_frees = 0;
}

Counting::~Counting() { t_counting = false; }

Counts Counting::counts() const noexcept { return Counts{t_allocations, t_frees}; }

}  // namespace grace::abi::test

#if !defined(GRACE_NO_ALLOC_COUNT)
void* operator new(std::size_t size) {
  if (void* block = counted(size)) {
    return block;
  }
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
  if (void* block = counted(size)) {
    return block;
  }
  throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void operator delete(void* block) noexcept { counted_free(block); }
void operator delete[](void* block) noexcept { counted_free(block); }
void operator delete(void* block, std::size_t) noexcept { counted_free(block); }
void operator delete[](void* block, std::size_t) noexcept { counted_free(block); }
void operator delete(void* block, const std::nothrow_t&) noexcept { counted_free(block); }
void operator delete[](void* block, const std::nothrow_t&) noexcept { counted_free(block); }
#endif
