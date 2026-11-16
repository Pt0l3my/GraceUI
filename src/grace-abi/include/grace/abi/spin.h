// spin.h: the lock the ABI's rules take on mods' threads (connect, disconnect, a notice): a flag spun on with a yield,
// held for a few copies at most. The present thread never takes it: what it reads comes through Notices' triple buffer.
#pragma once

#include <atomic>
#include <thread>

namespace grace::abi {

class SpinLock {
 public:
  constexpr SpinLock() = default;
  SpinLock(const SpinLock&) = delete;
  SpinLock& operator=(const SpinLock&) = delete;

  void lock() noexcept {
    while (flag_.test_and_set(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }
  void unlock() noexcept { flag_.clear(std::memory_order_release); }

 private:
  std::atomic_flag flag_{};
};

class SpinGuard {
 public:
  explicit SpinGuard(SpinLock& lock) noexcept : lock_(lock) { lock_.lock(); }
  ~SpinGuard() { lock_.unlock(); }
  SpinGuard(const SpinGuard&) = delete;
  SpinGuard& operator=(const SpinGuard&) = delete;

 private:
  SpinLock& lock_;
};

}  // namespace grace::abi
