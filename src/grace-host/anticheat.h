// anticheat.h: GraceUI's anti-cheat check (D88, D133; EldenNet ARCH §5.3's two clauses), run once, in the first
// GraceUI_RequestApi, before any table is returned and before the init thread hooks anything. A hit puts the session in
// the notice-only mode (session.h). grace-abi's anticheat.h holds the names and their matching; this gathers what is
// matched: the process's modules and its parent's image name.
#pragma once

#include <cstdint>

namespace grace::host::anticheat {

enum class Clause : std::uint32_t {
  none = 0,
  module = 1,  // a module of the process whose file name starts with the anti-cheat's prefix
  parent = 2,  // the parent process's image is the anti-cheat's launcher
};

// Runs the check: the module clause first, then the parent clause, which is skipped if the parent has already exited.
// A mod's thread, in the first request: it reads the process's module list and a snapshot of the system's processes,
// and allocates.
Clause check() noexcept;

// The module clause alone, for the re-check while the browser view's server runs (§7.2 item 1, D81): a module of an
// anti-cheat loaded since the first request. GraceUI's own thread; it reads the module list and allocates.
bool module_loaded() noexcept;

#if defined(GRACEUI_TEST_EXPORTS)
// A test build compares these names instead, so that the harness simulates a hit with no anti-cheat software present.
// Before the first request, or later for the module re-check; null or an empty name keeps the real one.
void set_names(const wchar_t* module_prefix, const wchar_t* parent) noexcept;
#endif

}  // namespace grace::host::anticheat
