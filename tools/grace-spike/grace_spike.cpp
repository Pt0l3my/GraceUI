// SPDX-License-Identifier: MIT
// grace_spike.cpp: grace-spike.dll, GraceUI's observer for the in-game spikes S0-E and S4 (D175), of our own, in the
// test tree only and never released. EldenNet's launcher loads it beside a test build of graceui.dll in a spike run
// and names the run's folder in ELDENNET_SPIKE_DIR; with that unset it does nothing at all.
//
//   - It finds GraceUI without loading it (by name, as it is looked for every 50 ms for up to ten minutes): GraceUI may
//     come first (the launcher's --also) or after EldenNet, which loads it itself (graceui-sys's LOAD).
//   - It connects as a mod (notices, panels, input, hotkeys), shows one banner, and on F9 opens a typing test: a panel
//     that takes the keyboard, the mouse and the pad for 60 s, then gives them back. What is typed reaches GraceUI's own
//     Dear ImGui (a mod's panel takes no characters through ABI 1); only how many is counted.
//   - Every 5 s it reads the test build's counters and writes one line of its report, graceui-spike.jsonl in that
//     folder: JSON Lines, kind "graceui_spike", schema 1, in a closed vocabulary (numbers, fixed words and our own
//     labels; no path, no name, nothing typed), each line stamped with QueryPerformanceCounter in milliseconds, the
//     clock of EldenNet's recordings and the launcher's Marks. tools/grace-spike/report.py reads it.
//
// Nothing in DllMain but starting its thread (D70). It reads the game's memory never, and calls no game code.
#define PSAPI_VERSION 2  // GetProcessMemoryInfo as kernel32's K32 function
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include "grace_spike.h"
#include "graceui_imgui.hpp"
#include "graceui_sys.hpp"
#include "graceui_test.h"
#include "imgui.h"

namespace {

// ---- The clock and the report.

std::int64_t now_ms() {
  LARGE_INTEGER count{};
  LARGE_INTEGER frequency{};
  QueryPerformanceCounter(&count);
  QueryPerformanceFrequency(&frequency);
  return frequency.QuadPart > 0 ? count.QuadPart / (frequency.QuadPart / 1000) : 0;
}

// One line of the report. Its keys and its words are this file's own, from fixed lists: nothing from outside is ever
// written into it but numbers.
class Line {
 public:
  explicit Line(const char* kind) {
    text_ = "{\"report\":\"graceui_spike\",\"schema\":1,\"t\":" + std::to_string(now_ms()) + ",\"k\":\"" + kind + "\"";
  }
  Line& num(const char* key, std::uint64_t value) {
    text_ += std::string(",\"") + key + "\":" + std::to_string(value);
    return *this;
  }
  Line& word(const char* key, const char* value) {
    text_ += std::string(",\"") + key + "\":\"" + value + "\"";
    return *this;
  }
  Line& nums(const char* key, std::initializer_list<std::uint64_t> values) {
    text_ += std::string(",\"") + key + "\":[";
    bool first = true;
    for (const std::uint64_t value : values) {
      text_ += (first ? "" : ",") + std::to_string(value);
      first = false;
    }
    text_ += "]";
    return *this;
  }
  std::string text() const { return text_ + "}\n"; }

