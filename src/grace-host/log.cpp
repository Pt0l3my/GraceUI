// log.cpp: GraceUI's in-memory log (log.h).
#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "draw_atlas.h"
#include "grace/abi/text.h"
#include "hooks.h"

namespace grace::host {
namespace {

constexpr std::uint32_t kLines = 64;
constexpr char kPrefix[] = "GraceUI: ";

struct Line {
  // 0 while empty or being written; otherwise the line's number plus one, stored once its text is complete.
  std::atomic<std::uint64_t> number;
  LogText text;
};

constinit Line g_lines[kLines]{};
constinit std::atomic<std::uint64_t> g_next{0};
constinit std::uint64_t g_drained = 0;  // GraceUI's own thread's: the next line to pass to the debugger

// The log file (D84, D282): Game\GraceUI\user\graceui.log, beside settings.json, the last session's kept as
// graceui-previous.log. GraceUI's own thread alone opens and writes it, in log_drain.
constexpr std::uint64_t kLogFileMax = 1024 * 1024;  // bytes of a session's file, after which it says so and stops (EST)
enum class FileState : std::uint8_t { unopened, open, off };
constinit FileState g_file_state = FileState::unopened;
HANDLE g_file = INVALID_HANDLE_VALUE;
constinit std::uint64_t g_file_bytes = 0;

void to_debugger_alone(const char* why) noexcept {
  char line[128];
  std::snprintf(line, sizeof line, "%sthe log file %s: lines go to the debugger alone\n", kPrefix, why);
  OutputDebugStringA(line);
}

// Opens the session's file at the first drain, the last session's moved aside; off for the session if it cannot.
void open_file() noexcept {
  g_file_state = FileState::off;
  try {
    std::wstring folder;
    if (!atlas::own_folder(folder)) {
      to_debugger_alone("has no folder");
      return;
    }
    const std::filesystem::path user = std::filesystem::path(folder) / L"user";
    std::error_code error;
    std::filesystem::create_directories(user, error);
    const std::filesystem::path path = user / L"graceui.log";
    // The last session's, for a report about it (a session that fell): kept once, the one before it replaced.
    MoveFileExW(path.c_str(), (user / L"graceui-previous.log").c_str(), MOVEFILE_REPLACE_EXISTING);
    g_file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) {
      to_debugger_alone("could not be opened");
      return;
    }
    g_file_state = FileState::open;
  } catch (...) {
    to_debugger_alone("could not be opened");
  }
}

// One line to the file, as the debugger has it: a line that seems to name a place on the player's machine is left out,
// and said to be (PV3); the file's bound reached, it says so once and takes no more.
void to_file(std::string_view text) noexcept {
  if (g_file_state == FileState::unopened) {
    open_file();
  }
  if (g_file_state != FileState::open) {
    return;
  }
  char scrubbed[96];
  if (grace::abi::names_a_path(text)) {
    std::snprintf(scrubbed, sizeof scrubbed, "%sa line that named a place on this PC was left out\n", kPrefix);
    text = scrubbed;
  }
  char full[96];
  if (g_file_bytes + text.size() > kLogFileMax) {
    std::snprintf(full, sizeof full, "%sthe log file is full: later lines go to the debugger alone\n", kPrefix);
    text = full;
    g_file_state = FileState::off;
  }
  DWORD written = 0;
  if (!WriteFile(g_file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)) {
    g_file_state = FileState::off;  // a disk full or gone: the debugger alone from now on
    return;
  }
  g_file_bytes += written;
}

}  // namespace

void log_line(const char* format, ...) noexcept {
  LogText text{};
  constexpr std::size_t prefix = sizeof kPrefix - 1;
  std::memcpy(text.data(), kPrefix, prefix);
  va_list args;
  va_start(args, format);
  const int written = std::vsnprintf(text.data() + prefix, text.size() - prefix - 1, format, args);
  va_end(args);
  if (written < 0) {
    return;
  }
  const std::uint64_t number = g_next.fetch_add(1, std::memory_order_relaxed);
  Line& line = g_lines[number % kLines];
  line.number.store(0, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  line.text = text;
  line.number.store(number + 1, std::memory_order_release);
  hooks::wake();  // GraceUI's own thread passes it to the debugger (log_drain)
}

void log_drain() noexcept {
  const std::uint64_t next = g_next.load(std::memory_order_acquire);
  std::uint64_t from = g_drained;
  if (next > kLines && from < next - kLines) {
    char skipped[64];
    std::snprintf(skipped, sizeof skipped, "%s%llu lines were not passed to the debugger\n", kPrefix,
                  static_cast<unsigned long long>(next - kLines - from));
    OutputDebugStringA(skipped);
    to_file(skipped);
    from = next - kLines;
  }
  for (; from < next; ++from) {
    const Line& line = g_lines[from % kLines];
    if (line.number.load(std::memory_order_acquire) != from + 1) {
      break;  // still being written: the next wake-up passes it on
    }
    LogText text = line.text;
    std::atomic_thread_fence(std::memory_order_acquire);
    if (line.number.load(std::memory_order_relaxed) != from + 1) {
      continue;  // rewritten while it was copied: lost to the ring
    }
    text.back() = '\0';
    const std::size_t length = std::strlen(text.data());
    if (length + 1 < text.size()) {
      text[length] = '\n';
      text[length + 1] = '\0';
    }
    OutputDebugStringA(text.data());
    to_file(std::string_view(text.data(), std::strlen(text.data())));
  }
  g_drained = from;
}

bool log_copy_line(std::uint32_t index, LogText& out) noexcept {
  const std::uint64_t next = g_next.load(std::memory_order_acquire);
  const std::uint64_t oldest = next > kLines ? next - kLines : 0;
  return log_copy_number(oldest + index, out);
}

std::uint64_t log_count() noexcept { return g_next.load(std::memory_order_acquire); }

bool log_copy_number(std::uint64_t number, LogText& out) noexcept {
  const std::uint64_t next = g_next.load(std::memory_order_acquire);
  if (number >= next || (next > kLines && number < next - kLines)) {
    return false;
  }
  const Line& line = g_lines[number % kLines];
  if (line.number.load(std::memory_order_acquire) != number + 1) {
    return false;
  }
  out = line.text;
  std::atomic_thread_fence(std::memory_order_acquire);
  if (line.number.load(std::memory_order_relaxed) != number + 1) {
    return false;  // rewritten while we copied it
  }
  out.back() = '\0';
  return true;
}

}  // namespace grace::host
