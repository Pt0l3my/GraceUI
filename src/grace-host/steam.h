// steam.h: Steam's floating keyboard for GraceUI's own text fields on a pad (§5.6, D275). GraceUI opens it through
// ISteamUtils's ShowFloatingGamepadTextInput in the steam_api64.dll the game has already loaded, by the flat API's C
// exports, and never loads Steam's library itself, links no Steamworks SDK and calls no game code (read-only, R2). Where
// the library is not loaded, lacks the exports or Steam declines (not a Steam Deck or Big Picture), nothing is shown and
// one log line says why. GraceUI's own thread alone calls these: Steam's call may take a while, and the present thread
// never waits (invariant 10).
#pragma once

#include <cstdint>

namespace grace::host::steam {

// The keyboard's kind, as Steam's EFloatingGamepadTextInputMode numbers it.
enum class KeyboardMode : int { single_line = 0, multiple_lines = 1, email = 2, numeric = 3 };

// Opens the floating keyboard beside a field at (x, y, w, h) in the game window's client pixels; whether Steam showed it.
bool show_keyboard(KeyboardMode mode, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) noexcept;
// Closes it, if GraceUI opened it.
void dismiss_keyboard() noexcept;

#if defined(GRACEUI_TEST_EXPORTS)
// A test build looks for this module instead of steam_api64.dll (the harness's stand-in, which records its calls), so
// that no file of the harness is named as Steam's own. Before the first show; null or empty keeps Steam's name.
void set_module_name(const wchar_t* name) noexcept;
#endif

}  // namespace grace::host::steam
