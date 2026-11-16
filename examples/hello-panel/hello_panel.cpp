// SPDX-License-Identifier: MIT
// hello_panel.cpp: the second example mod of our own (D85), in C++: a panel drawn with Dear ImGui's core, headless, at
// GraceUI's version (1.93.0), with no backend and no hook (D76, invariant 16). Everything about it is invented. It shows
// the shape a panel mod follows:
//
//   - Nothing in DllMain but starting a thread of its own; GraceUI is found and called from that thread (D70).
//   - In its registration window, before its first submit: a face for GraceUI's shared atlas (before the overlay's
//     warm-up), its own Dear ImGui atlas as a texture of its own (built once: no texture changes during play), and a
//     second texture.
//   - A panel, shown; then, every 10 ms, a frame of its own Dear ImGui turned into one draw list of GraceUI's: each
//     ImDrawList's vertices and indices appended, each command keeping its vertex offset, clip rectangles in the
//     panel's own pixels. Once the atlas is baked, a line laid out from the shared atlas's glyph table joins it.
//
// tools/grace-harness loads it and reads what it drew back from the game's frame (hello_panel.h).
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "graceui_sys.hpp"
#include "hello_panel.h"
#include "imgui.h"
#include "imgui_internal.h"  // ImFontAtlasBuildMain: a backend-less Dear ImGui builds its atlas once, itself

namespace {

static_assert(sizeof(ImDrawVert) == sizeof(GraceUiDrawVert) && sizeof(ImDrawIdx) == sizeof(std::uint16_t),
              "D76's layout is Dear ImGui's default one: no conversion is needed");

constexpr float kWidth = HELLO_PANEL_WIDTH;
constexpr float kHeight = HELLO_PANEL_HEIGHT;

HMODULE g_module = nullptr;

constexpr ImU32 rgba(unsigned r, unsigned g, unsigned b) {
  return (255u << IM_COL32_A_SHIFT) | (b << IM_COL32_B_SHIFT) | (g << IM_COL32_G_SHIFT) | (r << IM_COL32_R_SHIFT);
}

ImVec4 colour(unsigned r, unsigned g, unsigned b) {
  return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, 1.0f);
}

// Its face: Noto Sans Mono, beside this DLL in hello-panel\ (OFL; OFL.txt beside it).
std::vector<unsigned char> read_face() {
  std::vector<wchar_t> path(32768);
  DWORD length = GetModuleFileNameW(g_module, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) {
    return {};
  }
  while (length > 0 && path[length - 1] != L'\\' && path[length - 1] != L'/') {
    --length;
  }
  const wchar_t file[] = L"hello-panel\\NotoSansMono-Regular.ttf";
  path.resize(length);
  path.insert(path.end(), std::begin(file), std::end(file));
  const HANDLE handle = CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return {};
  }
  std::vector<unsigned char> data;
  LARGE_INTEGER size{};
  if (GetFileSizeEx(handle, &size) && size.QuadPart > 0 && size.QuadPart <= GRACEUI_FACE_BYTES_MAX) {
    data.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(handle, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) || read != data.size()) {
      data.clear();
    }
  }
  CloseHandle(handle);
  return data;
}

std::uint64_t register_rgba(const graceui::Api& api, GraceUiConsumer me, std::uint32_t width, std::uint32_t height,
                            const void* pixels) {
  GraceUiTextureDesc desc{};
  desc.size = sizeof desc;
  desc.format = GRACEUI_TEXTURE_RGBA8;
  desc.width = width;
  desc.height = height;
  desc.row_bytes = width * 4;
  desc.pixels = pixels;
  std::uint64_t id = 0;
  return api.register_texture(me, desc, id) == GRACEUI_OK ? id : 0;
}

// The glyph of `c` in a table GraceUI gave, or null: the table is sorted by code point.
const GraceUiGlyph* glyph_of(const GraceUiGlyphTable& table, std::uint32_t c) {
  const GraceUiGlyph* first = table.glyphs;
  const GraceUiGlyph* last = table.glyphs + table.glyph_count;
  const GraceUiGlyph* found =
      std::lower_bound(first, last, c, [](const GraceUiGlyph& glyph, std::uint32_t point) { return glyph.code_point < point; });
  return found != last && found->code_point == c ? found : nullptr;
}

// One frame of its own Dear ImGui, then its draw data as one list of GraceUI's.
struct Frame {
  std::vector<GraceUiDrawVert> vertices;
  std::vector<std::uint16_t> indices;
  std::vector<GraceUiDrawCommand> commands;
};

