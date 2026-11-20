// grace-spaces (D169): a mod's space across the ABI, end to end through the real server on loopback: its registration
// checked; a view's sub and cmd reaching the mod as frames from poll; its state reaching the view; its answer routed by
// the reply handle, once; a buffer too small kept for the next poll; an unanswered command answered internal; a mod that
// disconnects taking its space with it. And GraceUI's own space: the settings topic and its commands, shared with the
// in-game menu through one store.
#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <thread>

#include "client.h"
#include "grace/pages/builtin.h"
#include "grace/spaces/own_space.h"
#include "grace/spaces/registry.h"
#include "harness.h"

namespace {

using namespace grace;
using namespace grace::serve::iotest;
using protocol::Json;
using spaces::EventKind;
using spaces::Status;

constexpr std::uint64_t kMod = 7;

std::string demo_pages() { return std::string(pages::builtin_spaces()[1].pages); }
std::string demo_settings() { return std::string(pages::builtin_spaces()[1].settings); }

spaces::Registration demo_registration(const std::string& pages, const std::string& settings,
                                       std::uint32_t views = spaces::kViewBrowser) {
  spaces::Registration in;
  in.views = views;
  in.version = "1.0.0";
  in.increment = "0.2";
  in.pages = pages;
  in.settings = settings;
  return in;
}

struct Polled {
  Status status = Status::ok;
  EventKind kind = EventKind::none;
  std::uint64_t reply = 0;
  std::uint64_t length = 0;
  std::string frame;
};

Polled poll(spaces::Registry& registry, std::size_t capacity = 16384) {
  Polled out;
  std::string buffer(capacity, '\0');
  out.status = registry.poll(kMod, buffer.data(), capacity, out.kind, out.reply, out.length);
  if (out.status == Status::ok) {
    out.frame = buffer.substr(0, out.length);
  }
  return out;
}

// Polls until an event comes, at most 2 s.
Polled wait_event(spaces::Registry& registry) {
  for (int i = 0; i < 200; ++i) {
    Polled polled = poll(registry);
    if (polled.kind != EventKind::none || polled.status != Status::ok) {
      return polled;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return {};
}

Fixture::Options no_spaces() {
  Fixture::Options options;
  options.spaces = false;
  return options;
}

}  // namespace

TEST_CASE("a registration is checked: views, its own id, decodable page data, one space a mod") {
  spaces::Registry registry;
  const std::string pages = demo_pages();
  const std::string settings = demo_settings();
  std::string why;
  CHECK(registry.add(kMod, "lantern", demo_registration(pages, settings), why) == Status::refused);
  CHECK(why.find("another space") != std::string::npos);
  CHECK(registry.add(kMod, "demo", demo_registration(pages, settings, 0), why) == Status::refused);
  CHECK(registry.add(kMod, "demo", demo_registration(pages, "{\"schema\":1}"), why) == Status::refused);
  std::string broken = pages;
  broken.replace(broken.find("\"lantern.wicks\""), 15, "\"lantern.wickz\"");
  CHECK(registry.add(kMod, "demo", demo_registration(broken, settings), why) == Status::refused);
  CHECK(registry.add(kMod, "demo", demo_registration(pages, settings), why) == Status::ok);
  CHECK(registry.add(kMod, "demo", demo_registration(pages, settings), why) == Status::duplicate);
  CHECK(registry.add(kMod + 1, "demo", demo_registration(pages, settings), why) == Status::duplicate);
  CHECK(registry.wants_browser());
  registry.remove_owner(kMod);
  CHECK(registry.count() == 0);
  CHECK_FALSE(registry.wants_browser());
}

TEST_CASE("sub, publish, cmd and answer across the ABI, end to end through the server") {
  Fixture fixture(no_spaces());
  REQUIRE(fixture.started == serve::StartResult::ok);
  spaces::Registry registry;
  registry.attach(fixture.server);
  const std::string pages = demo_pages();
  const std::string settings = demo_settings();
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(pages, settings), why) == Status::ok);
  REQUIRE(fixture.wait_files());

  Json welcome;
  auto view = fixture.join(&welcome);
  REQUIRE(view != nullptr);
  CHECK(welcome["spaces"].size() == 1);
  CHECK(welcome["spaces"][0]["id"] == "demo");
  CHECK(welcome["spaces"][0]["version"] == "1.0.0");

  // The view watches a topic: the mod polls a sub frame for it.
  REQUIRE(view->send_json(frame("sub", {{"space", "demo"}, {"id", 1}, {"topic", "lantern.status"}})));
  Polled watched = wait_event(registry);
  REQUIRE(watched.kind == EventKind::watched);
  const Json sub = Json::parse(watched.frame);
  CHECK(sub["t"] == "sub");
  CHECK(sub["topic"] == "lantern.status");

