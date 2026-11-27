// steam.cpp: Steam's floating keyboard by the flat API's C exports of the already-loaded steam_api64.dll (steam.h,
// §5.6, D275). Resolved once, on GraceUI's own thread, at the first show: the module by name (never loaded by GraceUI,
// and pinned once found, so that its functions stay where they were found), ISteamUtils's newest accessor it exports
// (SteamAPI_SteamUtils_v0NN, the interface that has the floating keyboard being v010 or later), and the two functions.
// SAFETY: this file turns GetProcAddress's answers into the exports' C types; it is one of the files D110 allows for it
// (tools/check_safety.py).
#include "steam.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <cstddef>

#include "log.h"

namespace grace::host::steam {
namespace {

using Accessor = void* (*)();
using Show = bool (*)(void* utils, int mode, int x, int y, int w, int h);
using Dismiss = bool (*)(void* utils);

struct Exports {
  Accessor utils = nullptr;
  Show show = nullptr;
  Dismiss dismiss = nullptr;
};

constinit std::array<wchar_t, 64> g_module_name{};  // a test build's stand-in; empty: Steam's own
constinit Exports g_exports{};
constinit bool g_resolved = false;
constinit bool g_shown = false;  // GraceUI opened the keyboard and has not closed it
constinit std::uint32_t g_said = 0;  // the log lines already written, one each (bits)

void say_once(std::uint32_t bit, const char* line) noexcept {
  if ((g_said & bit) == 0) {
    g_said |= bit;
    log_line("%s", line);
  }
}

const wchar_t* module_name() noexcept { return g_module_name[0] != L'\0' ? g_module_name.data() : L"steam_api64.dll"; }

void resolve() noexcept {
  if (g_resolved) {
    return;
  }
  g_resolved = true;
  HMODULE module = nullptr;
  // Already loaded by the game, or nothing: GraceUI never loads it. Pinned, so that what is found stays.
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, module_name(), &module) == 0 || module == nullptr) {
    say_once(1, "Steam's floating keyboard: Steam's library is not loaded, so a pad's text fields have none");
    return;
  }
  FARPROC accessor = nullptr;
  for (int version = 30; version >= 10 && accessor == nullptr; --version) {
    char name[40];
    std::snprintf(name, sizeof name, "SteamAPI_SteamUtils_v%03d", version);
    accessor = GetProcAddress(module, name);
  }
  const FARPROC show = GetProcAddress(module, "SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput");
  const FARPROC dismiss = GetProcAddress(module, "SteamAPI_ISteamUtils_DismissFloatingGamepadTextInput");
  if (accessor == nullptr || show == nullptr) {
    say_once(2, "Steam's floating keyboard: this Steam library has none, so a pad's text fields have none");
    return;
  }
  // SAFETY: each address is the named export of Steam's flat C API, whose C signature these types repeat.
  g_exports.utils = reinterpret_cast<Accessor>(reinterpret_cast<void*>(accessor));
  // SAFETY: as above.
  g_exports.show = reinterpret_cast<Show>(reinterpret_cast<void*>(show));
  // SAFETY: as above; an older library may lack it (null), and then Steam closes its keyboard itself.
  g_exports.dismiss = dismiss != nullptr ? reinterpret_cast<Dismiss>(reinterpret_cast<void*>(dismiss)) : nullptr;
}

}  // namespace

bool show_keyboard(KeyboardMode mode, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) noexcept {
  resolve();
  if (g_exports.show == nullptr) {
    return false;
  }
  void* const utils = g_exports.utils();
  if (utils == nullptr) {
    say_once(4, "Steam's floating keyboard: Steam is not started in this game, so a pad's text fields have none");
    return false;
  }
  const bool shown = g_exports.show(utils, static_cast<int>(mode), x, y, w, h);
  g_shown = g_shown || shown;
  if (shown) {
    say_once(8, "Steam's floating keyboard opened for a text field on a pad");
  } else {
    say_once(16, "Steam's floating keyboard: Steam declined it (it opens on a Steam Deck or in Big Picture)");
  }
  return shown;
}

void dismiss_keyboard() noexcept {
  if (!g_shown) {
    return;
  }
  g_shown = false;
  if (g_exports.dismiss != nullptr) {
    if (void* const utils = g_exports.utils(); utils != nullptr) {
      g_exports.dismiss(utils);
    }
  }
}

#if defined(GRACEUI_TEST_EXPORTS)
void set_module_name(const wchar_t* name) noexcept {
  g_module_name.fill(L'\0');
  for (std::size_t i = 0; name != nullptr && name[i] != L'\0' && i + 1 < g_module_name.size(); ++i) {
    g_module_name[i] = name[i];
  }
}
#endif

}  // namespace grace::host::steam
