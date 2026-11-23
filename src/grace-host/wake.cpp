// wake.cpp: each mod's wait handle (wake.h, D212).
#include "wake.h"

#include <windows.h>

#include <atomic>

#include "grace/abi/consumers.h"

namespace grace::host::wake {
namespace {

// Index 0 is no mod's. Never closed: a thread that loaded one may set it at any time after.
constinit std::atomic<HANDLE> g_events[grace::abi::kMaxConsumers + 1] = {};

HANDLE event_of(std::uint32_t owner) noexcept {
  return owner != 0 && owner <= grace::abi::kMaxConsumers ? g_events[owner].load(std::memory_order_acquire) : nullptr;
}

}  // namespace

bool handle_for(std::uint32_t owner, void*& out) noexcept {
  out = nullptr;
  if (owner == 0 || owner > grace::abi::kMaxConsumers) {
    return false;
  }
  HANDLE event = g_events[owner].load(std::memory_order_acquire);
  if (event == nullptr) {
    HANDLE made = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset: one wait takes one setting
    if (made == nullptr) {
      return false;
    }
    if (!g_events[owner].compare_exchange_strong(event, made, std::memory_order_acq_rel)) {
      CloseHandle(made);  // another request of the same mod's made it first
    } else {
      event = made;
    }
  }
  HANDLE mine = nullptr;
  if (DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &mine, SYNCHRONIZE, FALSE, 0) == FALSE) {
    return false;
  }
  SetEvent(event);
  out = mine;
  return true;
}

void signal(std::uint32_t owner) noexcept {
  if (HANDLE event = event_of(owner); event != nullptr) {
    SetEvent(event);
  }
}

void signal_consumer(std::uint64_t consumer) noexcept { signal(static_cast<std::uint32_t>(consumer & 0xFFu)); }

void signal_owners(std::uint64_t owners) noexcept {
  for (std::uint32_t owner = 1; owner <= grace::abi::kMaxConsumers && owner < 64; ++owner) {
    if ((owners >> owner & 1u) != 0) {
      signal(owner);
    }
  }
}

void signal_all() noexcept {
  for (std::uint32_t owner = 1; owner <= grace::abi::kMaxConsumers; ++owner) {
    signal(owner);
  }
}

void forget(std::uint32_t owner) noexcept {
  if (HANDLE event = event_of(owner); event != nullptr) {
    ResetEvent(event);
  }
}

}  // namespace grace::host::wake