  // The mod publishes its state: the view gets it.
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"lantern.status","rev":1,"body":{"tiles":[]}})",
                         why) == Status::ok);
  const auto state = view->next_of("state");
  REQUIRE(state);
  CHECK((*state)["body"] == Json::object({{"tiles", Json::array()}}));
  // Another space's frame, an undeclared topic, the wrong shape, and not a frame at all are refused.
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"graceui","topic":"settings","rev":1,"body":{}})", why) ==
        Status::refused);
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"lantern.smoke","rev":1,"body":{}})", why) ==
        Status::refused);
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"lantern.log","rev":1,"body":{}})", why) ==
        Status::refused);
  CHECK(registry.publish(kMod, "lantern", why) == Status::refused);

  // A command: the mod polls its cmd frame and a reply handle; a buffer too small keeps it for the next poll.
  REQUIRE(view->send_json(frame("cmd", {{"space", "demo"}, {"id", 42}, {"name", "lantern.trim"}, {"args", Json::object()}})));
  Polled small;
  for (int i = 0; i < 200 && small.kind == EventKind::none; ++i) {
    small = poll(registry, 8);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(small.status == Status::full);
  CHECK(small.kind == EventKind::command);
  const Polled command = poll(registry);
  REQUIRE(command.kind == EventKind::command);
  CHECK(command.length == small.length);
  CHECK(command.reply == small.reply);
  const Json cmd = Json::parse(command.frame);
  CHECK(cmd["name"] == "lantern.trim");
  CHECK(cmd["id"] == 42);

  // The answer goes to the view by the reply handle, with its space and id set by GraceUI; once only.
  CHECK(registry.answer(kMod, command.reply, R"({"v":1,"t":"result","space":"x","id":0,"ok":true})", why) == Status::ok);
  const auto result = view->next_of("result");
  REQUIRE(result);
  CHECK((*result)["id"] == 42);
  CHECK((*result)["space"] == "demo");
  CHECK((*result)["ok"] == true);
  CHECK(registry.answer(kMod, command.reply, R"({"v":1,"t":"result","space":"demo","id":42,"ok":true})", why) ==
        Status::refused);
  CHECK(registry.answer(kMod, command.reply + 99, R"({"v":1,"t":"result","space":"demo","id":1,"ok":true})", why) ==
        Status::refused);
  CHECK(registry.pending() == 0);

  // A mod that disconnects takes its space: the view's later sub is an unknown topic.
  registry.remove_owner(kMod);
  REQUIRE(view->send_json(frame("sub", {{"space", "demo"}, {"id", 2}, {"topic", "lantern.log"}})));
  const auto refused = view->next_of("error");
  REQUIRE(refused);
  CHECK((*refused)["code"] == "unknown_topic");
}

TEST_CASE("a command left unanswered is answered internal after the deadline, with one log line") {
  Fixture fixture(no_spaces());
  REQUIRE(fixture.started == serve::StartResult::ok);
  std::vector<std::string> logged;
  spaces::Registry registry([&](std::string_view line) { logged.emplace_back(line); });
  registry.attach(fixture.server);
  const std::string pages = demo_pages();
  const std::string settings = demo_settings();
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(pages, settings), why) == Status::ok);
  REQUIRE(fixture.wait_files());
  auto view = fixture.join();
  REQUIRE(view != nullptr);
  REQUIRE(view->send_json(frame("cmd", {{"space", "demo"}, {"id", 5}, {"name", "lantern.clear_log"},
                                        {"args", Json::object({{"confirm", true}})}})));
  const Polled command = wait_event(registry);
  REQUIRE(command.kind == EventKind::command);
  CHECK(registry.expire(std::chrono::steady_clock::now()) == 0);
  CHECK(registry.expire(std::chrono::steady_clock::now() + spaces::kAnswerDeadline + std::chrono::seconds(1)) == 1);
  const auto result = view->next_of("result");
  REQUIRE(result);
  CHECK((*result)["error"] == "internal");
  REQUIRE(logged.size() == 1);
  CHECK(logged[0].find("demo") != std::string::npos);
  CHECK(registry.answer(kMod, command.reply, R"({"v":1,"t":"result","space":"demo","id":5,"ok":true})", why) ==
        Status::refused);  // past its deadline
}

