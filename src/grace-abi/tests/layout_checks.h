/* SPDX-License-Identifier: MIT */
/*
 * layout_checks.h: graceui.h's layout, field by field (D109): every struct's size and every field's offset, as ABI 1.0
 * fixes them for x64, and as ABI 1.1 appends to them (D176). Included by layout.c (compiled as C) and layout.cpp (as C++), on Linux and by both Windows
 * toolchains: a build that compiles them has the layout GraceUI promises. tools/check_safety.py keeps the header itself
 * append-only against the first one (D132); these keep the compilers honest about what it means.
 */
#ifndef GRACEUI_LAYOUT_CHECKS_H
#define GRACEUI_LAYOUT_CHECKS_H

#include <stddef.h>

#include "graceui.h"

#ifdef __cplusplus
#define GU_ASSERT(condition, message) static_assert(condition, message)
#else
#define GU_ASSERT(condition, message) _Static_assert(condition, message)
#endif

#define GU_SIZE(type, bytes) GU_ASSERT(sizeof(type) == (bytes), #type "'s size")
#define GU_AT(type, field, offset) GU_ASSERT(offsetof(type, field) == (offset), #type "." #field "'s offset")

GU_ASSERT(sizeof(void*) == 8, "x64 only");

GU_SIZE(GraceUiStatus, 4);
GU_SIZE(GraceUiConsumer, 8);

GU_SIZE(GraceUiConnectRequest, 64);
GU_AT(GraceUiConnectRequest, size, 0);
GU_AT(GraceUiConnectRequest, abi_minor, 4);
GU_AT(GraceUiConnectRequest, version, 8);
GU_AT(GraceUiConnectRequest, floor, 12);
GU_AT(GraceUiConnectRequest, id, 16);
GU_AT(GraceUiConnectRequest, id_length, 24);
GU_AT(GraceUiConnectRequest, name, 32);
GU_AT(GraceUiConnectRequest, name_length, 40);
GU_AT(GraceUiConnectRequest, caps, 48);
GU_AT(GraceUiConnectRequest, caps_length, 56);

GU_SIZE(GraceUiConnectAnswer, 48); /* 32 in ABI 1.0; the display appended in 1.2 (D180) */
GU_AT(GraceUiConnectAnswer, size, 0);
GU_AT(GraceUiConnectAnswer, status, 4);
GU_AT(GraceUiConnectAnswer, reason, 8);
GU_AT(GraceUiConnectAnswer, graceui_version, 12);
GU_AT(GraceUiConnectAnswer, floor, 16);
GU_AT(GraceUiConnectAnswer, abi_minor, 20);
GU_AT(GraceUiConnectAnswer, consumer, 24);
GU_AT(GraceUiConnectAnswer, ui_scale, 32);
GU_AT(GraceUiConnectAnswer, ui_scale_default, 36);
GU_AT(GraceUiConnectAnswer, display_width, 40);
GU_AT(GraceUiConnectAnswer, display_height, 44);

GU_SIZE(GraceUiNotice, 32);
GU_AT(GraceUiNotice, size, 0);
GU_AT(GraceUiNotice, tone, 4);
GU_AT(GraceUiNotice, key, 8);
GU_AT(GraceUiNotice, duration_ms, 12);
GU_AT(GraceUiNotice, text, 16);
GU_AT(GraceUiNotice, text_length, 24);

GU_SIZE(GraceUiTextureDesc, 32);
GU_AT(GraceUiTextureDesc, size, 0);
GU_AT(GraceUiTextureDesc, format, 4);
GU_AT(GraceUiTextureDesc, width, 8);
GU_AT(GraceUiTextureDesc, height, 12);
GU_AT(GraceUiTextureDesc, row_bytes, 16);
GU_AT(GraceUiTextureDesc, reserved, 20);
GU_AT(GraceUiTextureDesc, pixels, 24);

