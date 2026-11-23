// heap_guard.cpp: the test build's heap guard (heap_guard.h, D235).
#include "grace/guard/heap_guard.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>

namespace grace::guard {

// A block as malloc holds it: this header, the front guard, the caller's bytes, the tail guard.
struct alignas(16) Header {
  std::uint64_t seal = 0;  // kLive or kFreed, mixed with size and serial: a header written over fails it
  std::uint64_t size = 0;
  std::uint64_t serial = 0;
  const void* maker[kMakerFrames] = {};
  Header* prev = nullptr;  // the live list; the damaged list uses next alone
  Header* next = nullptr;
};

namespace {

constexpr std::size_t kOffset = sizeof(Header) + kGuardBytes;  // from the header to the caller's first byte
static_assert(kOffset % 16 == 0, "the caller's bytes keep malloc's alignment");

constexpr std::uint64_t kLive = 0x4755'4C49'5645'0001ull;   // "GULIVE"
constexpr std::uint64_t kFreed = 0x4755'4652'4545'0002ull;  // "GUFREE"
constexpr std::uint64_t kFreeing = 0x4755'4655'4c4c'0003ull;  // between its free and the quarantine: in no list
constexpr std::uint64_t kMix = 0x9E37'79B9'7F4A'7C15ull;
constexpr unsigned char kFront = 0xFB;
constexpr unsigned char kTail = 0xFE;
constexpr unsigned char kPoison = 0xDF;

std::uint64_t seal_of(std::uint64_t state, std::uint64_t size, std::uint64_t serial) noexcept {
  return state ^ (size * kMix) ^ (serial << 17) ^ (serial >> 47);
}

bool live(const Header& header) noexcept { return header.seal == seal_of(kLive, header.size, header.serial); }
bool kept(const Header& header) noexcept { return header.seal == seal_of(kFreed, header.size, header.serial); }
bool freeing(const Header& header) noexcept { return header.seal == seal_of(kFreeing, header.size, header.serial); }

unsigned char* bytes_of(Header* header) noexcept { return static_cast<unsigned char*>(static_cast<void*>(header)); }
const unsigned char* bytes_of(const Header* header) noexcept {
  return static_cast<const unsigned char*>(static_cast<const void*>(header));
}
unsigned char* user_of(Header* header) noexcept { return bytes_of(header) + kOffset; }
const unsigned char* user_of(const Header* header) noexcept { return bytes_of(header) + kOffset; }

Header* header_of(void* block) noexcept {
  return static_cast<Header*>(static_cast<void*>(static_cast<unsigned char*>(block) - kOffset));
}
const Header* header_of(const void* block) noexcept {
  return static_cast<const Header*>(static_cast<const void*>(static_cast<const unsigned char*>(block) - kOffset));
}

bool all(const unsigned char* bytes, std::size_t length, unsigned char value) noexcept {
  for (std::size_t i = 0; i < length; ++i) {
    if (bytes[i] != value) {
      return false;
    }
  }
  return true;
}

std::size_t poisoned_length(std::uint64_t size) noexcept {
  return size < kPoisonedBytes ? static_cast<std::size_t>(size) : kPoisonedBytes;
}

// A live block's guards: before_start or past_end when written over, else none.
Breach live_guards(const Header& header) noexcept {
  if (!all(bytes_of(&header) + sizeof(Header), kGuardBytes, kFront)) {
    return Breach::before_start;
  }
  if (!all(user_of(&header) + header.size, kGuardBytes, kTail)) {
    return Breach::past_end;
  }
  return Breach::none;
}

// A kept block: its header, guards and filled bytes as they were left at its free.
bool kept_sound(const Header& header) noexcept {
  return kept(header) && all(bytes_of(&header) + sizeof(Header), kGuardBytes, kFront) &&
         all(user_of(&header) + header.size, kGuardBytes, kTail) &&
         all(user_of(&header), poisoned_length(header.size), kPoison);
}

Found found_in(const Header& header, Breach breach) noexcept {
  Found found;
  found.breach = breach;
  found.size = header.size;
  found.serial = header.serial;
  for (std::size_t i = 0; i < kMakerFrames; ++i) {
    found.maker[i] = header.maker[i];
  }
  return found;
}

}  // namespace

const char* breach_name(Breach breach) noexcept {
  switch (breach) {
    case Breach::none: return "none";
    case Breach::before_start: return "written before its start";
    case Breach::past_end: return "written past its end";
    case Breach::after_free: return "written after its free";
    case Breach::freed_twice: return "freed twice";
    case Breach::not_ours: return "not a block of the guard's";
  }
  return "unknown";
}

void Guard::lock() const noexcept {
  while (busy_.exchange(true, std::memory_order_acquire)) {
    while (busy_.load(std::memory_order_relaxed)) {
      std::this_thread::yield();
    }
  }
}

void Guard::unlock() const noexcept { busy_.store(false, std::memory_order_release); }

void* Guard::allocate(std::size_t size, const void* const (&maker)[kMakerFrames]) noexcept {
  if (size > SIZE_MAX - kOffset - kGuardBytes) {
    return nullptr;
  }
  void* raw = std::malloc(kOffset + size + kGuardBytes);
  if (raw == nullptr) {
    return nullptr;
  }
  Header* header = ::new (raw) Header{};
  header->size = size;
  for (std::size_t i = 0; i < kMakerFrames; ++i) {
    header->maker[i] = maker[i];
  }
  std::memset(bytes_of(header) + sizeof(Header), kFront, kGuardBytes);
  std::memset(user_of(header) + size, kTail, kGuardBytes);
  lock();
  header->serial = ++next_serial_;
  header->seal = seal_of(kLive, size, header->serial);
  header->next = live_;
  if (live_ != nullptr) {
    live_->prev = header;
  }
  live_ = header;
  ++stats_.live_blocks;
  stats_.live_bytes += size;
  ++stats_.made;
  unlock();
  return user_of(header);
}

void Guard::keep_damaged(Header* header) noexcept {
  header->prev = nullptr;
  header->next = damaged_;
  damaged_ = header;
  ++stats_.damaged_blocks;
  ++stats_.breaches;
}

// The oldest kept block out of the quarantine (the lock held); null for a slot check_all emptied.
Header* Guard::pop_oldest() noexcept {
  Header* header = ring_[ring_first_];
  ring_[ring_first_] = nullptr;
  ring_first_ = (ring_first_ + 1) % kQuarantineBlocks;
  --ring_count_;
  if (header != nullptr) {
    --stats_.quarantined_blocks;
    stats_.quarantined_bytes -= header->size;
  }
  return header;
}

// A block out of the quarantine, checked with no lock held (no list holds it now): back to malloc if sound, else kept
// for good.
Found Guard::leave(Header* header) noexcept {
  if (header == nullptr) {
    return {};
  }
  if (kept_sound(*header)) {
    std::free(header);
    return {};
  }
  const Found found = found_in(*header, Breach::after_free);
  lock();
  keep_damaged(header);
  unlock();
  return found;
}

// The lock is held only to change the lists and the counts: the checks, the filling and malloc's free are made with the
// block in no list, so that other threads wait on none of them.
Found Guard::release(void* block) noexcept {
  if (block == nullptr) {
    return {};
  }
  Header* header = header_of(block);
  lock();
  if (!live(*header)) {
    Found found;
    if (kept(*header) || freeing(*header)) {
      found = found_in(*header, Breach::freed_twice);  // it stays where it is
    } else {
      found.breach = Breach::not_ours;  // nothing of it is trusted, and nothing of it is touched
    }
    ++stats_.breaches;
    unlock();
    return found;
  }
  if (header->prev != nullptr) {
    header->prev->next = header->next;
  } else {
    live_ = header->next;
  }
  if (header->next != nullptr) {
    header->next->prev = header->prev;
  }
  --stats_.live_blocks;
  stats_.live_bytes -= header->size;
  header->seal = seal_of(kFreeing, header->size, header->serial);  // a second free meanwhile is found as one
  unlock();

  const Breach breach = live_guards(*header);
  if (breach != Breach::none) {
    const Found found = found_in(*header, breach);
    lock();
    keep_damaged(header);
    unlock();
    return found;
  }
  if (header->size > kQuarantineLargest) {
    std::free(header);
    return {};
  }
  std::memset(user_of(header), kPoison, poisoned_length(header->size));
  header->prev = nullptr;
  header->next = nullptr;
  constexpr std::size_t kMostLeaving = 8;  // at each free; a quarantine over its bytes sheds the rest at the next ones
  Header* leaving[kMostLeaving] = {};
  std::size_t count = 0;
  lock();
  header->seal = seal_of(kFreed, header->size, header->serial);
  if (ring_count_ == kQuarantineBlocks) {
    leaving[count++] = pop_oldest();  // a full ring's oldest slot is the one this block takes
  }
  ring_[(ring_first_ + ring_count_) % kQuarantineBlocks] = header;
  ++ring_count_;
  ++stats_.quarantined_blocks;
  stats_.quarantined_bytes += header->size;
  while (ring_count_ > 1 && stats_.quarantined_bytes > kQuarantineBytes && count < kMostLeaving) {
    leaving[count++] = pop_oldest();
  }
  unlock();
  Found found;
  for (std::size_t i = 0; i < count; ++i) {
    const Found left = leave(leaving[i]);
    if (found.breach == Breach::none) {
      found = left;
    }
  }
  return found;
}

Found Guard::check_all() noexcept {
  lock();
  Found found;
  for (Header* header = live_; header != nullptr && found.breach == Breach::none; header = header->next) {
    ++stats_.blocks_checked;
    if (!live(*header)) {
      found = found_in(*header, Breach::before_start);  // its header written over: its links are not trusted
      ++stats_.breaches;
      break;
    }
    const Breach breach = live_guards(*header);
    if (breach != Breach::none) {
      found = found_in(*header, breach);
      if (header->prev != nullptr) {
        header->prev->next = header->next;
      } else {
        live_ = header->next;
      }
      if (header->next != nullptr) {
        header->next->prev = header->prev;
      }
      --stats_.live_blocks;
      stats_.live_bytes -= header->size;
      keep_damaged(header);
      break;
    }
  }
  for (std::size_t i = 0; i < ring_count_ && found.breach == Breach::none; ++i) {
    Header*& slot = ring_[(ring_first_ + i) % kQuarantineBlocks];
    if (slot == nullptr) {
      continue;
    }
    ++stats_.blocks_checked;
    if (!kept_sound(*slot)) {
      found = found_in(*slot, Breach::after_free);
      --stats_.quarantined_blocks;
      stats_.quarantined_bytes -= slot->size;
      keep_damaged(slot);
      slot = nullptr;
    }
  }
  ++stats_.checks;
  unlock();
  return found;
}

std::uint64_t Guard::serial_of(const void* block) const noexcept {
  if (block == nullptr) {
    return 0;
  }
  const Header* header = header_of(block);
  lock();
  const std::uint64_t serial = live(*header) || kept(*header) || freeing(*header) ? header->serial : 0;
  unlock();
  return serial;
}

Stats Guard::stats() const noexcept {
  lock();
  const Stats copy = stats_;
  unlock();
  return copy;
}

bool Guard::try_stats(Stats& out) const noexcept {
  if (busy_.exchange(true, std::memory_order_acquire)) {
    return false;
  }
  out = stats_;
  unlock();
  return true;
}

}  // namespace grace::guard
