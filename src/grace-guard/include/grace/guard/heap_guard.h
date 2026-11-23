// heap_guard.h: the test build's heap guard (D235), pure: no Win32. graceui.dll's test build makes every block of its
// own (its operator new and delete, and Dear ImGui's allocator) through one Guard: each block has a header, a guard of
// known bytes before it and another after it, and a freed block is filled with a known byte and kept a while in a
// quarantine before malloc has it back. A block is checked when it is freed and when it leaves the quarantine, and
// every block, live or kept, whenever check_all is called (GraceUI's thread, every few seconds). What a check finds is a
// Found: the breach's kind, the block's size, its number and the frames of the code that made it.
//
// A Guard is made at compile time and has no destructor (it lives in graceui.dll, which runs nothing at exit, §2.9).
// It takes its memory from std::malloc and makes nothing through operator new, so it can sit beneath it. A damaged
// block is never given back to malloc: it is kept on a list of its own, so that nothing writes over the evidence.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace grace::guard {

inline constexpr std::size_t kMakerFrames = 3;                         // the maker's frames kept with each block
inline constexpr std::size_t kGuardBytes = 16;                         // each guard's length, before and after
inline constexpr std::size_t kQuarantineBlocks = 4096;                 // freed blocks kept at most
inline constexpr std::size_t kQuarantineBytes = std::size_t{64} << 20;  // and their bytes at most
inline constexpr std::size_t kQuarantineLargest = std::size_t{1} << 20;  // a larger block goes back to malloc at once
inline constexpr std::size_t kPoisonedBytes = std::size_t{64} << 10;   // the most of a freed block filled and checked

enum class Breach : std::uint32_t {
  none = 0,
  before_start = 1,  // its front guard, or its header, written over while it was live
  past_end = 2,      // its tail guard written over while it was live
  after_free = 3,    // written to while it was kept after its free
  freed_twice = 4,   // freed again while it was kept
  not_ours = 5,      // given to release, but no block of this guard's (or its header so damaged that it is not one)
};

// The breach's name, as the log and the spike report give it: "written before its start", "written past its end",
// "written after its free", "freed twice", "not a block of the guard's".
const char* breach_name(Breach breach) noexcept;

// What a check found: Breach::none, or the first breach and its block's size, number (from 1, in the order made) and
// maker. For not_ours nothing of the block is known.
struct Found {
  Breach breach = Breach::none;
  std::uint64_t size = 0;
  std::uint64_t serial = 0;
  const void* maker[kMakerFrames] = {};
};

struct Stats {
  std::uint64_t live_blocks = 0;
  std::uint64_t live_bytes = 0;
  std::uint64_t quarantined_blocks = 0;
  std::uint64_t quarantined_bytes = 0;
  std::uint64_t damaged_blocks = 0;  // kept for good, never freed
  std::uint64_t made = 0;            // blocks made since the guard began
  std::uint64_t checks = 0;          // check_all calls finished
  std::uint64_t blocks_checked = 0;  // blocks they looked at
  std::uint64_t breaches = 0;        // breaches found, by any check
};

struct Header;

class Guard {
 public:
  constexpr Guard() noexcept = default;
  Guard(const Guard&) = delete;
  Guard& operator=(const Guard&) = delete;

  // A block of `size` bytes (0 is allowed), aligned as std::malloc aligns, or null when malloc has no room. The
  // maker's frames are kept with it, as given.
  void* allocate(std::size_t size, const void* const (&maker)[kMakerFrames]) noexcept;

  // Frees a block of this guard's (null is allowed). It is checked; a sound block is filled and kept in the quarantine
  // (or given back to malloc at once when larger than kQuarantineLargest), and the oldest kept blocks leave it, each
  // checked, while it holds more than its limits. The first breach found, in this block or in one leaving, is answered.
  Found release(void* block) noexcept;

  // Checks every live and every kept block; answers the first breach found, whose block is then kept for good and not
  // reported again. A live block whose header is written over ends the walk (its links can no longer be trusted).
  Found check_all() noexcept;

  // The block's number, or 0 when `block` is not a live or kept block of this guard's (its header is read).
  std::uint64_t serial_of(const void* block) const noexcept;

  Stats stats() const noexcept;

  // As stats, but only if no other thread holds the guard at this moment: for an exception handler, which must not
  // wait on a thread the exception stopped. False, with `out` untouched, when it is held.
  bool try_stats(Stats& out) const noexcept;

 private:
  void lock() const noexcept;
  void unlock() const noexcept;
  void keep_damaged(Header* header) noexcept;
  Found evict_oldest() noexcept;

  mutable std::atomic<bool> busy_{false};
  Header* live_ = nullptr;
  Header* damaged_ = nullptr;
  Header* ring_[kQuarantineBlocks] = {};
  std::size_t ring_first_ = 0;
  std::size_t ring_count_ = 0;
  std::uint64_t next_serial_ = 0;
  Stats stats_{};
};

}  // namespace grace::guard
