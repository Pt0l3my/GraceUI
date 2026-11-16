// notices.cpp: the notices GraceUI shows (notices.h).
#include "grace/abi/notices.h"

#include <cstring>

#include "grace/abi/text.h"

namespace grace::abi {
namespace {

void remove_at(NoticeList& list, std::uint32_t index) noexcept {
  for (std::uint32_t i = index; i + 1 < list.count; ++i) {
    list.items[i] = list.items[i + 1];
  }
  --list.count;
  list.items[list.count] = Notice{};
}

void write_text(Notice& notice, std::string_view text) noexcept {
  std::memcpy(notice.text, text.data(), text.size());  // valid_notice_text: at most kNoticeTextBytes
  notice.text_length = static_cast<std::uint32_t>(text.size());
}

}  // namespace

Posted Notices::post(const NoticeIn& in, std::int64_t now_ms) noexcept {
  const bool withdraw = in.text.empty();
  if ((withdraw && in.key == 0) || (!withdraw && !valid_notice_text(in.text)) || in.tone > GRACEUI_TONE_DANGER) {
    return Posted::invalid;
  }
  const SpinGuard guard(lock_);
  prune(now_ms);
  std::uint32_t owned = 0;
  for (std::uint32_t i = 0; i < master_.count; ++i) {
    Notice& notice = master_.items[i];
    if (notice.owner != in.owner) {
      continue;
    }
    if (in.key != 0 && notice.key == in.key) {
      if (withdraw) {
        remove_at(master_, i);
        publish();
        return Posted::withdrawn;
      }
      notice.tone = in.tone;
      notice.expires_ms = in.duration_ms == 0 ? 0 : now_ms + in.duration_ms;
      write_text(notice, in.text);
      publish();
      return Posted::replaced;
    }
    ++owned;
  }
  if (withdraw) {
    return Posted::withdrawn;  // nothing showing under that key: nothing to do
  }
  if (owned >= (in.owner == 0 ? kNoticesOwn : kNoticesPerMod) || master_.count >= kNoticeSlots) {
    return Posted::full;
  }
  Notice& notice = master_.items[master_.count++];
  notice = Notice{};
  notice.owner = in.owner;
  notice.key = in.key;
  notice.tone = in.tone;
  notice.expires_ms = in.duration_ms == 0 ? 0 : now_ms + in.duration_ms;
  notice.serial = next_serial_++;
  write_text(notice, in.text);
  publish();
  return Posted::shown;
}

void Notices::withdraw_owner(std::uint32_t owner, std::int64_t now_ms) noexcept {
  const SpinGuard guard(lock_);
  prune(now_ms);
  for (std::uint32_t i = master_.count; i-- > 0;) {
    if (master_.items[i].owner == owner) {
      remove_at(master_, i);
    }
  }
  publish();
}

void Notices::clear(std::int64_t now_ms) noexcept {
  const SpinGuard guard(lock_);
  prune(now_ms);
  while (master_.count > 0) {
    remove_at(master_, master_.count - 1);
  }
  publish();
}

std::uint32_t Notices::live(std::int64_t now_ms) const noexcept {
  const SpinGuard guard(lock_);
  std::uint32_t count = 0;
  for (std::uint32_t i = 0; i < master_.count; ++i) {
    count += master_.items[i].live_at(now_ms) ? 1u : 0u;
  }
  return count;
}

bool Notices::copy(std::uint32_t index, Notice& out, std::int64_t now_ms) const noexcept {
  const SpinGuard guard(lock_);
  for (std::uint32_t i = 0; i < master_.count; ++i) {
    if (master_.items[i].live_at(now_ms) && index-- == 0) {
      out = master_.items[i];
      return true;
    }
  }
  return false;
}

const NoticeList& Notices::read() noexcept {
  if ((middle_.load(std::memory_order_relaxed) & kFresh) != 0) {
    // Take the newest list and leave the one read so far as the writers' next middle buffer.
    front_ = middle_.exchange(front_, std::memory_order_acq_rel) & ~kFresh;
  }
  return buffers_[front_];
}

void Notices::prune(std::int64_t now_ms) noexcept {
  bool changed = false;
  for (std::uint32_t i = master_.count; i-- > 0;) {
    if (!master_.items[i].live_at(now_ms)) {
      remove_at(master_, i);
      changed = true;
    }
  }
  if (changed) {
    publish();
  }
}

void Notices::publish() noexcept {
  buffers_[back_] = master_;
  back_ = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel) & ~kFresh;
}

}  // namespace grace::abi
