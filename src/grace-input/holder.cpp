// holder.cpp: the input holder (holder.h). The holder is one 64-bit word, changed only by compare-and-swap, so a request
// and the close routine each take effect whole, or not at all.
#include "grace/input/holder.h"

namespace grace::input {
namespace {

constexpr std::uint64_t pack(const Holder& holder) noexcept {
  return std::uint64_t{holder.generation} | (std::uint64_t{holder.owner} << 32) |
         (std::uint64_t{static_cast<std::uint8_t>(holder.kind)} << 48) | (std::uint64_t{holder.wants & 0xFu} << 52) |
         (std::uint64_t{static_cast<std::uint8_t>(holder.phase)} << 56);
}

constexpr Holder unpack(std::uint64_t word) noexcept {
  Holder holder;
  holder.generation = static_cast<std::uint32_t>(word);
  holder.owner = static_cast<std::uint16_t>(word >> 32);
  holder.kind = static_cast<Kind>((word >> 48) & 0xFu);
  holder.wants = static_cast<std::uint8_t>((word >> 52) & 0xFu);
  holder.phase = static_cast<Phase>((word >> 56) & 0x3u);
  return holder;
}

constexpr std::uint64_t pack_close(const Holder& holder, Closed why) noexcept {
  return (pack(holder) & ~(std::uint64_t{0xFF} << 56)) | (std::uint64_t{static_cast<std::uint8_t>(why)} << 56);
}

constexpr std::int64_t later(std::int64_t a, std::int64_t b) noexcept { return a > b ? a : b; }

}  // namespace

Holder Capture::holder() const noexcept { return unpack(state_.load(std::memory_order_acquire)); }

Request Capture::request(std::uint16_t owner, Kind kind, std::uint8_t wants, const Conditions& conditions,
                         std::int64_t now_ms) noexcept {
  if (kind == Kind::none || (wants & want_all) == 0) {
    return {Answer::not_now, NotNow::invalid, {}};
  }
  std::uint64_t word = state_.load(std::memory_order_acquire);
  for (;;) {
    const Holder current = unpack(word);
    if (current.holds()) {
      if (current.phase == Phase::active && current.owner == owner && current.kind == kind) {
        return {Answer::granted, NotNow::none, current};
      }
      return {Answer::refused, NotNow::none, current};
    }
    if (!conditions.ready || conditions.off) {
      return {Answer::not_now, NotNow::not_ready, {}};
    }
    if (conditions.danger) {
      return {Answer::not_now, NotNow::danger, {}};
    }
    if (conditions.loading) {
      return {Answer::not_now, NotNow::loading, {}};
    }
    if (conditions.game_menu) {
      return {Answer::not_now, NotNow::game_menu, {}};
    }
    Holder next;
    next.generation = current.generation + 1;
    next.owner = owner;
    next.kind = kind;
    next.wants = static_cast<std::uint8_t>(wants & want_all);
    next.phase = Phase::active;
    // Written before the holder exists, so that tick() never measures the new holder from an older time; a request that
    // loses the race below wrote a time no earlier than the winner's, which costs nothing.
    granted_at_.store(now_ms, std::memory_order_relaxed);
    if (state_.compare_exchange_weak(word, pack(next), std::memory_order_acq_rel, std::memory_order_acquire)) {
      return {Answer::granted, NotNow::none, next};
    }
  }
}

namespace {

// What ends a holder among the conditions, if anything: input or the overlay off first, then danger, then loading.
Closed ending(const Conditions& conditions) noexcept {
  if (conditions.off) {
    return Closed::fault;
  }
  if (conditions.danger) {
    return Closed::danger;
  }
  return conditions.loading ? Closed::loading : Closed::none;
}

}  // namespace

Request Capture::request(std::uint16_t owner, Kind kind, std::uint8_t wants, ReadConditions read, const void* context,
                         std::int64_t now_ms) noexcept {
  const Request answer = request(owner, kind, wants, read(context), now_ms);
  if (answer.answer != Answer::granted) {
    return answer;
  }
  // The grant's compare-and-swap is above this fence, the second read below it; a writer stores, fences, then reads
  // the holder (settle). Of two such threads at least one sees the other's write.
  std::atomic_thread_fence(std::memory_order_seq_cst);
  const Closed why = ending(read(context));
  if (why == Closed::none) {
    return answer;
  }
  Request ended{Answer::not_now, NotNow::not_ready, close(why, now_ms, answer.holder.generation)};
  ended.why = why == Closed::danger ? NotNow::danger : why == Closed::loading ? NotNow::loading : NotNow::not_ready;
  return ended;
}

Holder Capture::settle(ReadConditions read, const void* context, std::int64_t now_ms) noexcept {
  std::atomic_thread_fence(std::memory_order_seq_cst);  // the writer's change above, the holder's read below
  const Closed why = ending(read(context));
  return why == Closed::none ? Holder{} : close(why, now_ms);
}

bool Capture::release(std::uint16_t owner, std::int64_t now_ms) noexcept {
  const Holder current = holder();
  if (current.phase != Phase::active || current.owner != owner) {
    return false;
  }
  return close(Closed::released, now_ms, current.generation).holds();
}

Holder Capture::close(Closed why, std::int64_t now_ms, std::uint32_t generation) noexcept {
  std::uint64_t word = state_.load(std::memory_order_acquire);
  for (;;) {
    const Holder current = unpack(word);
    if (current.phase != Phase::active || (generation != 0 && current.generation != generation)) {
      return {};
    }
    Holder closing = current;
    closing.phase = Phase::closing;
    if (!state_.compare_exchange_weak(word, pack(closing), std::memory_order_acq_rel, std::memory_order_acquire)) {
      continue;
    }
    // Only the close that won the swap gets here. Input stays captured while the devices are told (closes_), so that a
    // reader that sees the holder gone also sees the count that makes it mask what is held (invariant 8).
    closed_at_.store(now_ms, std::memory_order_relaxed);
    last_close_.store(pack_close(current, why), std::memory_order_relaxed);
    closes_.fetch_add(1, std::memory_order_release);
    Holder ended = current;
    ended.phase = Phase::none;
    state_.store(pack(ended), std::memory_order_release);
    return current;
  }
}

LastClose Capture::last_close() const noexcept {
  const std::uint64_t word = last_close_.load(std::memory_order_relaxed);
  LastClose last;
  last.holder = unpack(word & ~(std::uint64_t{0xFF} << 56));
  last.why = static_cast<Closed>(word >> 56);
  return last;
}

MenuKey Capture::menu_key(std::int64_t now_ms) noexcept {
  const Holder current = holder();
  if (current.phase != Phase::active) {
    return current.holds() ? MenuKey::nothing : MenuKey::open_menu;
  }
  if (current.kind == Kind::menu) {
    return close(Closed::menu_key, now_ms, current.generation).holds() ? MenuKey::closed_menu : MenuKey::nothing;
  }
  if (notice_generation_.load(std::memory_order_relaxed) == current.generation &&
      now_ms - notice_at_.load(std::memory_order_relaxed) <= kExitPressWindowMs) {
    return close(Closed::player, now_ms, current.generation).holds() ? MenuKey::player_exit : MenuKey::nothing;
  }
  notice_at_.store(now_ms, std::memory_order_relaxed);
  notice_generation_.store(current.generation, std::memory_order_relaxed);
  notices_.fetch_add(1, std::memory_order_relaxed);
  return MenuKey::notice;
}

void Capture::escape(Source source, bool down, std::int64_t now_ms) noexcept {
  std::atomic<std::int64_t>& since = escape_since_[static_cast<int>(source)];
  if (!down) {
    since.store(0, std::memory_order_relaxed);
    return;
  }
  std::int64_t up = 0;
  since.compare_exchange_strong(up, now_ms, std::memory_order_relaxed);  // a key held down keeps its first time
}

void Capture::rendered(std::int64_t now_ms) noexcept { last_render_.store(now_ms, std::memory_order_relaxed); }

Holder Capture::tick(std::int64_t now_ms) noexcept {
  const Holder current = holder();
  if (current.phase != Phase::active) {
    return {};
  }
  const std::int64_t granted = granted_at_.load(std::memory_order_relaxed);
  for (const std::atomic<std::int64_t>& since : escape_since_) {
    const std::int64_t down = since.load(std::memory_order_relaxed);
    if (down != 0 && now_ms - later(down, granted) >= kEscapeHoldMs) {
      return close(Closed::player, now_ms, current.generation);
    }
  }
  if (now_ms - later(last_render_.load(std::memory_order_relaxed), granted) > kNoRenderMs) {
    return close(Closed::no_render, now_ms, current.generation);
  }
  return {};
}

}  // namespace grace::input
