// grace-dev's fake host over real sockets on loopback (§9.2, D168), with grace-serve's own test client: the welcome
// naming the spaces; the settings topic from each space's settings store, its commands validated as GraceUI's own space
// validates them (invariant 3), a secret never in a frame or a printed line (invariant 5); fixture states and stream
// rows; the scenario's steps on time and its reactions; GraceUI's own commands; the web files; and a start that fails
// with its reason, leaving nothing running.
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "harness.h"
#include "host.h"
#include "support.h"

namespace {

using namespace grace::dev;
using namespace grace::serve::iotest;
using grace::dev::test::contains;
using grace::dev::test::TempFolder;
using grace::protocol::Json;
using Clock = std::chrono::steady_clock;

// A running grace-dev, its printed lines kept.
struct Host {
  std::mutex mutex;
  std::vector<std::string> lines;
  DevHost host{[this](Out, std::string_view line) {
    std::lock_guard lock(mutex);
    lines.emplace_back(line);
  }};
  Clock::time_point started;

  std::vector<std::string> said() {
    std::lock_guard lock(mutex);
    return lines;
  }
  // `line` printed, within `limit`.
  bool said_within(std::string_view line, Millis limit) {
    const Clock::time_point end = Clock::now() + limit;
    do {
      for (const std::string& one : said()) {
        if (one == line) {
          return true;
        }
      }
      std::this_thread::sleep_for(5ms);
    } while (Clock::now() < end);
    return false;
  }
  std::uint16_t port() const { return host.port(); }
};

HostOptions options_of(std::initializer_list<const char*> ids, double speed = kMinSpeed) {
  HostOptions options;
  for (const char* id : ids) {
    options.spaces.push_back(SpaceSource{id, {}});
  }
  options.speed = speed;  // the slowest: the scenarios' steps wait minutes, so only what a test does changes anything
  return options;
}

std::unique_ptr<Host> serve(HostOptions options) {
  auto out = std::make_unique<Host>();
  options.timings = fast_timings();
  for (int attempt = 0; attempt < 5; ++attempt) {
    options.port = free_port();
    std::string why;
    out->started = Clock::now();
    if (out->host.start(options, why)) {
      REQUIRE(out->host.wait_files(2s));
      return out;
    }
    REQUIRE_MESSAGE(out->host.start_result() == grace::serve::StartResult::port_taken, why);
  }
  FAIL("no free port");
  return nullptr;
}

// A browser's tab: a page load's cookie, a ticket, a WebSocket joined, and every frame it read.
struct View {
  explicit View(std::uint16_t port) {
    cookie = cookie_from(request(port, page_load(port)));
    REQUIRE(cookie.size() == 43);
    const std::string ticket = fetch_ticket(port, cookie);
    REQUIRE(ticket.size() == 43);
    REQUIRE(ws.join(port, cookie, ticket, &welcome));
  }

  void send(const Json& message) { REQUIRE(ws.send_json(message)); }
  std::uint32_t sub(std::string_view space, std::string_view topic) {
    const std::uint32_t id = next_id++;
    send(frame("sub", {{"space", std::string(space)}, {"id", id}, {"topic", std::string(topic)}}));
    return id;
  }
  std::uint32_t cmd(std::string_view space, std::string_view name, Json args = Json::object()) {
    const std::uint32_t id = next_id++;
    send(frame("cmd", {{"space", std::string(space)}, {"id", id}, {"name", std::string(name)}, {"args", args}}));
    return id;
  }
  std::optional<Json> next(Millis limit = 2s) {
    std::optional<Json> json = ws.next_json(limit);
    if (json.has_value()) {
      seen.push_back(json->dump());
    }
    return json;
  }
  // Frames until one matches, within `limit`.
  std::optional<Json> wait(const std::function<bool(const Json&)>& match, Millis limit = 3s) {
    const Clock::time_point end = Clock::now() + limit;
    while (Clock::now() < end) {
      std::optional<Json> json = next(std::chrono::duration_cast<Millis>(end - Clock::now()) + 1ms);
      if (!json.has_value()) {
        return std::nullopt;
      }
      if (match(*json)) {
        return json;
      }
    }
    return std::nullopt;
  }
  std::optional<Json> result(std::uint32_t id) {
    return wait([id](const Json& j) { return j["t"] == "result" && j["id"] == id; });
  }
  std::optional<Json> state(std::string_view topic, std::uint64_t after_rev = 0) {
    return wait([&](const Json& j) { return j["t"] == "state" && j["topic"] == topic && j["rev"] > after_rev; });
  }
  std::optional<Json> rows(std::string_view topic) {
    return wait([&](const Json& j) { return j["t"] == "rows" && j["topic"] == topic; });
  }
  // Nothing but pings within `limit`.
  bool quiet(Millis limit) {
    std::optional<Json> json = next(limit);
    return !json.has_value();
  }

