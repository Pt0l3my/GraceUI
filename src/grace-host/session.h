// session.h: what a session of GraceUI is to its mods (D73, D88, D131, D133): whether it is active or in the anti-cheat's
// notice-only mode, the mods connected (grace-abi's Consumers) and the notices showing (grace-abi's Notices), with
// GraceUI's own among them. The DLL's abi files reach it through host.h's abi1 functions; the render path reads the
// notices on the present thread through Notices::read, which never waits.
#pragma once

#include <cstdint>

#include "anticheat.h"
#include "grace/abi/consumers.h"
#include "grace/abi/notices.h"

namespace grace::host::session {

grace::abi::Consumers& consumers() noexcept;
grace::abi::Notices& notices() noexcept;

// The anti-cheat check found `clause` (D88): from now on input is off for the session (every detour and the window
// filter pure pass-through, capture never on), nothing but notices is drawn, connect answers inactive (anti-cheat) and
// notice stays callable. GraceUI's own notice says so once, and its log in one line. Before the init thread starts.
void enter_notice_only(anticheat::Clause clause) noexcept;
bool notice_only() noexcept;
anticheat::Clause anti_cheat() noexcept;

// Why the session is inactive, if it is: the notice-only mode, a host that failed to start, or a render path that
// turned itself off.
grace::abi::Inactive inactive() noexcept;

// GraceUI's own notices (owner 0): shown for kOwnNoticeMs, `key` 0 for a new one each time.
inline constexpr std::uint32_t kOwnNoticeMs = 20000;  // EST
inline constexpr std::uint32_t kOwnKeyAntiCheat = 1;
void own_notice(std::uint32_t key, std::uint32_t tone, const char* format, ...) noexcept
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

}  // namespace grace::host::session
