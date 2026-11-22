// A holder's events for its mod (holder_mail.h, D176), single-threaded and under ThreadSanitizer.
#include <doctest/doctest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "grace/input/holder_mail.h"

using grace::input::HolderMail;
using grace::input::Mail;
using grace::input::MailKind;

namespace {

constexpr std::uint16_t kOwner = 3;
constexpr std::uint64_t kToken = 77;

Mail key(std::uint32_t generation, std::uint32_t vk) {
  Mail mail;
  mail.kind = MailKind::key;
  mail.generation = generation;
  mail.key = vk;
  mail.pressed = 1;
  return mail;
}

Mail move(std::uint32_t generation, float x) {
  Mail mail;
  mail.kind = MailKind::pointer;
  mail.generation = generation;
  mail.x = x;
  mail.y = 1.0f;
  return mail;
}

std::vector<Mail> drain(HolderMail& mail, std::uint16_t owner = kOwner, std::uint64_t token = kToken) {
  std::vector<Mail> out;
  Mail one;
  std::uint32_t dropped = 0;
  while (mail.take(owner, token, one, dropped)) {
    out.push_back(one);
  }
  return out;
}

}  // namespace

TEST_CASE("nothing is kept for a holder that did not ask, or for another generation") {
  HolderMail mail;
  mail.post(key(5, 'A'));
  CHECK(drain(mail).empty());
  mail.enable(kOwner, kToken, 5);
  CHECK(mail.active(5));
  CHECK_FALSE(mail.active(6));
  mail.post(key(6, 'B'));  // another holder's
  mail.post(key(5, 'C'));
  const std::vector<Mail> got = drain(mail);
  REQUIRE(got.size() == 1);
  CHECK(got[0].key == 'C');
}

TEST_CASE("events come in order, a pointer's moves coalesced to the newest between other events") {
  HolderMail mail;
  mail.enable(kOwner, kToken, 5);
  mail.post(key(5, 'A'));
  mail.post(move(5, 10.0f));
  mail.post(move(5, 11.0f));
  mail.post(move(5, 12.0f));
  Mail click = move(5, 12.0f);
  click.button_event = 1;
  click.pressed = 1;
  mail.post(click);
  mail.post(move(5, 20.0f));
  const std::vector<Mail> got = drain(mail);
  REQUIRE(got.size() == 4);
  CHECK(got[0].kind == MailKind::key);
  CHECK(got[1].kind == MailKind::pointer);
  CHECK(got[1].x == 12.0f);  // three moves, the newest
  CHECK(got[2].button_event == 1);
  CHECK(got[3].x == 20.0f);
}

TEST_CASE("the end comes last, after the events not yet taken are discarded, and nothing after it") {
  HolderMail mail;
  mail.enable(kOwner, kToken, 5);
  mail.post(key(5, 'A'));
  mail.end(5, 3);
  mail.post(key(5, 'B'));  // after the end: not kept
  mail.end(5, 1);          // twice: once only
  const std::vector<Mail> got = drain(mail);
  REQUIRE(got.size() == 1);
  CHECK(got[0].kind == MailKind::ended);
  CHECK(got[0].why == 3);
  CHECK_FALSE(mail.any());
}

TEST_CASE("a new grant to the same mod: the old holder's end, then the new holder's events") {
  HolderMail mail;
  mail.enable(kOwner, kToken, 5);
  mail.post(key(5, 'A'));
  mail.end(5, 1);
  mail.enable(kOwner, kToken, 6);
  mail.post(key(6, 'B'));
  const std::vector<Mail> got = drain(mail);
  REQUIRE(got.size() == 2);
  CHECK(got[0].kind == MailKind::ended);
  CHECK(got[0].generation == 5);
  CHECK(got[1].key == 'B');
}

TEST_CASE("only the mod's own connection takes its mail; a new connection starts empty") {
  HolderMail mail;
  mail.enable(kOwner, kToken, 5);
  mail.post(key(5, 'A'));
  CHECK(drain(mail, kOwner, kToken + 1).empty());  // another connection of the same owner id
  CHECK(drain(mail, kOwner + 1, kToken).empty());  // another owner
  mail.end(5, 1);
  mail.enable(kOwner, kToken + 1, 6);  // the mod connected again: the old mail is gone
  CHECK(drain(mail, kOwner, kToken + 1).empty());
}

