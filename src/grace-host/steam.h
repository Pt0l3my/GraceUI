// steam.h: Steam's floating keyboard, for GraceUI's own text fields on a pad (§5.6, D275) and for the field of the mod
// that holds input (ABI 1.10's pad_keyboard, D276). GraceUI opens it through ISteamUtils's
// ShowFloatingGamepadTextInput in the steam_api64.dll the game has already loaded, by the flat API's C exports, and
// never loads Steam's library itself, links no Steamworks SDK and calls no game code (read-only, R2). Where the library
// is not loaded, lacks the exports or Steam declines (not a Steam Deck or Big Picture), nothing is shown and one log
// line says why. One keyboard at a time, for one holder of input: GraceUI's menu's (owner 0) or a mod's, by the holder's
// generation, so that it never outlives the holder it was opened for (invariant 7). GraceUI's own thread and a mod's
// thread may call these (a lock keeps them apart), never the present thread or the window procedure: Steam's call may
// take a while, and the present thread never waits (invariant 10).
#pragma once

#include <cstdint>

namespace grace::host::steam {

// The keyboard's kind, as Steam's EFloatingGamepadTextInputMode numbers it (and ABI 1.10's GRACEUI_PAD_KEYBOARD_*).
enum class KeyboardMode : int { single_line = 0, multiple_lines = 1, email = 2, numeric = 3 };

inline constexpr std::uint32_t kMenu = 0;  // GraceUI's menu, as an owner

enum class Outcome : std::uint8_t {
  shown,        // Steam showed it
  unavailable,  // no keyboard here: no library, no exports, Steam not started, or Steam declining
  gone,         // the holder it was asked for no longer holds input, before or during Steam's call: none left open
};

// Opens the floating keyboard for the active holder `generation` of `owner` beside a field at (x, y, w, h) in the game
// window's client pixels, closing another holder's first.
Outcome show_keyboard(std::uint32_t owner, std::uint32_t generation, KeyboardMode mode, std::int32_t x, std::int32_t y,
                      std::int32_t w, std::int32_t h) noexcept;
// Closes it, if GraceUI opened it for that holder.
void dismiss_keyboard(std::uint32_t owner, std::uint32_t generation) noexcept;
// Closes it, if GraceUI opened it for any holder of `owner`'s (a mod's disconnect, the menu's fault).
void dismiss_owner(std::uint32_t owner) noexcept;
// Whether a keyboard GraceUI opened is open now (any thread, lock-free): a holder's end then wakes GraceUI's thread,
// which closes it (follow_holder).
bool keyboard_open() noexcept;
// GraceUI's own thread, at each wake: a keyboard closes once the holder it was opened for no longer holds input,
// whatever ended it; if a mod's call holds the lock, the next wake tries again (it never waits for Steam).
void follow_holder() noexcept;

#if defined(GRACEUI_TEST_EXPORTS)
// A test build looks for this module instead of steam_api64.dll (the harness's stand-in, which records its calls), so
// that no file of the harness is named as Steam's own. Each call looks again; null or empty keeps Steam's name.
void set_module_name(const wchar_t* name) noexcept;
#endif

}  // namespace grace::host::steam
