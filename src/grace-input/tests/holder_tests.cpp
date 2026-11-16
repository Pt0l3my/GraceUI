// The input holder (holder.h): its answers, the one close routine from every exit path, and the player's exit.
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#include "grace/input/holder.h"

namespace {

using namespace grace::input;

constexpr Conditions kReady{true, false, false, false};
constexpr std::int64_t kStart = 100000;  // the tests' clock starts well after zero, as GetTickCount64's does

}  // namespace

TEST_CASE("request_input: granted, then refused naming the holder") {
  Capture capture;
  const Request first = capture.request(7, Kind::panel, want_all, kReady, kStart);
  REQUIRE(first.answer == Answer::granted);
  CHECK(first.holder.owner == 7);
  CHECK(first.holder.kind == Kind::panel);
  CHECK(first.holder.phase == Phase::active);
  CHECK(first.holder.generation == 1);

  const Request second = capture.request(9, Kind::menu, want_all, kReady, kStart + 1);
  CHECK(second.answer == Answer::refused);
  CHECK(second.holder.owner == 7);  // refused names the holder: its owner and its kind
  CHECK(second.holder.kind == Kind::panel);
  CHECK(capture.holder() == first.holder);  // nothing deposed it

  // The holder asking again for the same kind is told it holds input; for another kind it is refused like anyone.
  const Request again = capture.request(7, Kind::panel, want_all, kReady, kStart + 2);
  CHECK(again.answer == Answer::granted);
  CHECK(again.holder == first.holder);
  CHECK(capture.request(7, Kind::modal, want_all, kReady, kStart + 3).answer == Answer::refused);
}

TEST_CASE("request_input: not_now with its reason, and nothing queued behind it") {
  Capture capture;
  Request answer = capture.request(1, Kind::menu, want_all, Conditions{}, kStart);
  CHECK(answer.answer == Answer::not_now);
  CHECK(answer.why == NotNow::not_ready);
  answer = capture.request(1, Kind::menu, want_all, Conditions{true, true, false, false}, kStart);
  CHECK(answer.why == NotNow::danger);
  answer = capture.request(1, Kind::menu, want_all, Conditions{true, false, true, false}, kStart);
  CHECK(answer.why == NotNow::loading);
  answer = capture.request(1, Kind::menu, want_all, Conditions{true, false, false, true}, kStart);
  CHECK(answer.why == NotNow::game_menu);
  answer = capture.request(1, Kind::none, want_all, kReady, kStart);
  CHECK(answer.why == NotNow::invalid);
  answer = capture.request(1, Kind::panel, 0, kReady, kStart);
  CHECK(answer.why == NotNow::invalid);
  CHECK_FALSE(capture.holder().holds());  // a not_now leaves no holder and no request waiting
  CHECK(capture.request(1, Kind::menu, want_all, kReady, kStart).answer == Answer::granted);
}

TEST_CASE("a holder is refused to everyone, whatever the conditions") {
  Capture capture;
  REQUIRE(capture.request(3, Kind::panel, want_keyboard, kReady, kStart).answer == Answer::granted);
  const Request answer = capture.request(4, Kind::modal, want_all, Conditions{false, true, true, true}, kStart);
  CHECK(answer.answer == Answer::refused);
  CHECK(answer.holder.owner == 3);
}

TEST_CASE("release: only the holder's own ends it") {
  Capture capture;
  REQUIRE(capture.request(5, Kind::panel, want_all, kReady, kStart).answer == Answer::granted);
  CHECK_FALSE(capture.release(6, kStart + 10));
  CHECK(capture.holder().holds());
  CHECK(capture.release(5, kStart + 20));
  CHECK_FALSE(capture.holder().holds());
  CHECK(capture.last_close().why == Closed::released);
  CHECK(capture.last_close().holder.owner == 5);
  CHECK(capture.closes() == 1);
  CHECK(capture.closed_at() == kStart + 20);
  CHECK_FALSE(capture.release(5, kStart + 30));  // already ended
  CHECK(capture.closes() == 1);
}

