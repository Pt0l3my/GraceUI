// log.cpp: GraceUI's in-memory log (log.h).
#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

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

  // The debugger's copy, with a line end.
  const std::size_t length = std::strlen(text.data());
  text[length] = '\n';
  OutputDebugStringA(text.data());
}

bool log_copy_line(std::uint32_t index, LogText& out) noexcept {
  const std::uint64_t next = g_next.load(std::memory_order_acquire);
  const std::uint64_t oldest = next > kLines ? next - kLines : 0;
  const std::uint64_t number = oldest + index;
  if (number >= next) {
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