  WsClient ws;
  Json welcome;
  std::string cookie;
  std::vector<std::string> seen;
  std::uint32_t next_id = 1;
};

Json set(std::string_view key, Json value) { return Json{{"key", std::string(key)}, {"value", std::move(value)}}; }

std::vector<std::uint64_t> seqs(const Json& rows) {
  std::vector<std::uint64_t> out;
  for (const Json& row : rows["rows"]) {
    out.push_back(row["seq"].get<std::uint64_t>());
  }
  return out;
}

}  // namespace

TEST_CASE("hello and welcome: the spaces in the order given, at GraceUI's version and this increment") {
  auto dev = serve(options_of({"graceui", "demo"}));
  View view(dev->port());
  REQUIRE(view.welcome["spaces"].size() == 2);
  CHECK(view.welcome["spaces"][0]["id"] == "graceui");
  CHECK(view.welcome["spaces"][1]["id"] == "demo");
  CHECK(view.welcome["spaces"][1]["name"] == "Demo space");
  CHECK(view.welcome["spaces"][1]["version"] == GRACE_DEV_VERSION);
  CHECK(view.welcome["spaces"][1]["increment"] == "0.2");
  CHECK(view.welcome["graceui"] == GRACE_DEV_VERSION);
  CHECK(default_port() == grace::serve::kDefaultPort);  // dashboard.port's default is the server's own
  // With none given, both built-in spaces, GraceUI's own first.
  auto both = serve(HostOptions{});
  View other(both->port());
  REQUIRE(other.welcome["spaces"].size() == 2);
  CHECK(other.welcome["spaces"][0]["id"] == "graceui");
}

TEST_CASE("the settings topic comes from the space's settings store; settings.set changes it and publishes it anew") {
  auto dev = serve(options_of({"demo"}));
  View view(dev->port());
  view.sub("demo", "settings");
  const std::optional<Json> first = view.state("settings");
  REQUIRE(first.has_value());
  grace::protocol::SettingsBody body;
  grace::protocol::DecodeError error;
  REQUIRE_MESSAGE(grace::protocol::decode((*first)["body"], body, "", error), error.field, ": ", error.why);
  CHECK(body.values["lantern.brightness"] == 0.8);
  CHECK(body.values["lantern.oil_token"] == "");
  CHECK(body.changed.empty());
  CHECK(body.description.size() == 12);
  // The demo's scenario says what Wind shield, on inherit, resolves to (D163).
  REQUIRE(body.inherited.has_value());
  REQUIRE(body.inherited->size() == 1);
  CHECK((*body.inherited)[0].key == "lantern.shield");
  CHECK((*body.inherited)[0].on);

  const std::uint64_t rev = (*first)["rev"].get<std::uint64_t>();
  const std::uint32_t id = view.cmd("demo", "settings.set", set("lantern.brightness", 0.5));
  std::optional<Json> answer;
  std::optional<Json> fresh;
  for (int i = 0; i < 2 && (!answer || !fresh); ++i) {  // either may come first
    const std::optional<Json> next = view.wait([&](const Json& j) {
      return (j["t"] == "result" && j["id"] == id) || (j["t"] == "state" && j["rev"] > rev);
    });
    REQUIRE(next.has_value());
    (((*next)["t"] == "result") ? answer : fresh) = next;
  }
  REQUIRE(answer.has_value());
  CHECK((*answer)["ok"] == true);
  REQUIRE(fresh.has_value());
  CHECK((*fresh)["body"]["values"]["lantern.brightness"] == 0.5);
  CHECK((*fresh)["body"]["changed"] == Json::array({"lantern.brightness"}));
  // Inherit no more: the shield set on, nothing is inherited.
  view.cmd("demo", "settings.set", set("lantern.shield", "on"));
  const std::optional<Json> shielded = view.state("settings", (*fresh)["rev"].get<std::uint64_t>());
  REQUIRE(shielded.has_value());
  CHECK_FALSE((*shielded)["body"].contains("inherited"));
  // The same value again changes nothing: ok, and no new state.
  const std::uint32_t again = view.cmd("demo", "settings.set", set("lantern.shield", "on"));
  CHECK((*view.result(again))["ok"] == true);
  CHECK(view.quiet(100ms));
  CHECK(dev->said_within("demo: settings.set lantern.brightness: ok, 1 changed", 1s));
}

