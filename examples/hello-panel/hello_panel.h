/* SPDX-License-Identifier: MIT */
/*
 * hello_panel.h: what the second example mod draws, and where, so that tools/grace-harness can read it back from the
 * game's frame. Everything about it is invented.
 */
#ifndef HELLO_PANEL_H
#define HELLO_PANEL_H

#define HELLO_PANEL_ID "hello-panel"
#define HELLO_PANEL_NAME "Hello panel"

/* Its panel: on the panel layer, its top-left corner this far from the display's (GRACEUI_ANCHOR_TOP_LEFT). */
#define HELLO_PANEL_X 40
#define HELLO_PANEL_Y 120
#define HELLO_PANEL_WIDTH 240
#define HELLO_PANEL_HEIGHT 120

/* Its window's ground, drawn by its own Dear ImGui. */
#define HELLO_PANEL_GROUND_R 40
#define HELLO_PANEL_GROUND_G 140
#define HELLO_PANEL_GROUND_B 200

/* Its checker texture, 32 x 32 in squares of 8, drawn one to one at this place in the panel. */
#define HELLO_CHECKER_X 8
#define HELLO_CHECKER_Y 40
#define HELLO_CHECKER_SIZE 32
#define HELLO_CHECKER_SQUARE 8
#define HELLO_CHECKER_A_R 230 /* the square at its top-left corner */
#define HELLO_CHECKER_A_G 60
#define HELLO_CHECKER_A_B 40
#define HELLO_CHECKER_B_R 250
#define HELLO_CHECKER_B_G 220
#define HELLO_CHECKER_B_B 60

/* A line in its own Dear ImGui's font (its own atlas, its second texture), white, at its window's padding. */
#define HELLO_TEXT "Hello panel"
#define HELLO_TEXT_X 8
#define HELLO_TEXT_Y 8

/* A line laid out from GraceUI's shared atlas, with the glyph table of the face it registered (Noto Sans Mono, 16 px),
 * pen at the top of the line here, in gold. */
#define HELLO_GLYPHS "Shared atlas"
#define HELLO_GLYPHS_X 60
#define HELLO_GLYPHS_Y 46
#define HELLO_GLYPHS_PIXELS 16
#define HELLO_GLYPHS_R 250
#define HELLO_GLYPHS_G 200
#define HELLO_GLYPHS_B 80

#endif /* HELLO_PANEL_H */
