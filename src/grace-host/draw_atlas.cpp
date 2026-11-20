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
#include <cmath>
#include <iterator>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "grace/abi/registry.h"
#include "grace/tokens.h"
#include "grace/view/text.h"
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

// The menu's files (§3.4, D167), by family and weight; Cinzel publishes no static 500 or 600, so Regular and Bold serve.
enum MenuFile : std::uint8_t {
  noto_regular, noto_semibold, noto_mono, cinzel_regular, cinzel_bold, garamond_regular, garamond_medium,
  garamond_semibold, kMenuFiles
};
constexpr const wchar_t* kMenuPaths[kMenuFiles] = {
    L"fonts\\NotoSans-Regular.ttf",    L"fonts\\NotoSans-SemiBold.ttf",   L"fonts\\NotoSansMono-Regular.ttf",
    L"fonts\\Cinzel-Regular.ttf",      L"fonts\\Cinzel-Bold.ttf",         L"fonts\\EBGaramond-Regular.ttf",
    L"fonts\\EBGaramond-Medium.ttf",   L"fonts\\EBGaramond-SemiBold.ttf"};
constexpr const char* kMenuNames[kMenuFiles] = {  // as logged: relative to GraceUI's folder (PV5)
    "fonts\\NotoSans-Regular.ttf",    "fonts\\NotoSans-SemiBold.ttf",   "fonts\\NotoSansMono-Regular.ttf",
    "fonts\\Cinzel-Regular.ttf",      "fonts\\Cinzel-Bold.ttf",         "fonts\\EBGaramond-Regular.ttf",
    "fonts\\EBGaramond-Medium.ttf",   "fonts\\EBGaramond-SemiBold.ttf"};

MenuFile file_of(const grace::tokens::Type& type) noexcept {
  using grace::tokens::Font;
  switch (type.font) {
    case Font::display: return type.weight >= 600 ? cinzel_bold : cinzel_regular;
    case Font::serif: return type.weight >= 600 ? garamond_semibold : type.weight >= 500 ? garamond_medium : garamond_regular;
    case Font::sans: return type.weight >= 600 ? noto_semibold : noto_regular;
    case Font::mono: return noto_mono;
  }
  return noto_regular;
}

// The menu's characters: the default ranges and the arrows of its key hints and Related links, which Noto Sans and
// Cinzel lack: those faces take the four arrows from Noto Sans Mono, merged in (EB Garamond and the mono have them).
constexpr ImWchar kMenuRanges[] = {0x0020, 0x007E, 0x00A0, 0x00FF, 0x0100, 0x017F, 0x2000, 0x206F,
                                   0x2190, 0x2193, 0xFFFD, 0xFFFD, 0};
constexpr ImWchar kArrowRanges[] = {0x2190, 0x2193, 0};

bool has_arrows(MenuFile file) noexcept {
  return file == noto_mono || file == garamond_regular || file == garamond_medium || file == garamond_semibold;
}

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
constinit std::vector<unsigned char>* g_menu_data[kMenuFiles] = {};  // the init thread's, then read-only
constinit std::atomic<bool> g_menu_loaded{false};
constinit grace::view::Fonts* g_menu_fonts = nullptr;  // made at the bake, never changed or freed after it
constinit grace::view::Face* g_menu_faces = nullptr;   // kTypeCount of them
constinit float g_white_u = 0.0f;
constinit float g_white_v = 0.0f;
constinit float g_menu_scale = 0.0f;

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