TEST_CASE("a pad's buttons: one event a change, only while the holder takes the pad") {
  HolderMail mail;
  mail.enable(kOwner, kToken, 5);
  constexpr std::uint16_t kA = 0x1000;
  constexpr std::uint16_t kB = 0x2000;
  mail.pad(0, kA, 5, true);
  mail.pad(0, kA | kB, 5, true);
  mail.pad(0, kA | kB, 5, true);  // no change: nothing
  mail.pad(0, 0, 5, true);
  mail.pad(1, kB, 5, false);  // not taking the pad: nothing, but its state followed
  mail.pad(1, kB, 5, true);   // no change since: nothing
  const std::vector<Mail> got = drain(mail);
  REQUIRE(got.size() == 4);
  CHECK((got[0].button == kA && got[0].pressed == 1));
  CHECK((got[1].button == kB && got[1].pressed == 1));
  CHECK((got[2].button == kA && got[2].pressed == 0));
  CHECK((got[3].button == kB && got[3].pressed == 0));
  CHECK(got[3].kind == MailKind::pad);
}

TEST_CASE("a full box drops the newest and counts them; a full box still takes the end") {
  HolderMail mail;
  mail.enable(kOwner, kToken, 5);
  for (std::uint32_t i = 0; i < grace::input::kMailPerHolder + 10; ++i) {
    mail.post(key(5, i));
  }
  Mail first;
  std::uint32_t dropped = 0;
  REQUIRE(mail.take(kOwner, kToken, first, dropped));
  CHECK(first.key == 0);
  CHECK(dropped == 10);
  for (std::uint32_t i = 0; i < 20; ++i) {
    mail.post(key(5, 1000 + i));  // full again
  }
  mail.end(5, 8);
  const std::vector<Mail> got = drain(mail);
  REQUIRE_FALSE(got.empty());
  CHECK(got.back().kind == MailKind::ended);
  CHECK(got.back().why == 8);
}

TEST_CASE("posted from three threads while the mod takes from two: in order per thread, the end last") {
  HolderMail mail;
  mail.enable(kOwner, kToken, 5);
  std::atomic<bool> posting{true};
  std::atomic<std::uint32_t> taken{0};
  std::atomic<bool> ended{false};
  std::atomic<bool> after_end{false};
  std::vector<std::thread> takers;
  for (int t = 0; t < 2; ++t) {
    takers.emplace_back([&] {
      Mail one;
      std::uint32_t dropped = 0;
      for (;;) {
        // Read before taking: the other taker may take the end between this take and its bookkeeping, so only a key
        // taken once the end was already gone comes after it.
        const bool end_gone = ended.load();
        if (mail.take(kOwner, kToken, one, dropped)) {
          if (one.kind == MailKind::ended) {
            ended.store(true);
          } else if (end_gone) {
            after_end.store(true);
          }
          taken.fetch_add(1);
        } else if (!posting.load()) {
          return;
        } else {
          std::this_thread::yield();
        }
      }
    });
  }
  std::vector<std::thread> posters;
  for (std::uint32_t p = 0; p < 3; ++p) {
    posters.emplace_back([&mail, p] {
      for (std::uint32_t i = 0; i < 2000; ++i) {
        mail.post(key(5, p * 10000 + i));
      }
    });
  }
  for (std::thread& t : posters) {
    t.join();
  }
  mail.end(5, 1);
  posting.store(false);
  for (std::thread& t : takers) {
    t.join();
  }
  for (const Mail& rest : drain(mail)) {
    ended.store(ended.load() || rest.kind == MailKind::ended);
  }
  CHECK(ended.load());
  CHECK_FALSE(after_end.load());
}

namespace {
std::atomic<int> g_notified{0};
std::atomic<int> g_notified_owner{0};
void notified(std::uint16_t owner) noexcept {
  g_notified.fetch_add(1);
  g_notified_owner.store(owner);
}
}  // namespace

TEST_CASE("each event kept, and the end, tells the host whose it is, for the mod's wait handle (D212)") {
  HolderMail mail;
  mail.set_notify(&notified);
  g_notified.store(0);
  mail.post(key(5, 1));  // no holder asked yet: nothing kept, nothing told
  CHECK(g_notified.load() == 0);
  mail.enable(kOwner, kToken, 5);
  mail.post(key(5, 2));
  CHECK(g_notified.load() == 1);
  CHECK(g_notified_owner.load() == kOwner);
  mail.post(key(4, 3));  // an earlier holder's: not kept, not told
  CHECK(g_notified.load() == 1);
  mail.end(5, 1);
  CHECK(g_notified.load() == 2);
  mail.end(5, 1);  // its end was posted already
  CHECK(g_notified.load() == 2);
}
