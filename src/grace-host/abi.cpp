// abi.cpp: grace-host's entry points for graceui.dll's exports (D110): the host's start (D71, D72, D133), the guard
// against a second copy and the naming of one (invariant 18, D135), and, in a test build, what the test exports read
// and set.
//
// SAFETY: the raw pointers from callers here are the test exports' output buffers, each written within the capacity
// the caller gives. The casts give GetModuleHandleExW an address to find a module by (a function of this module, or a
// module's own base address, which is its handle), and turn GetProcAddress's answer into the export's own type; each
// module is held by a reference of ours while its export is looked up and called, so it cannot be unloaded meanwhile.
#include "grace/host/host.h"

#include <windows.h>

#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2  // EnumProcessModules as kernel32's K32EnumProcessModules: no psapi.dll
#endif
#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>
#include <vector>

#include "anticheat.h"
#include "counters.h"
#include "draw.h"
#include "draw_atlas.h"
#include "grace/abi/spin.h"
#include "grace/abi/text.h"
#include "hooks.h"
#include "hooks_input.h"
#include "input.h"
#include "log.h"
#include "menu.h"
#include "session.h"
#include "spaces.h"
#include "state.h"

namespace grace::host {
namespace {

// The first request's guard: 0 never asked, 1 the first request is running, 2 it is done.
constinit std::atomic<int> g_asked{0};
constinit std::atomic<Copy> g_copy{Copy::none};

// How long a request made while the first is still running waits for it (EST, L9): a caller that holds the loader lock
// (a mod asking from its DllMain, which D70 forbids) would otherwise spin for ever while the first request waits for
// that lock in GetModuleHandleExW, and the game would hang at start. Past it the request answers null, as a request
// that cannot be served does; the mod may ask again.
constexpr ULONGLONG kFirstRequestWaitMs = 2000;
constinit std::atomic<bool> g_wait_logged{false};

#if defined(GRACEUI_TEST_EXPORTS)
constinit std::atomic<std::uint32_t> g_test_first_delay{0};  // the first request takes this much longer (L9's check)
#endif

// Pins graceui.dll: from now on FreeLibrary never unloads it (D107, §2.9), so the init thread and the detours always
// have their code.
bool pin_this_module() noexcept {
  HMODULE module = nullptr;
  // SAFETY: the address of a function in this module, read by GetModuleHandleExW only to find the module.
  return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&pin_this_module), &module) != FALSE;
}

HMODULE this_module() noexcept {
  HMODULE module = nullptr;
  // SAFETY: as above; no reference is taken.
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&this_module), &module);
  return module;
}

Copy first_request() noexcept {
#if defined(GRACEUI_TEST_EXPORTS)
  if (const std::uint32_t delay = g_test_first_delay.load(std::memory_order_relaxed); delay != 0) {
    Sleep(delay);
  }
#endif
  // D72: one per process, named after it. A copy that finds the mutex already made idles from its first call.
  wchar_t name[64];
  std::swprintf(name, sizeof name / sizeof name[0], L"Local\\GraceUI.Host.%lu",
                static_cast<unsigned long>(GetCurrentProcessId()));
  const HANDLE mutex = CreateMutexW(nullptr, FALSE, name);
  const DWORD error = GetLastError();
  if (mutex == nullptr) {
    state::host.store(HostState::failed, std::memory_order_release);
    log_line("the single-instance mutex could not be made (error %lu): this copy serves no table",
             static_cast<unsigned long>(error));
    return Copy::none;
  }
  if (error == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    state::host.store(HostState::idle_copy, std::memory_order_release);
    log_line("another copy of GraceUI is active in this process: this copy stays idle (D72)");
    return Copy::idle;
  }
  // The mutex's handle stays open for the life of the process: it is never closed.
  log_line("GraceUI %u.%u.%u starting (ABI 1.%u, a preview)", GRACEUI_BUILD_MAJOR, GRACEUI_BUILD_MINOR,
           GRACEUI_BUILD_PATCH, GRACEUI_ABI1_MINOR);
  // The panels, the textures and faces, and the providers' claims, made here before any table is returned, so that the
  // present thread never makes them (invariant 10).
  try {
    session::allocate();
  } catch (...) {
    state::host.store(HostState::failed, std::memory_order_release);
    log_line("GraceUI's stores could not be made: the host stays idle");
    return Copy::active;
  }
  // D88, D133: the anti-cheat check, before any table is returned and before anything is hooked.
  if (const anticheat::Clause clause = anticheat::check(); clause != anticheat::Clause::none) {
    session::enter_notice_only(clause);
  }
  if (!pin_this_module()) {
    state::host.store(HostState::failed, std::memory_order_release);
    log_line("graceui.dll could not pin itself: the host stays idle");
    return Copy::active;  // its table is served: connect answers inactive (the host failed)
  }
  state::host.store(HostState::started, std::memory_order_release);
  if (!hooks::prepare_wake()) {
    log_line("GraceUI's own thread's wake-up could not be made: it looks for work every 50 ms instead");
  }
  const HANDLE thread = CreateThread(nullptr, 0, &hooks::init_thread_main, nullptr, 0, nullptr);
  if (thread == nullptr) {
    state::host.store(HostState::failed, std::memory_order_release);
    log_line("the init thread could not start: the host stays idle");
    return Copy::active;
  }
  state::init_threads_started.fetch_add(1, std::memory_order_relaxed);
  CloseHandle(thread);
  log_line("GraceUI %u.%u.%u started", GRACEUI_BUILD_MAJOR, GRACEUI_BUILD_MINOR, GRACEUI_BUILD_PATCH);
  return Copy::active;
}