TEST_CASE("each refusal is the store's: its code, and its words in the result's body") {
  auto dev = serve(options_of({"demo", "graceui"}));
  View view(dev->port());
  struct Case {
    const char* space;
    const char* name;
    Json args;
    const char* code;
  };
  const Case cases[] = {
      {"demo", "settings.set", set("lantern.festival", true), "locked"},       // later, and locked
      {"demo", "settings.set", set("lantern.night_only", true), "not_now"},    // waiting on its check (D169)
      {"demo", "settings.set", set("lantern.brightness", 2.0), "invalid_value"},
      {"demo", "settings.set", set("lantern.flame", 1), "unknown_key"},
      {"demo", "settings.reset", Json{{"scope", "page"}, {"target", "demo.behaviour"}}, "confirm_required"},
      {"demo", "settings.reset", Json{{"scope", "all"}}, "confirm_required"},
      {"demo", "settings.set", Json{{"key", "lantern.brightness"}}, "invalid_value"},  // no value
      {"graceui", "settings.set", set("dashboard.lan", true), "locked"},
      {"graceui", "settings.set", set("ui.scale", 1.013), "invalid_value"},  // off its step
  };
  for (const Case& c : cases) {
    CAPTURE(c.args.dump());
    const std::optional<Json> answer = view.result(view.cmd(c.space, c.name, c.args));
    REQUIRE(answer.has_value());
    CHECK((*answer)["error"] == c.code);
    CHECK_FALSE(answer->contains("ok"));
    REQUIRE((*answer)["detail"].is_string());
    CHECK_FALSE((*answer)["detail"].get<std::string>().empty());
  }
  // Confirmed, a page's reset is taken.
  const std::optional<Json> reset = view.result(
      view.cmd("demo", "settings.reset", Json{{"scope", "page"}, {"target", "demo.behaviour"}, {"confirm", true}}));
  CHECK((*reset)["ok"] == true);
}

TEST_CASE("a secret, once set, shows only \"set\" in every frame, and no line grace-dev prints holds it") {
  constexpr std::string_view kToken = "invented-oil-token-4417";
  auto dev = serve(options_of({"demo"}));
  View view(dev->port());
  View other(dev->port());
  view.sub("demo", "settings");
  other.sub("demo", "settings");
  REQUIRE(view.state("settings").has_value());
  REQUIRE(other.state("settings").has_value());
  const std::uint32_t id = view.cmd("demo", "settings.set", set("lantern.oil_token", std::string(kToken)));
  const std::optional<Json> set_state = view.wait([](const Json& j) {
    return j["t"] == "state" && j["body"]["values"]["lantern.oil_token"] == "set";
  });
  REQUIRE(set_state.has_value());
  CHECK(set_state->dump().find("lantern.oil_token") != std::string::npos);
  REQUIRE(other.wait([](const Json& j) { return j["t"] == "state" && j["body"]["values"]["lantern.oil_token"] == "set"; }));
  CHECK(std::find(set_state->at("body")["changed"].begin(), set_state->at("body")["changed"].end(),
                  "lantern.oil_token") != set_state->at("body")["changed"].end());
  // A refused secret (too long) is answered without its value too.
  view.cmd("demo", "settings.set", set("lantern.oil_token", std::string(1100, 'k') + std::string(kToken)));
  view.quiet(150ms);
  other.quiet(50ms);
  bool answered = false;
  for (const std::string& frame : view.seen) {
    answered = answered || contains(frame, "\"id\":" + std::to_string(id));
    CHECK_FALSE(contains(frame, kToken));
  }
  CHECK(answered);
  for (const std::string& frame : other.seen) {
    CHECK_FALSE(contains(frame, kToken));
  }
  for (const std::string& line : dev->said()) {
    CHECK_FALSE(contains(line, kToken));
    CHECK_FALSE(contains(line, view.cookie));
  }
}

