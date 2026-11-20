// The mods whose notices the player has quieted (quiet.h, G32, D181).
#include <doctest/doctest.h>

#include <string>
#include <thread>
#include <vector>

#include "grace/abi/quiet.h"

using grace::abi::QuietNotices;

TEST_CASE("quiet ids: set and cleared by id, sorted, kept between sessions; an id connect would refuse is not") {
  QuietNotices quiet;
  const std::uint64_t start = quiet.revision();
  CHECK(quiet.set("lantern-keeper", true));
  CHECK(quiet.set("ashen-notes", true));
  CHECK(quiet.quiet("lantern-keeper"));
  CHECK_FALSE(quiet.quiet("tide-pool"));
  CHECK(quiet.ids() == std::vector<std::string>{"ashen-notes", "lantern-keeper"});
  const std::uint64_t after = quiet.revision();
  CHECK(after > start);
  CHECK(quiet.set("ashen-notes", true));  // as it was: nothing changes
  CHECK(quiet.revision() == after);
  CHECK(quiet.set("ashen-notes", false));
  CHECK_FALSE(quiet.quiet("ashen-notes"));
  CHECK_FALSE(quiet.set("Not An Id", true));
  CHECK_FALSE(quiet.set("", true));
  QuietNotices next;
  CHECK(next.restore({"lantern-keeper", "Bad Id", "tide-pool"}) == 2);
  CHECK(next.ids() == std::vector<std::string>{"lantern-keeper", "tide-pool"});
}

TEST_CASE("at most kQuietMods quiet ids; a mod can always be shown again") {
  QuietNotices quiet;
  for (std::size_t i = 0; i < grace::abi::kQuietMods; ++i) {
    REQUIRE(quiet.set("mod-" + std::to_string(i), true));
  }
  CHECK_FALSE(quiet.set("one-more", true));
  CHECK(quiet.set("mod-0", false));
  CHECK(quiet.set("one-more", true));
}

TEST_CASE("kept notices: newest first, the oldest going past kQuietKept, withdrawals not kept") {
  QuietNotices quiet;
  quiet.keep("lantern-keeper", "Lantern keeper", 0, "The wick is trimmed.");
  quiet.keep("lantern-keeper", "Lantern keeper", 0, "");
  quiet.keep("tide-pool", "Tide pool", 0, "The tide turns.");
  std::vector<QuietNotices::Kept> kept = quiet.kept();
  REQUIRE(kept.size() == 2);
  CHECK(kept[0].text == "The tide turns.");
  CHECK(kept[0].name == "Tide pool");
  CHECK(kept[1].id == "lantern-keeper");
  CHECK(kept[0].serial > kept[1].serial);
  for (std::size_t i = 0; i < grace::abi::kQuietKept + 5; ++i) {
    quiet.keep("tide-pool", "Tide pool", 0, "Wave " + std::to_string(i));
  }
  kept = quiet.kept();
  CHECK(kept.size() == grace::abi::kQuietKept);
  CHECK(kept.front().text == "Wave " + std::to_string(grace::abi::kQuietKept + 4));
}

TEST_CASE("a kept notice with a key is replaced by the mod's next one under that key, as on screen") {
  QuietNotices quiet;
  quiet.keep("lantern-keeper", "Lantern keeper", 7, "Trimming: a quarter done.");
  quiet.keep("tide-pool", "Tide pool", 7, "The tide turns.");  // another mod's key 7: its own
  quiet.keep("lantern-keeper", "Lantern keeper", 7, "Trimming: half done.");
  quiet.keep("lantern-keeper", "Lantern keeper", 0, "A wick lit.");
  quiet.keep("lantern-keeper", "Lantern keeper", 0, "A wick lit.");  // without a key, each is its own
  const std::vector<QuietNotices::Kept> kept = quiet.kept();
  REQUIRE(kept.size() == 4);
  const std::uint64_t ids = quiet.ids_revision();
  quiet.keep("tide-pool", "Tide pool", 0, "Ebb.");
  CHECK(quiet.ids_revision() == ids);  // the kept list is not what is kept between sessions
  quiet.set("tide-pool", true);
  CHECK(quiet.ids_revision() > ids);
  CHECK(kept[2].text == "Trimming: half done.");
  CHECK(kept[2].key == 7);
  CHECK(kept[3].id == "tide-pool");
}

TEST_CASE("mods' threads keep while GraceUI's thread sets and reads") {
  QuietNotices quiet;
  std::vector<std::thread> threads;
  for (int t = 0; t < 3; ++t) {
    threads.emplace_back([&quiet, t] {
      for (int i = 0; i < 500; ++i) {
        quiet.keep("mod-" + std::to_string(t), "A mod", 0, "A notice");
        (void)quiet.quiet("mod-" + std::to_string(t));
      }
    });
  }
  for (int i = 0; i < 500; ++i) {
    quiet.set("mod-1", (i & 1) != 0);
    (void)quiet.kept();
  }
  for (std::thread& t : threads) {
    t.join();
  }
  CHECK(quiet.kept().size() == grace::abi::kQuietKept);
}
