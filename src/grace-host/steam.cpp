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

#include "input.h"
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

constexpr std::uint64_t kNobody = ~std::uint64_t{0};

std::uint64_t who(std::uint32_t owner, std::uint32_t generation) noexcept {
  return (std::uint64_t{owner} << 32) | generation;
}
std::uint32_t owner_of(std::uint64_t holder) noexcept { return static_cast<std::uint32_t>(holder >> 32); }
std::uint32_t generation_of(std::uint64_t holder) noexcept { return static_cast<std::uint32_t>(holder); }

SRWLOCK g_lock = SRWLOCK_INIT;
constinit std::array<wchar_t, 64> g_module_name{};  // a test build's stand-in; empty: Steam's own
constinit Exports g_exports{};
constinit bool g_resolved = false;
constinit std::uint32_t g_said = 0;  // the log lines already written, one each (bits)
constinit std::atomic<std::uint64_t> g_for{kNobody};  // the holder GraceUI opened the keyboard for, and has not closed it

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

// Whether holder `generation` of `owner` (kMenu: GraceUI's menu) still holds input, active.
bool still_holds(std::uint32_t owner, std::uint32_t generation) noexcept {
  const grace::input::Holder holder = input::capture().holder();
  const bool menu = holder.kind == grace::input::Kind::menu;
  return holder.phase == grace::input::Phase::active && holder.generation == generation && holder.owner == owner &&
         menu == (owner == kMenu);
}

// Under the lock: closes the keyboard GraceUI opened, whoever for.
void close_locked() noexcept {
  if (g_for.exchange(kNobody, std::memory_order_seq_cst) == kNobody) {
    return;
  }
  if (g_exports.dismiss != nullptr) {
    if (void* const utils = g_exports.utils(); utils != nullptr) {
      g_exports.dismiss(utils);
    }
  }
}

}  // namespace

Outcome show_keyboard(std::uint32_t owner, std::uint32_t generation, KeyboardMode mode, std::int32_t x, std::int32_t y,
                      std::int32_t w, std::int32_t h) noexcept {
  const Locked locked;
  resolve();
  if (g_exports.show == nullptr) {
    return Outcome::unavailable;
  }
  if (!still_holds(owner, generation)) {
    return Outcome::gone;  // its holder ended before the lock was taken: nothing shown, nothing of another's closed
  }
  close_locked();  // one keyboard at a time: another holder's goes first
  void* const utils = g_exports.utils();
  if (utils == nullptr) {
    say_once(4, "Steam's floating keyboard: Steam is not started in this game, so a pad's text fields have none");
    return Outcome::unavailable;
  }
  if (!g_exports.show(utils, static_cast<int>(mode), x, y, w, h)) {
    say_once(16, "Steam's floating keyboard: Steam declined it (it opens on a Steam Deck or in Big Picture)");
    return Outcome::unavailable;
  }
  // Recorded, then the holder read again: a holder that ended during Steam's call either is seen here, or its close
  // (input.cpp's closed, which fences before it reads keyboard_open) sees the record and wakes GraceUI's thread.
  g_for.store(who(owner, generation), std::memory_order_seq_cst);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  if (!still_holds(owner, generation)) {
    close_locked();
    return Outcome::gone;
  }
  say_once(8, owner == kMenu ? "Steam's floating keyboard opened for a text field of the menu's on a pad"
                             : "Steam's floating keyboard opened for a mod's field on a pad");
  return Outcome::shown;
}

void dismiss_keyboard(std::uint32_t owner, std::uint32_t generation) noexcept {
  if (g_for.load(std::memory_order_acquire) != who(owner, generation)) {
    return;  // none, or another holder's
  }
  const Locked locked;
  if (g_for.load(std::memory_order_acquire) == who(owner, generation)) {
    close_locked();
  }
}

void dismiss_owner(std::uint32_t owner) noexcept {
  const std::uint64_t now = g_for.load(std::memory_order_acquire);
  if (now == kNobody || owner_of(now) != owner) {
    return;
  }
  const Locked locked;
  if (const std::uint64_t again = g_for.load(std::memory_order_acquire); again != kNobody && owner_of(again) == owner) {
    close_locked();
  }
}

bool keyboard_open() noexcept { return g_for.load(std::memory_order_seq_cst) != kNobody; }

void follow_holder() noexcept {
  const std::uint64_t now = g_for.load(std::memory_order_acquire);
  if (now == kNobody || still_holds(owner_of(now), generation_of(now))) {
    return;
  }
  if (TryAcquireSRWLockExclusive(&g_lock) == 0) {
    return;  // a call is in Steam: its own reading of the holder after it, or the next wake, closes it
  }
  if (const std::uint64_t again = g_for.load(std::memory_order_acquire);
      again != kNobody && !still_holds(owner_of(again), generation_of(again))) {
    close_locked();
  }
  ReleaseSRWLockExclusive(&g_lock);
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
