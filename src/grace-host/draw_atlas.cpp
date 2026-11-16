// draw_atlas.cpp: the shared atlas's faces (draw_atlas.h; §3.16, D80, D117).
//
// SAFETY: the casts here give GetModuleHandleExW an address to find graceui.dll by (a function of this module), and hand
// Dear ImGui a font's data as the non-const pointer its API takes: with FontDataOwnedByAtlas off it neither writes nor
// frees it, and the data (GraceUI's own file, read by the init thread, or a mod's copy, which the registry keeps for the
// session once its table is sealed) outlives the atlas, which is never destroyed. The glyph tables handed to mods point
// at vectors made once at warm-up and never changed or freed.
#include "draw_atlas.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <type_traits>
#include <vector>

#include "grace/abi/registry.h"
#include "imgui.h"
#include "imgui_internal.h"  // ImFontAtlasBuildMain, ImFontBaked's glyphs
#include "log.h"
#include "session.h"

namespace grace::host::atlas {
namespace {

static_assert(std::is_same_v<ImWchar, std::uint32_t>,
              "Dear ImGui's characters are 32-bit (grace_imconfig.h), so a face's ranges reach it as they are");

constexpr std::uint32_t kMaxTables = 1 + grace::abi::kMaxFaces;
constexpr DWORD kPathUnits = 32768;
constexpr wchar_t kNoticeFile[] = L"fonts\\NotoSans-Regular.ttf";
constexpr char kNoticeFileName[] = "fonts\\NotoSans-Regular.ttf";  // as logged: relative to GraceUI's folder (PV5)

struct Table {
  std::uint32_t face_id;
  float pixel_height;
  float ascent;
  float descent;
  float line_gap;
  const std::vector<GraceUiGlyph>* glyphs;
};

constinit std::vector<unsigned char>* g_notice_data = nullptr;  // the init thread's, then read-only
constinit std::atomic<bool> g_notice_loaded{false};
constinit Table g_tables[kMaxTables] = {};
constinit std::uint32_t g_table_count = 0;
constinit std::atomic<std::uint32_t> g_generation{0};
constinit ImFont* g_notice_font = nullptr;
constinit bool g_notice_grace = false;

// GraceUI's folder (the folder of graceui.dll), with its last separator.
bool own_folder(std::vector<wchar_t>& path, DWORD& length) {
  HMODULE module = nullptr;
  // SAFETY: the address of a function in this module, read only to find the module; no reference is taken.
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&own_folder), &module)) {
    return false;
  }
  path.assign(kPathUnits, L'\0');
  length = GetModuleFileNameW(module, path.data(), kPathUnits);
  if (length == 0 || length >= kPathUnits) {
    return false;
  }
  while (length > 0 && path[length - 1] != L'\\' && path[length - 1] != L'/') {
    --length;
  }
  return length > 0;
}

// The whole file, if it is no larger than a face may be.
bool read_font_file(const wchar_t* path, std::vector<unsigned char>& out) {
  const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
            static_cast<std::uint64_t>(size.QuadPart) <= grace::abi::kFaceBytesMax;
  if (ok) {
    out.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size();
  }
  CloseHandle(file);
  return ok;
}

ImFontConfig face_config() {
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;  // the data is GraceUI's or the registry's, kept for the session
  config.OversampleH = 2;
  config.OversampleV = 1;
  return config;
}

// A face's glyph table, made once from its bake at its own size.
void add_table(ImFont* font, std::uint32_t face_id) {
  if (font == nullptr || g_table_count >= kMaxTables) {
    return;
  }
  ImFontBaked* baked = font->GetFontBaked(font->LegacySize);
  if (baked == nullptr) {
    return;
  }
  auto* glyphs = new std::vector<GraceUiGlyph>();
  glyphs->reserve(static_cast<std::size_t>(baked->Glyphs.Size));
  for (const ImFontGlyph& glyph : baked->Glyphs) {
    glyphs->push_back(GraceUiGlyph{glyph.Codepoint, glyph.AdvanceX, glyph.X0, glyph.Y0, glyph.X1, glyph.Y1, glyph.U0,
                                   glyph.V0, glyph.U1, glyph.V1});
  }
  std::stable_sort(glyphs->begin(), glyphs->end(),
                   [](const GraceUiGlyph& a, const GraceUiGlyph& b) { return a.code_point < b.code_point; });
  glyphs->erase(std::unique(glyphs->begin(), glyphs->end(),
                            [](const GraceUiGlyph& a, const GraceUiGlyph& b) { return a.code_point == b.code_point; }),
                glyphs->end());
  Table& table = g_tables[g_table_count++];
  table.face_id = face_id;
  table.pixel_height = baked->Size;
  table.ascent = baked->Ascent;
  table.descent = baked->Descent;
  table.line_gap = std::max(0.0f, baked->Size - (baked->Ascent - baked->Descent));
  table.glyphs = glyphs;
}

}  // namespace

