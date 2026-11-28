/* SPDX-License-Identifier: MIT */
/*
 * graceui_mock.h: a mock of GraceUI's ABI 1 in C, for a mod's own unit tests (D85, D285). It is a GraceUiApi1 table
 * whose slots answer as GraceUI's do for what a test needs, record every call, and give poll the events a test queues,
 * so that a mod's logic (its connect, its page frames, its commands' answers, its hotkeys and notices) is tested on
 * any system, without the game, a window or graceui.dll. Pure C99, the standard library alone; build graceui_mock.c
 * into the test, never into the mod.
 *
 * A mod that keeps graceui_sys_find in the thread it starts, and hands the table to the rest of its code, tests that
 * code by handing it graceui_mock_api() instead:
 *
 *     graceui_mock_reset();
 *     my_mod_start(graceui_mock_api());                     the mod connects and registers its space
 *     reply = graceui_mock_push_frame(1, GRACEUI_EVENT_COMMAND, "{\"v\":1,\"t\":\"cmd\",...}");
 *     my_mod_poll_once();                                   the mod polls and answers
 *     assert(strstr(graceui_mock_answer_to(reply), "\"ok\":true") != NULL);
 *
 * What it does not do, which GraceUI and its harness test: it draws nothing and runs no present thread (set_on_present
 * keeps the callback for graceui_mock_present); it parses no JSON, so page data and frames are recorded as given, and
 * the page tool's lint checks a space's page data (tools/pages/build.py); it checks a draw list's counts against
 * GraceUI's limits, not its contents; it has no wake event. It is one mock in the process, for one test at a time:
 * call it from one thread.
 */
#ifndef GRACEUI_MOCK_H
#define GRACEUI_MOCK_H

#include <stddef.h>

#include "graceui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Each slot of ABI 1's table, in its order (GraceUiMockCall.slot). */
#define GRACEUI_MOCK_HAS_CAP 1u
#define GRACEUI_MOCK_CONNECT 2u
#define GRACEUI_MOCK_DISCONNECT 3u
#define GRACEUI_MOCK_REGISTER_PAGES 4u
#define GRACEUI_MOCK_PUBLISH 5u
#define GRACEUI_MOCK_POLL 6u
#define GRACEUI_MOCK_ANSWER 7u
#define GRACEUI_MOCK_REGISTER_TEXTURE 8u
#define GRACEUI_MOCK_REGISTER_FACE 9u
#define GRACEUI_MOCK_GLYPH_TABLE 10u
#define GRACEUI_MOCK_CREATE_PANEL 11u
#define GRACEUI_MOCK_SUBMIT 12u
#define GRACEUI_MOCK_SHOW_PANEL 13u
#define GRACEUI_MOCK_HIDE_PANEL 14u
#define GRACEUI_MOCK_REQUEST_INPUT 15u
#define GRACEUI_MOCK_RELEASE_INPUT 16u
#define GRACEUI_MOCK_REGISTER_HOTKEY 17u
#define GRACEUI_MOCK_NOTICE 18u
#define GRACEUI_MOCK_SET_WORLD_STATE 19u
#define GRACEUI_MOCK_LOG 20u
#define GRACEUI_MOCK_SET_CARET 21u          /* ABI 1.1 */
#define GRACEUI_MOCK_DESCRIBE_HOTKEY 22u    /* ABI 1.4 */
#define GRACEUI_MOCK_WAKE_EVENT 23u         /* ABI 1.5 */
#define GRACEUI_MOCK_SET_ON_PRESENT 24u     /* ABI 1.8 */
#define GRACEUI_MOCK_PAD_KEYBOARD 25u       /* ABI 1.10 */
#define GRACEUI_MOCK_SET_HOTKEY_ENABLED 26u /* ABI 1.11 */
#define GRACEUI_MOCK_SLOTS 27u

/* The GraceUI version it answers until graceui_mock_set_version: the GraceUI it was built with, if its build says so. */
#ifndef GRACEUI_MOCK_VERSION
#define GRACEUI_MOCK_VERSION GRACEUI_VERSION_PACK(0, 2, 0)
#endif

/* One call to the table, as the mock recorded it. */
typedef struct GraceUiMockCall {
  uint32_t slot;            /* GRACEUI_MOCK_* */
  GraceUiStatus status;     /* what the slot answered */
  GraceUiConsumer consumer; /* the handle the call named; connect's: the one it gave (0 if none) */
  uint64_t id;              /* the panel, texture, face or hotkey the call named or was given; answer's reply; notice's
                               key; log's level; set_world_state's flags; poll's event kind (0 for none) */
  uint64_t value;           /* submit: the list's vertex count; notice: its tone; register_hotkey: its key;
                               set_hotkey_enabled: enabled; request_input: its flags; connect: the mod's ABI minor */
  const char* text;         /* a copy of what the call carried, NUL-terminated ("" for nothing): connect's id, a
                               frame (publish, answer, register_pages' page data), a notice's or a log line's text, a
                               hotkey's action, a face's name */
  uint64_t text_length;
} GraceUiMockCall;

/* ---- The mock. */

/* A fresh mock: every call, connection, event and setting forgotten, its settings as each says below. */
void graceui_mock_reset(void);

