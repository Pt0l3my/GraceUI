// GraceUI's log, for now in memory: the newest lines in a fixed ring, each also sent to OutputDebugString. A line holds
// no path, no user or machine name and no game text (PV3); the file under GraceUI\user\ comes with GraceUI's settings.
// Any thread may write a line: it never allocates and never waits (invariant 10 holds on the present thread).
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

}  // namespace grace::host
