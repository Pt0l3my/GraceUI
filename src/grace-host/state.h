// The host's shared state: what its threads (a consumer's thread in GraceUI_RequestApi, the init thread, the game's
// present thread and any thread that executes command lists) tell each other, as atomics only. Nothing here waits.
#pragma once

#include <atomic>
#include <cstdint>

namespace grace::host {

enum class HostState : std::uint32_t {
  not_asked = 0,  // GraceUI_RequestApi never called in this copy
  started = 1,    // this copy holds the mutex; its init thread runs
  hooked = 2,     // the hooks are installed
  failed = 3,     // the init thread gave up; the host stays idle and the game runs untouched
  idle_copy = 4,  // another copy of GraceUI holds the mutex (D72, invariant 18)
};

// How the present queue was found (D116).
enum class QueueMethod : std::uint32_t {
  none = 0,
  swap_chain = 1,   // the swap chain answered GetDevice for ID3D12CommandQueue
  resize1 = 2,      // the game named it in ResizeBuffers1
  present = 3,      // a direct queue executed inside the original Present, on its thread
  only_direct = 4,  // the only direct queue of the swap chain's device seen executing
};

namespace state {

inline constinit std::atomic<HostState> host{HostState::not_asked};
inline constinit std::atomic<std::uint32_t> init_threads_started{0};
inline constinit std::atomic<std::uint32_t> hook_install_runs{0};
// The init thread's step (D220), set before each: 1 its fonts, 2 waiting for the game's window, 3 Direct3D bound, 4 the
// hook targets read from a swap chain of GraceUI's own, 5 the input targets, 6 the install, 7 hooked (a give-up stays at
// its step). Read from any thread, so that an init that stops says where; 0 before the thread starts.
inline constinit std::atomic<std::uint32_t> init_step{0};
inline constinit std::atomic<std::uint32_t> hooks_installed{0};  // every hook: the table's and the window procedure's
inline constinit std::atomic<std::uint32_t> render_hooks{0};
inline constinit std::atomic<std::uint32_t> dinput_hooks{0};
inline constinit std::atomic<std::uint32_t> xinput_hooks{0};

// Present and Present1 calls, outermost only; command lists executed between two presents are stamped with it.
inline constinit std::atomic<std::uint64_t> presents{0};
inline constinit std::atomic<std::uint32_t> passed_through{0};
inline constinit std::atomic<std::uint32_t> resizes{0};
inline constinit std::atomic<std::uint32_t> direct_queues_seen{0};

// Written by the render path (draw.cpp) for anyone to read.
inline constinit std::atomic<bool> bound{false};
inline constinit std::atomic<std::uint32_t> binds{0};
inline constinit std::atomic<QueueMethod> queue_method{QueueMethod::none};
inline constinit std::atomic<bool> renderer_ready{false};
inline constinit std::atomic<bool> atlas_ready{false};
inline constinit std::atomic<std::uint32_t> atlas_width{0};
inline constinit std::atomic<std::uint32_t> atlas_height{0};
inline constinit std::atomic<bool> render_off{false};
inline constinit std::atomic<std::uint64_t> frames_drawn{0};
inline constinit std::atomic<std::uint64_t> frames_skipped_busy{0};

// The test panel (test builds turn it on): GraceUI's own drawing, on the menu's layer (D76).
inline constinit std::atomic<bool> test_panel{false};

// Panels, draw lists and textures (S10.4.c), for the log and the test exports.
inline constinit std::atomic<std::uint64_t> lists_submitted{0};  // published
inline constinit std::atomic<std::uint64_t> lists_refused{0};
inline constinit std::atomic<std::uint64_t> panel_frames{0};     // frames that drew at least one panel's list
inline constinit std::atomic<std::uint32_t> texture_uploads{0};  // copies queued on the GPU (the upload counter)
inline constinit std::atomic<std::uint32_t> textures_uploaded{0};

}  // namespace state
}  // namespace grace::host
