/* SPDX-License-Identifier: MIT */
/*
 * graceui.h: GraceUI's public C ABI (D73, D109), the one way any mod reaches GraceUI (invariant 15).
 *
 * Plain C, default x64 packing. A mod finds graceui.dll from a worker thread of its own, never from DllMain (D70), and
 * calls GraceUI_RequestApi(1) for ABI 1's table. One compatibility rule: the same major, and a minor at least the one
 * the mod was built against, or slots checked by the table's size (D74).
 *
 * GraceUI 0.1 exports the two functions below. ABI 1's table, GraceUiApi1, comes with roadmap item S10.4.b; until then
 * GraceUI_RequestApi answers null for every major, and the first call starts the host all the same.
 */
#ifndef GRACEUI_H
#define GRACEUI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(GRACEUI_BUILDING_DLL)
#define GRACEUI_API __declspec(dllexport)
#else
#define GRACEUI_API __declspec(dllimport)
#endif

/* GraceUI's version as three numbers packed in one: major (10 bits), minor (10 bits) and patch (12 bits). The macros
 * work in unsigned arithmetic without a cast, so C++ code built with -Wold-style-cast may use them too. */
#define GRACEUI_VERSION_PACK(major, minor, patch) \
  (((0u + (major)) << 22) | ((0u + (minor)) << 12) | (0u + (patch)))
#define GRACEUI_VERSION_MAJOR(v) ((0u + (v)) >> 22)
#define GRACEUI_VERSION_MINOR(v) (((0u + (v)) >> 12) & 0x3FFu)
#define GRACEUI_VERSION_PATCH(v) ((0u + (v)) & 0xFFFu)

/* The ABI majors this header describes. */
#define GRACEUI_ABI_MAJOR 1u

/*
 * GraceUI's own version (GRACEUI_VERSION_PACK). Needs no start: callable from any thread, at any time after the DLL is
 * loaded, and it starts nothing.
 */
GRACEUI_API uint32_t GraceUI_Version(void);

/*
 * The table of ABI major `major`, or null. The first call in the process takes GraceUI's single-instance mutex and,
 * in the copy that takes it, starts the host: a short-lived thread that waits for the game's window and installs the
 * overlay's hooks (D71, D72). A second copy of GraceUI loaded from another folder starts nothing and answers null
 * (invariant 18). Never call it from DllMain. In GraceUI 0.1 it answers null for every major (see the top of this file).
 */
GRACEUI_API const void* GraceUI_RequestApi(uint32_t major);

#ifdef __cplusplus
}
#endif

#endif /* GRACEUI_H */
