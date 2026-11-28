// steam.h: Steam's floating keyboard, for GraceUI's own text fields on a pad (§5.6, D275) and for the field of the mod
// that holds input (ABI 1.10's pad_keyboard, D276). GraceUI opens it through ISteamUtils's
// ShowFloatingGamepadTextInput in the steam_api64.dll the game has already loaded, by the flat API's C exports, and
// never loads Steam's library itself, links no Steamworks SDK and calls no game code (read-only, R2). Where the library
// is not loaded, lacks the exports or Steam declines (not a Steam Deck or Big Picture), nothing is shown and one log
// line says why. One keyboard at a time, for one owner: GraceUI's menu (0) or a mod (its owner id). GraceUI's own thread
// and a mod's thread may call these (a lock keeps them apart), never the present thread: Steam's call may take a while,
// and the present thread never waits (invariant 10).
#pragma once

#include <cstdint>

namespace grace::host::steam {

// The keyboard's kind, as Steam's EFloatingGamepadTextInputMode numbers it (and ABI 1.10's GRACEUI_PAD_KEYBOARD_*).
enum class KeyboardMode : int { single_line = 0, multiple_lines = 1, email = 2, numeric = 3 };

inline constexpr std::uint32_t kMenu = 0;  // GraceUI's menu, as an owner

// Opens the floating keyboard for `owner` beside a field at (x, y, w, h) in the game window's client pixels, closing
// another owner's first; whether Steam showed it.
bool show_keyboard(std::uint32_t owner, KeyboardMode mode, std::int32_t x, std::int32_t y, std::int32_t w,
                   std::int32_t h) noexcept;
// Closes it, if GraceUI opened it for `owner`.
void dismiss_keyboard(std::uint32_t owner) noexcept;
// Whether a keyboard GraceUI opened for a mod is open now (any thread, lock-free): a holder's end then wakes GraceUI's
// thread, which closes it (follow_holder).
bool open_for_a_mod() noexcept;
// GraceUI's own thread, at each wake: a mod's keyboard closes once that mod no longer holds input (invariant 7).
void follow_holder(std::uint32_t holder_owner, bool mod_holds) noexcept;

#if defined(GRACEUI_TEST_EXPORTS)
// A test build looks for this module instead of steam_api64.dll (the harness's stand-in, which records its calls), so
// that no file of the harness is named as Steam's own. Each call looks again; null or empty keeps Steam's name.
void set_module_name(const wchar_t* name) noexcept;
#endif

}  // namespace grace::host::steam
