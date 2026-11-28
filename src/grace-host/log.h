// GraceUI's log: the newest lines in a fixed ring, each passed by GraceUI's own thread (log_drain), never by the thread
// that wrote it, to OutputDebugString and to GraceUI\user\graceui.log (D84, D282; the last session's kept as
// graceui-previous.log): with a listener attached OutputDebugString waits for it (up to 10 s), with a debugger it is a
// round trip to the debugger (L2), and a file waits on the disk. A line holds no path, no user or machine name and no
// game text (PV3); one that seems to name a place on the player's PC is left out of the file. Any thread may write a
// line: it never allocates and never waits (invariant 10 holds on the present thread).
#pragma once

#include <array>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define GRACE_PRINTF_LIKE __attribute__((format(printf, 1, 2)))
#else
#define GRACE_PRINTF_LIKE
#endif

namespace grace::host {

inline constexpr std::uint32_t kLogLineBytes = 160;
using LogText = std::array<char, kLogLineBytes>;

// Writes one line, formatted as printf does, cut to fit kLogLineBytes.
void log_line(const char* format, ...) noexcept GRACE_PRINTF_LIKE;

// Copies line `index` (0 the oldest still kept) into `out`, NUL-terminated; answers false past the newest, or if the
// line is being rewritten at that moment.
bool log_copy_line(std::uint32_t index, LogText& out) noexcept;

// The lines written since GraceUI loaded (D230), and line `number` of them (0 the first) while the ring still keeps
// it: false once overwritten, or if it is being written at that moment. A reader that keeps the next number it wants
// misses no line the ring still holds and repeats none.
std::uint64_t log_count() noexcept;
bool log_copy_number(std::uint64_t number, LogText& out) noexcept;

// GraceUI's own thread only: sends the lines written since its last call to OutputDebugString and the log file, oldest
// first; lines the ring has already overwritten are skipped, with a line saying how many.
void log_drain() noexcept;

}  // namespace grace::host