// ---- Other copies in the process (D72, D135).

// The loaded modules other than this one that export GraceUI_RequestApi: each with a reference of ours, which the
// caller gives back with FreeLibrary.
std::vector<HMODULE> other_copies() {
  std::vector<HMODULE> modules(512);
  DWORD needed = 0;
  const HANDLE process = GetCurrentProcess();
  for (;;) {
    const DWORD bytes = static_cast<DWORD>(modules.size() * sizeof(HMODULE));
    if (!EnumProcessModules(process, modules.data(), bytes, &needed)) {
      return {};
    }
    if (needed <= bytes) {
      modules.resize(needed / sizeof(HMODULE));
      break;
    }
    modules.resize(needed / sizeof(HMODULE) + 64);
  }
  const HMODULE self = this_module();
  std::vector<HMODULE> copies;
  for (const HMODULE module : modules) {
    HMODULE held = nullptr;
    // SAFETY: a module's handle is its base address, an address inside it; the reference keeps it loaded until the
    // caller's FreeLibrary. A module unloaded since the list was made is not found, and is skipped.
    if (module == self || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                              reinterpret_cast<LPCWSTR>(module), &held)) {
      continue;
    }
    if (held != self && GetProcAddress(held, "GraceUI_RequestApi") != nullptr) {
      copies.push_back(held);
    } else {
      FreeLibrary(held);
    }
  }
  return copies;
}

// A copy's name relative to the game's folder (the folder of the process's executable), in UTF-8; outside it, its file
// name and the words "outside the game's folder". Never a whole path (PV5).
bool relative_name(HMODULE module, char* out, int capacity) {
  constexpr DWORD kUnits = 32768;
  std::vector<wchar_t> path(kUnits);
  std::vector<wchar_t> program(kUnits);
  const DWORD path_length = GetModuleFileNameW(module, path.data(), kUnits);
  const DWORD program_length = GetModuleFileNameW(nullptr, program.data(), kUnits);
  if (path_length == 0 || path_length >= kUnits || program_length == 0 || program_length >= kUnits) {
    return false;
  }
  int folder = static_cast<int>(program_length);
  while (folder > 0 && program[folder - 1] != L'\\' && program[folder - 1] != L'/') {
    --folder;
  }
  const wchar_t* name = path.data();
  int name_length = static_cast<int>(path_length);
  const char* outside = "";
  if (folder > 0 && name_length > folder &&
      CompareStringOrdinal(path.data(), folder, program.data(), folder, TRUE) == CSTR_EQUAL) {
    name += folder;
    name_length -= folder;
  } else {
    int start = name_length;
    while (start > 0 && path[start - 1] != L'\\' && path[start - 1] != L'/') {
      --start;
    }
    name += start;
    name_length -= start;
    outside = " (outside the game's folder)";
  }
  // In UTF-8, whole if it fits; otherwise its end, from a character's start, after "..." (L12): a copy deep in a mod
  // manager's folders is named all the same, by the part that tells most, its folder and file.
  const int needed = WideCharToMultiByte(CP_UTF8, 0, name, name_length, nullptr, 0, nullptr, nullptr);
  if (needed <= 0 || capacity <= 64 + 4) {
    return false;
  }
  std::vector<char> utf8(static_cast<std::size_t>(needed));
  if (WideCharToMultiByte(CP_UTF8, 0, name, name_length, utf8.data(), needed, nullptr, nullptr) != needed) {
    return false;
  }
  const std::size_t room = static_cast<std::size_t>(capacity) - 64 - 4;  // the words after it, and "..." before it
  const std::string_view whole(utf8.data(), utf8.size());
  const std::size_t start = grace::abi::utf8_tail_start(whole, room);
  std::snprintf(out, static_cast<std::size_t>(capacity), "%s%.*s%s", start != 0 ? "..." : "",
                static_cast<int>(whole.size() - start), whole.data() + start, outside);
  return true;
}