GU_SIZE(GraceUiFaceDesc, 48); /* 40 in ABI 1.0; flags appended in 1.2 (D180) */
GU_AT(GraceUiFaceDesc, size, 0);
GU_AT(GraceUiFaceDesc, pixel_height, 4);
GU_AT(GraceUiFaceDesc, data, 8);
GU_AT(GraceUiFaceDesc, data_length, 16);
GU_AT(GraceUiFaceDesc, ranges, 24);
GU_AT(GraceUiFaceDesc, range_count, 32);
GU_AT(GraceUiFaceDesc, flags, 40);
GU_AT(GraceUiFaceDesc, reserved, 44);

GU_SIZE(GraceUiGlyph, 40);
GU_AT(GraceUiGlyph, code_point, 0);
GU_AT(GraceUiGlyph, advance, 4);
GU_AT(GraceUiGlyph, x0, 8);
GU_AT(GraceUiGlyph, y0, 12);
GU_AT(GraceUiGlyph, x1, 16);
GU_AT(GraceUiGlyph, y1, 20);
GU_AT(GraceUiGlyph, u0, 24);
GU_AT(GraceUiGlyph, v0, 28);
GU_AT(GraceUiGlyph, u1, 32);
GU_AT(GraceUiGlyph, v1, 36);

GU_SIZE(GraceUiGlyphTable, 48);
GU_AT(GraceUiGlyphTable, size, 0);
GU_AT(GraceUiGlyphTable, generation, 4);
GU_AT(GraceUiGlyphTable, face_id, 8);
GU_AT(GraceUiGlyphTable, glyph_count, 12);
GU_AT(GraceUiGlyphTable, pixel_height, 16);
GU_AT(GraceUiGlyphTable, ascent, 20);
GU_AT(GraceUiGlyphTable, descent, 24);
GU_AT(GraceUiGlyphTable, line_gap, 28);
GU_AT(GraceUiGlyphTable, atlas, 32);
GU_AT(GraceUiGlyphTable, glyphs, 40);

GU_SIZE(GraceUiPanelDesc, 32);
GU_AT(GraceUiPanelDesc, size, 0);
GU_AT(GraceUiPanelDesc, layer, 4);
GU_AT(GraceUiPanelDesc, anchor, 8);
GU_AT(GraceUiPanelDesc, flags, 12);
GU_AT(GraceUiPanelDesc, x, 16);
GU_AT(GraceUiPanelDesc, y, 20);
GU_AT(GraceUiPanelDesc, width, 24);
GU_AT(GraceUiPanelDesc, height, 28);

/* Dear ImGui's default ImDrawVert (D76): 20 bytes, position, texture coordinate, colour. */
GU_SIZE(GraceUiDrawVert, 20);
GU_AT(GraceUiDrawVert, x, 0);
GU_AT(GraceUiDrawVert, y, 4);
GU_AT(GraceUiDrawVert, u, 8);
GU_AT(GraceUiDrawVert, v, 12);
GU_AT(GraceUiDrawVert, colour, 16);

GU_SIZE(GraceUiDrawCommand, 40);
GU_AT(GraceUiDrawCommand, clip_x0, 0);
GU_AT(GraceUiDrawCommand, clip_y0, 4);
GU_AT(GraceUiDrawCommand, clip_x1, 8);
GU_AT(GraceUiDrawCommand, clip_y1, 12);
GU_AT(GraceUiDrawCommand, texture, 16);
GU_AT(GraceUiDrawCommand, vertex_offset, 24);
GU_AT(GraceUiDrawCommand, vertex_count, 28);
GU_AT(GraceUiDrawCommand, index_offset, 32);
GU_AT(GraceUiDrawCommand, index_count, 36);

GU_SIZE(GraceUiDrawList, 48);
GU_AT(GraceUiDrawList, size, 0);
GU_AT(GraceUiDrawList, atlas_generation, 4);
GU_AT(GraceUiDrawList, vertex_count, 8);
GU_AT(GraceUiDrawList, index_count, 12);
GU_AT(GraceUiDrawList, command_count, 16);
GU_AT(GraceUiDrawList, command_size, 20);
GU_AT(GraceUiDrawList, vertices, 24);
GU_AT(GraceUiDrawList, indices, 32);
GU_AT(GraceUiDrawList, commands, 40);

