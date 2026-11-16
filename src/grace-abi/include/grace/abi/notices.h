// notices.h: the notices GraceUI shows (D76's notice layer; D134): each mod's, up to GRACEUI_NOTICES_PER_MOD at once,
// and GraceUI's own. Mods' threads post, replace and withdraw them under a spin lock; the present thread reads the newest
// list through a triple buffer, so it never waits and never meets a list half written (invariant 10). Pure: times are
// milliseconds of the caller's monotonic clock.
#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "grace/abi/spin.h"
#include "graceui.h"

namespace grace::abi {

inline constexpr std::uint32_t kNoticeSlots = 16;                            // notices showing at once, in all
inline constexpr std::uint32_t kNoticesPerMod = GRACEUI_NOTICES_PER_MOD;      // a mod's
inline constexpr std::uint32_t kNoticesOwn = 8;                               // GraceUI's own (owner 0)
inline constexpr std::uint32_t kNoticeTextBytes = GRACEUI_NOTICE_TEXT_MAX;

struct Notice {
  std::uint32_t owner = 0;        // the mod's number (consumers.h), or 0 for GraceUI's own
  std::uint32_t key = 0;          // 0: none
  std::uint32_t tone = GRACEUI_TONE_NORMAL;
  std::uint32_t text_length = 0;
  std::int64_t expires_ms = 0;    // 0: until replaced or withdrawn
  std::uint64_t serial = 0;       // when it was first posted: a replaced notice keeps its place
  char text[kNoticeTextBytes] = {};

  constexpr bool live_at(std::int64_t now_ms) const noexcept { return expires_ms == 0 || now_ms < expires_ms; }
  std::string_view view() const noexcept { return {text, text_length}; }
};

// The notices showing, oldest first.
struct NoticeList {
  std::uint32_t count = 0;
  Notice items[kNoticeSlots]{};
};

struct NoticeIn {
  std::uint32_t owner = 0;
  std::uint32_t key = 0;
  std::uint32_t tone = GRACEUI_TONE_NORMAL;
  std::uint32_t duration_ms = 0;  // 0: until replaced or withdrawn
  std::string_view text;          // empty, with a key, withdraws that key's notice
};

enum class Posted : std::uint8_t {
  shown = 0,      // a new notice
  replaced = 1,   // the key's notice, in its place, with the new text, tone and duration
  withdrawn = 2,  // the key's notice is gone (or was not showing)
  full = 3,       // the owner has its notices showing already, or all kNoticeSlots are taken
  invalid = 4,    // a text that is not a notice's text (text.h), an empty one without a key, or an unknown tone
};

class Notices {
 public:
  constexpr Notices() = default;
  Notices(const Notices&) = delete;
  Notices& operator=(const Notices&) = delete;

  // Mods' threads, and GraceUI's own: never the present thread.
  Posted post(const NoticeIn& in, std::int64_t now_ms) noexcept;
  void withdraw_owner(std::uint32_t owner, std::int64_t now_ms) noexcept;  // a mod disconnected
  void clear(std::int64_t now_ms) noexcept;                                // every notice gone (the test exports)
  std::uint32_t live(std::int64_t now_ms) const noexcept;
  // The `index`-th notice showing at `now_ms`, oldest first; false past the last.
  bool copy(std::uint32_t index, Notice& out, std::int64_t now_ms) const noexcept;

  // The present thread only (one thread at a time): the newest list. It stays as it is until this thread's next read,
  // and may hold notices that have since expired (Notice::live_at).
  const NoticeList& read() noexcept;

 private:
  void prune(std::int64_t now_ms) noexcept;  // under lock_: the master list loses its expired notices
  void publish() noexcept;                   // under lock_: the master list becomes the newest

  static constexpr std::uint32_t kFresh = 4;  // in middle_: the middle buffer holds a list the reader has not taken

  mutable SpinLock lock_;
  NoticeList master_{};        // the writers' list (under lock_)
  std::uint64_t next_serial_ = 1;
  NoticeList buffers_[3]{};
  std::uint32_t back_ = 0;     // the writers' buffer (under lock_)
  std::atomic<std::uint32_t> middle_{1};
  std::uint32_t front_ = 2;    // the reader's buffer
};

}  // namespace grace::abi