 private:
  std::string text_;
};

class Report {
 public:
  bool open(const std::wstring& folder) {
    CreateDirectoryW(folder.c_str(), nullptr);  // the launcher makes it; an existing one is fine
    const std::wstring path = folder + L"\\" + GRACE_SPIKE_FILE;
    file_ = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                        nullptr);
    return file_ != INVALID_HANDLE_VALUE;
  }
  // Written at once, unbuffered: a crash loses at most the line being written.
  void write(const Line& line) const {
    const std::string text = line.text();
    DWORD written = 0;
    WriteFile(file_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
  }

 private:
  HANDLE file_ = INVALID_HANDLE_VALUE;
};

// ---- The fixed words.

const char* spike_word(const std::wstring& folder) {
  // The launcher's folder is "<word>-<run number>": the word alone, from the last part, if it is one we know.
  const std::size_t slash = folder.find_last_of(L"\\/");
  const std::wstring last = slash == std::wstring::npos ? folder : folder.substr(slash + 1);
  const std::wstring word = last.substr(0, last.find_last_of(L'-'));
  return word == L"s0e" ? "s0e" : word == L"s0e_alone" ? "s0e_alone" : word == L"s4" ? "s4" : "unknown";
}

const char* host_word(std::uint32_t host) {
  switch (host) {
    case GRACEUI_TEST_HOST_NOT_ASKED: return "not_asked";
    case GRACEUI_TEST_HOST_STARTED: return "started";
    case GRACEUI_TEST_HOST_HOOKED: return "hooked";
    case GRACEUI_TEST_HOST_FAILED: return "failed";
    case GRACEUI_TEST_HOST_IDLE_COPY: return "idle_copy";
    default: return "unknown";
  }
}

const char* queue_word(std::uint32_t method) {
  switch (method) {
    case GRACEUI_TEST_QUEUE_NONE: return "none";
    case GRACEUI_TEST_QUEUE_SWAP_CHAIN: return "swap_chain";
    case GRACEUI_TEST_QUEUE_RESIZE1: return "resize1";
    case GRACEUI_TEST_QUEUE_PRESENT: return "present";
    case GRACEUI_TEST_QUEUE_ONLY_DIRECT: return "only_direct";
    default: return "unknown";
  }
}

const char* holder_word(std::uint32_t kind) {
  switch (kind) {
    case 0: return "none";
    case GRACEUI_TEST_KIND_MENU: return "menu";
    case GRACEUI_TEST_KIND_PANEL: return "panel";
    case GRACEUI_TEST_KIND_MODAL: return "modal";
    default: return "unknown";
  }
}

const char* close_word(std::uint32_t reason) {
  switch (reason) {
    case GRACEUI_TEST_CLOSED_NONE: return "none";
    case GRACEUI_TEST_CLOSED_RELEASED: return "released";
    case GRACEUI_TEST_CLOSED_MENU_KEY: return "menu_key";
    case GRACEUI_TEST_CLOSED_DANGER: return "danger";
    case GRACEUI_TEST_CLOSED_LOADING: return "loading";
    case GRACEUI_TEST_CLOSED_FOCUS_LOST: return "focus_lost";
    case GRACEUI_TEST_CLOSED_NO_RENDER: return "no_render";
    case GRACEUI_TEST_CLOSED_FAULT: return "fault";
    case GRACEUI_TEST_CLOSED_PLAYER: return "player";
    default: return "unknown";
  }
}

const char* prologue_word(std::uint32_t prologue) {
  return prologue == GRACEUI_TEST_PROLOGUE_PLAIN  ? "plain"
         : prologue == GRACEUI_TEST_PROLOGUE_JUMP ? "jump"
                                                  : "unknown";
}

const char* connect_word(GraceUiStatus status) {
  switch (status) {
    case GRACEUI_OK: return "ok";
    case GRACEUI_TOO_OLD: return "too_old";
    case GRACEUI_DUPLICATE_ID: return "duplicate_id";
    case GRACEUI_INACTIVE: return "inactive";
    default: return "refused";
  }
}

const char* present(const wchar_t* module) { return GetModuleHandleW(module) != nullptr ? "present" : "absent"; }

std::uint32_t environment_ms(const wchar_t* name, std::uint32_t fallback, std::uint32_t low, std::uint32_t high) {
  wchar_t text[16] = {};
  const DWORD length = GetEnvironmentVariableW(name, text, 16);
  if (length == 0 || length >= 16) {
    return fallback;
  }
  const unsigned long value = std::wcstoul(text, nullptr, 10);
  return value < low ? low : value > high ? high : static_cast<std::uint32_t>(value);
}

// ---- The test build's counters, read from the GraceUI found.

template <class Fn>
Fn export_of(HMODULE module, const char* name) {
  const FARPROC found = module != nullptr ? GetProcAddress(module, name) : nullptr;
  // SAFETY: graceui_test.h gives the export of that name this type; void (*)(void) is the one type every compiler lets
  // a function pointer pass through.
  return found != nullptr ? reinterpret_cast<Fn>(reinterpret_cast<void (*)(void)>(found)) : nullptr;
}

struct Counters {
  using GetState = std::uint32_t (*)(GraceUiTestState*);
  using GetInput = std::uint32_t (*)(GraceUiTestInput*);
  using GetMenu = std::uint32_t (*)(GraceUiTestMenu*);
  using GetCosts = std::uint32_t (*)(GraceUiTestFrameCost*, std::uint32_t);
  using GetSetting = std::uint32_t (*)(const char*, char*, std::uint32_t);
  GetState state = nullptr;
  GetInput input = nullptr;
  GetMenu menu = nullptr;
  GetCosts costs = nullptr;
  GetSetting setting = nullptr;

