// GraceUI's configuration of Dear ImGui 1.93.0 (D107), read by imgui.h through IMGUI_USER_CONFIG in every file that
// includes it: the library's own, its two backends' and grace-host's. One configuration for all of them, since some of
// these switches change the library's structures.
#pragma once

// Only what GraceUI uses. No obsolete API, demo window or debug tool; no file, shell, clipboard or IME call from the
// library: GraceUI reads its own files itself (D84), opens nothing in a shell, and the input half owns the window's
// text input and IME placement (D69).
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_DISABLE_DEMO_WINDOWS
#define IMGUI_DISABLE_DEBUG_TOOLS
#define IMGUI_DISABLE_FILE_FUNCTIONS
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
#define IMGUI_DISABLE_WIN32_FUNCTIONS

// The Win32 backend without its gamepad code: it loads no XInput DLL and reads no pad, so the host's XInput detour is
// the only pad reader (§5.4, D107).
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD

// A failed assertion in Dear ImGui throws, and the detour that ran it catches the exception: the render path turns
// itself off for the session with one log line (D100, D110, D121), rather than abort() in the game's process or carry
// on with broken state. No file name is passed: it would be a build machine's path.
namespace grace::host {
[[noreturn]] void imgui_assert_failed(const char* expression, int line);
}  // namespace grace::host
#define IM_ASSERT(_EXPR)                                         \
  do {                                                           \
    if (!(_EXPR)) ::grace::host::imgui_assert_failed(#_EXPR, __LINE__); \
  } while (0)
