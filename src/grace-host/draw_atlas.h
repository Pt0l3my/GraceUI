// The shared atlas's faces (draw_atlas.cpp; §3.16, D80, D107, D117): GraceUI's own, read from its fonts folder by the
// init thread, and every mod's registered before warm-up, baked once into Dear ImGui's static atlas at warm-up; then
// each face's glyph table, which mods read through glyph_table to lay out their own text.
#pragma once

#include <cstdint>
#include <string>

#include "graceui.h"

struct ImFont;
struct ImFontAtlas;

namespace grace::view {
struct Fonts;
}

namespace grace::host::atlas {

// GraceUI's notices' face (§3.8): Noto Sans Regular at 15 px, line height 22 (`ui`), from GraceUI's own fonts folder.
inline constexpr float kNoticeFacePixels = 15.0f;
inline constexpr float kNoticeLineHeight = 22.0f;
inline constexpr std::uint32_t kNoticeFace = 1;  // its face id

// The init thread, before the hooks: GraceUI's own faces' files, read whole from `fonts\` beside graceui.dll (D51): the
// notices' face and the menu's (§3.4, D167). A file that is absent or not a font leaves Dear ImGui's own font for the
// notices, or Noto Sans Regular for the menu's roles it served, with one log line naming it by its relative name.
void load_own_fonts() noexcept;

// Warm-up, on GraceUI's own thread once the present thread has sealed the mods' faces (D139 as amended): adds GraceUI's
// own face and the first `mod_faces` of the mods' committed faces, in registration order, to `fonts`, bakes the atlas
// and builds each face's glyph table, then raises the atlas's generation to 1. A mod's face that Dear ImGui cannot load
// is left out, with one log line, and so is every face past `mod_faces`. May throw (D121): the caller then tries again
// with fewer faces on a fresh atlas, and this starts its tables afresh.
// `menu_scale` above 0 bakes the menu's faces too, one for each type role at its size times that scale (the effective
// scale, §3.13); 0 leaves them out, and the menu cannot open this session (the caller's last try, D167).
void bake(ImFontAtlas* fonts, std::uint32_t mod_faces, float menu_scale);

// The atlas's generation: 0 before the bake, then 1 for the session (GraceUI 0.1 never rebuilds it).
std::uint32_t generation() noexcept;

// Any thread, once the generation is not 0: face `face_id`'s glyph table (its glyphs GraceUI's, read-only, kept for the
// session); false for a face the atlas does not hold.
bool glyph_table(std::uint32_t face_id, GraceUiGlyphTable& out) noexcept;

// The present thread, after the bake: the notices' font, and whether it is GraceUI's own face (else Dear ImGui's).
ImFont* notice_font() noexcept;
bool notice_face_is_grace() noexcept;
std::uint32_t faces_baked() noexcept;  // GraceUI's and the mods' faces in the atlas

// GraceUI's own folder (graceui.dll's), as a path with its last separator: false if it cannot be named.
bool own_folder(std::wstring& out) noexcept;

// Any thread, once the generation is not 0: the menu's faces by type role, or null if they were left out (D167); the
// atlas's white pixel, which solid shapes sample; and the scale they were baked at.
const grace::view::Fonts* menu_fonts() noexcept;
void white_pixel(float& u, float& v) noexcept;
float menu_scale() noexcept;

#if defined(GRACEUI_TEST_EXPORTS)
// A test build's limit on the atlas's texture, a power of two (0: Dear ImGui's own, 8192), so that a bake can be made to
// run out of room and the faces' fallback be seen.
void test_set_limit(std::uint32_t side) noexcept;
#endif

}  // namespace grace::host::atlas
