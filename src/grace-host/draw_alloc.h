// The allocation functions of a test build (draw_alloc.cpp): Dear ImGui's, set before its context exists, and the
// global operator new and delete of graceui.dll's own module, all through the heap guard (D235), each counting on a
// thread inside a detour; and the guard's check on GraceUI's thread and its test exports.
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(GRACEUI_TEST_EXPORTS)
#include <windows.h>

#include "graceui_test.h"
#endif

namespace grace::host::draw_alloc {

#if defined(GRACEUI_TEST_EXPORTS)
void* imgui_alloc(std::size_t size, void* user) noexcept;
void imgui_free(void* block, void* user) noexcept;

// The first request: GRACEUI_TEST_HEAP_CHECK_MS read, and the handler that ends a run on a breach added, last.
void begin_guard() noexcept;
// GraceUI's thread: how long it may wait between checks, and the check itself once its period has passed.
DWORD check_period_ms() noexcept;
void check_heap_when_due() noexcept;

void fill_heap_guard(GraceUiTestHeapGuard& out) noexcept;
std::uint32_t heap_guard_probe(std::uint32_t kind, std::uint32_t raise) noexcept;
#endif

}  // namespace grace::host::draw_alloc
