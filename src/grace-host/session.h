// session.h: what a session of GraceUI is to its mods (D73, D88, D131, D133): whether it is active or in the anti-cheat's
// notice-only mode, the mods connected (grace-abi's Consumers) and the notices showing (grace-abi's Notices), with
// GraceUI's own among them. The DLL's abi files reach it through host.h's abi1 functions; the render path reads the
// notices on the present thread through Notices::read, which never waits.
#pragma once

#include <cstdint>

#include <string_view>

#include "anticheat.h"
#include "grace/abi/consumers.h"
#include "grace/abi/notices.h"
#include "grace/abi/panels.h"
#include "grace/abi/present.h"
#include "grace/abi/quiet.h"
#include "grace/abi/registry.h"
#include "grace/abi/world.h"

namespace grace::host::session {

grace::abi::Consumers& consumers() noexcept;
grace::abi::Notices& notices() noexcept;
// S10.4.c's stores, made once by allocate() in the first request, before any table is returned, so that neither a mod's
// thread nor the present thread ever makes them (they hold vectors: no static object of graceui.dll may, D71).
grace::abi::Panels& panels() noexcept;
grace::abi::Registry& registry() noexcept;
grace::abi::World& world() noexcept;
// The panels' on_present callbacks (D259).
grace::abi::PresentHooks& present() noexcept;
// The mods whose callbacks were suspended this session, by id (D259): one that connects again stays suspended.
// suspend_present answers whether the id is newly kept.
bool suspend_present(std::string_view id) noexcept;
bool present_suspended(std::string_view id) noexcept;
// The mods whose notices the player quieted, and what they posted meanwhile (G32, D181).
grace::abi::QuietNotices& quiet() noexcept;
void allocate();

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
inline constexpr std::uint32_t kOwnKeyControls = 2;  // "‹Mod› has the controls" (D77)
inline constexpr std::uint32_t kOwnKeyMenu = 3;      // why the menu did not open (§5.1, D167)
inline constexpr std::uint32_t kControlsNoticeMs = 2000;  // as long as the player's exit waits for the second press
void own_notice(std::uint32_t key, std::uint32_t tone, const char* format, ...) noexcept
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

// One of GraceUI's own notices with a text made without formatting: callable from the window procedure's filter.
void own_notice_text(std::uint32_t key, std::uint32_t tone, std::uint32_t duration_ms, std::string_view text) noexcept;

// "‹Mod› has the controls" (D77, §5.1): the first press of the menu key while mod `owner` (or the menu, owner 0) holds
// input. The window procedure's filter calls it.
void controls_notice(std::uint32_t owner) noexcept;

// The input holder a mod's request_input made: its generation and its panel, so that hiding the panel or the mod's
// disconnect ends that holder and no later one.
void set_input_panel(std::uint32_t generation, std::uint64_t panel) noexcept;
bool input_panel(std::uint32_t& generation, std::uint64_t& panel) noexcept;

}  // namespace grace::host::session
