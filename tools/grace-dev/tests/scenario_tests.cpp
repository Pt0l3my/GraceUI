// grace-dev's spaces and scenarios (D168): the built-in spaces and an author's folder load and pass their checks; a bad
// folder and a bad scenario each fail with a message naming the file, the field or the line; the demo's own scenario
// is whole.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "host.h"
#include "json_text.h"
#include "scenario.h"
#include "space_data.h"
#include "support.h"

namespace {

using namespace grace::dev;
using grace::dev::test::contains;
using grace::dev::test::TempFolder;
using grace::protocol::Json;

const std::filesystem::path kTests = test::tests_folder();

SpaceData demo() {
  SpaceData space;
  std::string why;
  REQUIRE_MESSAGE(load_builtin("demo", space, why), why);
  return space;
}

std::string refusal(std::string_view text) {
  Scenario scenario;
  std::string why;
  CHECK_FALSE(load_scenario(text, "test.json", demo(), scenario, why));
  return why;
}

}  // namespace

TEST_CASE("the built-in spaces load, GraceUI's own first; the demo's fixtures and scenario come with it") {
  CHECK(builtin_ids() == std::vector<std::string>{"graceui", "demo"});
  SpaceData own;
  std::string why;
  REQUIRE(load_builtin("graceui", own, why));
  CHECK(own.fixtures.contains("graceui.about"));  // GraceUI's own pages' sample states
  CHECK(own.fixtures["graceui.compatibility"]["checks"].size() == 5);
  CHECK(own.scenario_text.empty());
  const SpaceData lanterns = demo();
  CHECK(lanterns.fixtures.contains("lantern.status"));
  CHECK(lanterns.fixtures["lantern.log"].size() == 3);
  CHECK(lanterns.scenario_name == "fixtures/demo/scenario.json");
  CHECK_FALSE(load_builtin("lighthouse", own, why));
  CHECK(why == "there is no built-in space lighthouse (there are: graceui demo)");
}

TEST_CASE("the demo's own scenario is whole: its steps in time order, its reactions and what Wind shield inherits") {
  const SpaceData space = demo();
  Scenario scenario;
  std::string why;
  REQUIRE_MESSAGE(load_scenario(space.scenario_text, space.scenario_name, space, scenario, why), why);
  CHECK(scenario.length == 60000);
  CHECK_FALSE(scenario.loop);
  CHECK(scenario.steps.size() == 9);
  CHECK(scenario.steps.front().line == 10);
  CHECK(scenario.reactions.size() == 2);
  REQUIRE(scenario.reaction("lantern.clear_log") != nullptr);
  CHECK(scenario.reaction("lantern.clear_log")->actions.front().kind == Action::Kind::clear);
  REQUIRE(scenario.inherited.size() == 1);
  CHECK(scenario.inherited[0].key == "lantern.shield");
}

TEST_CASE("an author's space folder loads; its fixtures and scenario with it") {
  SpaceData space;
  std::string why;
  REQUIRE_MESSAGE(load_folder(kTests / "tidepool", space, why), why);
  CHECK(space.id == "tidepool");
  CHECK(space.origin == "the space folder 'tidepool'");
  CHECK(space.fixtures["pool.notes"].size() == 2);
  CHECK(space.scenario_name == "scenario.json");
  Scenario scenario;
  REQUIRE_MESSAGE(load_scenario(space.scenario_text, space.scenario_name, space, scenario, why), why);
  CHECK(scenario.steps.size() == 2);
  CHECK(scenario.length == 2000);
}