void build(Frame& frame, std::uint64_t checker, const GraceUiGlyphTable* glyphs) {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(kWidth, kHeight);  // the panel's own pixels: GraceUI puts it where its anchor says
  io.DeltaTime = 0.01f;
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(kWidth, kHeight));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, colour(HELLO_PANEL_GROUND_R, HELLO_PANEL_GROUND_G, HELLO_PANEL_GROUND_B));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(HELLO_TEXT_X, HELLO_TEXT_Y));
  ImGui::Begin("hello-panel", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoInputs);
  ImGui::TextUnformatted(HELLO_TEXT);
  if (checker != 0) {
    ImGui::SetCursorPos(ImVec2(HELLO_CHECKER_X, HELLO_CHECKER_Y));
    ImGui::Image(ImTextureRef(checker), ImVec2(HELLO_CHECKER_SIZE, HELLO_CHECKER_SIZE));
  }
  if (glyphs != nullptr) {
    // A line of its own layout, from GraceUI's shared atlas: one quad per glyph, all under one texture change.
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->PushTexture(ImTextureRef(glyphs->atlas));
    float pen = HELLO_GLYPHS_X;
    const ImU32 gold = rgba(HELLO_GLYPHS_R, HELLO_GLYPHS_G, HELLO_GLYPHS_B);
    for (const char* c = HELLO_GLYPHS; *c != '\0'; ++c) {
      const GraceUiGlyph* glyph = glyph_of(*glyphs, static_cast<unsigned char>(*c));
      if (glyph == nullptr) {
        continue;
      }
      if (glyph->x1 > glyph->x0) {
        list->PrimReserve(6, 4);
        list->PrimRectUV(ImVec2(pen + glyph->x0, HELLO_GLYPHS_Y + glyph->y0),
                         ImVec2(pen + glyph->x1, HELLO_GLYPHS_Y + glyph->y1), ImVec2(glyph->u0, glyph->v0),
                         ImVec2(glyph->u1, glyph->v1), gold);
      }
      pen += glyph->advance;
    }
    list->PopTexture();
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  ImGui::Render();

  // Every ImDrawList appended into one list: each command keeps its own vertex offset, its indices relative to it.
  frame.vertices.clear();
  frame.indices.clear();
  frame.commands.clear();
  const ImDrawData* data = ImGui::GetDrawData();
  for (const ImDrawList* list : data->CmdLists) {
    const std::uint32_t vertex_base = static_cast<std::uint32_t>(frame.vertices.size());
    const std::uint32_t index_base = static_cast<std::uint32_t>(frame.indices.size());
    frame.vertices.resize(vertex_base + static_cast<std::size_t>(list->VtxBuffer.Size));
    std::memcpy(frame.vertices.data() + vertex_base, list->VtxBuffer.Data,
                static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
    frame.indices.insert(frame.indices.end(), list->IdxBuffer.Data, list->IdxBuffer.Data + list->IdxBuffer.Size);
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
      if (cmd.UserCallback != nullptr || cmd.ElemCount == 0) {
        continue;
      }
      GraceUiDrawCommand command{};
      command.clip_x0 = std::clamp(cmd.ClipRect.x - data->DisplayPos.x, 0.0f, kWidth);
      command.clip_y0 = std::clamp(cmd.ClipRect.y - data->DisplayPos.y, 0.0f, kHeight);
      command.clip_x1 = std::clamp(cmd.ClipRect.z - data->DisplayPos.x, command.clip_x0, kWidth);
      command.clip_y1 = std::clamp(cmd.ClipRect.w - data->DisplayPos.y, command.clip_y0, kHeight);
      command.texture = cmd.GetTexID();
      command.vertex_offset = vertex_base + cmd.VtxOffset;
      command.vertex_count = std::min<std::uint32_t>(static_cast<std::uint32_t>(list->VtxBuffer.Size) - cmd.VtxOffset,
                                                     65536u);
      command.index_offset = index_base + cmd.IdxOffset;
      command.index_count = cmd.ElemCount;
      frame.commands.push_back(command);
    }
  }
}

