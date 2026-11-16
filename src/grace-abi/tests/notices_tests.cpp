// The notices GraceUI shows (notices.h): new, replaced and withdrawn by key, timed and untimed, bounded per mod and in
// all, and the present thread's reads, which never wait and never meet a list half written.
#include <doctest/doctest.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "grace/abi/notices.h"

using namespace grace::abi;

namespace {

constexpr std::int64_t kNow = 100000;

NoticeIn notice(std::uint32_t owner, std::string_view text, std::uint32_t key = 0, std::uint32_t duration_ms = 0) {
  NoticeIn in;
  in.owner = owner;
  in.key = key;
  in.duration_ms = duration_ms;
  in.text = text;
  return in;
}

std::string text_of(const Notice& n) { return std::string(n.view()); }

}  // namespace

TEST_CASE("a notice is shown, replaced in its place by its key, and withdrawn by an empty text") {
  auto notices = std::make_unique<Notices>();
  CHECK(notices->post(notice(1, "First"), kNow) == Posted::shown);
  CHECK(notices->post(notice(1, "Banner", 7), kNow) == Posted::shown);
  CHECK(notices->post(notice(2, "Another mod's"), kNow) == Posted::shown);
  CHECK(notices->live(kNow) == 3);

  NoticeIn replacement = notice(1, "Banner, changed", 7);
  replacement.tone = GRACEUI_TONE_DANGER;
  CHECK(notices->post(replacement, kNow + 5) == Posted::replaced);
  Notice second;
  REQUIRE(notices->copy(1, second, kNow + 5));
  CHECK(text_of(second) == "Banner, changed");  // in its place: second still
  CHECK(second.tone == GRACEUI_TONE_DANGER);
  CHECK(notices->live(kNow + 5) == 3);

  // The same key from another mod is that mod's own key.
  CHECK(notices->post(notice(2, "Key 7 of mod 2", 7), kNow) == Posted::shown);
  CHECK(notices->live(kNow) == 4);

  CHECK(notices->post(notice(1, "", 7), kNow) == Posted::withdrawn);
  CHECK(notices->live(kNow) == 3);
  CHECK(notices->post(notice(1, "", 7), kNow) == Posted::withdrawn);  // withdrawing twice does nothing more
  Notice first;
  REQUIRE(notices->copy(0, first, kNow));
  CHECK(text_of(first) == "First");
}

TEST_CASE("a timed notice goes when its time is up; an untimed one stays") {
  auto notices = std::make_unique<Notices>();
  CHECK(notices->post(notice(1, "Toast", 0, 3000), kNow) == Posted::shown);
  CHECK(notices->post(notice(1, "Banner", 1, 0), kNow) == Posted::shown);
  CHECK(notices->live(kNow + 2999) == 2);
  CHECK(notices->live(kNow + 3000) == 1);
  Notice left;
  REQUIRE(notices->copy(0, left, kNow + 3000));
  CHECK(text_of(left) == "Banner");
  CHECK(notices->live(kNow + 10'000'000) == 1);

  // Replacing a timed notice restarts its time.
  auto restarted = std::make_unique<Notices>();
  CHECK(restarted->post(notice(1, "Toast", 9, 1000), kNow) == Posted::shown);
  CHECK(restarted->post(notice(1, "Toast again", 9, 1000), kNow + 900) == Posted::replaced);
  CHECK(restarted->live(kNow + 1500) == 1);
  CHECK(restarted->live(kNow + 1900) == 0);
}

TEST_CASE("bounds: GRACEUI_NOTICES_PER_MOD a mod, eight of GraceUI's own, sixteen in all") {
  auto notices = std::make_unique<Notices>();
  for (std::uint32_t i = 1; i < kNoticesPerMod; ++i) {
    CHECK(notices->post(notice(1, "One of mod 1's"), kNow) == Posted::shown);
  }
  CHECK(notices->post(notice(1, "Keyed", 5), kNow) == Posted::shown);
  CHECK(notices->post(notice(1, "One too many"), kNow) == Posted::full);
  CHECK(notices->post(notice(1, "Keyed, replaced at the limit", 5), kNow) == Posted::replaced);
  // A timed one expires, and makes room.
  auto timed = std::make_unique<Notices>();
  CHECK(timed->post(notice(3, "Short", 0, 100), kNow) == Posted::shown);
  for (std::uint32_t i = 1; i < kNoticesPerMod; ++i) {
    CHECK(timed->post(notice(3, "Long"), kNow) == Posted::shown);
  }
  CHECK(timed->post(notice(3, "Waits for room"), kNow + 50) == Posted::full);
  CHECK(timed->post(notice(3, "Room now"), kNow + 100) == Posted::shown);

  for (std::uint32_t i = 0; i < kNoticesOwn; ++i) {
    CHECK(notices->post(notice(0, "GraceUI's own"), kNow) == Posted::shown);
  }
  CHECK(notices->post(notice(0, "GraceUI's ninth"), kNow) == Posted::full);
  // 4 + 8 = 12 showing; four more mods' notices fill the sixteen.
  CHECK(notices->post(notice(2, "a"), kNow) == Posted::shown);
  CHECK(notices->post(notice(2, "b"), kNow) == Posted::shown);
  CHECK(notices->post(notice(3, "c"), kNow) == Posted::shown);
  CHECK(notices->post(notice(3, "d"), kNow) == Posted::shown);
  CHECK(notices->post(notice(4, "e"), kNow) == Posted::full);
  CHECK(notices->live(kNow) == kNoticeSlots);
}

