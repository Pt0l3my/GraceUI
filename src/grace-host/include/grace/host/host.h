// grace-host's entry points for graceui.dll's exports and ABI 1's table (src/graceui/abi*.cpp). Everything here is
// noexcept: no exception leaves grace-host through them (D110). The DLL's abi files copy a mod's structs and strings
// out of its memory before they call in (D73, D110): nothing here takes a pointer from a mod.
#pragma once

#include <cstdint>
#include <string_view>

#include "grace/abi/consumers.h"
#include "graceui.h"

#if defined(GRACEUI_TEST_EXPORTS)
#include "graceui_test.h"
#endif

namespace grace::host {

// What this copy of graceui.dll is in the process (D72, D135): the one that holds the per-process mutex, whose table is
// served whatever its host's state; another copy, idle; or none, if the mutex could not be made.
enum class Copy : std::uint32_t { none = 0, active = 1, idle = 2 };

// The first call in this copy takes the per-process mutex and, if this copy is the first to take it, runs the anti-cheat
// check, pins the DLL and starts the init thread (D71, D72, D133); a call that comes while the first is still running
// waits for it, so every caller gets the same answer. The active copy names every other copy loaded in the process in
// one notice each (invariant 18), at each request; an idle copy tells the active one of itself at its first.
Copy request_start() noexcept;

namespace abi1 {

bool has_cap(std::string_view name) noexcept;
grace::abi::ConnectResult connect(const grace::abi::ConnectRequest& request) noexcept;
GraceUiStatus disconnect(GraceUiConsumer consumer) noexcept;
GraceUiStatus notice(GraceUiConsumer consumer, std::uint32_t tone, std::uint32_t key, std::uint32_t duration_ms,
                     std::string_view text) noexcept;
// What a slot answers that this GraceUI does not provide yet: not connected, inactive, or not yet.
GraceUiStatus not_yet(GraceUiConsumer consumer) noexcept;
// A connect refused before it reached the rules (a string longer than its bound, a struct below its first size): its log
// line, as the rules' own refusals have theirs, up to a bound in a session.
void connect_refused(std::uint32_t reason) noexcept;
// A refused call or a caught exception: one log line, the first time for each slot.
void refused(const char* slot, const char* why) noexcept;
void fault(const char* slot) noexcept;

}  // namespace abi1

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
void test_set_anti_cheat_names(const wchar_t* module_prefix, const wchar_t* parent) noexcept;
bool test_copy_notice(std::uint32_t index, GraceUiTestNotice& out) noexcept;
void test_clear_notices() noexcept;
bool test_copy_consumer(std::uint32_t index, GraceUiTestConsumer& out) noexcept;
#endif

}  // namespace grace::host
