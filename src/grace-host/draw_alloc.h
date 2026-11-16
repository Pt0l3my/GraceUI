// The allocation functions of a test build (draw_alloc.cpp): Dear ImGui's, set before its context exists, and the
// global operator new and delete of graceui.dll's own module, each counting on a thread inside a detour.
#pragma once

#include <cstddef>

namespace grace::host::draw_alloc {

#if defined(GRACEUI_TEST_EXPORTS)
void* imgui_alloc(std::size_t size, void* user) noexcept;
void imgui_free(void* block, void* user) noexcept;
#endif

}  // namespace grace::host::draw_alloc