TEST_CASE("fixture states, and a stream's rows from the oldest held, in order") {
  auto dev = serve(options_of({"demo", "graceui"}));
  View view(dev->port());
  view.sub("demo", "lantern.status");
  const std::optional<Json> status = view.state("lantern.status");
  REQUIRE(status.has_value());
  CHECK((*status)["body"]["tiles"][0]["value"] == "Steady");
  CHECK((*status)["rev"] == 1);
  view.sub("demo", "lantern.log");
  const std::optional<Json> log = view.rows("lantern.log");
  REQUIRE(log.has_value());
  CHECK((*log)["from_seq"] == 1);
  CHECK(seqs(*log) == std::vector<std::uint64_t>{1, 2, 3});
  CHECK((*log)["rows"][2]["line"] == "The flame guttered in a draught, then steadied.");
  view.sub("demo", "lantern.journal");
  const std::optional<Json> journal = view.rows("lantern.journal");
  REQUIRE(journal.has_value());
  CHECK(seqs(*journal) == std::vector<std::uint64_t>{1, 2});
  // A topic with no fixture publishes nothing (absence is silent), and grace-dev says so once.
  view.sub("graceui", "graceui.about");
  CHECK(view.quiet(150ms));
  CHECK(dev->said_within("graceui: graceui.about is watched, but has no fixture state: it publishes nothing", 1s));
}

TEST_CASE("an author's space folder: its fixtures, its scripted steps near their times, and its reaction's refusal") {
  HostOptions options;
  options.spaces = {SpaceSource{{}, test::tests_folder() / "tidepool"}};
  options.speed = 2;  // its note at 1 s comes at 500 ms, its state at 2 s at 1 s
  auto dev = serve(options);
  View view(dev->port());
  REQUIRE(view.welcome["spaces"][0]["id"] == "tidepool");
  view.sub("tidepool", "pool.notes");
  view.sub("tidepool", "pool.status");
  // Every frame of the two topics as it comes, with when it came.
  std::vector<std::pair<Json, Millis>> frames;
  const Clock::time_point end = Clock::now() + 4s;
  while (frames.size() < 4 && Clock::now() < end) {
    const std::optional<Json> next = view.next(1s);
    if (next.has_value() && ((*next)["t"] == "rows" || (*next)["t"] == "state")) {
      frames.emplace_back(*next, std::chrono::duration_cast<Millis>(Clock::now() - dev->started));
    }
  }
  REQUIRE(frames.size() == 4);
  // The fixture's rows and state first, in either order, as soon as they are watched.
  const bool rows_first = frames[0].first["t"] == "rows";
  const Json& held = frames[rows_first ? 0 : 1].first;
  const Json& status = frames[rows_first ? 1 : 0].first;
  CHECK(seqs(held) == std::vector<std::uint64_t>{7, 8});
  CHECK(status["body"]["tiles"][0]["value"] == "A hand's span");
  CHECK(status["rev"] == 1);
  CHECK(frames[1].second < 450ms);
  const Json& note = frames[2].first;
  CHECK(seqs(note) == std::vector<std::uint64_t>{9});
  CHECK(note["rows"][0]["line"] == "A gull looked in and left.");
  CHECK(frames[2].second >= 450ms);
  CHECK(frames[2].second <= 2000ms);
  const Json& later = frames[3].first;
  CHECK(later["rev"] == 2);
  CHECK(later["body"]["tiles"][0]["value"] == "A finger's width");
  CHECK(frames[3].second >= 950ms);
  CHECK(frames[3].second <= 3000ms);
  // Said on grace-dev's thread once its last step is out, which may be just after the view has the step's frame.
  CHECK(dev->said_within("tidepool: the scenario has played to its end", 1s));

  const std::optional<Json> refused = view.result(view.cmd("tidepool", "pool.count"));
  REQUIRE(refused.has_value());
  CHECK((*refused)["error"] == "not_now");
  CHECK((*refused)["detail"] == "The crabs are hiding; count them at low water.");
}