constexpr int kNamedCopies = 8;
constinit HMODULE g_named[kNamedCopies] = {};
constinit std::atomic<std::uint32_t> g_named_count{0};
constinit grace::abi::SpinLock g_naming;

// The active copy: names, once each, every other copy loaded in the process, in one notice and one log line.
void name_other_copies() noexcept {
  try {
    const grace::abi::SpinGuard guard(g_naming);
    for (const HMODULE copy : other_copies()) {
      std::uint32_t count = g_named_count.load(std::memory_order_relaxed);
      bool named = false;
      for (std::uint32_t i = 0; i < count; ++i) {
        named = named || g_named[i] == copy;
      }
      char name[400];
      if (!named && count < kNamedCopies && relative_name(copy, name, sizeof name)) {
        g_named[count] = copy;
        g_named_count.store(count + 1, std::memory_order_release);
        log_line("a second copy of GraceUI is loaded, and idle: %s", name);
        session::own_notice(0, GRACEUI_TONE_WARNING,
                            "A second copy of GraceUI was loaded from another folder and is idle: %s", name);
      }
      FreeLibrary(copy);
    }
  } catch (...) {
    log_line("looking for other copies of GraceUI failed (an exception)");
  }
}

// An idle copy, once its first request is done: it asks every other copy for ABI 1's table, so that the active one,
// which answers, hears of this copy and names it at once (a copy answers a request with null from its first, and its
// own first request is over before it asks, so no two copies wait on each other).
void tell_active_copy() noexcept {
  try {
    for (const HMODULE copy : other_copies()) {
      // SAFETY: the export of that name has graceui.h's type in every copy of GraceUI.
      const auto request = reinterpret_cast<GraceUiRequestApiFn>(
          reinterpret_cast<void (*)()>(GetProcAddress(copy, "GraceUI_RequestApi")));
      if (request != nullptr) {
        (void)request(GRACEUI_ABI_MAJOR);
      }
      FreeLibrary(copy);
    }
  } catch (...) {
    log_line("an idle copy could not tell the active one of itself (an exception)");
  }
}

}  // namespace

Copy request_start() noexcept {
  int expected = 0;
  if (!g_asked.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
    // The first request may still be running on another thread: wait for it (a mod's thread; never DllMain, D70),
    // for a bounded time.
    const ULONGLONG since = GetTickCount64();
    while (g_asked.load(std::memory_order_acquire) != 2) {
      if (GetTickCount64() - since > kFirstRequestWaitMs) {
        if (!g_wait_logged.exchange(true, std::memory_order_relaxed)) {
          log_line("a request came while the first was still running and was answered null after 2 s (is a mod "
                   "asking from its DllMain?)");
        }
        return Copy::none;
      }
      SwitchToThread();
    }
    const Copy copy = g_copy.load(std::memory_order_acquire);
    if (copy == Copy::active) {
      name_other_copies();
    }
    return copy;
  }
  const Copy copy = first_request();
  g_copy.store(copy, std::memory_order_release);
  g_asked.store(2, std::memory_order_release);
  if (copy == Copy::active) {
    name_other_copies();
  } else if (copy == Copy::idle) {
    tell_active_copy();
  }
  return copy;
}