  bool bind(HMODULE module) {
    state = export_of<GetState>(module, "GraceUI_Test_GetState");
    input = export_of<GetInput>(module, "GraceUI_Test_GetInput");
    menu = export_of<GetMenu>(module, "GraceUI_Test_GetMenu");
    costs = export_of<GetCosts>(module, "GraceUI_Test_GetFrameCosts");
    setting = export_of<GetSetting>(module, "GraceUI_Test_GetSetting");
    return state != nullptr && input != nullptr && menu != nullptr && costs != nullptr && setting != nullptr;
  }
  GraceUiTestState read_state() const {
    GraceUiTestState out{};
    out.size = sizeof out;
    state(&out);
    return out;
  }
  GraceUiTestInput read_input() const {
    GraceUiTestInput out{};
    out.size = sizeof out;
    input(&out);
    return out;
  }
  GraceUiTestMenu read_menu() const {
    GraceUiTestMenu out{};
    out.size = sizeof out;
    menu(&out);
    return out;
  }
  std::uint32_t ui_scale_x100() const {
    char text[32] = {};
    if (setting("ui.scale", text, sizeof text) == 0) {
      return 0;
    }
    return static_cast<std::uint32_t>(std::strtod(text, nullptr) * 100.0 + 0.5);
  }
};

struct CostSummary {
  std::uint64_t frames = 0;
  std::uint64_t own_p50_us = 0;
  std::uint64_t own_p99_us = 0;
  std::uint64_t detour_p99_us = 0;
};

std::uint64_t percentile_us(std::vector<std::uint32_t>& ns, unsigned percent) {
  if (ns.empty()) {
    return 0;
  }
  std::sort(ns.begin(), ns.end());
  const std::size_t at = std::min(ns.size() - 1, ns.size() * percent / 100);
  return ns[at] / 1000;
}

// The newest frames' cost (up to 600): GraceUI's own work (the detour less the runtime's submission) and the whole.
CostSummary costs_of(const Counters& counters) {
  std::array<GraceUiTestFrameCost, 600> samples{};
  const std::uint32_t got = counters.costs(samples.data(), static_cast<std::uint32_t>(samples.size()));
  std::vector<std::uint32_t> own;
  std::vector<std::uint32_t> whole;
  for (std::uint32_t i = 0; i < got; ++i) {
    whole.push_back(samples[i].total_ns);
    own.push_back(samples[i].total_ns > samples[i].submit_ns ? samples[i].total_ns - samples[i].submit_ns : 0);
  }
  CostSummary out;
  out.frames = got;
  out.own_p50_us = percentile_us(own, 50);
  out.own_p99_us = percentile_us(own, 99);
  out.detour_p99_us = percentile_us(whole, 99);
  return out;
}

using StandInCount = LONG64 (*)();
using StandInState = LONG (*)();

const char* standin_word(std::uint64_t& presents) {
  const HMODULE standin = GetModuleHandleW(GRACE_STANDIN_FILE);
  if (standin == nullptr) {
    return "absent";
  }
  const auto count = export_of<StandInCount>(standin, "GraceStandIn_Presents");
  const auto patched = export_of<StandInState>(standin, "GraceStandIn_Patched");
  if (count == nullptr || patched == nullptr) {
    return "unknown";
  }
  presents = static_cast<std::uint64_t>(count());
  const LONG state = patched();
  return state == 1 ? "patched" : state == 2 ? "failed" : "waiting";
}

void write_state(const Report& report, const Counters& counters) {
  const GraceUiTestState s = counters.read_state();
  const GraceUiTestInput in = counters.read_input();
  const GraceUiTestMenu menu = counters.read_menu();
  const CostSummary cost = costs_of(counters);
  std::uint64_t standin_presents = 0;
  const char* standin = standin_word(standin_presents);
  DWORD handles = 0;
  GetProcessHandleCount(GetCurrentProcess(), &handles);
  PROCESS_MEMORY_COUNTERS_EX memory{};
  memory.cb = sizeof memory;
  // SAFETY: PROCESS_MEMORY_COUNTERS_EX begins with PROCESS_MEMORY_COUNTERS, and cb says which it is.
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof memory);
  Line line("state");
  line.word("host", host_word(s.host))
      .num("install_runs", s.hook_install_runs)
      .num("init_threads", s.init_threads_started)
      .num("hooks", s.hooks_installed)
      .num("dinput_hooks", in.dinput_hooks)
      .num("xinput_hooks", in.xinput_hooks)
      .num("window_filter", in.window_filter)
      .num("bound", s.bound)
      .num("binds", s.binds)
      .word("queue", queue_word(s.queue_method))
      .num("presents", s.presents)
      .num("frames_drawn", s.frames_drawn)
      .num("frames_skipped", s.frames_skipped_busy)
      .num("render_off", s.render_off)
      .num("input_off", in.input_off)
      .num("allocations", s.allocations)
      .num("frees", s.frees)
      .num("consumers", s.consumers)
      .num("notices", s.notices)
      .num("panels_shown", s.panels_shown)
      .num("lists_submitted", s.lists_submitted)
      .num("lists_refused", s.lists_refused)
      .num("textures", s.textures)
      .num("texture_uploads", s.texture_uploads)
      .nums("atlas", {s.atlas_width, s.atlas_height})
      .num("atlas_generation", s.atlas_generation)
      .num("resizes", s.resizes)
      .num("passed_through", s.passed_through)
      .word("prologue", prologue_word(s.present_prologue))
      .word("holder", holder_word(in.holder_kind))
      .num("closes", in.closes)
      .word("last_close", close_word(in.last_close_reason))
      .nums("passed", {in.passed_presses, in.passed_releases, in.passed_characters, in.passed_mouse, in.passed_raw})
      .nums("masked_held", {in.masked_keyboard_held, in.masked_mouse_held, in.masked_pad_held})
      .nums("masked_free", {in.masked_keyboard_free, in.masked_mouse_free, in.masked_pad_free})
      .num("chars_seen", in.chars_seen)
      .num("menu_open", menu.open)
      .num("menu_scale_x100", static_cast<std::uint64_t>(menu.scale * 100.0f + 0.5f))
      .num("ui_scale_x100", counters.ui_scale_x100())
      .word("eldennet", present(L"eldennet.dll"))
      .word("standin", standin)
      .num("standin_presents", standin_presents)
      .num("handles", handles)
      .num("private_kb", memory.PrivateUsage / 1024)
      .num("cost_frames", cost.frames)
      .num("own_p50_us", cost.own_p50_us)
      .num("own_p99_us", cost.own_p99_us)
      .num("detour_p99_us", cost.detour_p99_us);
  report.write(line);
}

