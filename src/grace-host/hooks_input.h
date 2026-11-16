// hooks_input.h: the input half's hooks (hooks_input.cpp), which hooks.cpp's one table installs with the render half's
// (D102, D120): DirectInput's GetDeviceState, XInput's XInputGetState and the filter on the game's window procedure.
#pragma once

#include <windows.h>

namespace grace::host::hooks_input {

// XInput is hooked in every one of these DLLs that is already loaded at install, found by name, so GraceUI reads no
// import table of the game's (D69); GraceUI never loads one itself, and one loaded later is not hooked.
inline constexpr int kXInputDlls = 5;
inline constexpr const wchar_t* kXInputNames[kXInputDlls] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll",
                                                             L"xinput1_2.dll", L"xinput1_1.dll"};
inline constexpr const char* kXInputNamesNarrow[kXInputDlls] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll",
                                                                "xinput1_2.dll", "xinput1_1.dll"};

// GetDeviceState as DirectInput's keyboard and mouse have it: one function in Wine's dinput8, perhaps two elsewhere.
inline constexpr int kDirectInputTargets = 2;

struct Targets {
  void* get_device_state[kDirectInputTargets] = {};  // null where not found, or the same function as the first
  void* xinput_get_state[kXInputDlls] = {};          // null where the DLL is not loaded
};

// Finds the input half's targets in the DLLs already loaded, and pins each DLL it will hook, so that none is unloaded
// under a hook (GraceUI never unhooks, D107). Init thread only.
void find_targets(Targets& out) noexcept;

// The detours and the variables MinHook writes each original into, by slot (hooks.cpp's install loop).
void* get_device_state_detour(int slot) noexcept;
void** get_device_state_original(int slot) noexcept;
void* xinput_detour(int slot) noexcept;
void** xinput_original(int slot) noexcept;

// The filter on the game's window procedure (D125): installed once on each window it is given, never removed, and
// active on one at a time, the newest (input::window()); a filter on a window it has left passes every message on.
// GraceUI's own thread only. Answers whether `window` is now the one filtered.
bool filter_window(HWND window) noexcept;
// Windows filtered so far (at most kWindowFilters, EST): the first found, then any bound swap chain's that differed.
inline constexpr int kWindowFilters = 4;
int windows_filtered() noexcept;

// Pins the module that holds `address`, so that it is never unloaded under a hook (GraceUI never unhooks, D107).
void pin_module_of(void* address) noexcept;

}  // namespace grace::host::hooks_input