TEST_CASE("GraceUI's own space: the settings topic and its commands, one store for both views") {
  Fixture fixture(no_spaces());
  REQUIRE(fixture.started == serve::StartResult::ok);
  spaces::OwnSpace own;
  std::string why;
  REQUIRE_MESSAGE(own.load(why), why);
  int disconnects = 0;
  own.attach(fixture.server, "0.2.0", spaces::OwnSpace::Hooks{[&] { ++disconnects; }});
  REQUIRE(fixture.wait_files());
  auto view = fixture.join();
  REQUIRE(view != nullptr);
  // Frames as they come, kept by kind: a command's result and the state it changed may arrive in either order.
  std::vector<Json> results;
  std::vector<Json> states;
  const auto serve_until = [&](std::string_view kind) {
    std::vector<Json>& kept = kind == "result" ? results : states;
    for (int i = 0; i < 300 && kept.empty(); ++i) {
      own.serve();
      if (auto json = view->next_json(std::chrono::milliseconds(10))) {
        ((*json)["t"] == "result" ? results : states).push_back(*json);
      }
    }
    if (kept.empty()) {
      return std::optional<Json>();
    }
    std::optional<Json> first = kept.front();
    kept.erase(kept.begin());
    return first;
  };

  REQUIRE(view->send_json(frame("sub", {{"space", "graceui"}, {"id", 1}, {"topic", "settings"}})));
  auto state = serve_until("state");
  REQUIRE(state);
  CHECK((*state)["body"]["values"]["ui.scale"] == 1.0);
  CHECK((*state)["body"]["changed"].empty());

  // The browser sets a value: answered ok, and the topic goes out again.
  REQUIRE(view->send_json(frame("cmd", {{"space", "graceui"}, {"id", 9}, {"name", "settings.set"},
                                        {"args", {{"key", "ui.scale"}, {"value", 1.1}}}})));
  auto result = serve_until("result");
  REQUIRE(result);
  CHECK((*result)["ok"] == true);
  state = serve_until("state");
  REQUIRE(state);
  CHECK((*state)["body"]["values"]["ui.scale"] == 1.1);
  CHECK(own.revision() == 2);

  // The in-game menu's change reaches the browser through the same store.
  protocol::Cmd from_menu{"graceui", 0, "settings.set", Json::object({{"key", "ui.menu.theme"}, {"value", "clear"}})};
  CHECK(own.apply(from_menu).ok);
  state = serve_until("state");
  REQUIRE(state);
  CHECK((*state)["body"]["values"]["ui.menu.theme"] == "clear");

  // A refusal carries the store's code and words; menu.close has no menu in the browser; disconnect renews.
  REQUIRE(view->send_json(frame("cmd", {{"space", "graceui"}, {"id", 10}, {"name", "settings.set"},
                                        {"args", {{"key", "dashboard.lan"}, {"value", true}}}})));
  result = serve_until("result");
  REQUIRE(result);
  CHECK((*result)["error"] == "locked");
  CHECK((*result)["detail"] == "Only in a later version.");
  REQUIRE(view->send_json(frame("cmd", {{"space", "graceui"}, {"id", 11}, {"name", "menu.close"},
                                        {"args", Json::object()}})));
  result = serve_until("result");
  REQUIRE(result);
  CHECK((*result)["error"] == "not_now");
  REQUIRE(view->send_json(frame("cmd", {{"space", "graceui"}, {"id", 12}, {"name", "dashboard.disconnect"},
                                        {"args", Json::object({{"confirm", true}})}})));
  result = serve_until("result");
  REQUIRE(result);
  CHECK((*result)["ok"] == true);
  CHECK(disconnects == 1);
}

TEST_CASE("the menu's side, with no server: the space listed, a watch, its state, a command and its result (D171)") {
  spaces::Registry registry;
  int wakes = 0;
  registry.set_game_wake([&] { ++wakes; });
  const std::string pages = demo_pages();
  const std::string settings = demo_settings();
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(pages, settings, spaces::kViewGame), why) == Status::ok);
  const auto listed = registry.game_spaces();
  REQUIRE(listed.size() == 1);
  CHECK(listed[0].id == "demo");
  CHECK(listed[0].name == "Demo space");
  CHECK(listed[0].pages->pages.size() == 4);
  CHECK(listed[0].settings->settings.size() == 12);

  registry.game_watch("demo", "settings", true);
  registry.game_watch("demo", "settings", true);  // twice: still one watch, one event
  Polled watched = poll(registry);
  REQUIRE(watched.kind == EventKind::watched);
  CHECK(Json::parse(watched.frame)["topic"] == "settings");
  CHECK(poll(registry).kind == EventKind::none);

  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"settings","rev":4,"body":{"values":{"lantern.wicks":5}}})",
                         why) == Status::ok);
  Json body;
  std::uint64_t rev = 0;
  REQUIRE(registry.game_state("demo", "settings", 0, body, rev));
  CHECK(rev == 4);
  CHECK(body["values"]["lantern.wicks"] == 5);
  CHECK_FALSE(registry.game_state("demo", "settings", 4, body, rev));  // nothing newer
  CHECK(wakes >= 1);

  std::uint64_t ticket = 0;
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 3, "settings.set",
                                                      Json::object({{"key", "lantern.wicks"}, {"value", 6}})},
                                ticket));
  CHECK_FALSE(registry.game_command("elsewhere", protocol::Cmd{}, ticket));
  const Polled command = poll(registry);
  REQUIRE(command.kind == EventKind::command);
  CHECK(Json::parse(command.frame)["name"] == "settings.set");
  CHECK(registry.answer(kMod, command.reply,
                        R"({"v":1,"t":"result","space":"demo","id":0,"error":"invalid_value","detail":"No."})",
                        why) == Status::ok);
  const auto results = registry.take_game_results();
  REQUIRE(results.size() == 1);
  CHECK(results[0].ticket == command.reply);
  CHECK(results[0].result.id == 3);
  CHECK(results[0].result.error == protocol::ErrorCode::invalid_value);
  CHECK(registry.take_game_results().empty());

  registry.game_watch("demo", "settings", false);
  CHECK(poll(registry).kind == EventKind::unwatched);
  CHECK_FALSE(registry.game_state("demo", "settings", 0, body, rev));  // forgotten with the watch
}