TEST_CASE("invalid notices are refused, and change nothing") {
  auto notices = std::make_unique<Notices>();
  CHECK(notices->post(notice(1, ""), kNow) == Posted::invalid);  // empty, without a key
  CHECK(notices->post(notice(1, "bad\xC3"), kNow) == Posted::invalid);
  CHECK(notices->post(notice(1, std::string(kNoticeTextBytes + 1, 'x')), kNow) == Posted::invalid);
  NoticeIn toned = notice(1, "An unknown tone");
  toned.tone = 3;
  CHECK(notices->post(toned, kNow) == Posted::invalid);
  CHECK(notices->live(kNow) == 0);
  CHECK(notices->post(notice(1, std::string(kNoticeTextBytes, 'x')), kNow) == Posted::shown);  // the longest
}

TEST_CASE("a disconnected mod's notices are withdrawn, and the others stay") {
  auto notices = std::make_unique<Notices>();
  notices->post(notice(1, "Mod 1"), kNow);
  notices->post(notice(2, "Mod 2"), kNow);
  notices->post(notice(1, "Mod 1 again", 4), kNow);
  notices->withdraw_owner(1, kNow);
  CHECK(notices->live(kNow) == 1);
  Notice left;
  REQUIRE(notices->copy(0, left, kNow));
  CHECK(left.owner == 2);
  notices->clear(kNow);
  CHECK(notices->live(kNow) == 0);
}

TEST_CASE("the present thread's read: the newest list, unchanged until its next read") {
  auto notices = std::make_unique<Notices>();
  CHECK(notices->read().count == 0);
  notices->post(notice(1, "One"), kNow);
  const NoticeList& first = notices->read();
  REQUIRE(first.count == 1);
  CHECK(text_of(first.items[0]) == "One");
  notices->post(notice(1, "Two"), kNow);
  notices->post(notice(1, "Three"), kNow);
  CHECK(first.count == 1);  // what the reader holds does not move under it
  const NoticeList& newest = notices->read();
  REQUIRE(newest.count == 3);
  CHECK(text_of(newest.items[2]) == "Three");
  CHECK(&notices->read() == &newest);  // nothing new: the same list
}

TEST_CASE("writers and the present thread at once: every list the reader sees is whole") {
  auto notices = std::make_unique<Notices>();
  std::atomic<bool> done{false};
  // Each writer replaces its own keyed notice with texts whose every byte is one letter: a list half written would show
  // a text of two letters, or a length that does not match the letter's count.
  std::vector<std::thread> writers;
  for (std::uint32_t owner = 1; owner <= 3; ++owner) {
    writers.emplace_back([&, owner] {
      for (int round = 0; round < 3000; ++round) {
        const char letter = static_cast<char>('a' + (round % 26));
        const std::string text(static_cast<std::size_t>(1 + (round * 7) % 200), letter);
        notices->post(notice(owner, text, 1), kNow);
        if (round % 50 == 0) {
          notices->post(notice(owner, "", 1), kNow);
        }
      }
    });
  }
  std::atomic<int> torn{0};
  std::thread reader([&] {
    std::uint64_t reads = 0;
    while (!done.load(std::memory_order_acquire) || reads < 1000) {
      const NoticeList& list = notices->read();
      if (list.count > kNoticeSlots) {
        torn.fetch_add(1);
        break;
      }
      for (std::uint32_t i = 0; i < list.count; ++i) {
        const Notice& n = list.items[i];
        bool whole = n.text_length >= 1 && n.text_length <= 200;
        for (std::uint32_t c = 1; whole && c < n.text_length; ++c) {
          whole = n.text[c] == n.text[0];
        }
        torn.fetch_add(whole ? 0 : 1);
      }
      ++reads;
    }
  });
  for (std::thread& writer : writers) {
    writer.join();
  }
  done.store(true, std::memory_order_release);
  reader.join();
  CHECK(torn.load() == 0);
  CHECK(notices->live(kNow) == 3);
}
