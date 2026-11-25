// grace-dev --dump (§9.2's in-game view on Linux, D168): a page's structure dump against a golden file, the same dump as
// grace-view's own goldens, a list that passes the checker, and the command line's checks of its values. Set
// GRACE_UPDATE_GOLDEN=1 to write the golden anew.
#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "cli.h"
#include "dump.h"
#include "space_data.h"
#include "support.h"

namespace {

using namespace grace::dev;

SpaceData builtin(std::string_view id) {
  SpaceData space;
  std::string why;
  REQUIRE_MESSAGE(load_builtin(id, space, why), why);
  return space;
}

// At GraceUI's default ui.scale unless a test names one, as the command line does.
DumpResult dumped(std::string_view id, std::string page, float width, float height,
                  double ui_scale = default_ui_scale(), bool clear = false) {
  DumpResult out;
  std::string why;
  REQUIRE_MESSAGE(dump_page(builtin(id), DumpRequest{std::move(page), width, height, ui_scale, clear}, out, why), why);
  return out;
}

// The text between the header line and the counts' line: the structure dump itself.
std::string body_of(const std::string& text) {
  const std::size_t first = text.find('\n') + 1;
  const std::size_t last = text.rfind("# ");
  return text.substr(first, last - first);
}

}  // namespace

TEST_CASE("--dump: the demo's Overview at 800x600, as its golden says") {
  const DumpResult result = dumped("demo", "demo.overview", 800, 600);
  CHECK(result.passes);
  const std::filesystem::path golden = test::tests_folder() / "golden" / "demo-overview-800x600.txt";
#ifndef _WIN32  // goldens are written on Linux, and only compared elsewhere
  if (const char* update = std::getenv("GRACE_UPDATE_GOLDEN"); update != nullptr && update[0] == '1') {
    std::ofstream(golden, std::ios::binary) << result.text;
  }
#endif
  CHECK_MESSAGE(test::read_text(golden) == result.text, "the dump differs from ", golden.string(),
                " (run with GRACE_UPDATE_GOLDEN=1 to accept it)");
  CHECK(test::contains(result.text, "# demo demo.overview: 800x600, ui.scale 1.25, scale 0.694, theme grace"));
  CHECK(test::contains(result.text, "check_draw_list: passes\n"));
}

TEST_CASE("--dump draws a master's chosen row: the demo's Lamps at 1280x720, the first lamp's detail from its member") {
  // The master list's topic chooses its first row; the row's member (lantern.lamp/harbour-mouth, D215) is then given
  // too, as the menu watches it (EldenNet's Named NPCs goldens, its WHY§524).
  const DumpResult result = dumped("demo", "demo.lamps", 1280, 720);
  CHECK(result.passes);
  const std::filesystem::path golden = test::tests_folder() / "golden" / "demo-lamps-1280x720.txt";
#ifndef _WIN32
  if (const char* update = std::getenv("GRACE_UPDATE_GOLDEN"); update != nullptr && update[0] == '1') {
    std::ofstream(golden, std::ios::binary) << result.text;
  }
#endif
  CHECK_MESSAGE(test::read_text(golden) == result.text, "the dump differs from ", golden.string(),
                " (run with GRACE_UPDATE_GOLDEN=1 to accept it)");
  CHECK(test::contains(result.text, "\"Harbour mouth\""));  // the chosen row
  CHECK(test::contains(result.text, "\"Tall\""));           // its member's flame
  CHECK_FALSE(test::contains(result.text, "Nothing to show."));
}

TEST_CASE("--dump draws what grace-view's own goldens hold: GraceUI's Menu page at 1920x1080, its default size") {
  const DumpResult result = dumped("graceui", "graceui.menu", 1920, 1080);
  const std::filesystem::path golden =
      path_from_utf8(GRACE_DEV_TESTS) / ".." / ".." / ".." / "src" / "grace-view" / "tests" / "golden" /
      "menu-1920x1080.txt";
  CHECK(body_of(result.text) == test::read_text(golden));
  CHECK(result.passes);
}

