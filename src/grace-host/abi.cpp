// abi.cpp: grace-host's entry points for graceui.dll's exports (D110): the host's start (D71, D72) and, in a test build,
// what the test exports read and set.
//
// SAFETY: the raw pointers from callers here are the test exports' output buffers, each written within the capacity
// the caller gives; the one cast is the address of a function of this module, which GetModuleHandleExW reads as an
// address inside graceui.dll to pin it.
#include "grace/host/host.h"

#include <windows.h>

#include <atomic>
#include <cwchar>

#include "counters.h"
#include "hooks.h"
#include "input.h"
#include "log.h"
#include "state.h"

namespace grace::host {
namespace {

// The first call's guard: 0 never asked, 1 asked (whatever came of it).
constinit std::atomic<int> g_asked{0};

// Pins graceui.dll: from now on FreeLibrary never unloads it (D107, §2.9), so the init thread and the detours always
// have their code.
bool pin_this_module() noexcept {
  HMODULE module = nullptr;
  // SAFETY: the address of a function in this module, read by GetModuleHandleExW only to find the module.
  return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&pin_this_module), &module) != FALSE;
}

}  // namespace

bool request_start() noexcept {
  int expected = 0;
  if (!g_asked.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
    const HostState now = state::host.load(std::memory_order_acquire);
    return now == HostState::started || now == HostState::hooked;
  }
  // D72: one per process, named after it. A copy that finds the mutex already made idles from its first call.
  wchar_t name[64];
  std::swprintf(name, sizeof name / sizeof name[0], L"Local\\GraceUI.Host.%lu",
                static_cast<unsigned long>(GetCurrentProcessId()));
  const HANDLE mutex = CreateMutexW(nullptr, FALSE, name);
  const DWORD error = GetLastError();
  if (mutex == nullptr) {
    state::host.store(HostState::failed, std::memory_order_release);
    log_line("the single-instance mutex could not be made (error %lu): the host stays idle",
             static_cast<unsigned long>(error));
    return false;
  }
  if (error == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    state::host.store(HostState::idle_copy, std::memory_order_release);
    log_line("another copy of GraceUI is active in this process: this copy stays idle (D72)");
    return false;
  }
  // The mutex's handle stays open for the life of the process: it is never closed.
  if (!pin_this_module()) {
    state::host.store(HostState::failed, std::memory_order_release);
    log_line("graceui.dll could not pin itself: the host stays idle");
    return false;
  }
  state::host.store(HostState::started, std::memory_order_release);
  const HANDLE thread = CreateThread(nullptr, 0, &hooks::init_thread_main, nullptr, 0, nullptr);
  if (thread == nullptr) {
    state::host.store(HostState::failed, std::memory_order_release);
    log_line("the init thread could not start: the host stays idle");
    return false;
  }
  state::init_threads_started.fetch_add(1, std::memory_order_relaxed);
  CloseHandle(thread);
  log_line("GraceUI %u.%u.%u started", GRACEUI_BUILD_MAJOR, GRACEUI_BUILD_MINOR, GRACEUI_BUILD_PATCH);
  return true;
}

#if defined(GRACEUI_TEST_EXPORTS)

void test_set_panel(bool on) noexcept { state::test_panel.store(on, std::memory_order_relaxed); }

void test_fill_state(GraceUiTestState& out) noexcept {
  out.host = static_cast<std::uint32_t>(state::host.load(std::memory_order_acquire));
  out.init_threads_started = state::init_threads_started.load(std::memory_order_relaxed);
  out.hook_install_runs = state::hook_install_runs.load(std::memory_order_relaxed);
  out.hooks_installed = state::hooks_installed.load(std::memory_order_relaxed);
  out.bound = state::bound.load(std::memory_order_relaxed) ? 1u : 0u;
  out.binds = state::binds.load(std::memory_order_relaxed);
  out.queue_method = static_cast<std::uint32_t>(state::queue_method.load(std::memory_order_relaxed));
  out.direct_queues_seen = state::direct_queues_seen.load(std::memory_order_relaxed);
  out.renderer_ready = state::renderer_ready.load(std::memory_order_relaxed) ? 1u : 0u;
  out.atlas_ready = state::atlas_ready.load(std::memory_order_relaxed) ? 1u : 0u;
  out.atlas_width = state::atlas_width.load(std::memory_order_relaxed);
  out.atlas_height = state::atlas_height.load(std::memory_order_relaxed);
  out.render_off = state::render_off.load(std::memory_order_relaxed) ? 1u : 0u;
  out.resizes = state::resizes.load(std::memory_order_relaxed);
  out.passed_through = state::passed_through.load(std::memory_order_relaxed);
  out.presents = state::presents.load(std::memory_order_relaxed);
  out.frames_drawn = state::frames_drawn.load(std::memory_order_relaxed);
  out.frames_skipped_busy = state::frames_skipped_busy.load(std::memory_order_relaxed);
  out.allocations = counters::allocations.load(std::memory_order_relaxed);
  out.frees = counters::frees.load(std::memory_order_relaxed);
}

