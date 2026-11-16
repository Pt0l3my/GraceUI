// The render path (draw.cpp), as hooks.cpp drives it from the detours. Each call runs on the thread of the detour that
// makes it (Present's: the game's present thread), never waits, and may throw (a failed Dear ImGui assertion, D121):
// hooks.cpp catches everything at the detour and turns the render path off for the session.
#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include "state.h"

namespace grace::host {

// What a failed Dear ImGui assertion throws (grace_imconfig.h, D121).
struct ImGuiAssertion {
  const char* expression;
  int line;
};

}  // namespace grace::host

namespace grace::host::draw {

// Binds the render path to a Direct3D 12 swap chain presenting to a window of this process. Takes over `device`'s
// reference; `swap_chain` is not owned: hooks.cpp unbinds before it ends (its tag, D118).
void bind(IDXGISwapChain3* swap_chain, ID3D12Device* device, const DXGI_SWAP_CHAIN_DESC1& desc, HWND window);

// The bound swap chain has ended: forget everything that pointed into it. The renderer itself stays, for the next.
void unbind() noexcept;

bool has_queue() noexcept;

// The bound swap chain's device, borrowed (no reference), or null.
ID3D12Device* device() noexcept;

// The bound swap chain's present queue, found as D116 says. Takes over `queue`'s reference.
void set_queue(ID3D12CommandQueue* queue, QueueMethod method) noexcept;

// ResizeBuffers or ResizeBuffers1 is about to replace the bound swap chain's buffers. Callable from any thread, before
// the original call: the render path then forgets every back buffer it knew (it holds no reference to any, D118) and
// reads the swap chain's new size, count and format before it draws again.
void mark_buffers_stale() noexcept;

// One frame, before the original Present: draws onto the current back buffer and executes it on the present queue if
// anything is to be drawn, and otherwise leaves the frame untouched.
void render();

// Turns the render path off for this session, with one log line; the game's frames pass through untouched from then.
void turn_off(const char* why) noexcept;

}  // namespace grace::host::draw
