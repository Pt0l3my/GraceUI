// The init thread (hooks.cpp), which grace-host's abi.cpp starts once (D71).
#pragma once

#include <windows.h>

#include <cstdint>

namespace grace::host::hooks {

// Waits for the game's window, reads the hook targets and installs the hooks, once; then stays for the session as
// GraceUI's own thread (D120 as amended), asleep until woken for what no game thread should wait on: baking the shared
// atlas (draw.h), moving the window filter to the bound swap chain's window, and passing the log to the debugger.
DWORD WINAPI init_thread_main(LPVOID unused) noexcept;

// GraceUI's own thread's wake-up: made by the first request before the thread starts (answers whether it was made;
// without it the thread looks every 50 ms), and set by any thread, which never waits for it.
bool prepare_wake() noexcept;
void wake() noexcept;

#if defined(GRACEUI_TEST_EXPORTS)
// Installed hook `index`, in install order: its name in D102's published list, and the DLL it was found in where that
// is one of several (XInput's); false past the last.
bool installed_hook(std::uint32_t index, const char*& name, const char*& module) noexcept;
// Present's first bytes as GraceUI found them at install (GRACEUI_TEST_PROLOGUE_*; D86, D175).
std::uint32_t present_prologue() noexcept;
#endif

}  // namespace grace::host::hooks
