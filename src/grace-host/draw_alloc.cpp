// draw_alloc.cpp: in a test build, every allocation and free that graceui.dll's own code makes (its operator new and
// delete, which Dear ImGui's DX12 backend also uses, and Dear ImGui's allocator) is counted while a detour runs
// GraceUI's work on that thread, for the harness's allocation test (invariant 10). A release build compiles nothing
// here: graceui.dll then uses the CRT's own functions and Dear ImGui's defaults.
#if defined(GRACEUI_TEST_EXPORTS)

#include "draw_alloc.h"

#include <cstdlib>
#include <new>

#include "counters.h"

namespace grace::host::counters {
constinit thread_local bool t_counting = false;
}  // namespace grace::host::counters

namespace {

using grace::host::counters::t_counting;

void* counted_alloc(std::size_t size) noexcept {
  if (t_counting) {
    grace::host::counters::allocations.fetch_add(1, std::memory_order_relaxed);
  }
  return std::malloc(size == 0 ? 1 : size);
}

void counted_free(void* block) noexcept {
  if (block != nullptr && t_counting) {
    grace::host::counters::frees.fetch_add(1, std::memory_order_relaxed);
  }
  std::free(block);
}

}  // namespace

namespace grace::host::draw_alloc {

// SAFETY: Dear ImGui calls these in pairs, with a block from imgui_alloc or null; malloc and free do the rest.
void* imgui_alloc(std::size_t size, void* /*user*/) noexcept { return counted_alloc(size); }
void imgui_free(void* block, void* /*user*/) noexcept { counted_free(block); }

}  // namespace grace::host::draw_alloc

// SAFETY: graceui.dll's own replacements of the global allocation functions (its module only: the CRT is static). They
// keep the standard contract: new throws std::bad_alloc when malloc fails, delete accepts null.
void* operator new(std::size_t size) {
  if (void* block = counted_alloc(size)) {
    return block;
  }
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
  if (void* block = counted_alloc(size)) {
    return block;
  }
  throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted_alloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return counted_alloc(size); }
void operator delete(void* block) noexcept { counted_free(block); }
void operator delete[](void* block) noexcept { counted_free(block); }
void operator delete(void* block, std::size_t) noexcept { counted_free(block); }
void operator delete[](void* block, std::size_t) noexcept { counted_free(block); }
void operator delete(void* block, const std::nothrow_t&) noexcept { counted_free(block); }
void operator delete[](void* block, const std::nothrow_t&) noexcept { counted_free(block); }

#endif  // GRACEUI_TEST_EXPORTS
