// The init thread (hooks.cpp), which grace-host's abi.cpp starts once (D71).
#pragma once

#include <windows.h>

#include <cstdint>

namespace grace::host::hooks {

// Waits for the game's window, reads the hook targets and installs the hooks, once, then returns (the thread exits).
DWORD WINAPI init_thread_main(LPVOID unused) noexcept;

#if defined(GRACEUI_TEST_EXPORTS)
// Installed hook `index`, in install order: its name in D102's published list, and the DLL it was found in where that
// is one of several (XInput's); false past the last.
bool installed_hook(std::uint32_t index, const char*& name, const char*& module) noexcept;
#endif

}  // namespace grace::host::hooks
