// SPDX-License-Identifier: MIT
// hello_debug_view.cpp: the third example mod of our own (D85, D156), in C++, the shape of EldenNet's banner and F11
// view (its part 12) with invented content:
//
//   - Nothing in DllMain but starting a thread of its own; GraceUI is found and called from that thread (D70).
//   - A banner: one notice under the mod's own key with a duration of 0, shown until it is replaced, its text replaced
//     in place as the view opens and closes (D134).
//   - A hotkey of GraceUI's, F11 (D155): GraceUI watches for it and keeps it from the game; the mod takes its presses
//     through poll on its own thread, and toggles its view on each.
//   - Its view: a panel, hidden until F11, drawn by its own Dear ImGui, headless (no backend, no hook, invariant 16),
//     its atlas registered once as its own texture and each frame turned into one GraceUI list by graceui_imgui.hpp.
//
// tools/grace-harness loads it, presses F11 and reads what it drew back from the game's frame (hello_debug_view.h).
#include <windows.h>

#include <cstdint>

#include "graceui_imgui.hpp"
#include "graceui_sys.hpp"
#include "hello_debug_view.h"
#include "imgui.h"

namespace {

constexpr float kWidth = HELLO_DEBUG_WIDTH;
constexpr float kHeight = HELLO_DEBUG_HEIGHT;

ImVec4 colour(unsigned r, unsigned g, unsigned b) {
  return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, 1.0f);
}

void banner(const graceui::Api& api, GraceUiConsumer me, const char* text) {
  api.notice(me, text, GRACEUI_TONE_NORMAL, HELLO_DEBUG_BANNER_KEY, 0);
}

// One frame of the view: a window filling the panel, with a few lines of invented figures.
void build(graceui::imgui::DrawList& list, unsigned frame, unsigned toggles) {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(kWidth, kHeight);  // the panel's own pixels
  io.DeltaTime = 0.01f;
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(kWidth, kHeight));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, colour(HELLO_DEBUG_GROUND_R, HELLO_DEBUG_GROUND_G, HELLO_DEBUG_GROUND_B));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("hello-debug-view", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoInputs);
  ImGui::TextUnformatted("Hello debug view");
  ImGui::Separator();
  ImGui::Text("Frames built: %u", frame);
  ImGui::Text("Times opened: %u", (toggles + 1) / 2);
  ImGui::TextUnformatted("Every figure here is invented.");
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  ImGui::Render();
  list.build(*ImGui::GetDrawData(), kWidth, kHeight);
}

DWORD WINAPI debug_view_worker(LPVOID) {
  const graceui::Api api = graceui::Api::find(GRACEUI_SYS_LOAD, GRACEUI_SYS_TIMEOUT_MS);
  graceui::Mod mod;
  mod.id = HELLO_DEBUG_ID;
  mod.name = HELLO_DEBUG_NAME;
  mod.version = GRACEUI_VERSION_PACK(1, 0, 0);
  mod.floor = GRACEUI_VERSION_PACK(0, 1, 0);
  mod.caps = "notices panels textures hotkeys";
  GraceUiConnectAnswer answer{};
  if (!api || api.connect(mod, answer) != GRACEUI_OK) {
    // No GraceUI, or nothing can be drawn this session. In the anti-cheat's notice-only mode (inactive, with a handle)
    // only notices work, and the view stays closed.
    if (answer.status == GRACEUI_INACTIVE && answer.reason == GRACEUI_INACTIVE_ANTI_CHEAT) {
      banner(api, answer.consumer, HELLO_DEBUG_BANNER_NO_HOTKEYS);
    }
    return 0;
  }
  const GraceUiConsumer me = answer.consumer;
  std::uint32_t hotkey = 0;
  if (!api.has_cap(GRACEUI_CAP_HOTKEYS) || !api.has_cap(GRACEUI_CAP_PANELS) ||
      api.register_hotkey(me, HELLO_DEBUG_ACTION, "Open the debug view", HELLO_DEBUG_KEY, 0, hotkey) != GRACEUI_OK) {
    banner(api, me, HELLO_DEBUG_BANNER_NO_HOTKEYS);
    return 0;
  }
  banner(api, me, HELLO_DEBUG_BANNER_CLOSED);

  // Its own Dear ImGui, headless; its atlas registered in its registration window, before its first submit.
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  graceui::imgui::prepare(io);
  io.Fonts->AddFontDefaultBitmap();
  if (graceui::imgui::register_atlas(api, me, io) == 0) {
    return 0;
  }

  GraceUiPanelDesc desc{};
  desc.size = sizeof desc;
  desc.layer = GRACEUI_LAYER_PANEL;
  desc.anchor = GRACEUI_ANCHOR_BOTTOM_LEFT;
  desc.x = HELLO_DEBUG_X;
  desc.y = HELLO_DEBUG_Y;
  desc.width = kWidth;
  desc.height = kHeight;
  std::uint64_t panel = 0;
  if (api.create_panel(me, desc, panel) != GRACEUI_OK) {
    return 0;
  }

  graceui::imgui::DrawList list;
  bool open = false;
  unsigned toggles = 0;
  unsigned frames = 0;
  for (;;) {
    // Every press since the last look: each one toggles the view (D155's count).
    GraceUiEvent event{};
    GraceUiStatus status = GRACEUI_OK;
    while ((status = api.poll(me, event)) == GRACEUI_OK && event.kind != GRACEUI_EVENT_NONE) {
      if (event.kind == GRACEUI_EVENT_HOTKEY && event.hotkey_id == hotkey) {
        toggles += event.count;
        if ((event.count & 1u) != 0) {
          open = !open;
          if (open) {
            build(list, ++frames, toggles);
            api.submit(me, panel, list.view());  // its first list before it shows, so it never shows empty
            api.show_panel(me, panel);
            banner(api, me, HELLO_DEBUG_BANNER_OPEN);
          } else {
            api.hide_panel(me, panel);
            banner(api, me, HELLO_DEBUG_BANNER_CLOSED);
          }
        }
      }
    }
    if (status == GRACEUI_NOT_CONNECTED) {
      return 0;  // disconnected: nothing more to show
    }
    if (open) {
      build(list, ++frames, toggles);
      if (!list.empty()) {
        api.submit(me, panel, list.view());
      }
    }
    Sleep(10);
  }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    const HANDLE thread = CreateThread(nullptr, 0, &debug_view_worker, nullptr, 0, nullptr);  // runs once the loader is done
    if (thread != nullptr) {
      CloseHandle(thread);
    }
  }
  return TRUE;
}
