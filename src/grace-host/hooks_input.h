// hooks_input.h: the input half's hooks (hooks_input.cpp), which hooks.cpp's one table installs with the render half's
// (D102, D120): DirectInput's GetDeviceState, XInput's XInputGetState and the filter on the game's window procedure.
#pragma once

#include <windows.h>

#include <cstdint>

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

#if defined(GRACEUI_TEST_EXPORTS)
// A pad's state as an XInput read would give it, through the same filter as the detour's (a harness has no pad).
void test_pad(std::uint32_t user, std::uint16_t buttons) noexcept;
std::uint16_t test_pad_read(std::uint32_t user, std::uint16_t buttons, std::uint32_t& packet) noexcept;
// A whole pad, its triggers and sticks too (the menu's right stick, D242).
void test_pad_state(std::uint32_t user, std::uint16_t buttons, std::uint8_t left_trigger, std::uint8_t right_trigger,
                    std::int16_t left_x, std::int16_t left_y, std::int16_t right_x, std::int16_t right_y) noexcept;
// The active window's procedure as GetWindowLongPtr answers it now, and whether it is GraceUI's filter (D240); false
// with no window filtered.
bool window_lead(const void*& procedure, bool& ours) noexcept;
#endif

// Pins the module that holds `address`, so that it is never unloaded under a hook (GraceUI never unhooks, D107).
void pin_module_of(void* address) noexcept;

}  // namespace grace::host::hooks_input