TEST_CASE("a fast, looping scenario plays its passes again, each from the fixture's states") {
  TempFolder folder;
  folder.write("quick.json", R"({"scenario": 1, "space": "demo", "length_ms": 1000, "loop": true, "steps": [
    {"at": 500, "state": "lantern.status", "body": {"tiles": [], "sentence": "Dark for a moment."}},
    {"at": 600, "rows": "lantern.log", "add": [{"time": "Now", "line": "A blink."}]}
  ]})");
  HostOptions options = options_of({"demo"}, 10);  // a pass every 100 ms
  options.scenarios = {folder.path / "quick.json"};
  auto dev = serve(options);
  View view(dev->port());
  view.sub("demo", "lantern.log");
  // Rows from several passes, numbered on from the fixture's, whatever the replay held.
  std::vector<std::uint64_t> numbers;
  const Clock::time_point end = Clock::now() + 3s;
  while (numbers.size() < 6 && Clock::now() < end) {
    const std::optional<Json> rows = view.rows("lantern.log");
    REQUIRE(rows.has_value());
    for (const std::uint64_t seq : seqs(*rows)) {
      numbers.push_back(seq);
    }
  }
  REQUIRE(numbers.size() >= 6);
  for (std::size_t i = 1; i < numbers.size(); ++i) {
    CHECK(numbers[i] == numbers[i - 1] + 1);
  }
  view.sub("demo", "lantern.status");
  // The status goes dark at 500 ms of each pass and back to its fixture at the next pass's start.
  REQUIRE(view.wait([](const Json& j) { return j["t"] == "state" && j["body"]["sentence"] == "Dark for a moment."; }));
  REQUIRE(view.wait([](const Json& j) {
    return j["t"] == "state" && j["body"]["tiles"].size() == 3 && j["body"]["tiles"][0]["value"] == "Steady";
  }));
}

TEST_CASE("the scenario's reactions: lantern.clear_log empties the log and starts it over; lantern.trim") {
  auto dev = serve(options_of({"demo"}));
  View view(dev->port());
  view.sub("demo", "lantern.log");
  REQUIRE(seqs(*view.rows("lantern.log")) == std::vector<std::uint64_t>{1, 2, 3});
  const std::uint32_t id = view.cmd("demo", "lantern.clear_log");
  std::optional<Json> answer;
  std::optional<Json> cleared;
  std::optional<Json> fresh;
  for (int i = 0; i < 3 && (!answer || !cleared || !fresh); ++i) {
    const std::optional<Json> next = view.wait([&](const Json& j) {
      return (j["t"] == "result" && j["id"] == id) || (j["t"] == "rows" && j["topic"] == "lantern.log");
    });
    REQUIRE(next.has_value());
    (((*next)["t"] == "result") ? answer : (*next)["rows"].empty() ? cleared : fresh) = next;
  }
  CHECK((*answer)["ok"] == true);
  REQUIRE(cleared.has_value());
  CHECK((*cleared)["from_seq"] == 5);  // an empty batch past a gap: every view clears the log now (protocol 1.1)
  REQUIRE(fresh.has_value());
  CHECK((*fresh)["from_seq"] == 5);  // past a gap: the stream starts over there
  CHECK(seqs(*fresh) == std::vector<std::uint64_t>{5});
  CHECK((*fresh)["rows"][0]["line"] == "The log was cleared. The journal is kept.");
  // A view that comes later sees the log as it is now: the one row, not the cleared ones.
  View later(dev->port());
  later.sub("demo", "lantern.log");
  const std::optional<Json> replay = later.rows("lantern.log");
  REQUIRE(replay.has_value());
  CHECK(seqs(*replay) == std::vector<std::uint64_t>{5});

  view.sub("demo", "lantern.status");
  REQUIRE(view.state("lantern.status").has_value());
  CHECK((*view.result(view.cmd("demo", "lantern.trim")))["ok"] == true);
  const std::optional<Json> trimmed = view.state("lantern.status", 1);
  REQUIRE(trimmed.has_value());
  CHECK((*trimmed)["body"]["tiles"][2]["value"] == "Just now");
}

TEST_CASE("with no scenario a space's own command is answered ok, and nothing changes") {
  HostOptions options = options_of({"demo"});
  options.no_scenario = true;
  auto dev = serve(options);
  View view(dev->port());
  view.sub("demo", "lantern.status");
  REQUIRE(view.state("lantern.status").has_value());
  view.sub("demo", "settings");
  const std::optional<Json> settings = view.state("settings");
  REQUIRE(settings.has_value());
  CHECK_FALSE((*settings)["body"].contains("inherited"));  // no scenario says what Wind shield inherits
  CHECK((*view.result(view.cmd("demo", "lantern.trim")))["ok"] == true);
  CHECK(view.quiet(150ms));
}

