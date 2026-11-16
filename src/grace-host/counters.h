// What a test build of graceui.dll counts for the harness (GRACEUI_TEST_EXPORTS): allocations and frees on the present
// thread while the detours run GraceUI's own work (invariant 10's test), and each frame's detour cost. A release build
// counts nothing, and CountingScope is empty.
#pragma once

#include <atomic>
#include <cstdint>

namespace grace::host::counters {

#if defined(GRACEUI_TEST_EXPORTS)

inline constinit std::atomic<std::uint64_t> allocations{0};
inline constinit std::atomic<std::uint64_t> frees{0};

// True on a thread while a detour runs GraceUI's own work there: the allocation functions count only then
// (draw_alloc.cpp).
extern constinit thread_local bool t_counting;

class CountingScope {
 public:
  CountingScope() noexcept : previous_(t_counting) { t_counting = true; }
  ~CountingScope() { t_counting = previous_; }
  CountingScope(const CountingScope&) = delete;
  CountingScope& operator=(const CountingScope&) = delete;

 private:
  bool previous_;
};

// The newest per-frame detour costs, in nanoseconds: the detour's whole time less the original Present's, and the part
// of it spent inside the runtime's ExecuteCommandLists and Signal on GraceUI's own submission (under Wine on Mesa's
// software Vulkan a submission can run the "GPU" work on the calling thread).
inline constexpr std::uint32_t kCostSamples = 4096;
inline constinit std::atomic<std::uint32_t> costs_total_ns[kCostSamples]{};
inline constinit std::atomic<std::uint32_t> costs_submit_ns[kCostSamples]{};
inline constinit std::atomic<std::uint64_t> cost_count{0};

// Ticks spent in the current frame's submission (draw.cpp adds to it; the Present detour reads and clears it).
inline constinit std::atomic<std::int64_t> frame_submit_ticks{0};

inline std::uint32_t clamp_ns(std::uint64_t ns) noexcept {
  return ns > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<std::uint32_t>(ns);
}

inline void record_cost(std::uint64_t total_ns, std::uint64_t submit_ns) noexcept {
  const std::uint64_t n = cost_count.load(std::memory_order_relaxed);
  costs_total_ns[n % kCostSamples].store(clamp_ns(total_ns), std::memory_order_relaxed);
  costs_submit_ns[n % kCostSamples].store(clamp_ns(submit_ns), std::memory_order_relaxed);
  cost_count.store(n + 1, std::memory_order_release);
}

// The input half's own work per call, in nanoseconds, by kind (GRACEUI_TEST_COST_*): the window procedure's filter
// for one message, and the DirectInput and XInput detours for one read, each without the original's time.
inline constexpr std::uint32_t kInputCostKinds = 3;
inline constinit std::atomic<std::uint32_t> input_costs_ns[kInputCostKinds][kCostSamples]{};
inline constinit std::atomic<std::uint64_t> input_cost_count[kInputCostKinds]{};

inline void record_input_cost(std::uint32_t kind, std::uint64_t ns) noexcept {
  const std::uint64_t n = input_cost_count[kind].fetch_add(1, std::memory_order_relaxed);
  input_costs_ns[kind][n % kCostSamples].store(clamp_ns(ns), std::memory_order_relaxed);
}

// What Dear ImGui received from the holder: the code points after each NewFrame, oldest first, and its state after the
// last frame.
inline constexpr std::uint32_t kCharSamples = 1024;
inline constinit std::atomic<std::uint32_t> chars[kCharSamples]{};
inline constinit std::atomic<std::uint32_t> char_count{0};
inline constinit std::atomic<std::uint32_t> imgui_keys_down{0};
inline constinit std::atomic<std::int32_t> imgui_mouse_x{0};
inline constinit std::atomic<std::int32_t> imgui_mouse_y{0};
inline constinit std::atomic<std::uint32_t> imgui_draws_cursor{0};
inline constinit std::atomic<std::uint64_t> frames_fed{0};

inline void record_char(std::uint32_t code_point) noexcept {
  const std::uint32_t n = char_count.load(std::memory_order_relaxed);
  if (n < kCharSamples) {
    chars[n].store(code_point, std::memory_order_relaxed);
    char_count.store(n + 1, std::memory_order_release);
  }
}

#else

class CountingScope {
 public:
  CountingScope() noexcept {}  // user-provided, so that an unused scope is not a warning
};

#endif

}  // namespace grace::host::counters
