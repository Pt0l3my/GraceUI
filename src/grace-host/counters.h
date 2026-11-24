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

// D239: each kind's longest call since load (a maximum, which a ring of samples and its p99 would lose), and its calls
// over 2 ms and over 8 ms.
inline constinit std::atomic<std::uint64_t> input_cost_max_ns[kInputCostKinds]{};
inline constinit std::atomic<std::uint32_t> input_over_2ms[kInputCostKinds]{};
inline constinit std::atomic<std::uint32_t> input_over_8ms[kInputCostKinds]{};

inline void raise_max(std::atomic<std::uint64_t>& max, std::uint64_t value) noexcept {
  std::uint64_t seen = max.load(std::memory_order_relaxed);
  while (value > seen && !max.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
  }
}

inline void record_input_cost(std::uint32_t kind, std::uint64_t ns) noexcept {
  const std::uint64_t n = input_cost_count[kind].fetch_add(1, std::memory_order_relaxed);
  input_costs_ns[kind][n % kCostSamples].store(clamp_ns(ns), std::memory_order_relaxed);
  raise_max(input_cost_max_ns[kind], ns);
  if (ns > 2'000'000) {
    input_over_2ms[kind].fetch_add(1, std::memory_order_relaxed);
  }
  if (ns > 8'000'000) {
    input_over_8ms[kind].fetch_add(1, std::memory_order_relaxed);
  }
}

// D239: the longest gap between one thread's DirectInput reads of the keyboard, and of the mouse (the game's polls: a
// press shorter than a late poll's gap is lost to a game that reads state), and the gaps over 100 ms; and the heap
// guard's longest time inside a detour (its lock is the one thing of GraceUI's a detour could wait on).
enum Poll : std::uint32_t { poll_keyboard, poll_mouse, kPollKinds };
inline constinit std::atomic<std::uint64_t> poll_gap_max_us[kPollKinds]{};
inline constinit std::atomic<std::uint32_t> poll_gaps_over_100ms[kPollKinds]{};
inline constinit std::atomic<std::uint64_t> guard_in_detour_max_ns{0};

inline void record_poll_gap(Poll kind, std::uint64_t us) noexcept {
  raise_max(poll_gap_max_us[kind], us);
  if (us > 100'000) {
    poll_gaps_over_100ms[kind].fetch_add(1, std::memory_order_relaxed);
  }
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

// The input half as the in-game spikes read it (S0-E's input half, S4; D175): input of a device the holder takes that
// still reached the game, by kind (a press, a release the game saw begin, a character, the mouse, raw input), and the
// DirectInput and XInput reads GraceUI changed, while a holder held and after it closed (the closing press, until it
// is released).
enum Passed : std::uint32_t { press, release, character, mouse, raw, kPassedKinds };
enum Masked : std::uint32_t { dinput_keyboard, dinput_mouse, xinput, kMaskedKinds };
inline constinit std::atomic<std::uint32_t> passed_held[kPassedKinds]{};
inline constinit std::atomic<std::uint32_t> masked_held[kMaskedKinds]{};
inline constinit std::atomic<std::uint32_t> masked_after[kMaskedKinds]{};

inline void count_passed(Passed kind) noexcept { passed_held[kind].fetch_add(1, std::memory_order_relaxed); }

// D238: the game window's focus messages as its filter saw them, and the presses that reached the game while no one
// held input: a key or button down in a DirectInput read (after GraceUI's filter, against the same thread's previous
// read), and a WM_KEYDOWN or WM_SYSKEYDOWN (not a repeat) or a button-down message.
enum Focus : std::uint32_t { focus_gained, focus_lost, app_active, app_inactive, kFocusKinds };
enum FreeDown : std::uint32_t { dinput_key, dinput_button, window_key, window_button, kFreeDownKinds };
inline constinit std::atomic<std::uint32_t> focus_events[kFocusKinds]{};
inline constinit std::atomic<std::uint32_t> free_downs[kFreeDownKinds]{};
inline void count_focus(Focus kind) noexcept { focus_events[kind].fetch_add(1, std::memory_order_relaxed); }
inline void count_free_down(FreeDown kind, std::uint32_t count = 1) noexcept {
  free_downs[kind].fetch_add(count, std::memory_order_relaxed);
}
inline void count_masked(Masked kind, bool held) noexcept {
  (held ? masked_held : masked_after)[kind].fetch_add(1, std::memory_order_relaxed);
}

#else

class CountingScope {
 public:
  CountingScope() noexcept {}  // user-provided, so that an unused scope is not a warning
};

#endif

}  // namespace grace::host::counters
