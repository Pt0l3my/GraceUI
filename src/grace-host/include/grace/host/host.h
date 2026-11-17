// grace-host's entry points for graceui.dll's exports and ABI 1's table (src/graceui/abi*.cpp). Everything here is
// noexcept: no exception leaves grace-host through them (D110). The DLL's abi files copy a mod's structs and strings
// out of its memory before they call in (D73, D110): nothing here takes a pointer from a mod.
#pragma once

#include <cstdint>
#include <string_view>

#include "grace/abi/consumers.h"
#include "grace/abi/drawlist.h"
#include "grace/abi/panels.h"
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

// ---- S10.4.c's slots (surfaces.cpp). Each answers not_connected for a handle that names no mod, then inactive while
// the session is (the notice-only mode included), before anything else. The DLL's abi files copy what a mod hands in;
// where the copy must go straight into GraceUI's store (pixels, a font, a draw list), they pass a function of theirs
// that makes it, called once GraceUI has made room, with the context they give.

// A texture of `width` x `height` RGBA8 pixels; `copy` writes its rows, packed, into the `width * height * 4` bytes it
// is given.
using CopyPixels = void (*)(const void* context, unsigned char* out);
GraceUiStatus register_texture(GraceUiConsumer consumer, std::uint32_t width, std::uint32_t height, CopyPixels copy,
                               const void* context, std::uint64_t& id) noexcept;

// A face of `pixel_height`: `copy` writes its font file (`data_length` bytes) and its ranges (`range_count` pairs, none
// for GraceUI's default) into what it is given.
using CopyFace = void (*)(const void* context, unsigned char* data, std::uint32_t* ranges);
GraceUiStatus register_face(GraceUiConsumer consumer, float pixel_height, std::uint64_t data_length,
                            std::uint64_t range_count, CopyFace copy, const void* context, std::uint32_t& id) noexcept;

GraceUiStatus glyph_table(GraceUiConsumer consumer, std::uint32_t face_id, GraceUiGlyphTable& out) noexcept;

GraceUiStatus create_panel(GraceUiConsumer consumer, const grace::abi::PanelDesc& desc, std::uint64_t& id) noexcept;
GraceUiStatus show_panel(GraceUiConsumer consumer, std::uint64_t panel, bool visible) noexcept;

// A draw list's counts as the mod gives them, checked against the bounds before anything is copied.
struct ListShape {
  std::uint32_t vertex_count = 0;
  std::uint32_t index_count = 0;
  std::uint32_t command_count = 0;
  std::uint32_t command_size = 0;  // the mod's stride
  std::uint32_t atlas_generation = 0;
  bool arrays = false;             // every array a count names is there (not null)
};
// `copy` fills the vectors, already sized to the shape's counts, with the mod's vertices, indices and commands.
using CopyList = void (*)(const void* context, grace::abi::DrawListData& out);
GraceUiStatus submit(GraceUiConsumer consumer, std::uint64_t panel, const ListShape& shape, CopyList copy,
                     const void* context) noexcept;

GraceUiStatus request_input(GraceUiConsumer consumer, std::uint32_t kind, std::uint32_t wants, std::uint64_t panel,
                            GraceUiInputAnswer& out) noexcept;
GraceUiStatus release_input(GraceUiConsumer consumer) noexcept;
GraceUiStatus set_world_state(GraceUiConsumer consumer, std::uint32_t flags) noexcept;
// A hotkey (D155): `key` a virtual key, the strings already copied; `id` set with GRACEUI_OK.
GraceUiStatus register_hotkey(GraceUiConsumer consumer, std::uint32_t key, std::uint32_t modifiers,
                              std::uint32_t pad_chord, std::string_view action, std::string_view name,
                              std::uint32_t& id) noexcept;
// The mod's next event (D155): hotkey presses in GraceUI 0.1; `out` is GraceUI's own, zeroed by the caller.
GraceUiStatus poll(GraceUiConsumer consumer, GraceUiEvent& out) noexcept;
// A mod disconnected: its panels, textures, claims and input go with it.
void forget(GraceUiConsumer consumer, std::uint32_t owner) noexcept;
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
void test_set_first_request_delay(std::uint32_t ms) noexcept;
void test_turn_off_render() noexcept;
void test_set_atlas_limit(std::uint32_t side) noexcept;
std::uint64_t test_panel_published(std::uint64_t panel) noexcept;
#endif

}  // namespace grace::host