DWORD WINAPI hello_worker(LPVOID) {
  GraceUiSysFound found{};
  found.size = sizeof found;
  const graceui::Api api = graceui::Api::find(GRACEUI_SYS_LOAD, GRACEUI_SYS_TIMEOUT_MS, &found);
  graceui::Mod mod;
  mod.id = HELLO_PANEL_ID;
  mod.name = HELLO_PANEL_NAME;
  mod.version = GRACEUI_VERSION_PACK(1, 0, 0);
  mod.floor = GRACEUI_VERSION_PACK(0, 1, 0);
  mod.caps = "panels textures faces";
  GraceUiConnectAnswer answer{};
  if (!api || !api.has_cap(GRACEUI_CAP_PANELS) || api.connect(mod, answer) != GRACEUI_OK) {
    return 0;  // no GraceUI with panels, or nothing can be drawn this session: this mod shows nothing
  }
  const GraceUiConsumer me = answer.consumer;

  // Its face, for the shared atlas: before warm-up, or never.
  const std::vector<unsigned char> font = read_face();
  std::uint32_t face = 0;
  if (!font.empty()) {
    GraceUiFaceDesc desc{};
    desc.size = sizeof desc;
    desc.pixel_height = HELLO_GLYPHS_PIXELS;
    desc.data = font.data();
    desc.data_length = font.size();  // the default ranges: Latin, Latin-1, Latin Extended-A, General Punctuation
    if (api.register_face(me, desc, face) != GRACEUI_OK) {
      face = 0;
    }
  }

  // Its own Dear ImGui, headless: no backend, no platform; its atlas built once and registered as its own texture.
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // GraceUI draws each command with its vertex offset
  io.Fonts->AddFontDefaultBitmap();
  ImFontAtlasBuildMain(io.Fonts);
  ImTextureData* atlas = io.Fonts->TexData;
  const std::uint64_t atlas_id = register_rgba(api, me, static_cast<std::uint32_t>(atlas->Width),
                                               static_cast<std::uint32_t>(atlas->Height), atlas->Pixels);
  if (atlas_id == 0) {
    return 0;
  }
  atlas->SetTexID(atlas_id);
  atlas->SetStatus(ImTextureStatus_OK);

  // Its checker, in two colours.
  std::vector<std::uint32_t> checker(HELLO_CHECKER_SIZE * HELLO_CHECKER_SIZE);
  for (int y = 0; y < HELLO_CHECKER_SIZE; ++y) {
    for (int x = 0; x < HELLO_CHECKER_SIZE; ++x) {
      const bool a = ((x / HELLO_CHECKER_SQUARE) + (y / HELLO_CHECKER_SQUARE)) % 2 == 0;
      checker[static_cast<std::size_t>(y) * HELLO_CHECKER_SIZE + x] =
          a ? rgba(HELLO_CHECKER_A_R, HELLO_CHECKER_A_G, HELLO_CHECKER_A_B)
            : rgba(HELLO_CHECKER_B_R, HELLO_CHECKER_B_G, HELLO_CHECKER_B_B);
    }
  }
  const std::uint64_t checker_id = register_rgba(api, me, HELLO_CHECKER_SIZE, HELLO_CHECKER_SIZE, checker.data());

  // Its panel, shown.
  GraceUiPanelDesc panel_desc{};
  panel_desc.size = sizeof panel_desc;
  panel_desc.layer = GRACEUI_LAYER_PANEL;
  panel_desc.anchor = GRACEUI_ANCHOR_TOP_LEFT;
  panel_desc.x = HELLO_PANEL_X;
  panel_desc.y = HELLO_PANEL_Y;
  panel_desc.width = kWidth;
  panel_desc.height = kHeight;
  std::uint64_t panel = 0;
  if (api.create_panel(me, panel_desc, panel) != GRACEUI_OK || api.show_panel(me, panel) != GRACEUI_OK) {
    return 0;
  }

  // Its frames, from this thread: the first submit ends its registration window.
  GraceUiGlyphTable glyphs{};
  bool have_glyphs = false;
  Frame frame;
  for (;;) {
    if (face != 0 && !have_glyphs) {
      have_glyphs = api.glyph_table(me, face, glyphs) == GRACEUI_OK;  // GRACEUI_NOT_READY until warm-up
    }
    build(frame, checker_id, have_glyphs ? &glyphs : nullptr);
    GraceUiDrawList list{};
    list.size = sizeof list;
    list.atlas_generation = have_glyphs ? glyphs.generation : 0;
    list.vertex_count = static_cast<std::uint32_t>(frame.vertices.size());
    list.index_count = static_cast<std::uint32_t>(frame.indices.size());
    list.command_count = static_cast<std::uint32_t>(frame.commands.size());
    list.command_size = sizeof(GraceUiDrawCommand);
    list.vertices = frame.vertices.data();
    list.indices = frame.indices.data();
    list.commands = frame.commands.data();
    if (api.submit(me, panel, list) == GRACEUI_NOT_CONNECTED) {
      return 0;
    }
    Sleep(10);
  }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = module;
    DisableThreadLibraryCalls(module);
    const HANDLE thread = CreateThread(nullptr, 0, &hello_worker, nullptr, 0, nullptr);  // runs once the loader is done
    if (thread != nullptr) {
      CloseHandle(thread);
    }
  }
  return TRUE;
}