TEST_CASE("a bad space folder fails, naming its file and the field") {
  SpaceData space;
  std::string why;
  CHECK_FALSE(load_folder(kTests / "bad-space", space, why));
  CHECK(why == "the space folder 'bad-space': pages.json: pages[0].help.steps: not a list");
  CHECK_FALSE(load_folder(kTests / "nowhere", space, why));
  CHECK(why == "the space folder 'nowhere': not a folder");

  struct Case {
    const char* file;
    const char* text;
    const char* says;
  };
  const Case cases[] = {
      {"topics.json", R"({"pool.depth": {}})", "topics.json: pool.depth is not a topic the space declares"},
      {"topics.json", R"({"pool.status": []})", "topics.json: pool.status: a state topic's fixture is an object"},
      {"topics.json", R"({"settings": {}})", "topics.json: settings: the settings topic comes from settings.json"},
      {"topics.json", R"({"pool.notes": [{"seq": 1}, {"seq": 3}]})",
       "pool.notes: row 2's seq does not follow on from the row before it by one"},
      {"topics.json", R"({"pool.notes": [{"seq": 1}, {"seq": 2}, {"seq": 3}, {"seq": 4}, {"seq": 5}]})",
       "pool.notes: more rows than its ring holds (4)"},
      {"topics.json", "{\n  \"pool.status\": {\n}", "topics.json: line 3: not JSON"},
      {"settings.json", R"({"schema": {"number": 1, "hash": "x"}, "settings": [{"key": "pool.watching"}]})",
       "settings.json: settings[0].type"},
      {"scenario.json", R"({"scenario": 1, "space": "tidepool", "steps": [{"at": 5, "clear": "pool.status"}]})",
       "scenario.json line 1: step 1: pool.status is a state topic: rows and clear are for streams"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.text);
    TempFolder folder;
    folder.copy_from(kTests / "tidepool");
    folder.write(c.file, c.text);
    SpaceData loaded;
    if (std::string(c.file) == "scenario.json") {
      REQUIRE(load_folder(folder.path, loaded, why));
      Scenario scenario;
      CHECK_FALSE(load_scenario(loaded.scenario_text, loaded.scenario_name, loaded, scenario, why));
    } else {
      CHECK_FALSE(load_folder(folder.path, loaded, why));
    }
    CHECK_MESSAGE(contains(why, c.says), why);
  }
}

TEST_CASE("a space's own pages and commands are checked before it is served") {
  TempFolder folder;
  folder.copy_from(kTests / "tidepool");
  const std::string original = test::read_text(kTests / "tidepool" / "pages.json");
  struct Case {
    const char* from;
    const char* to;
    const char* says;
  };
  const Case cases[] = {
      {R"("command": "pool.count")", R"("command": "pool.empty")",
       "tidepool.today's action count sends pool.empty, which the space does not declare"},
      {R"("topic": "pool.notes")", R"("topic": "pool.status")", "live_feed cannot draw the state topic pool.status"},
      {R"("pool.count"])", R"("pool.count", "dashboard.disconnect"])",
       "dashboard.disconnect is a command of GraceUI's own space alone"},
      {R"({"id": "settings", "shape": "state"},)", "", "declares no settings topic, so no view would see them"},
      {R"("keys": ["pool.watching", "pool.every_s"])", R"("keys": ["pool.watching", "pool.depth"])",
       "pages.json and settings.json disagree: tidepool.watch names pool.depth"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.to);
    std::string text = original;
    const std::size_t at = text.find(c.from);
    REQUIRE(at != std::string::npos);
    text.replace(at, std::string_view(c.from).size(), c.to);
    folder.write("pages.json", text);
    SpaceData loaded;
    std::string why;
    CHECK_FALSE(load_folder(folder.path, loaded, why));
    CHECK_MESSAGE(contains(why, c.says), why);
  }
}

TEST_CASE("a bad scenario fails its check, naming its file, its line and its step") {
  const std::string text = test::read_text(kTests / "bad-scenario.json");
  Scenario scenario;
  std::string why;
  CHECK_FALSE(load_scenario(text, "bad-scenario.json", demo(), scenario, why));
  CHECK(why == "bad-scenario.json line 14: step 2: lantern.wick is not a topic of the space demo");

  struct Case {
    const char* text;
    const char* says;
  };
  const Case cases[] = {
      {"{\"scenario\": 1,\n\"space\": \"demo\",\n\"steps\": [\n}", "test.json line 4: not JSON"},
      {R"({"scenario": 2, "space": "demo"})", "test.json: \"scenario\" is its format, 1"},
      {R"({"scenario": 1, "space": "tidepool"})", "\"space\" names the space it plays in, demo"},
      {R"({"scenario": 1, "space": "demo", "tempo": 3})", "it has a field \"tempo\", which a scenario does not take"},
      {"{\"scenario\": 1, \"space\": \"demo\", \"steps\": [\n  {\"at\": 10, \"clear\": \"lantern.log\"},\n"
       "  {\"at\": 5, \"clear\": \"lantern.log\"}\n]}",
       "test.json line 3: step 2: it comes before the step above it"},
      {R"({"scenario": 1, "space": "demo", "steps": [{"at": 1, "state": "settings", "body": {}}]})",
       "the settings topic changes only through settings.set and settings.reset"},
      {R"({"scenario": 1, "space": "demo", "steps": [{"at": 1, "state": "lantern.status"}]})",
       "state takes a body, an object"},
      {R"({"scenario": 1, "space": "demo", "steps": [{"at": 1, "rows": "lantern.log", "add": [{"seq": 9}]}]})",
       "a row has no seq of its own: grace-dev numbers the rows"},
      {R"({"scenario": 1, "space": "demo", "steps": [{"at": 1, "rows": "lantern.log", "clear": "lantern.log"}]})",
       "it names exactly one of state, rows and clear"},
      {R"({"scenario": 1, "space": "demo", "steps": [{"rows": "lantern.log", "add": [{}]}]})",
       "it has \"at\", its time in whole milliseconds"},
      {R"({"scenario": 1, "space": "demo", "reactions": [{"on": "settings.set"}]})",
       "settings.set is GraceUI's own: settings go through the settings store"},
      {R"({"scenario": 1, "space": "demo", "reactions": [{"on": "lantern.polish"}]})",
       "lantern.polish is not a command of the space demo"},
      {R"({"scenario": 1, "space": "demo", "reactions": [{"on": "lantern.trim"}, {"on": "lantern.trim"}]})",
       "reaction 2: lantern.trim has a reaction already"},
      {R"({"scenario": 1, "space": "demo", "reactions": [{"on": "lantern.trim", "answer": {"error": "sulking"}}]})",
       "answer's error is not one of the protocol's codes"},
      {R"({"scenario": 1, "space": "demo", "reactions": [{"on": "lantern.trim", "do": [{"at": 4, "clear": "lantern.log"}]}]})",
       "action 1: a reaction's action has no time"},
      {R"({"scenario": 1, "space": "demo", "inherited": [{"key": "lantern.mode", "on": true, "source": "x"}]})",
       "lantern.mode is not an inherit tri-state of the space (D163)"},
      {R"({"scenario": 1, "space": "demo", "length_ms": 5, "steps": [{"at": 9, "clear": "lantern.log"}]})",
       "\"length_ms\" is shorter than its last step's time"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.text);
    const std::string said = refusal(c.text);
    CHECK_MESSAGE(contains(said, c.says), said);
  }
}

