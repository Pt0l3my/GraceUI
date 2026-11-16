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
inline constinit std::atomic<std::uint32_t> hooks_installed{0};

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

// The test panel (test builds turn it on; nothing else draws in GraceUI 0.1).
inline constinit std::atomic<bool> test_panel{false};

}  // namespace state
}  // namespace grace::host