#if defined(GRACEUI_TEST_EXPORTS)

void test_set_panel(bool on) noexcept { state::test_panel.store(on, std::memory_order_relaxed); }

void test_set_first_request_delay(std::uint32_t ms) noexcept {
  g_test_first_delay.store(ms, std::memory_order_relaxed);
}

void test_turn_off_render() noexcept { draw::test_turn_off(); }

void test_set_atlas_limit(std::uint32_t side) noexcept { atlas::test_set_limit(side); }

void test_fill_menu(GraceUiTestMenu& out) noexcept {
  menu::TestView view;
  menu::test_view(view);
  out.open = view.open;
  out.generation = view.generation;
  out.inputs = view.inputs;
  out.lists = view.lists;
  out.frames = view.frames;
  out.refusals = view.refusals;
  out.layout_us = view.layout_us;
  out.vertices = view.vertices;
  out.indices = view.indices;
  out.commands = view.commands;
  out.off = view.off;
  out.scale = view.scale;
  out.faces = view.faces;
  out.baked_scale = view.baked_scale;
  std::memcpy(out.page, view.page, sizeof out.page);
  std::memcpy(out.focused, view.focused, sizeof out.focused);
}

void test_fill_server(GraceUiTestServer& out) noexcept {
  spaces::TestView view{};
  spaces::test_view(view);
  out.running = view.running;
  out.port = view.port;
  out.spaces = view.spaces;
  out.pending = view.pending;
  out.failed = view.failed;
}

std::uint32_t test_copy_setting(const char* key, char* out, std::uint32_t capacity) noexcept {
  return menu::test_setting(key, out, capacity);
}

void test_set_recheck_ms(std::uint32_t ms) noexcept { spaces::test_set_recheck_ms(ms); }

std::uint32_t test_pairing_code(char* out, std::uint32_t capacity) noexcept {
  return spaces::test_pairing_code(out, capacity);
}

void test_set_network_addresses(const std::uint8_t* addresses, std::uint32_t count) noexcept {
  spaces::test_set_network_addresses(addresses, count);
}

std::uint32_t test_menu_shows(const char* text) noexcept { return menu::test_shows(text) ? 1 : 0; }

std::uint32_t test_init_step() noexcept { return state::init_step.load(std::memory_order_acquire); }

std::uint32_t test_menu_bounds(const char* name, float* out) noexcept {
  float values[8] = {};
  if (out == nullptr || !menu::test_bounds(name, values)) {
    return 0;
  }
  std::copy(std::begin(values), std::end(values), out);
  return 1;
}

std::uint32_t test_copy_topic(const char* topic, char* out, std::uint32_t capacity) noexcept {
  return spaces::test_topic(topic, out, capacity);
}

std::uint64_t test_panel_published(std::uint64_t panel) noexcept {
  grace::abi::Panels& panels = session::panels();
  for (std::uint32_t i = 0; i < grace::abi::kMaxPanels; ++i) {
    grace::abi::PanelView view;
    if (panels.view(i, view) && view.id == panel) {
      return panels.lists(i).published();
    }
  }
  return 0;
}

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
  out.notice_only = session::notice_only() ? 1u : 0u;
  out.anti_cheat = static_cast<std::uint32_t>(session::anti_cheat());
  out.consumers = session::consumers().count();
  out.notices = session::notices().live(input::now_ms());
  out.copies_named = g_named_count.load(std::memory_order_acquire);
  out.panels = session::panels().count();
  out.panels_shown = session::panels().visible();
  out.lists_submitted = state::lists_submitted.load(std::memory_order_relaxed);
  out.lists_refused = state::lists_refused.load(std::memory_order_relaxed);
  out.panel_frames = state::panel_frames.load(std::memory_order_relaxed);
  out.textures = session::registry().textures();
  out.textures_ready = session::registry().textures_ready();
  out.texture_uploads = state::texture_uploads.load(std::memory_order_relaxed);
  out.textures_uploaded = state::textures_uploaded.load(std::memory_order_relaxed);
  out.faces = session::registry().faces();
  out.faces_baked = atlas::faces_baked();
  out.atlas_generation = atlas::generation();
  out.notice_face_grace = atlas::notice_face_is_grace() ? 1u : 0u;
  out.world_flags = session::world().combined();
  out.providers = session::world().providers();
  out.present_prologue = hooks::present_prologue();
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

