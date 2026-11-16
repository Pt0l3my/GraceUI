// anticheat.cpp: the anti-cheat check's Win32 side (anticheat.h): the process's modules by EnumProcessModules and their
// file names, and the parent's image name from a Toolhelp snapshot of the processes. Nothing is opened in another
// process, and nothing is written. The matching is grace-abi's (pure, tested on Linux).
#include "anticheat.h"

#include <windows.h>

#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2  // EnumProcessModules and GetModuleBaseNameW as kernel32's K32 functions: no psapi.dll
#endif
#include <psapi.h>
#include <tlhelp32.h>

#include <atomic>
#include <string_view>
#include <vector>

#include "grace/abi/anticheat.h"
#include "log.h"

namespace grace::host::anticheat {
namespace {

constexpr std::size_t kNameUnits = MAX_PATH;

// A file name as UTF-16 units of our own: Windows' wchar_t is UTF-16, copied unit by unit (no cast of the buffer).
struct Name {
  char16_t units[kNameUnits] = {};
  std::size_t length = 0;

  void assign(const wchar_t* text, std::size_t count) noexcept {
    length = 0;
    for (; length < count && length < kNameUnits && text[length] != L'\0'; ++length) {
      units[length] = static_cast<char16_t>(text[length]);
    }
  }
  std::u16string_view view() const noexcept { return {units, length}; }
};

#if defined(GRACEUI_TEST_EXPORTS)
// The test build's names, written before the first request and read in it.
constinit Name g_test_prefix{};
constinit Name g_test_parent{};
constinit std::atomic<bool> g_test_names{false};
#endif

std::u16string_view module_prefix() noexcept {
#if defined(GRACEUI_TEST_EXPORTS)
  if (g_test_names.load(std::memory_order_acquire) && g_test_prefix.length != 0) {
    return g_test_prefix.view();
  }
#endif
  return grace::abi::kAntiCheatModulePrefix;
}

std::u16string_view parent_name() noexcept {
#if defined(GRACEUI_TEST_EXPORTS)
  if (g_test_names.load(std::memory_order_acquire) && g_test_parent.length != 0) {
    return g_test_parent.view();
  }
#endif
  return grace::abi::kAntiCheatParent;
}

bool module_clause() {
  const HANDLE process = GetCurrentProcess();
  std::vector<HMODULE> modules(512);
  DWORD needed = 0;
  for (;;) {
    const DWORD bytes = static_cast<DWORD>(modules.size() * sizeof(HMODULE));
    if (!EnumProcessModules(process, modules.data(), bytes, &needed)) {
      log_line("the anti-cheat check could not list the process's modules (error %lu)",
               static_cast<unsigned long>(GetLastError()));
      return false;
    }
    if (needed <= bytes) {
      modules.resize(needed / sizeof(HMODULE));
      break;
    }
    modules.resize(needed / sizeof(HMODULE) + 64);  // more were loaded meanwhile: room for them, and look again
  }
  wchar_t buffer[kNameUnits];
  Name name;
  for (const HMODULE module : modules) {
    const DWORD length = GetModuleBaseNameW(process, module, buffer, kNameUnits);
    if (length == 0) {
      continue;
    }
    name.assign(buffer, length);
    if (grace::abi::anti_cheat_module(name.view(), module_prefix())) {
      return true;
    }
  }
  return false;
}

bool parent_clause() noexcept {
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    log_line("the anti-cheat check could not see the parent process (error %lu): that clause is skipped",
             static_cast<unsigned long>(GetLastError()));
    return false;
  }
  const DWORD self = GetCurrentProcessId();
  DWORD parent = 0;
  bool found_self = false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof entry;
  for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
    if (entry.th32ProcessID == self) {
      parent = entry.th32ParentProcessID;
      found_self = true;
      break;
    }
  }
  bool hit = false;
  if (found_self && parent != 0) {
    // A parent that has exited is not in the snapshot: the clause is skipped (EldenNet ARCH §5.3). Its number may have
    // been reused by now, which can only err towards a hit.
    Name name;
    entry.dwSize = sizeof entry;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
      if (entry.th32ProcessID == parent) {
        name.assign(entry.szExeFile, kNameUnits);
        hit = grace::abi::anti_cheat_parent(name.view(), parent_name());
        break;
      }
    }
  }
  CloseHandle(snapshot);
  return hit;
}

}  // namespace

Clause check() noexcept {
  try {
    if (module_clause()) {
      return Clause::module;
    }
    if (parent_clause()) {
      return Clause::parent;
    }
  } catch (...) {
    log_line("the anti-cheat check failed (an exception): it found nothing");
  }
  return Clause::none;
}

#if defined(GRACEUI_TEST_EXPORTS)
void set_names(const wchar_t* module_prefix_text, const wchar_t* parent) noexcept {
  if (module_prefix_text != nullptr) {
    g_test_prefix.assign(module_prefix_text, kNameUnits);
  }
  if (parent != nullptr) {
    g_test_parent.assign(parent, kNameUnits);
  }
  g_test_names.store(true, std::memory_order_release);
}
#endif

}  // namespace grace::host::anticheat