TEST_CASE("a looping scenario needs a length, and a --scenario plays only in a space being served") {
  TempFolder folder;
  folder.write("still.json", R"({"scenario": 1, "space": "demo", "loop": true})");
  folder.write("elsewhere.json", R"({"scenario": 1, "space": "tidepool"})");
  HostOptions options;
  options.spaces = {SpaceSource{"demo", {}}};
  options.scenarios = {folder.path / "still.json"};
  Loaded loaded;
  std::string why;
  CHECK_FALSE(load(options, loaded, why));
  CHECK(why == "still.json: it loops, but has no length: give it length_ms, or a step after 0 ms");
  options.scenarios = {folder.path / "elsewhere.json"};
  CHECK_FALSE(load(options, loaded, why));
  CHECK(why == "elsewhere.json: it plays in the space tidepool, which grace-dev is not serving");
  options.spaces = {SpaceSource{"demo", {}}, SpaceSource{"demo", {}}};
  options.scenarios.clear();
  CHECK_FALSE(load(options, loaded, why));
  CHECK(why == "the built-in space demo: its id, demo, is the built-in space demo's too");
  options.spaces = {SpaceSource{"demo", {}}};
  options.loop = true;
  REQUIRE(load(options, loaded, why));  // the demo's own scenario has a length
  CHECK(loaded.scenarios[0]->loop);
  options.no_scenario = true;
  REQUIRE(load(options, loaded, why));
  CHECK_FALSE(loaded.scenarios[0].has_value());
}

TEST_CASE("JSON's first error and a list's elements are found by line") {
  Json json;
  std::string why;
  CHECK_FALSE(parse_json("{\n  \"a\": 1,\n  \"b\": \n}\n", json, why));
  CHECK(why == "line 4: not JSON");
  CHECK_FALSE(parse_json("", json, why));
  CHECK(why == "line 1: not JSON");
  CHECK_FALSE(parse_json("{\"a\": \"\xff\"}", json, why));  // not UTF-8
  const std::string text =
      "{\"about\": \"a ] and a [ and \\\" in a string\",\n"
      " \"other\": [1, 2],\n"
      " \"steps\": [\n"
      "   {\"at\": 1, \"add\": [[], {\"x\": \"]\"}]},\n"
      "\n"
      "   {\"at\": 2},  {\"at\": 3}\n"
      " ],\n"
      " \"nested\": {\"steps\": [9]}\n"
      "}\n";
  REQUIRE(parse_json(text, json, why));
  CHECK(element_lines(text, "steps") == std::vector<std::size_t>{4, 6, 6});
  CHECK(element_lines(text, "other") == std::vector<std::size_t>{2, 2});
  CHECK(element_lines(text, "nowhere").empty());
  CHECK(element_lines("{\"steps\": []}", "steps").empty());
}