TEST_CASE("the one close routine, from every exit path") {
  const Closed paths[] = {Closed::released, Closed::menu_key,   Closed::danger, Closed::loading,
                          Closed::focus_lost, Closed::no_render, Closed::fault,  Closed::player};
  for (const Kind kind : {Kind::menu, Kind::panel, Kind::modal}) {
    Capture capture;
    std::uint64_t closes = 0;
    for (const Closed why : paths) {
      CAPTURE(static_cast<int>(why));
      CAPTURE(static_cast<int>(kind));
      const Request granted = capture.request(2, kind, want_all, kReady, kStart);
      REQUIRE(granted.answer == Answer::granted);
      const Holder closed = capture.close(why, kStart + 5);
      CHECK(closed.generation == granted.holder.generation);
      CHECK(closed.kind == kind);
      CHECK_FALSE(capture.holder().holds());
      CHECK(capture.last_close().why == why);
      CHECK(capture.last_close().holder.generation == granted.holder.generation);
      CHECK(capture.closes() == ++closes);
      // A second close of the same holder does nothing: one close per holder, whoever asks first.
      CHECK_FALSE(capture.close(Closed::fault, kStart + 6).holds());
      CHECK(capture.closes() == closes);
      CHECK(capture.last_close().why == why);
    }
  }
}

TEST_CASE("a close aimed at an earlier holder leaves the new one alone") {
  Capture capture;
  const Request first = capture.request(1, Kind::panel, want_all, kReady, kStart);
  capture.close(Closed::released, kStart + 1);
  const Request second = capture.request(2, Kind::panel, want_all, kReady, kStart + 2);
  REQUIRE(second.answer == Answer::granted);
  CHECK(second.holder.generation == first.holder.generation + 1);
  CHECK_FALSE(capture.close(Closed::no_render, kStart + 3, first.holder.generation).holds());
  CHECK(capture.holder().owner == 2);
  CHECK(capture.close(Closed::no_render, kStart + 3, second.holder.generation).holds());
}

TEST_CASE("the menu key: opens with no holder, closes the menu, warns before the player's exit") {
  Capture capture;
  CHECK(capture.menu_key(kStart) == MenuKey::open_menu);

  REQUIRE(capture.request(1, Kind::menu, want_all, kReady, kStart).answer == Answer::granted);
  CHECK(capture.menu_key(kStart + 100) == MenuKey::closed_menu);
  CHECK(capture.last_close().why == Closed::menu_key);

  // Another holder: the first press says "‹Mod› has the controls"; a second within 2 s is the player's exit.
  REQUIRE(capture.request(8, Kind::panel, want_all, kReady, kStart + 200).answer == Answer::granted);
  CHECK(capture.menu_key(kStart + 300) == MenuKey::notice);
  CHECK(capture.notices() == 1);
  CHECK(capture.holder().holds());
  CHECK(capture.menu_key(kStart + 300 + 2000) == MenuKey::player_exit);
  CHECK(capture.last_close().why == Closed::player);
  CHECK(capture.last_close().holder.owner == 8);
}

TEST_CASE("the menu key: a second press after 2 s, or after another holder's notice, warns again") {
  Capture capture;
  REQUIRE(capture.request(8, Kind::modal, want_all, kReady, kStart).answer == Answer::granted);
  CHECK(capture.menu_key(kStart + 10) == MenuKey::notice);
  CHECK(capture.menu_key(kStart + 10 + 2001) == MenuKey::notice);
  CHECK(capture.holder().holds());
  CHECK(capture.notices() == 2);
  capture.close(Closed::released, kStart + 3000);
  REQUIRE(capture.request(9, Kind::panel, want_all, kReady, kStart + 3001).answer == Answer::granted);
  CHECK(capture.menu_key(kStart + 3002) == MenuKey::notice);  // the last holder's notice does not count
  CHECK(capture.holder().owner == 9);
}

