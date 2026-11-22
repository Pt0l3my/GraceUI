// wake.h: each mod's wait handle (D212, D71's "optional event handle", ABI 1.5): one auto-reset Win32 event per mod
// number, made at the mod's first ask and never closed while GraceUI runs (it never unloads), so that a thread that
// read it can always set it. GraceUI sets it whenever something is kept for the mod's poll: a hotkey's press or change,
// a holder's event, a view's watch, unwatch or command, the display. Setting it never waits, from any thread.
#pragma once

#include <cstdint>

namespace grace::host::wake {

// The mod `owner`'s event (1 to GRACEUI_MODS_MAX), made if need be, set once at once (so that a wait first polls what
// may already wait), and a handle of the mod's own to it in `out` (wait access only), which the mod closes. False if
// Windows makes neither.
bool handle_for(std::uint32_t owner, void*& out) noexcept;

void signal(std::uint32_t owner) noexcept;             // the mod numbered `owner`, if it asked for a handle
void signal_consumer(std::uint64_t consumer) noexcept; // by its connection's handle (the number in its low byte)
void signal_owners(std::uint64_t owners) noexcept;     // bit `owner` for each
void signal_all() noexcept;                            // every mod that asked (the display)
void forget(std::uint32_t owner) noexcept;             // its mod disconnected: unset, kept for the number's next mod

}  // namespace grace::host::wake