void test_reset_counters() noexcept {
  counters::allocations.store(0, std::memory_order_relaxed);
  counters::frees.store(0, std::memory_order_relaxed);
  counters::cost_count.store(0, std::memory_order_release);
  for (std::atomic<std::uint64_t>& count : counters::input_cost_count) {
    count.store(0, std::memory_order_release);
  }
  counters::char_count.store(0, std::memory_order_release);
}

std::uint32_t test_copy_frame_costs(GraceUiTestFrameCost* out, std::uint32_t capacity) noexcept {
  const std::uint64_t count = counters::cost_count.load(std::memory_order_acquire);
  const std::uint64_t kept = count < counters::kCostSamples ? count : counters::kCostSamples;
  const std::uint64_t wanted = kept < capacity ? kept : capacity;
  for (std::uint64_t i = 0; i < wanted; ++i) {
    const std::uint64_t slot = (count - wanted + i) % counters::kCostSamples;
    // SAFETY: i < wanted <= capacity, the caller's count of elements at `out`.
    out[i].total_ns = counters::costs_total_ns[slot].load(std::memory_order_relaxed);
    out[i].submit_ns = counters::costs_submit_ns[slot].load(std::memory_order_relaxed);
  }
  return static_cast<std::uint32_t>(wanted);
}

std::uint32_t test_copy_log_line(std::uint32_t index, char* out, std::uint32_t capacity) noexcept {
  LogText line{};
  if (capacity == 0 || !log_copy_line(index, line)) {
    return 0;
  }
  std::uint32_t length = 0;
  while (length + 1 < capacity && length < line.size() && line[length] != '\0') {
    // SAFETY: length + 1 < capacity, the caller's size of `out`, which leaves room for the terminating NUL.
    out[length] = line[length];
    ++length;
  }
  out[length] = '\0';
  return length;
}

namespace {

std::uint32_t copy_text(const char* text, char* out, std::uint32_t capacity, std::uint32_t length) noexcept {
  std::uint32_t i = 0;
  for (; text != nullptr && i + 1 < capacity && text[i] != '\0'; ++i) {
    // SAFETY: i + 1 < capacity, the caller's size of `out`, which leaves room for the terminating NUL.
    out[length + i] = text[i];
  }
  return i;
}

}  // namespace

std::uint32_t test_copy_hook_name(std::uint32_t index, char* out, std::uint32_t capacity) noexcept {
  const char* name = nullptr;
  const char* module = nullptr;
  if (capacity == 0 || !hooks::installed_hook(index, name, module)) {
    return 0;
  }
  std::uint32_t length = copy_text(name, out, capacity, 0);
  if (module != nullptr) {
    length += copy_text(" in ", out, capacity - length, length);
    length += copy_text(module, out, capacity - length, length);
  }
  // SAFETY: copy_text left room for the NUL within `capacity`.
  out[length] = '\0';
  return length;
}

void test_request_input(std::uint32_t owner, std::uint32_t kind, std::uint32_t wants, GraceUiTestAnswer& out) noexcept {
  const grace::input::Request answer = input::request(static_cast<std::uint16_t>(owner),
                                                      static_cast<grace::input::Kind>(kind <= 3 ? kind : 0),
                                                      static_cast<std::uint8_t>(wants & grace::input::want_all));
  out.answer = static_cast<std::uint32_t>(answer.answer);
  out.why = static_cast<std::uint32_t>(answer.why);
  out.owner = answer.holder.owner;
  out.kind = static_cast<std::uint32_t>(answer.holder.kind);
  out.generation = answer.holder.generation;
}

