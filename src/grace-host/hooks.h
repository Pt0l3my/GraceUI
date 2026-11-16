// The init thread (hooks.cpp), which grace-host's abi.cpp starts once (D71).
#pragma once

#include <windows.h>

namespace grace::host::hooks {

// Waits for the game's window, reads the hook targets and installs the hooks, once, then returns (the thread exits).
DWORD WINAPI init_thread_main(LPVOID unused) noexcept;

}  // namespace grace::host::hooks