TEST_CASE("GraceUI's own commands: dashboard.disconnect renews the session; menu.close has no menu here") {
  auto dev = serve(options_of({"graceui"}));
  View view(dev->port());
  const std::optional<Json> closed = view.result(view.cmd("graceui", "menu.close"));
  REQUIRE(closed.has_value());
  CHECK((*closed)["error"] == "not_now");
  const std::string old_cookie = view.cookie;
  const std::optional<Json> answer = view.result(view.cmd("graceui", "dashboard.disconnect"));
  REQUIRE(answer.has_value());
  CHECK((*answer)["ok"] == true);
  const std::optional<Json> bye = view.wait([](const Json& j) { return j["t"] == "bye"; });
  REQUIRE(bye.has_value());
  CHECK((*bye)["reason"] == "unauthorised");
  CHECK(request(dev->port(), ticket_post(dev->port(), old_cookie)).status == 403);
  View again(dev->port());  // a page load gets a new cookie, and joins
  CHECK(again.cookie != old_cookie);
}

TEST_CASE("the web files: the placeholder page, a GraceUI folder's listed files, and a plain folder from memory") {
  {
    auto dev = serve(options_of({"demo"}));
    const Reply page = request(dev->port(), page_load(dev->port()));
    CHECK(page.status == 200);
    CHECK(contains(page.body, "grace-dev is running"));
    CHECK(page.field("set-cookie").has_value());
  }
  {
    WebFolder folder;
    HostOptions options = options_of({"demo"});
    options.web = folder.path;
    auto dev = serve(options);
    CHECK(dev->host.loaded().web_from_folder);
    CHECK(dev->host.stats().files_ok);
    const Reply script = request(dev->port(), plain_get(dev->port(), "/assets/app.js"));
    CHECK(script.status == 200);
    CHECK(script.body == folder.script);
    CHECK(request(dev->port(), plain_get(dev->port(), "/stray.html")).status == 404);  // not listed
  }
  {
    TempFolder folder;
    folder.write("index.html", "<!DOCTYPE html>\n<title>Tide pool</title><p>An invented page.</p>\n");
    folder.write("assets/app.3f9c.js", "console.log('the tide turns');\n");
    folder.write("notes.md", "Not a type grace-serve serves.\n");
    folder.write(".cache/stale.js", "console.log('hidden');\n");
    HostOptions options = options_of({"demo"});
    options.web = folder.path;
    auto dev = serve(options);
    CHECK_FALSE(dev->host.loaded().web_from_folder);
    CHECK(dev->host.loaded().web_files.size() == 2);
    CHECK(dev->host.loaded().web_skipped == std::vector<std::string>{"notes.md"});
    const Reply page = request(dev->port(), page_load(dev->port()));
    CHECK(page.status == 200);
    CHECK(contains(page.body, "Tide pool"));
    CHECK(request(dev->port(), plain_get(dev->port(), "/assets/app.3f9c.js")).status == 200);
    CHECK(request(dev->port(), plain_get(dev->port(), "/notes.md")).status == 404);
    CHECK(request(dev->port(), plain_get(dev->port(), "/.cache/stale.js")).status != 200);
  }
}

TEST_CASE("a start that fails says why, and leaves nothing running") {
  Host dev;
  std::string why;
  HostOptions options;
  options.spaces = {SpaceSource{{}, test::tests_folder() / "bad-space"}};
  options.port = free_port();
  CHECK_FALSE(dev.host.start(options, why));
  CHECK(why == "the space folder 'bad-space': pages.json: pages[0].help.steps: not a list");
  CHECK_FALSE(dev.host.running());
  CHECK(dev.host.stats().threads == 0);

  options = options_of({"demo"});
  options.scenarios = {test::tests_folder() / "bad-scenario.json"};
  options.port = free_port();
  CHECK_FALSE(dev.host.start(options, why));
  CHECK(why == "bad-scenario.json line 14: step 2: lantern.wick is not a topic of the space demo");
  CHECK_FALSE(dev.host.running());

  TempFolder empty;
  options = options_of({"demo"});
  options.web = empty.path / "nowhere";
  CHECK_FALSE(dev.host.start(options, why));
  CHECK(why == "the web folder 'nowhere': not a folder");

  // A port another program holds: the server fails closed, and grace-dev names the way out.
  options = options_of({"demo"});
  options.port = free_port();
  PortHolder holder(options.port);
  REQUIRE(holder.held());
  CHECK_FALSE(dev.host.start(options, why));
  CHECK(dev.host.start_result() == grace::serve::StartResult::port_taken);
  CHECK(contains(why, "choose another with --port"));
  CHECK_FALSE(dev.host.running());
  // And then it starts on another, its spaces from the failed start gone.
  options.port = free_port();
  REQUIRE_MESSAGE(dev.host.start(options, why), why);
  View view(dev.host.port());
  CHECK(view.welcome["spaces"].size() == 1);
}