// ---- The typing test's panel: a few lines drawn by its own headless Dear ImGui (no backend, invariant 16).

void build(graceui::imgui::DrawList& list, std::uint32_t seconds_left, std::uint32_t characters) {
  constexpr float kWidth = GRACE_SPIKE_WIDTH;
  constexpr float kHeight = GRACE_SPIKE_HEIGHT;
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(kWidth, kHeight);
  io.DeltaTime = 0.1f;
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(kWidth, kHeight));
  ImGui::Begin("grace-spike", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoInputs);
  ImGui::TextUnformatted("Typing test: type anything, W, A, S and D among it, and move the mouse.");
  ImGui::TextUnformatted("The Tarnished should not move. Hold Esc for a second to stop early.");
  ImGui::Separator();
  ImGui::Text("Seconds left: %u", seconds_left);
  ImGui::Text("Characters counted: %u", characters);
  ImGui::End();
  ImGui::Render();
  list.build(*ImGui::GetDrawData(), kWidth, kHeight);
}

struct Typing {
  bool on = false;
  std::int64_t started = 0;
  GraceUiTestInput base{};
};

std::uint32_t delta(std::uint32_t now, std::uint32_t base) { return now >= base ? now - base : 0; }

void write_typing(const Report& report, const Counters& counters, const Typing& typing, std::uint32_t target_ms) {
  const GraceUiTestInput in = counters.read_input();
  const GraceUiTestInput& b = typing.base;
  Line line("typing");
  line.num("started", static_cast<std::uint64_t>(typing.started))
      .num("held_ms", static_cast<std::uint64_t>(now_ms() - typing.started))
      .num("target_ms", target_ms)
      .num("chars", delta(in.chars_seen, b.chars_seen))
      .word("end", in.holder_kind == GRACEUI_TEST_KIND_PANEL ? "held" : close_word(in.last_close_reason))
      .nums("passed", {delta(in.passed_presses, b.passed_presses), delta(in.passed_releases, b.passed_releases),
                       delta(in.passed_characters, b.passed_characters), delta(in.passed_mouse, b.passed_mouse),
                       delta(in.passed_raw, b.passed_raw)})
      .nums("masked_held", {delta(in.masked_keyboard_held, b.masked_keyboard_held),
                            delta(in.masked_mouse_held, b.masked_mouse_held),
                            delta(in.masked_pad_held, b.masked_pad_held)});
  report.write(line);
}