TEST_CASE("--dump at another scale and theme: the menu's own scale, and a palette that changes no structure") {
  const DumpResult grace = dumped("demo", "demo.behaviour", 1280, 800, 1.5);
  const DumpResult clear = dumped("demo", "demo.behaviour", 1280, 800, 1.5, true);
  CHECK(test::contains(grace.text, "ui.scale 1.50, scale 1.111, theme grace"));
  CHECK(body_of(grace.text) == body_of(clear.text));
  CHECK(grace.passes);
  CHECK(clear.passes);
  // GraceUI's own Menu page shows the theme it is drawn in: changed from its default, with its lozenge.
  const DumpResult menu = dumped("graceui", "graceui.menu", 1920, 1080, 1, true);
  CHECK(body_of(menu.text) != body_of(dumped("graceui", "graceui.menu", 1920, 1080).text));
  DumpResult out;
  std::string why;
  CHECK_FALSE(dump_page(builtin("demo"), DumpRequest{"demo.nowhere"}, out, why));
  CHECK(why == "the built-in space demo: it has no page demo.nowhere");
}

TEST_CASE("the command line: each value checked as GraceUI's own settings take it") {
  Cli cli;
  std::string why;
  REQUIRE(parse_cli({"--dump", "demo", "demo.log", "--size", "1280x720", "--scale", "1.25", "--theme", "clear"}, cli,
                    why));
  CHECK(cli.mode == Cli::Mode::dump);
  CHECK(cli.dump.width == 1280);
  CHECK(cli.dump.ui_scale == 1.25);
  CHECK(cli.dump.clear);
  REQUIRE(parse_cli({"--space", "demo", "--space-dir", "x", "--speed", "20", "--loop", "--port", "9000"}, cli, why));
  CHECK(cli.mode == Cli::Mode::serve);
  CHECK(cli.host.spaces.size() == 2);
  CHECK(cli.host.speed == 20);
  CHECK(cli.host.port == 9000);
  REQUIRE(parse_cli({}, cli, why));
  CHECK(cli.host.port == 0);  // dashboard.port's default, when the host starts
  struct Case {
    std::vector<std::string> args;
    const char* says;
  };
  const Case cases[] = {
      {{"--port", "80"}, "--port: outside its range (as dashboard.port takes it)"},
      {{"--port", "eighty"}, "--port is a whole number"},
      {{"--dump", "demo", "demo.log", "--size", "1280x720", "--scale", "1.013"}, "--scale: not on its step"},
      {{"--dump", "demo", "demo.log", "--size", "1280x720", "--scale", "3"}, "--scale: outside its range"},
      {{"--dump", "demo", "demo.log", "--size", "1280x720", "--theme", "neon"}, "--theme: not one of its choices"},
      {{"--dump", "demo", "demo.log"}, "--dump needs --size WxH"},
      {{"--dump", "demo", "demo.log", "--size", "1280"}, "--size is WxH"},
      {{"--dump", "demo", "demo.log", "--size", "640x480", "--port", "9000"}, "--dump takes --size, --scale"},
      {{"--size", "640x480"}, "--size, --scale and --theme are for --dump"},
      {{"--speed", "0"}, "--speed is a number from 0.01 to 1000"},
      {{"--speed", "fast"}, "--speed is a number from 0.01 to 1000"},
      {{"--increment", "two"}, "--increment is a version's major and minor"},
      {{"--space"}, "--space needs a value"},
      {{"--dump", "demo"}, "--dump needs 2 values"},
      {{"--colour"}, "not an option of grace-dev's: --colour"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.says);
    CHECK_FALSE(parse_cli(c.args, cli, why));
    CHECK_MESSAGE(test::contains(why, c.says), why);
  }
  REQUIRE(parse_cli({"--port", "9000", "--help"}, cli, why));
  CHECK(cli.mode == Cli::Mode::help);
}

TEST_CASE("--dump gives a stream's fixture to the view as the menu's copy: {rows}, its newest 200 alone (D262)") {
  // The demo's own Log: its rows, drawn as in game.
  const DumpResult log = dumped("demo", "demo.log", 1920, 1080);
  CHECK(log.passes);
  CHECK(test::contains(log.text, "\"The wick was trimmed.\""));
  // A stream longer than the menu keeps: the oldest past 200 are not drawn, the newest are.
  SpaceData space = builtin("demo");
  grace::protocol::Json rows = grace::protocol::Json::array();
  for (int i = 1; i <= 250; ++i) {
    rows.push_back({{"seq", i}, {"time", "19:00"}, {"line", "Line " + std::to_string(i) + " of the night."},
                    {"level", "info"}});
  }
  space.fixtures["lantern.log"] = rows;
  DumpResult out;
  std::string why;
  REQUIRE_MESSAGE(dump_page(space, DumpRequest{"demo.log", 1920, 1080, default_ui_scale(), false}, out, why), why);
  CHECK(test::contains(out.text, "\"Line 250 of the night.\""));  // the newest, first in a live feed
  CHECK_FALSE(test::contains(out.text, "\"Line 50 of the night.\""));  // past the menu's 200
}