// Copies at most `length` bytes of `text`, stopping at a NUL, into `out`, NUL-terminated.
template <std::size_t N>
void copy_into(char (&out)[N], const char* text, std::size_t length) noexcept {
  std::size_t i = 0;
  for (; i + 1 < N && i < length && text[i] != '\0'; ++i) {
    out[i] = text[i];
  }
  out[i] = '\0';
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
  static_assert(GRACEUI_TEST_WORLD_DANGER == GRACEUI_WORLD_DANGER && GRACEUI_TEST_WORLD_LOADING == GRACEUI_WORLD_LOADING &&
                GRACEUI_TEST_WORLD_GAME_MENU == GRACEUI_WORLD_GAME_MENU);
  input::test_set_world(flags);
}

void test_set_caret(std::int32_t x, std::int32_t y, std::int32_t height) noexcept { input::set_caret(x, y, height); }

void test_set_code_page(std::uint32_t code_page) noexcept { input::test_set_code_page(code_page); }

void test_pad(std::uint32_t user, std::uint16_t buttons) noexcept { hooks_input::test_pad(user, buttons); }
std::uint16_t test_pad_read(std::uint32_t user, std::uint16_t buttons, std::uint32_t& packet) noexcept {
  return hooks_input::test_pad_read(user, buttons, packet);
}

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
  // SAFETY: a window handle is a value, given to the harness as a number to compare with its own windows'.
  out.window = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(input::window()));
  out.windows_filtered = static_cast<std::uint32_t>(hooks_input::windows_filtered());
  const auto passed = [](counters::Passed kind) { return counters::passed_held[kind].load(std::memory_order_relaxed); };
  out.passed_presses = passed(counters::press);
  out.passed_releases = passed(counters::release);
  out.passed_characters = passed(counters::character);
  out.passed_mouse = passed(counters::mouse);
  out.passed_raw = passed(counters::raw);
  out.masked_keyboard_held = counters::masked_held[counters::dinput_keyboard].load(std::memory_order_relaxed);
  out.masked_mouse_held = counters::masked_held[counters::dinput_mouse].load(std::memory_order_relaxed);
  out.masked_pad_held = counters::masked_held[counters::xinput].load(std::memory_order_relaxed);
  out.masked_keyboard_free = counters::masked_after[counters::dinput_keyboard].load(std::memory_order_relaxed);
  out.masked_mouse_free = counters::masked_after[counters::dinput_mouse].load(std::memory_order_relaxed);
  out.masked_pad_free = counters::masked_after[counters::xinput].load(std::memory_order_relaxed);
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

void test_set_anti_cheat_names(const wchar_t* module_prefix, const wchar_t* parent) noexcept {
  anticheat::set_names(module_prefix, parent);
}

bool test_copy_notice(std::uint32_t index, GraceUiTestNotice& out) noexcept {
  grace::abi::Notice notice;
  if (!session::notices().copy(index, notice, input::now_ms())) {
    return false;
  }
  out.owner = notice.owner;
  out.key = notice.key;
  out.tone = notice.tone;
  out.timed = notice.expires_ms != 0 ? 1u : 0u;
  out.text_length = notice.text_length;
  copy_into(out.text, notice.text, notice.text_length);
  return true;
}

void test_clear_notices() noexcept { session::notices().clear(input::now_ms()); }

bool test_copy_consumer(std::uint32_t index, GraceUiTestConsumer& out) noexcept {
  grace::abi::ConsumerInfo info;
  if (!session::consumers().info(index, info)) {
    return false;
  }
  out.owner = info.owner;
  out.version = info.version;
  out.floor = info.floor;
  out.abi_minor = info.abi_minor;
  copy_into(out.id, info.id, sizeof info.id);
  copy_into(out.name, info.name, sizeof info.name);
  copy_into(out.caps, info.caps, sizeof info.caps);
  return true;
}

#endif  // GRACEUI_TEST_EXPORTS

}  // namespace grace::host