bool test_release_input(std::uint32_t owner) noexcept { return input::release(static_cast<std::uint16_t>(owner)); }

void test_set_world(std::uint32_t flags) noexcept {
  input::set_world((flags & GRACEUI_TEST_WORLD_DANGER) != 0, (flags & GRACEUI_TEST_WORLD_LOADING) != 0,
                   (flags & GRACEUI_TEST_WORLD_GAME_MENU) != 0);
}

void test_set_caret(std::int32_t x, std::int32_t y, std::int32_t height) noexcept { input::set_caret(x, y, height); }

void test_set_code_page(std::uint32_t code_page) noexcept { input::test_set_code_page(code_page); }

void test_fill_input(GraceUiTestInput& out) noexcept {
  const grace::input::Holder holder = input::capture().holder();
  const grace::input::LastClose last = input::capture().last_close();
  out.holder_owner = holder.holds() ? holder.owner : 0;
  out.holder_kind = holder.holds() ? static_cast<std::uint32_t>(holder.kind) : 0;
  out.holder_wants = holder.holds() ? holder.wants : 0;
  out.generation = holder.generation;
  out.last_closed_owner = last.holder.owner;
  out.last_closed_kind = static_cast<std::uint32_t>(last.holder.kind);
  out.last_close_reason = static_cast<std::uint32_t>(last.why);
  out.closes = static_cast<std::uint32_t>(input::capture().closes());
  out.notices = input::capture().notices();
  out.window_filter = input::window_filtered() ? 1u : 0u;
  out.window_unicode = input::window_unicode() ? 1u : 0u;
  out.render_hooks = state::render_hooks.load(std::memory_order_relaxed);
  out.dinput_hooks = state::dinput_hooks.load(std::memory_order_relaxed);
  out.xinput_hooks = state::xinput_hooks.load(std::memory_order_relaxed);
  out.input_off = input::off() ? 1u : 0u;
  out.events_dropped = input::test_events_dropped();
  out.chars_seen = counters::char_count.load(std::memory_order_acquire);
  out.imgui_keys_down = counters::imgui_keys_down.load(std::memory_order_relaxed);
  out.imgui_draws_cursor = counters::imgui_draws_cursor.load(std::memory_order_relaxed);
  out.imgui_mouse_x = counters::imgui_mouse_x.load(std::memory_order_relaxed);
  out.imgui_mouse_y = counters::imgui_mouse_y.load(std::memory_order_relaxed);
  out.frames_fed = counters::frames_fed.load(std::memory_order_relaxed);
}

std::uint32_t test_copy_chars(std::uint32_t* out, std::uint32_t capacity) noexcept {
  const std::uint32_t count = counters::char_count.load(std::memory_order_acquire);
  const std::uint32_t wanted = count < capacity ? count : capacity;
  for (std::uint32_t i = 0; i < wanted; ++i) {
    // SAFETY: i < wanted <= capacity, the caller's count of elements at `out`.
    out[i] = counters::chars[i].load(std::memory_order_relaxed);
  }
  return wanted;
}

std::uint32_t test_copy_input_costs(std::uint32_t kind, std::uint32_t* out, std::uint32_t capacity) noexcept {
  if (kind >= counters::kInputCostKinds) {
    return 0;
  }
  const std::uint64_t count = counters::input_cost_count[kind].load(std::memory_order_acquire);
  const std::uint64_t kept = count < counters::kCostSamples ? count : counters::kCostSamples;
  const std::uint64_t wanted = kept < capacity ? kept : capacity;
  for (std::uint64_t i = 0; i < wanted; ++i) {
    // SAFETY: i < wanted <= capacity, the caller's count of elements at `out`.
    out[i] = counters::input_costs_ns[kind][(count - wanted + i) % counters::kCostSamples].load(
        std::memory_order_relaxed);
  }
  return static_cast<std::uint32_t>(wanted);
}

#endif  // GRACEUI_TEST_EXPORTS

}  // namespace grace::host
