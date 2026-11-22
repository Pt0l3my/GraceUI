// holder_mail.cpp: a holder's events for its mod (holder_mail.h, D176).
#include "grace/input/holder_mail.h"

namespace grace::input {

HolderMail::~HolderMail() {
  for (std::atomic<Box*>& slot : boxes_) {
    delete slot.load(std::memory_order_acquire);
  }
}

HolderMail::Box* HolderMail::box_of(std::uint16_t owner) const noexcept {
  Box* box = boxes_[owner % kMailBoxes].load(std::memory_order_acquire);
  return box != nullptr && box->owner.load(std::memory_order_acquire) == owner ? box : nullptr;
}

void HolderMail::enable(std::uint16_t owner, std::uint64_t token, std::uint32_t generation) {
  std::atomic<Box*>& slot = boxes_[owner % kMailBoxes];
  Box* box = slot.load(std::memory_order_acquire);
  if (box == nullptr) {
    const std::lock_guard<std::mutex> making(making_);
    box = slot.load(std::memory_order_acquire);
    if (box == nullptr) {
      box = new Box();  // never freed while GraceUI runs: a producer may hold it at any time
      slot.store(box, std::memory_order_release);
    }
  }
  if (box->token.load(std::memory_order_acquire) != token || box->owner.load(std::memory_order_acquire) != owner) {
    // Another connection's mail (a mod gone, or a box shared by two owners): emptied before this one's comes.
    const std::lock_guard<std::mutex> taking(box->taking);
    Mail stale;
    while (box->ring.pop(stale)) {
    }
    box->held = false;
    box->pending_end.store(0, std::memory_order_release);
    box->ring.take_dropped();
    box->owner.store(owner, std::memory_order_release);
    box->token.store(token, std::memory_order_release);
  }
  active_.store(pack(owner, generation), std::memory_order_release);
}

bool HolderMail::active(std::uint32_t generation) const noexcept {
  const std::uint64_t now = active_.load(std::memory_order_acquire);
  return now != 0 && generation != 0 && static_cast<std::uint32_t>(now) == generation;
}

bool HolderMail::any() const noexcept { return active_.load(std::memory_order_acquire) != 0; }

void HolderMail::post(const Mail& mail) noexcept {
  const std::uint64_t now = active_.load(std::memory_order_acquire);
  if (now == 0 || mail.generation == 0 || static_cast<std::uint32_t>(now) != mail.generation) {
    return;
  }
  if (Box* box = box_of(static_cast<std::uint16_t>(now >> 32)); box != nullptr) {
    box->ring.push(mail);  // full: the newest dropped and counted
  }
}

void HolderMail::pad(std::uint32_t user, std::uint16_t buttons, std::uint32_t generation, bool takes_pad) noexcept {
  if (user >= kMailPads) {
    return;
  }
  const std::uint16_t before = pad_raw_[user].exchange(buttons, std::memory_order_relaxed);
  const std::uint16_t changed = static_cast<std::uint16_t>(before ^ buttons);
  if (changed == 0 || !takes_pad || !active(generation)) {
    return;
  }
  for (std::uint32_t bit = 0; bit < 16; ++bit) {
    const auto mask = static_cast<std::uint16_t>(1u << bit);
    if ((changed & mask) != 0) {
      Mail mail;
      mail.kind = MailKind::pad;
      mail.generation = generation;
      mail.button = mask;
      mail.pressed = (buttons & mask) != 0 ? 1 : 0;
      mail.pad = user;
      post(mail);
    }
  }
}

void HolderMail::end(std::uint32_t generation, std::uint32_t why) noexcept {
  std::uint64_t now = active_.load(std::memory_order_acquire);
  if (now == 0 || generation == 0 || static_cast<std::uint32_t>(now) != generation ||
      !active_.compare_exchange_strong(now, 0, std::memory_order_acq_rel)) {
    return;  // it had not asked for its events, or its end was posted already
  }
  if (Box* box = box_of(static_cast<std::uint16_t>(now >> 32)); box != nullptr) {
    Mail mail;
    mail.kind = MailKind::ended;
    mail.generation = generation;
    mail.why = why;
    if (!box->ring.push(mail)) {
      // A full box: the end matters more than any event before it, which the mod would discard anyway.
      Mail stale;
      while (box->ring.pop(stale)) {
      }
      if (!box->ring.push(mail)) {
        // Still full: a pop on another thread has claimed a slot and not yet given it back. The end waits in the box's
        // own slot, which take() gives first: whatever the ring holds of this holder is discarded anyway (D207).
        box->pending_end.store((std::uint64_t{generation} << 32) | why, std::memory_order_release);
      }
    }
  }
}

bool HolderMail::pop(Box& box, Mail& out) {
  if (box.held) {
    box.held = false;
    out = box.next;
    return true;
  }
  return box.ring.pop(out);
}

bool HolderMail::take(std::uint16_t owner, std::uint64_t token, Mail& out, std::uint32_t& dropped) {
  Box* box = box_of(owner);
  if (box == nullptr || box->token.load(std::memory_order_acquire) != token) {
    return false;
  }
  const std::lock_guard<std::mutex> taking(box->taking);
  const std::uint64_t now = active_.load(std::memory_order_acquire);
  const std::uint32_t current =
      now != 0 && static_cast<std::uint16_t>(now >> 32) == owner ? static_cast<std::uint32_t>(now) : 0;
  if (const std::uint64_t pending = box->pending_end.exchange(0, std::memory_order_acq_rel); pending != 0) {
    out = Mail{};
    out.kind = MailKind::ended;
    out.generation = static_cast<std::uint32_t>(pending >> 32);
    out.why = static_cast<std::uint32_t>(pending);
    dropped = box->ring.take_dropped();
    return true;
  }
  Mail mail;
  while (pop(*box, mail)) {
    if (mail.kind != MailKind::ended && mail.generation != current) {
      continue;  // a holder that has ended: its events are gone, its end comes
    }
    const auto is_move = [](const Mail& m) {
      return m.kind == MailKind::pointer && m.button_event == 0 && m.wheel_x == 0.0f && m.wheel_y == 0.0f;
    };
    if (is_move(mail)) {
      Mail later;
      while (box->ring.pop(later)) {
        if (is_move(later) && later.generation == mail.generation) {
          mail = later;  // only the newest position
          continue;
        }
        box->held = true;
        box->next = later;
        break;
      }
    }
    out = mail;
    dropped = box->ring.take_dropped();
    return true;
  }
  return false;
}

}  // namespace grace::input
