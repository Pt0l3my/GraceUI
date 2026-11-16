// grace-host's entry points for graceui.dll's exports (src/graceui/abi.cpp). Everything here is noexcept: no exception
// leaves grace-host through them (D110).
#pragma once

#include <cstdint>

#if defined(GRACEUI_TEST_EXPORTS)
#include "graceui_test.h"
#endif

namespace grace::host {

// The first call in this copy of graceui.dll takes the per-process mutex and, if this copy is the first to take it,
// pins the DLL and starts the init thread (D71, D72); every later call does nothing. Answers whether this copy is the
// active host.
bool request_start() noexcept;

#if defined(GRACEUI_TEST_EXPORTS)
void test_set_panel(bool on) noexcept;
void test_fill_state(GraceUiTestState& out) noexcept;
void test_reset_counters() noexcept;
std::uint32_t test_copy_frame_costs(GraceUiTestFrameCost* out, std::uint32_t capacity) noexcept;
std::uint32_t test_copy_log_line(std::uint32_t index, char* out, std::uint32_t capacity) noexcept;
std::uint32_t test_copy_hook_name(std::uint32_t index, char* out, std::uint32_t capacity) noexcept;
void test_request_input(std::uint32_t owner, std::uint32_t kind, std::uint32_t wants, GraceUiTestAnswer& out) noexcept;
bool test_release_input(std::uint32_t owner) noexcept;
void test_set_world(std::uint32_t flags) noexcept;
void test_set_caret(std::int32_t x, std::int32_t y, std::int32_t height) noexcept;
void test_set_code_page(std::uint32_t code_page) noexcept;
void test_fill_input(GraceUiTestInput& out) noexcept;
std::uint32_t test_copy_chars(std::uint32_t* out, std::uint32_t capacity) noexcept;
std::uint32_t test_copy_input_costs(std::uint32_t kind, std::uint32_t* out, std::uint32_t capacity) noexcept;
#endif

}  // namespace grace::host
