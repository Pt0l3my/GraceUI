// steam.cpp: Steam's floating keyboard by the flat API's C exports of the already-loaded steam_api64.dll (steam.h,
// §5.6, D275, D276). Resolved once, at the first show: the module by name (never loaded by GraceUI, and pinned once
// found, so that its functions stay where they were found), ISteamUtils's newest accessor it exports
// (SteamAPI_SteamUtils_v0NN, the interface that has the floating keyboard being v010 or later), and the two functions.
// One lock (an SRW lock: nothing allocated, nothing to destroy) keeps GraceUI's thread and a mod's thread apart; the
// present thread never comes here.
// SAFETY: this file turns GetProcAddress's answers into the exports' C types; it is one of the files D110 allows for it
// (tools/check_safety.py).
#include "steam.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>

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

constexpr std::uint32_t kNobody = 0xFFFFFFFFu;

SRWLOCK g_lock = SRWLOCK_INIT;
constinit std::array<wchar_t, 64> g_module_name{};  // a test build's stand-in; empty: Steam's own
constinit Exports g_exports{};
constinit bool g_resolved = false;
constinit std::uint32_t g_said = 0;  // the log lines already written, one each (bits)
constinit std::atomic<std::uint32_t> g_owner{kNobody};  // whom GraceUI opened the keyboard for, and has not closed it

// Under the lock.
class Locked {
 public:
  Locked() noexcept { AcquireSRWLockExclusive(&g_lock); }
  ~Locked() { ReleaseSRWLockExclusive(&g_lock); }
  Locked(const Locked&) = delete;
  Locked& operator=(const Locked&) = delete;
};

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
  if (accessor == nullptr || show == nullptr || dismiss == nullptr) {
    // Without Dismiss GraceUI could open a keyboard it could not close when the field or its holder goes.
    say_once(2, "Steam's floating keyboard: this Steam library has none, so a pad's text fields have none");
    return;
  }
  // SAFETY: each address is the named export of Steam's flat C API, whose C signature these types repeat.
  g_exports.utils = reinterpret_cast<Accessor>(reinterpret_cast<void*>(accessor));
  // SAFETY: as above.
  g_exports.show = reinterpret_cast<Show>(reinterpret_cast<void*>(show));
  // SAFETY: as above.
  g_exports.dismiss = reinterpret_cast<Dismiss>(reinterpret_cast<void*>(dismiss));
}

// Under the lock: closes the keyboard GraceUI opened, whoever for.
void close_locked() noexcept {
  if (g_owner.exchange(kNobody, std::memory_order_acq_rel) == kNobody) {
    return;
  }
  if (g_exports.dismiss != nullptr) {
    if (void* const utils = g_exports.utils(); utils != nullptr) {
      g_exports.dismiss(utils);
    }
  }
}

}  // namespace

bool show_keyboard(std::uint32_t owner, KeyboardMode mode, std::int32_t x, std::int32_t y, std::int32_t w,
                   std::int32_t h) noexcept {
  const Locked locked;
  resolve();
  if (g_exports.show == nullptr) {
    return false;
  }
  close_locked();  // one keyboard at a time: another owner's goes first
  void* const utils = g_exports.utils();
  if (utils == nullptr) {
    say_once(4, "Steam's floating keyboard: Steam is not started in this game, so a pad's text fields have none");
    return false;
  }
  const bool shown = g_exports.show(utils, static_cast<int>(mode), x, y, w, h);
  if (shown) {
    g_owner.store(owner, std::memory_order_release);
    say_once(8, owner == kMenu ? "Steam's floating keyboard opened for a text field of the menu's on a pad"
                               : "Steam's floating keyboard opened for a mod's field on a pad");
  } else {
    say_once(16, "Steam's floating keyboard: Steam declined it (it opens on a Steam Deck or in Big Picture)");
  }
  return shown;
}

void dismiss_keyboard(std::uint32_t owner) noexcept {
  if (g_owner.load(std::memory_order_acquire) != owner) {
    return;  // none, or another owner's
  }
  const Locked locked;
  if (g_owner.load(std::memory_order_acquire) == owner) {
    close_locked();
  }
}

bool open_for_a_mod() noexcept {
  const std::uint32_t owner = g_owner.load(std::memory_order_acquire);
  return owner != kNobody && owner != kMenu;
}

void follow_holder(std::uint32_t holder_owner, bool mod_holds) noexcept {
  const std::uint32_t owner = g_owner.load(std::memory_order_acquire);
  if (owner == kNobody || owner == kMenu || (mod_holds && holder_owner == owner)) {
    return;  // none, the menu's (its own thread closes it), or its mod still holds input
  }
  dismiss_keyboard(owner);
}

#if defined(GRACEUI_TEST_EXPORTS)
void set_module_name(const wchar_t* name) noexcept {
  const Locked locked;
  close_locked();
  g_module_name.fill(L'\0');
  for (std::size_t i = 0; name != nullptr && name[i] != L'\0' && i + 1 < g_module_name.size(); ++i) {
    g_module_name[i] = name[i];
  }
  g_resolved = false;  // looked for again, by the new name
  g_exports = Exports{};
  g_said = 0;
}
#endif

}  // namespace grace::host::steam