TEST_CASE("the menu's watch and the browser's merge: one WATCHED, one UNWATCHED, whichever comes first") {
  Fixture fixture(no_spaces());
  REQUIRE(fixture.started == serve::StartResult::ok);
  spaces::Registry registry;
  registry.attach(fixture.server);
  const std::string pages = demo_pages();
  const std::string settings = demo_settings();
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(pages, settings, spaces::kViewGame | spaces::kViewBrowser),
                       why) == Status::ok);
  REQUIRE(fixture.wait_files());
  auto view = fixture.join();
  REQUIRE(view != nullptr);
  REQUIRE(view->send_json(frame("sub", {{"space", "demo"}, {"id", 1}, {"topic", "lantern.status"}})));
  REQUIRE(wait_event(registry).kind == EventKind::watched);
  registry.game_watch("demo", "lantern.status", true);  // the mod builds it already
  CHECK(poll(registry).kind == EventKind::none);
  // A state goes to both: the browser's view, and the menu's copy.
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"lantern.status","rev":2,"body":{"tiles":[]}})",
                         why) == Status::ok);
  REQUIRE(view->next_of("state"));
  Json body;
  std::uint64_t rev = 0;
  CHECK(registry.game_state("demo", "lantern.status", 0, body, rev));
  REQUIRE(view->send_json(frame("unsub", {{"space", "demo"}, {"id", 2}, {"topic", "lantern.status"}})));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(poll(registry).kind == EventKind::none);  // the menu still watches it
  registry.game_watch("demo", "lantern.status", false);
  CHECK(poll(registry).kind == EventKind::unwatched);
}

TEST_CASE("the menu's command left unanswered: internal after the deadline, in the menu's results") {
  spaces::Registry registry;
  const std::string pages = demo_pages();
  const std::string settings = demo_settings();
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(pages, settings, spaces::kViewGame), why) == Status::ok);
  std::uint64_t ticket = 0;
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 9, "lantern.trim", Json::object()}, ticket));
  REQUIRE(poll(registry).kind == EventKind::command);
  CHECK(registry.expire(std::chrono::steady_clock::now() + spaces::kAnswerDeadline + std::chrono::seconds(1)) == 1);
  const auto results = registry.take_game_results();
  REQUIRE(results.size() == 1);
  CHECK(results[0].ticket == ticket);
  CHECK(results[0].result.error == protocol::ErrorCode::internal);
}

TEST_CASE("the menu's command is held to the server's rules: declared, and a destructive one confirmed (D173)") {
  spaces::Registry registry;
  const std::string pages = demo_pages();
  const std::string settings = demo_settings();
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(pages, settings, spaces::kViewGame), why) == Status::ok);
  std::uint64_t unknown = 0;
  std::uint64_t unconfirmed = 0;
  std::uint64_t confirmed = 0;
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 1, "lantern.smoke", Json::object()}, unknown));
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 2, "lantern.clear_log", Json::object()}, unconfirmed));
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 3, "lantern.clear_log",
                                                      Json::object({{"confirm", true}})}, confirmed));
  const Polled command = poll(registry);  // the confirmed one alone reaches the mod
  REQUIRE(command.kind == EventKind::command);
  CHECK(command.reply == confirmed);
  CHECK(poll(registry).kind == EventKind::none);
  const auto results = registry.take_game_results();
  REQUIRE(results.size() == 2);
  CHECK(results[0].ticket == unknown);
  CHECK(results[0].result.error == protocol::ErrorCode::unknown_command);
  CHECK(results[1].ticket == unconfirmed);
  CHECK(results[1].result.error == protocol::ErrorCode::confirm_required);
}