// A menu face from its bake, for grace-view's text layout: its glyphs copied once, sorted by code point.
void make_menu_face(ImFont* font, grace::view::Face& face) {
  ImFontBaked* baked = font != nullptr ? font->GetFontBaked(font->LegacySize) : nullptr;
  if (baked == nullptr) {
    return;
  }
  auto* glyphs = new std::vector<grace::view::Glyph>();
  glyphs->reserve(static_cast<std::size_t>(baked->Glyphs.Size));
  for (const ImFontGlyph& glyph : baked->Glyphs) {
    glyphs->push_back(grace::view::Glyph{glyph.Codepoint, glyph.AdvanceX, glyph.X0, glyph.Y0, glyph.X1, glyph.Y1,
                                         glyph.U0, glyph.V0, glyph.U1, glyph.V1});
  }
  std::stable_sort(glyphs->begin(), glyphs->end(), [](const grace::view::Glyph& a, const grace::view::Glyph& b) {
    return a.code_point < b.code_point;
  });
  glyphs->erase(std::unique(glyphs->begin(), glyphs->end(),
                            [](const grace::view::Glyph& a, const grace::view::Glyph& b) {
                              return a.code_point == b.code_point;
                            }),
                glyphs->end());
  face.pixel_height = baked->Size;
  face.ascent = baked->Ascent;
  face.descent = baked->Descent;
  face.texture = grace::abi::kAtlasTexture;
  face.glyphs = std::span<const grace::view::Glyph>(glyphs->data(), glyphs->size());
}

