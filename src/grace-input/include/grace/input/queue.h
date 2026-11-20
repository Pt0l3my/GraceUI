// queue.h: how the input half's threads hand each other data without waiting (invariant 10): a bounded ring with one
// producer and one consumer (the window procedure's thread to the present thread), one with many of each (a holder's
// events to its mod, D176), and the newest value of something
// written by the input detours' threads and read by the present thread (a sequence lock that never blocks: a reader
// that meets a write in progress tries a few times, then goes without for that frame).
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace grace::input {

template <class T, std::uint32_t N>
class SpscRing {
  static_assert(N != 0 && (N & (N - 1)) == 0, "N is a power of two");
  static_assert(std::is_trivially_copyable_v<T>);

 public:
  constexpr SpscRing() = default;
  SpscRing(const SpscRing&) = delete;
  SpscRing& operator=(const SpscRing&) = delete;

  // The producer's: false when the ring is full, and the item is dropped and counted.
  bool push(const T& item) noexcept {
    const std::uint32_t head = head_.load(std::memory_order_relaxed);
    if (head - tail_.load(std::memory_order_acquire) >= N) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    slots_[head & (N - 1)] = item;
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  // The consumer's: false when the ring is empty.
  bool pop(T& item) noexcept {
    const std::uint32_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;
    }
    item = slots_[tail & (N - 1)];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  std::uint32_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

 private:
  std::atomic<std::uint32_t> head_{0};
  std::atomic<std::uint32_t> tail_{0};
  std::atomic<std::uint32_t> dropped_{0};
  std::array<T, N> slots_{};
};

// A bounded ring for many producers and many consumers that never waits (a holder's events for its mod, D176: pushed
// on the window procedure's thread, the XInput detour's and whichever thread ends a holder; popped by the mod's poll on
// any of its threads). Each slot carries a sequence number (D. Vyukov's bounded queue): a push or a pop claims its
// place with one compare-and-swap and never waits for another thread; a push into a full ring drops the item and counts
// it, a pop of an empty one answers false.
template <class T, std::uint32_t N>
class MpmcRing {
  static_assert(N >= 2 && (N & (N - 1)) == 0, "N is a power of two");
  static_assert(std::is_trivially_copyable_v<T>);

 public:
  MpmcRing() noexcept {
    for (std::uint32_t i = 0; i < N; ++i) {
      slots_[i].sequence.store(i, std::memory_order_relaxed);
    }
  }
  MpmcRing(const MpmcRing&) = delete;
  MpmcRing& operator=(const MpmcRing&) = delete;

  bool push(const T& item) noexcept {
    std::uint32_t position = head_.load(std::memory_order_relaxed);
    for (;;) {
      Slot& slot = slots_[position & (N - 1)];
      const std::uint32_t sequence = slot.sequence.load(std::memory_order_acquire);
      const auto difference = static_cast<std::int32_t>(sequence - position);
      if (difference == 0) {
        if (head_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed)) {
          slot.item = item;
          slot.sequence.store(position + 1, std::memory_order_release);
          return true;
        }
      } else if (difference < 0) {
        dropped_.fetch_add(1, std::memory_order_relaxed);  // full: the newest is dropped, nothing reordered
        return false;
      } else {
        position = head_.load(std::memory_order_relaxed);
      }
    }
  }

  bool pop(T& item) noexcept {
    std::uint32_t position = tail_.load(std::memory_order_relaxed);
    for (;;) {
      Slot& slot = slots_[position & (N - 1)];
      const std::uint32_t sequence = slot.sequence.load(std::memory_order_acquire);
      const auto difference = static_cast<std::int32_t>(sequence - (position + 1));
      if (difference == 0) {
        if (tail_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed)) {
          item = slot.item;
          slot.sequence.store(position + N, std::memory_order_release);
          return true;
        }
      } else if (difference < 0) {
        return false;  // empty
      } else {
        position = tail_.load(std::memory_order_relaxed);
      }
    }
  }

  // The items dropped since the last call (a consumer's count to report).
  std::uint32_t take_dropped() noexcept { return dropped_.exchange(0, std::memory_order_relaxed); }

 private:
  struct Slot {
    std::atomic<std::uint32_t> sequence{0};
    T item{};
  };
  alignas(64) std::atomic<std::uint32_t> head_{0};
  alignas(64) std::atomic<std::uint32_t> tail_{0};
  std::atomic<std::uint32_t> dropped_{0};
  std::array<Slot, N> slots_{};
};

template <class T>
class Latest {
  static_assert(std::is_trivially_copyable_v<T>);
  static constexpr std::size_t kWords = (sizeof(T) + 7) / 8;

 public:
  constexpr Latest() = default;
  Latest(const Latest&) = delete;
  Latest& operator=(const Latest&) = delete;

  // Any thread. Answers false, and writes nothing, while another thread's write is in progress: the two hold the same
  // kind of news, and the next call writes.
  bool publish(const T& value) noexcept {
    std::uint32_t sequence = sequence_.load(std::memory_order_relaxed);
    if ((sequence & 1u) != 0 ||
        !sequence_.compare_exchange_strong(sequence, sequence + 1, std::memory_order_acquire,
                                           std::memory_order_relaxed)) {
      return false;
    }
    std::atomic_thread_fence(std::memory_order_release);
    std::array<std::uint64_t, kWords> words{};
    std::memcpy(words.data(), &value, sizeof(T));
    for (std::size_t i = 0; i < kWords; ++i) {
      words_[i].store(words[i], std::memory_order_relaxed);
    }
    sequence_.store(sequence + 2, std::memory_order_release);
    return true;
  }

  // Any thread. False if nothing was ever written, or if writes kept it busy for every try.
  bool read(T& out) const noexcept {
    for (int attempt = 0; attempt < 4; ++attempt) {
      const std::uint32_t before = sequence_.load(std::memory_order_acquire);
      if (before == 0) {
        return false;
      }
      if ((before & 1u) != 0) {
        continue;
      }
      std::array<std::uint64_t, kWords> words{};
      for (std::size_t i = 0; i < kWords; ++i) {
        words[i] = words_[i].load(std::memory_order_relaxed);
      }
      std::atomic_thread_fence(std::memory_order_acquire);
      if (sequence_.load(std::memory_order_relaxed) == before) {
        std::memcpy(static_cast<void*>(&out), words.data(), sizeof(T));  // T is trivially copyable (checked above)
        return true;
      }
    }
    return false;
  }

 private:
  std::atomic<std::uint32_t> sequence_{0};
  std::array<std::atomic<std::uint64_t>, kWords> words_{};
};

}  // namespace grace::input