void load_own_fonts() noexcept {
  try {
    std::vector<wchar_t> path;
    DWORD folder = 0;
    if (!own_folder(path, folder)) {
      log_line("GraceUI's own folder could not be named: notices use Dear ImGui's own font");
      return;
    }
    path.resize(folder);
    path.insert(path.end(), std::begin(kNoticeFile), std::end(kNoticeFile));  // with its terminating NUL
    auto* data = new std::vector<unsigned char>();
    if (!read_font_file(path.data(), *data) || !grace::abi::valid_font(data->data(), data->size())) {
      delete data;
      log_line("%s is missing or not a font: notices use Dear ImGui's own font", kNoticeFileName);
      return;
    }
    g_notice_data = data;
    g_notice_loaded.store(true, std::memory_order_release);
  } catch (...) {
    log_line("GraceUI's own fonts could not be read (an exception): notices use Dear ImGui's own font");
  }
}

void bake(ImFontAtlas* fonts) {
  grace::abi::Registry& registry = session::registry();
  registry.seal_faces();  // from now on register_face answers too late (D80)

  // GraceUI's own face first: Dear ImGui's default font is the first added.
  if (g_notice_loaded.load(std::memory_order_acquire)) {
    const ImFontConfig config = face_config();
    // SAFETY: Dear ImGui reads the data and keeps the pointer; it never writes or frees it (FontDataOwnedByAtlas).
    g_notice_font = fonts->AddFontFromMemoryTTF(g_notice_data->data(), static_cast<int>(g_notice_data->size()),
                                                kNoticeFacePixels, &config, grace::abi::default_ranges());
    g_notice_grace = g_notice_font != nullptr;
  }
  if (g_notice_font == nullptr) {
    g_notice_font = fonts->AddFontDefaultBitmap();
  }

  // The mods' faces, in registration order. A file Dear ImGui cannot parse is a mod's error, not GraceUI's: while they
  // are added, its recoverable errors are logged and answered rather than thrown (D121 stays for everything else).
  ImFont* mod_fonts[grace::abi::kMaxFaces] = {};
  std::uint32_t mod_ids[grace::abi::kMaxFaces] = {};
  std::uint32_t mod_count = 0;
  ImGuiIO& io = ImGui::GetIO();
  const bool asserts = io.ConfigErrorRecoveryEnableAssert;
  io.ConfigErrorRecoveryEnableAssert = false;
  grace::abi::FaceData face;
  for (std::uint32_t i = 0; registry.face(i, face) && mod_count < grace::abi::kMaxFaces; ++i) {
    const ImFontConfig config = face_config();
    const std::uint32_t* ranges = face.ranges->empty() ? grace::abi::default_ranges() : face.ranges->data();
    // SAFETY: as above; the registry keeps the face's data and ranges for the session once its table is sealed.
    ImFont* font = fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(face.data->data()),
                                               static_cast<int>(face.data->size()), face.pixel_height, &config,
                                               ranges);
    if (font == nullptr) {
      log_line("face %u could not be loaded from its mod's file: it is left out of the atlas", face.id);
      continue;
    }
    mod_fonts[mod_count] = font;
    mod_ids[mod_count] = face.id;
    ++mod_count;
  }
  io.ConfigErrorRecoveryEnableAssert = asserts;

  ImFontAtlasBuildMain(fonts);  // static mode (D117): every glyph of every range baked now, at each face's own size
  add_table(g_notice_font, kNoticeFace);
  for (std::uint32_t i = 0; i < mod_count; ++i) {
    add_table(mod_fonts[i], mod_ids[i]);
  }
  g_generation.store(1, std::memory_order_release);
  log_line("the atlas holds %u faces: %s and %u of mods'", g_table_count,
           g_notice_grace ? "GraceUI's notices' face" : "Dear ImGui's own font for notices", mod_count);
}

std::uint32_t generation() noexcept { return g_generation.load(std::memory_order_acquire); }

bool glyph_table(std::uint32_t face_id, GraceUiGlyphTable& out) noexcept {
  if (g_generation.load(std::memory_order_acquire) == 0) {
    return false;
  }
  for (std::uint32_t i = 0; i < g_table_count; ++i) {
    const Table& table = g_tables[i];
    if (table.face_id == face_id) {
      out.generation = 1;
      out.face_id = face_id;
      out.glyph_count = static_cast<std::uint32_t>(table.glyphs->size());
      out.pixel_height = table.pixel_height;
      out.ascent = table.ascent;
      out.descent = table.descent;
      out.line_gap = table.line_gap;
      out.atlas = grace::abi::kAtlasTexture;
      out.glyphs = table.glyphs->data();
      return true;
    }
  }
  return false;
}

ImFont* notice_font() noexcept { return g_notice_font; }
bool notice_face_is_grace() noexcept { return g_generation.load(std::memory_order_acquire) != 0 && g_notice_grace; }
std::uint32_t faces_baked() noexcept { return g_generation.load(std::memory_order_acquire) != 0 ? g_table_count : 0; }

}  // namespace grace::host::atlas