void load_menu_fonts(const std::vector<wchar_t>& folder_path, DWORD folder) {
  bool any = false;
  for (int i = 0; i < kMenuFiles; ++i) {
    std::vector<wchar_t> path(folder_path.begin(), folder_path.begin() + folder);
    const std::wstring_view name(kMenuPaths[i]);
    path.insert(path.end(), name.begin(), name.end());
    path.push_back(L'\0');
    auto* data = new std::vector<unsigned char>();
    if (!read_font_file(path.data(), *data) || !grace::abi::valid_font(data->data(), data->size())) {
      delete data;
      log_line("%s is missing or not a font: the menu draws its roles in Noto Sans Regular", kMenuNames[i]);
      continue;
    }
    g_menu_data[i] = data;
    any = true;
  }
  g_menu_loaded.store(any, std::memory_order_release);
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
    load_menu_fonts(path, folder);
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

#if defined(GRACEUI_TEST_EXPORTS)
namespace {
constinit std::atomic<std::uint32_t> g_test_limit{0};
}  // namespace
void test_set_limit(std::uint32_t side) noexcept { g_test_limit.store(side, std::memory_order_relaxed); }
#endif

void bake(ImFontAtlas* fonts, std::uint32_t mod_faces, float menu_scale, float content_scale) {
  grace::abi::Registry& registry = session::registry();  // sealed by the present thread before this runs (D80)
  // A fresh start: an earlier attempt that failed left nothing GraceUI keeps.
  g_table_count = 0;
  g_notice_font = nullptr;
  g_notice_grace = false;
#if defined(GRACEUI_TEST_EXPORTS)
  if (const std::uint32_t side = g_test_limit.load(std::memory_order_relaxed); side != 0) {
    fonts->TexMaxWidth = static_cast<int>(side);
    fonts->TexMaxHeight = static_cast<int>(side);
  }
#endif

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

  // The menu's faces (D167): one for each type role, at its size times the effective scale, each from its file, or from
  // Noto Sans Regular where its own is missing; left out when the menu's faces are not asked for or not loaded.
  ImFont* menu_fonts[grace::tokens::kTypeCount] = {};
  const bool menu = menu_scale > 0.0f && g_menu_loaded.load(std::memory_order_acquire) &&
                    (g_menu_data[noto_regular] != nullptr || g_notice_loaded.load(std::memory_order_acquire));
  if (menu) {
    for (int role = 0; role < grace::tokens::kTypeCount; ++role) {
      const grace::tokens::Type& type = grace::tokens::kTypes[role];
      MenuFile file = file_of(type);
      const std::vector<unsigned char>* data = g_menu_data[file];
      if (data == nullptr) {
        data = g_menu_data[noto_regular] != nullptr ? g_menu_data[noto_regular] : g_notice_data;
        file = noto_regular;
      }
      const ImFontConfig config = face_config();
      const float pixels = std::clamp(type.size * menu_scale, 6.0f, 128.0f);
      // SAFETY: as above: GraceUI's own file, read by the init thread and kept for the session.
      menu_fonts[role] = fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data->data()),
                                                     static_cast<int>(data->size()), pixels, &config, kMenuRanges);
      if (const std::vector<unsigned char>* mono = g_menu_data[noto_mono];
          menu_fonts[role] != nullptr && !has_arrows(file) && mono != nullptr) {
        ImFontConfig merge = face_config();
        merge.MergeMode = true;
        // SAFETY: as above.
        fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(mono->data()), static_cast<int>(mono->size()), pixels,
                                    &merge, kArrowRanges);
      }
    }
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
  std::uint32_t i = 0;
  for (; i < mod_faces && registry.face(i, face) && mod_count < grace::abi::kMaxFaces; ++i) {
    const ImFontConfig config = face_config();
    const std::uint32_t* ranges = face.ranges->empty() ? grace::abi::default_ranges() : face.ranges->data();
    // SAFETY: as above; the registry keeps the face's data and ranges for the session once its table is sealed.
    // A scaled face at its pixel height times the content scale (D180); any other at its pixel height.
    const float pixels = face.scaled ? std::clamp(std::round(face.pixel_height * content_scale),
                                                  grace::abi::kFacePixelsMin, grace::abi::kFacePixelsMax)
                                     : face.pixel_height;
    ImFont* font = fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(face.data->data()),
                                               static_cast<int>(face.data->size()), pixels, &config, ranges);
    if (font == nullptr) {
      log_line("face %u could not be loaded from its mod's file: it is left out of the atlas", face.id);
      continue;
    }
    mod_fonts[mod_count] = font;
    mod_ids[mod_count] = face.id;
    ++mod_count;
  }
  io.ConfigErrorRecoveryEnableAssert = asserts;
  if (registry.face(i, face)) {
    log_line("the shared atlas leaves out the mods' faces from face %u on: they did not fit in one bake", face.id);
  }

  ImFontAtlasBuildMain(fonts);  // static mode (D117): every glyph of every range baked now, at each face's own size
  add_table(g_notice_font, kNoticeFace);
  for (std::uint32_t i = 0; i < mod_count; ++i) {
    add_table(mod_fonts[i], mod_ids[i]);
  }
  g_white_u = fonts->TexUvWhitePixel.x;
  g_white_v = fonts->TexUvWhitePixel.y;
  if (menu) {
    if (g_menu_faces == nullptr) {
      g_menu_faces = new grace::view::Face[grace::tokens::kTypeCount]();
      g_menu_fonts = new grace::view::Fonts();
    }
    bool whole = true;
    for (int role = 0; role < grace::tokens::kTypeCount; ++role) {
      g_menu_faces[role] = grace::view::Face{};
      make_menu_face(menu_fonts[role], g_menu_faces[role]);
      whole = whole && !g_menu_faces[role].glyphs.empty();
      g_menu_fonts->by_role[static_cast<std::size_t>(role)] = &g_menu_faces[role];
    }
    g_menu_fonts->generation = 1;
    g_menu_scale = whole ? menu_scale : 0.0f;
    if (!whole) {
      log_line("the menu's faces could not all be baked: the menu cannot open this session");
    }
  }
  g_generation.store(1, std::memory_order_release);
  log_line("the atlas holds %s, %s and %u of the mods' faces",
           g_notice_grace ? "GraceUI's notices' face" : "Dear ImGui's own font for notices",
           g_menu_scale > 0.0f ? "the menu's eleven" : "none of the menu's", mod_count);
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

bool own_folder(std::wstring& out) noexcept {
  try {
    std::vector<wchar_t> path;
    DWORD length = 0;
    if (!own_folder(path, length)) {
      return false;
    }
    out.assign(path.data(), length);
    return true;
  } catch (...) {
    return false;
  }
}

const grace::view::Fonts* menu_fonts() noexcept {
  return g_generation.load(std::memory_order_acquire) != 0 && g_menu_scale > 0.0f ? g_menu_fonts : nullptr;
}

void white_pixel(float& u, float& v) noexcept {
  u = g_white_u;
  v = g_white_v;
}

float menu_scale() noexcept { return g_generation.load(std::memory_order_acquire) != 0 ? g_menu_scale : 0.0f; }

}  // namespace grace::host::atlas