/* Its table, as graceui_sys_find gives GraceUI's: valid until the process ends. */
const GraceUiApi1* graceui_mock_api(void);

/* GraceUI's version, as the table and connect answer it (GRACEUI_MOCK_VERSION by default); connect answers
 * GRACEUI_TOO_OLD to a mod whose floor is above it. */
void graceui_mock_set_version(uint32_t version);

/* The ABI minor it serves (GRACEUI_ABI1_MINOR by default, at most that): the table's size and abi_minor are that
 * minor's, so GRACEUI_API1_COVERS is false for every later slot, as an older GraceUI's table is. */
void graceui_mock_set_minor(uint32_t minor);

/* The caps has_cap names, separated by spaces (every GRACEUI_CAP_* of this header by default; NULL for that). */
void graceui_mock_set_caps(const char* caps);

/* connect answers GRACEUI_INACTIVE with this GRACEUI_INACTIVE_* reason and a handle, after which every slot answers
 * GRACEUI_INACTIVE but notice, log, poll and disconnect, as in GraceUI's notice-only mode (D88); 0, the default, for an
 * active GraceUI. */
void graceui_mock_set_inactive(uint32_t reason);

/* The slot's next call answers `status` and does nothing else (a GRACEUI_FAULT, a full store, a GRACEUI_NOT_READY),
 * then the slot works again. */
void graceui_mock_fail_next(uint32_t slot, GraceUiStatus status);

/* What request_input answers while no mod holds input (GRACEUI_INPUT_GRANTED by default): GRACEUI_INPUT_REFUSED with
 * `holder` named (the menu is "graceui"), or GRACEUI_INPUT_NOT_NOW with `why` (a GRACEUI_NOT_NOW_*). A mod holding input
 * refuses every other mod's request, naming the holder, whatever this says. */
void graceui_mock_set_input_answer(uint32_t answer, uint32_t why, const char* holder);

/* glyph_table's answer for every face (GRACEUI_NOT_READY by default, as before GraceUI's warm-up); with `table` given,
 * GRACEUI_OK and that table, its glyphs and pixels the test's own, which must outlive the calls. */
void graceui_mock_set_glyph_table(const GraceUiGlyphTable* table);

/* ---- Events for a mod's poll, in the order queued, at most 256 waiting for each mod. Each answers 0 if not queued (the
 * handle names no connected mod, or its queue is full). An event is given to a poll whose GraceUiEvent can hold it, as
 * GraceUI gives it (a protocol frame to an event of GraceUI 0.2's size or more, a holder's or the display's to one of
 * ABI 1.1's or 1.2's); a smaller event's poll passes it by and it waits. */

/* A hotkey's presses: `count` of them, 1 or more, by its key. Answers 1 if queued. */
int graceui_mock_push_hotkey(GraceUiConsumer consumer, uint32_t hotkey_id, uint32_t count);

/* A view's frame for the mod's space: GRACEUI_EVENT_WATCHED, _UNWATCHED or _COMMAND, `frame` its JSON, copied. Answers
 * a command's reply handle, which answer takes once; 1 for a watch or an unwatch; 0 if not queued. */
uint64_t graceui_mock_push_frame(GraceUiConsumer consumer, uint32_t kind, const char* frame);

/* Any other event (a holder's key or character, the display, a hotkey's change), its fields copied up to `event->size`
 * bytes; its frame and file are not read. Answers 1 if queued. */
int graceui_mock_push_event(GraceUiConsumer consumer, const GraceUiEvent* event);

/* ---- What the mod did. */

size_t graceui_mock_call_count(void);                     /* calls recorded, in order */
const GraceUiMockCall* graceui_mock_call(size_t index);   /* the index-th, NULL past the last */
size_t graceui_mock_count(uint32_t slot);                 /* calls to one slot */
const GraceUiMockCall* graceui_mock_last(uint32_t slot);  /* the last call to one slot, NULL if none */
const char* graceui_mock_answer_to(uint64_t reply);       /* the result frame the mod answered a command with, NULL if
                                                             it has not answered */
GraceUiConsumer graceui_mock_input_holder(void);          /* the handle holding input, 0 if none */
uint32_t graceui_mock_panel_shown(uint64_t panel_id);     /* 1 if the panel is shown, 0 if hidden or unknown */
uint32_t graceui_mock_hotkey_enabled(uint32_t hotkey_id); /* 1 if enabled (as registered), 0 if paused or unknown */

/* One frame of a panel's on_present callback, as GraceUI's present thread would call it: `list` zeroed but for its size
 * and command_size, then handed to the callback with `frame` (its panel set to `panel_id`; NULL for a frame of 1/60 s at
 * 1920 by 1080). Answers the callback's answer (GRACEUI_PRESENT_KEEP or _DRAW), or GRACEUI_PRESENT_KEEP if the panel
 * has none. The list's arrays are the callback's own, valid until it is called again. */
uint32_t graceui_mock_present(uint64_t panel_id, const GraceUiPresentFrame* frame, GraceUiDrawList* list);

#ifdef __cplusplus
}
#endif

#endif /* GRACEUI_MOCK_H */