TEST_CASE("the player's exit: Escape held 1 s while a holder has input, from either source") {
  for (const Source source : {Source::messages, Source::direct_input}) {
    Capture capture;
    REQUIRE(capture.request(4, Kind::panel, want_all, kReady, kStart).answer == Answer::granted);
    capture.rendered(kStart);
    capture.escape(source, true, kStart + 100);
    capture.escape(source, true, kStart + 600);  // still down: the first time stands
    capture.rendered(kStart + 1000);
    CHECK_FALSE(capture.tick(kStart + 1099).holds());
    const Holder closed = capture.tick(kStart + 1100);
    CHECK(closed.owner == 4);
    CHECK(capture.last_close().why == Closed::player);
  }
}

TEST_CASE("the player's exit: a short press, or one released, is not an exit") {
  Capture capture;
  REQUIRE(capture.request(4, Kind::panel, want_all, kReady, kStart).answer == Answer::granted);
  capture.escape(Source::messages, true, kStart + 100);
  capture.escape(Source::messages, false, kStart + 300);
  capture.rendered(kStart + 2000);
  CHECK_FALSE(capture.tick(kStart + 2000).holds());
  // The other source's own press counts on its own.
  capture.escape(Source::direct_input, true, kStart + 2100);
  capture.rendered(kStart + 3000);
  CHECK_FALSE(capture.tick(kStart + 3000).holds());
  CHECK(capture.tick(kStart + 3100).holds());
}

TEST_CASE("the player's exit counts only the time the holder has had input") {
  Capture capture;
  capture.escape(Source::messages, true, kStart);  // Escape already down, as the game's own menu opens
  REQUIRE(capture.request(4, Kind::panel, want_all, kReady, kStart + 5000).answer == Answer::granted);
  capture.rendered(kStart + 5900);
  CHECK_FALSE(capture.tick(kStart + 5900).holds());
  CHECK(capture.tick(kStart + 6000).holds());
}

TEST_CASE("the watchdog: 2 s without an overlay frame ends the holder") {
  Capture capture;
  REQUIRE(capture.request(4, Kind::menu, want_all, kReady, kStart).answer == Answer::granted);
  capture.rendered(kStart + 500);
  CHECK_FALSE(capture.tick(kStart + 2500).holds());
  const Holder closed = capture.tick(kStart + 2501);
  CHECK(closed.owner == 4);
  CHECK(capture.last_close().why == Closed::no_render);
  CHECK_FALSE(capture.tick(kStart + 9000).holds());  // nobody to close
}

TEST_CASE("one holder at a time, whatever the threads do (ThreadSanitizer)") {
  // Four threads ask, release and close at once. Each grant must have taken input from nobody: then every grant has a
  // generation of its own, one more than the last, and every close ended exactly one holder.
  Capture capture;
  std::atomic<bool> go{false};
  std::atomic<int> closed{0};
  std::vector<std::vector<std::uint32_t>> grants(4);
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      const auto owner = static_cast<std::uint16_t>(t + 1);
      while (!go.load()) {
      }
      for (int i = 0; i < 2000; ++i) {
        const Request answer = capture.request(owner, Kind::panel, want_all, kReady, kStart);
        if (answer.answer == Answer::granted) {
          if (grants[t].empty() || grants[t].back() != answer.holder.generation) {
            grants[t].push_back(answer.holder.generation);
          }
          if (capture.release(owner, kStart)) {
            closed.fetch_add(1);
          }
        } else if (i % 7 == 0 && capture.close(Closed::fault, kStart).holds()) {
          closed.fetch_add(1);
        }
      }
    });
  }
  go.store(true);
  for (std::thread& thread : threads) {
    thread.join();
  }
  std::vector<std::uint32_t> all;
  for (const std::vector<std::uint32_t>& own : grants) {
    all.insert(all.end(), own.begin(), own.end());
  }
  std::sort(all.begin(), all.end());
  CHECK(std::adjacent_find(all.begin(), all.end()) == all.end());
  REQUIRE_FALSE(all.empty());
  CHECK(all.back() == all.size());  // generations 1 to n, each granted once
  CHECK(capture.holder().generation == all.size());
  CHECK(static_cast<std::uint64_t>(closed.load()) == capture.closes());
  CHECK(capture.closes() <= all.size());
}