DWORD WINAPI spike_worker(LPVOID) {
  wchar_t folder[MAX_PATH] = {};
  const DWORD length = GetEnvironmentVariableW(GRACE_SPIKE_DIR_VARIABLE, folder, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return 0;  // not a spike run: nothing at all
  }
  Report report;
  if (!report.open(folder)) {
    return 0;
  }
  const std::uint32_t typing_ms = environment_ms(L"GRACE_SPIKE_TYPING_MS", GRACE_SPIKE_TYPING_MS, 1000, 600000);
  const std::uint32_t state_ms = environment_ms(L"GRACE_SPIKE_STATE_MS", GRACE_SPIKE_STATE_MS, 200, 60000);

  // GraceUI, found without loading it: by name, every 50 ms, for up to ten minutes.
  HMODULE graceui = nullptr;
  for (int i = 0; i < 12000 && (graceui = GetModuleHandleW(L"graceui.dll")) == nullptr; ++i) {
    Sleep(50);
  }
  const char* eldennet_at_find = present(L"eldennet.dll");
  GraceUiSysFound found{};
  found.size = sizeof found;
  const graceui::Api api = graceui != nullptr ? graceui::Api::find(0, 10000, &found) : graceui::Api();
  Counters counters;
  graceui::Mod mod;
  mod.id = GRACE_SPIKE_ID;
  mod.name = GRACE_SPIKE_NAME;
  mod.version = GRACEUI_VERSION_PACK(1, 0, 0);
  mod.floor = GRACEUI_VERSION_PACK(0, 2, 0);
  mod.caps = "notices panels input hotkeys";
  GraceUiConnectAnswer answer{};
  const GraceUiStatus connected = api ? api.connect(mod, answer) : GRACEUI_REFUSED;
  const bool test_build = api && counters.bind(found.module);
  std::uint64_t standin_presents = 0;
  Line header("header");
  header.word("spike", spike_word(folder))
      .nums("graceui", {GRACEUI_VERSION_MAJOR(found.version), GRACEUI_VERSION_MINOR(found.version),
                        GRACEUI_VERSION_PATCH(found.version)})
      .num("abi_minor", answer.abi_minor)
      .word("found", !api ? "absent" : found.how == GRACEUI_SYS_BY_NAME ? "by_name" : "by_scan")
      .word("build", test_build ? "test" : "release")
      .word("eldennet_at_find", eldennet_at_find)
      .word("steam_overlay", GetModuleHandleW(L"GameOverlayRenderer64.dll") != nullptr ? "loaded" : "absent")
      .word("standin", standin_word(standin_presents))
      .word("connect", api ? connect_word(connected) : "absent")
      .num("typing_ms", typing_ms)
      .num("state_ms", state_ms);
  report.write(header);
  if (!api || !test_build) {
    return 0;  // nothing to read: the report says why
  }
  const bool drawing = connected == GRACEUI_OK;
  const GraceUiConsumer me = answer.consumer;

  // The typing test: its hotkey, its panel and its Dear ImGui, made in its registration window.
  std::uint32_t hotkey = 0;
  std::uint64_t panel = 0;
  graceui::imgui::DrawList list;
  bool can_type = false;
  if (drawing) {
    api.notice(me, GRACE_SPIKE_BANNER, GRACEUI_TONE_NORMAL, GRACE_SPIKE_BANNER_KEY, 0);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    graceui::imgui::prepare(io);
    io.Fonts->AddFontDefaultBitmap();
    GraceUiPanelDesc desc{};
    desc.size = sizeof desc;
    desc.layer = GRACEUI_LAYER_PANEL;
    desc.anchor = GRACEUI_ANCHOR_CENTRE;
    desc.flags = GRACEUI_PANEL_TAKES_INPUT;
    desc.width = GRACE_SPIKE_WIDTH;
    desc.height = GRACE_SPIKE_HEIGHT;
    const GraceUiStatus bound = api.register_hotkey(me, GRACE_SPIKE_ACTION, "Open the typing test", GRACE_SPIKE_KEY,
                                                    0, hotkey);
    can_type = graceui::imgui::register_atlas(api, me, io) != 0 && api.create_panel(me, desc, panel) == GRACEUI_OK &&
               (bound == GRACEUI_OK || bound == GRACEUI_UNBOUND);
    report.write(Line("typing_ready").word("hotkey", bound == GRACEUI_OK        ? "bound"
                                                     : bound == GRACEUI_UNBOUND ? "unbound"
                                                                                : "refused")
                                     .word("panel", can_type ? "made" : "refused"));
  }

  Typing typing;
  std::int64_t next_state = now_ms();
  for (;;) {
    GraceUiEvent event{};
    while (drawing && api.poll(me, event) == GRACEUI_OK && event.kind != GRACEUI_EVENT_NONE) {
      if (event.kind == GRACEUI_EVENT_HOTKEY && event.hotkey_id == hotkey && can_type && !typing.on) {
        build(list, typing_ms / 1000, 0);
        api.submit(me, panel, list.view());
        api.show_panel(me, panel);
        GraceUiInputRequest request{};
        request.size = sizeof request;
        request.kind = GRACEUI_HOLDER_PANEL;
        request.wants = GRACEUI_WANT_KEYBOARD | GRACEUI_WANT_MOUSE | GRACEUI_WANT_PAD;
        request.panel = panel;
        GraceUiInputAnswer granted{};
        granted.size = sizeof granted;
        api.request_input(me, request, granted);
        if (granted.answer == GRACEUI_INPUT_GRANTED) {
          typing.on = true;
          typing.started = now_ms();
          typing.base = counters.read_input();
        } else {
          api.hide_panel(me, panel);
          report.write(Line("typing_refused").word("answer", granted.answer == GRACEUI_INPUT_REFUSED ? "refused"
                                                                                                    : "not_now"));
        }
      }
    }
    if (typing.on) {
      const GraceUiTestInput in = counters.read_input();
      const std::int64_t held = now_ms() - typing.started;
      const bool still_ours = in.holder_kind == GRACEUI_TEST_KIND_PANEL;
      if (!still_ours || held >= static_cast<std::int64_t>(typing_ms)) {
        if (still_ours) {
          api.release_input(me);  // its time is up: given back through the one close routine
          Sleep(50);
        }
        write_typing(report, counters, typing, typing_ms);
        api.hide_panel(me, panel);
        typing.on = false;
      } else {
        const std::uint32_t left = static_cast<std::uint32_t>((static_cast<std::int64_t>(typing_ms) - held) / 1000);
        build(list, left, delta(in.chars_seen, typing.base.chars_seen));
        if (!list.empty()) {
          api.submit(me, panel, list.view());
        }
      }
    }
    if (now_ms() >= next_state) {
      write_state(report, counters);
      next_state = now_ms() + state_ms;
    }
    Sleep(typing.on ? 100 : 50);
  }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    const HANDLE thread = CreateThread(nullptr, 0, &spike_worker, nullptr, 0, nullptr);  // runs once the loader is done
    if (thread != nullptr) {
      CloseHandle(thread);
    }
  }
  return TRUE;
}