GU_SIZE(GraceUiInputRequest, 32); /* 24 in ABI 1.0; flags and reserved2 appended in 1.1 (D176) */
GU_AT(GraceUiInputRequest, size, 0);
GU_AT(GraceUiInputRequest, kind, 4);
GU_AT(GraceUiInputRequest, wants, 8);
GU_AT(GraceUiInputRequest, reserved, 12);
GU_AT(GraceUiInputRequest, panel, 16);
GU_AT(GraceUiInputRequest, flags, 24);
GU_AT(GraceUiInputRequest, reserved2, 28);

GU_SIZE(GraceUiInputAnswer, 56);
GU_AT(GraceUiInputAnswer, size, 0);
GU_AT(GraceUiInputAnswer, answer, 4);
GU_AT(GraceUiInputAnswer, why, 8);
GU_AT(GraceUiInputAnswer, holder_kind, 12);
GU_AT(GraceUiInputAnswer, holder, 16);

GU_SIZE(GraceUiCaret, 32); /* ABI 1.1 (D176) */
GU_AT(GraceUiCaret, size, 0);
GU_AT(GraceUiCaret, reserved, 4);
GU_AT(GraceUiCaret, panel, 8);
GU_AT(GraceUiCaret, x, 16);
GU_AT(GraceUiCaret, y, 20);
GU_AT(GraceUiCaret, height, 24);
GU_AT(GraceUiCaret, reserved2, 28);

GU_SIZE(GraceUiHotkeyDesc, 48);
GU_AT(GraceUiHotkeyDesc, size, 0);
GU_AT(GraceUiHotkeyDesc, key, 4);
GU_AT(GraceUiHotkeyDesc, modifiers, 8);
GU_AT(GraceUiHotkeyDesc, pad_chord, 12);
GU_AT(GraceUiHotkeyDesc, action, 16);
GU_AT(GraceUiHotkeyDesc, action_length, 24);
GU_AT(GraceUiHotkeyDesc, name, 32);
GU_AT(GraceUiHotkeyDesc, name_length, 40);

GU_SIZE(GraceUiEvent, 136); /* 48 in GraceUI 0.2 (ABI 1.0); a holder's fields in 1.1 (D176); the display's in 1.2 */
GU_AT(GraceUiEvent, size, 0);
GU_AT(GraceUiEvent, kind, 4);
GU_AT(GraceUiEvent, hotkey_id, 8);
GU_AT(GraceUiEvent, count, 12);
GU_AT(GraceUiEvent, reply, 16);
GU_AT(GraceUiEvent, frame, 24);
GU_AT(GraceUiEvent, frame_capacity, 32);
GU_AT(GraceUiEvent, frame_length, 40);
GU_AT(GraceUiEvent, key, 48);
GU_AT(GraceUiEvent, modifiers, 52);
GU_AT(GraceUiEvent, pressed, 56);
GU_AT(GraceUiEvent, repeat, 60);
GU_AT(GraceUiEvent, code_point, 64);
GU_AT(GraceUiEvent, button, 68);
GU_AT(GraceUiEvent, button_event, 72);
GU_AT(GraceUiEvent, pad, 76);
GU_AT(GraceUiEvent, why, 80);
GU_AT(GraceUiEvent, dropped, 84);
GU_AT(GraceUiEvent, x, 88);
GU_AT(GraceUiEvent, y, 92);
GU_AT(GraceUiEvent, wheel_x, 96);
GU_AT(GraceUiEvent, wheel_y, 100);
GU_AT(GraceUiEvent, display_width, 104);
GU_AT(GraceUiEvent, display_height, 108);
GU_AT(GraceUiEvent, ui_scale, 112);
GU_AT(GraceUiEvent, ui_scale_default, 116);
GU_AT(GraceUiEvent, scale, 120);
GU_AT(GraceUiEvent, content_scale, 124);
GU_AT(GraceUiEvent, faces_scale, 128);
GU_AT(GraceUiEvent, reserved3, 132);

