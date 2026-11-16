// input.h: the input holder (D77), a stub until S10.4.a's input half. In the render half no holder ever exists, so the
// detours capture and mask nothing and the render path asks the window for nothing. The input half fills this in:
// request_input answered at once (invariant 17), the one close routine (invariant 7), the closing press kept from the
// game (invariant 8), and the window's input, which its window-procedure filter queues, handed to Dear ImGui on the
// present thread. Its four hooks join the one table in hooks.cpp.
#pragma once

#include <cstdint>

namespace grace::host::input {

enum class Holder : std::uint32_t { none = 0, menu = 1, panel = 2, modal = 3 };

// Who holds input now; any thread.
Holder holder() noexcept;

// Called by the render path on the present thread just before Dear ImGui's NewFrame. While a holder has input, the input
// half hands Dear ImGui the window's input here (ImGui_ImplWin32_NewFrame and the queued events); the render half has no
// holder, so this does nothing.
void before_imgui_frame() noexcept;

}  // namespace grace::host::input
