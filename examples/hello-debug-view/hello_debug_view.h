/* SPDX-License-Identifier: MIT */
/*
 * hello_debug_view.h: what the third example mod shows, and where, so that tools/grace-harness can read it back from
 * the game's frame and GraceUI's notices. Everything about it is invented.
 */
#ifndef HELLO_DEBUG_VIEW_H
#define HELLO_DEBUG_VIEW_H

#define HELLO_DEBUG_ID "hello-debug-view"
#define HELLO_DEBUG_NAME "Hello debug view"

/* Its banner: one notice under its own key, shown until replaced (a duration of 0), its text replaced in place as the
 * view opens and closes. */
#define HELLO_DEBUG_BANNER_KEY 1
#define HELLO_DEBUG_BANNER_CLOSED "Hello debug view: press F11 to open it."
#define HELLO_DEBUG_BANNER_OPEN "Hello debug view: open. F11 closes it."
#define HELLO_DEBUG_BANNER_NO_HOTKEYS "Hello debug view: this GraceUI has no hotkeys, so the view stays closed."

/* Its hotkey: F11 (a Windows virtual key), with no modifier. */
#define HELLO_DEBUG_ACTION "debug-view"
#define HELLO_DEBUG_KEY 0x7A

/* Its panel: on the panel layer, hidden until F11, its bottom-left corner this far from the display's
 * (GRACEUI_ANCHOR_BOTTOM_LEFT). */
#define HELLO_DEBUG_X 40
#define HELLO_DEBUG_Y 40
#define HELLO_DEBUG_WIDTH 280
#define HELLO_DEBUG_HEIGHT 120

/* Its window's ground, drawn by its own Dear ImGui. */
#define HELLO_DEBUG_GROUND_R 120
#define HELLO_DEBUG_GROUND_G 70
#define HELLO_DEBUG_GROUND_B 160

#endif /* HELLO_DEBUG_VIEW_H */