GU_SIZE(GraceUiSpaceDesc, 64);
GU_AT(GraceUiSpaceDesc, size, 0);
GU_AT(GraceUiSpaceDesc, views, 4);
GU_AT(GraceUiSpaceDesc, version, 8);
GU_AT(GraceUiSpaceDesc, reserved, 12);
GU_AT(GraceUiSpaceDesc, pages, 16);
GU_AT(GraceUiSpaceDesc, pages_length, 24);
GU_AT(GraceUiSpaceDesc, settings, 32);
GU_AT(GraceUiSpaceDesc, settings_length, 40);
GU_AT(GraceUiSpaceDesc, increment, 48);
GU_AT(GraceUiSpaceDesc, increment_length, 56);

GU_SIZE(GraceUiWorldState, 8);
GU_AT(GraceUiWorldState, size, 0);
GU_AT(GraceUiWorldState, flags, 4);

/* The table: its four numbers, then D73's twenty slots in D73's order, 8 bytes each, then ABI 1.1's one (D176). */
GU_SIZE(GraceUiApi1, 192); /* 184 in ABI 1.1 to 1.3; describe_hotkey appended in 1.4 (D210) */
GU_AT(GraceUiApi1, size, 0);
GU_AT(GraceUiApi1, abi_minor, 4);
GU_AT(GraceUiApi1, graceui_version, 8);
GU_AT(GraceUiApi1, reserved, 12);
GU_AT(GraceUiApi1, has_cap, 16);
GU_AT(GraceUiApi1, connect, 24);
GU_AT(GraceUiApi1, disconnect, 32);
GU_AT(GraceUiApi1, register_pages, 40);
GU_AT(GraceUiApi1, publish, 48);
GU_AT(GraceUiApi1, poll, 56);
GU_AT(GraceUiApi1, answer, 64);
GU_AT(GraceUiApi1, register_texture, 72);
GU_AT(GraceUiApi1, register_face, 80);
GU_AT(GraceUiApi1, glyph_table, 88);
GU_AT(GraceUiApi1, create_panel, 96);
GU_AT(GraceUiApi1, submit, 104);
GU_AT(GraceUiApi1, show_panel, 112);
GU_AT(GraceUiApi1, hide_panel, 120);
GU_AT(GraceUiApi1, request_input, 128);
GU_AT(GraceUiApi1, release_input, 136);
GU_AT(GraceUiApi1, register_hotkey, 144);
GU_AT(GraceUiApi1, notice, 152);
GU_AT(GraceUiApi1, set_world_state, 160);
GU_AT(GraceUiApi1, log, 168);
GU_AT(GraceUiApi1, set_caret, 176);
GU_AT(GraceUiApi1, describe_hotkey, 184);

/* The numbers a mod compiles in. */
GU_ASSERT(GRACEUI_ABI_MAJOR == 1u && GRACEUI_ABI1_MINOR == 4u, "ABI 1.4");
GU_SIZE(GraceUiHotkeyBinding, 104); /* ABI 1.4 (D210) */
GU_AT(GraceUiHotkeyBinding, key_text, 24);
GU_AT(GraceUiHotkeyBinding, chord_text, 56);
GU_ASSERT(GRACEUI_PAD_CHORD(0x0001u, 0x0020u) == 0x00010020u, "a chord: the anchor high, the key low (D204)");
GU_ASSERT(GRACEUI_VERSION_PACK(1, 2, 3) == ((1u << 22) | (2u << 12) | 3u), "the version's packing");
GU_ASSERT(GRACEUI_VERSION_MAJOR(GRACEUI_VERSION_PACK(1023, 1023, 4095)) == 1023u, "a major's ten bits");
GU_ASSERT(GRACEUI_VERSION_MINOR(GRACEUI_VERSION_PACK(1023, 1023, 4095)) == 1023u, "a minor's ten bits");
GU_ASSERT(GRACEUI_VERSION_PATCH(GRACEUI_VERSION_PACK(1023, 1023, 4095)) == 4095u, "a patch's twelve bits");

#endif /* GRACEUI_LAYOUT_CHECKS_H */
