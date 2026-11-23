// grace-spaces (D169): a mod's space across the ABI, end to end through the real server on loopback: its registration
// checked; a view's sub and cmd reaching the mod as frames from poll; its state reaching the view; its answer routed by
// the reply handle, once; a buffer too small kept for the next poll; an unanswered command answered internal; a mod that
// disconnects taking its space with it. And GraceUI's own space: the settings topic and its commands, shared with the
// in-game menu through one store.
#include <doctest/doctest.h>

#include <atomic>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "client.h"
#include "grace/pages/builtin.h"
#include "grace/serve/codec.h"
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
  // A destructive action must carry the words its second press shows (§7.3, D173), whatever a mod's tools checked.
  std::string unconfirmed = pages;
  const std::string confirm = ",\"confirm\":\"The log's lines are removed. The journal is kept.\"";
  REQUIRE(unconfirmed.find(confirm) != std::string::npos);
  unconfirmed.erase(unconfirmed.find(confirm), confirm.size());
  CHECK(registry.add(kMod, "demo", demo_registration(unconfirmed, settings), why) == Status::refused);
  CHECK(why == "a destructive action without its confirm text");
  // GraceUI's own space's id is not a mod's.
  std::string own = pages;
  own.replace(own.find("\"space\":\"demo\""), 14, "\"space\":\"graceui\"");
  CHECK(registry.add(kMod, "graceui", demo_registration(own, settings), why) == Status::refused);
  CHECK(why.find("GraceUI's own") != std::string::npos);
  // The pages topic is the framework's: a space may not declare its own.
  std::string pages_topic = pages;
  const std::string status_topic = "{\"id\":\"lantern.status\"";
  pages_topic.replace(pages_topic.find(status_topic), status_topic.size(), "{\"id\":\"pages\"");
  CHECK(registry.add(kMod, "demo", demo_registration(pages_topic, settings), why) == Status::refused);
  CHECK(why == "pages is the framework's own topic");
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
  own.attach(fixture.server, "0.2.0", spaces::OwnSpace::Hooks{[&] { ++disconnects; }, {}, {}});
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
  CHECK((*state)["body"]["values"]["ui.scale"] == 1.25);
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
                                        {"args", {{"key", "ui.scale"}, {"value", 9.0}}}})));
  result = serve_until("result");
  REQUIRE(result);
  CHECK((*result)["error"] == "invalid_value");
  CHECK((*result).contains("detail"));
  // dashboard.pair has no code to show in a browser (D66); Forget needs a paired device's id.
  REQUIRE(view->send_json(frame("cmd", {{"space", "graceui"}, {"id", 13}, {"name", "dashboard.pair"},
                                        {"args", Json::object()}})));
  result = serve_until("result");
  REQUIRE(result);
  CHECK((*result)["error"] != nullptr);
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

TEST_CASE("GraceUI's other topics and commands (D181): bodies the host sets, published to a watching view") {
  Fixture fixture(no_spaces());
  REQUIRE(fixture.started == serve::StartResult::ok);
  spaces::OwnSpace own;
  std::string why;
  REQUIRE_MESSAGE(own.load(why), why);
  std::vector<protocol::Cmd> taken;
  spaces::OwnSpace::Hooks hooks;
  hooks.command = [&](const protocol::Cmd& command) {
    taken.push_back(command);
    pages::Outcome outcome;
    outcome.ok = command.args.value("on", true);
    if (!outcome.ok) {
      outcome.error = protocol::ErrorCode::not_now;
      outcome.detail = "Not now.";
    }
    return outcome;
  };
  own.attach(fixture.server, "0.2.0", hooks);
  REQUIRE(fixture.wait_files());
  // Before the host sets it, a topic has no body; an undeclared topic is never taken.
  CHECK(own.state_of("graceui.mods") == nullptr);
  CHECK_FALSE(own.set_state("graceui.nothing", Json::object()));
  const Json mods = {{"columns", Json::array()}, {"rows", Json::array({{{"id", "demo"}}})}};
  REQUIRE(own.set_state("graceui.mods", mods));
  REQUIRE(own.state_of("graceui.mods") != nullptr);
  CHECK(*own.state_of("graceui.mods") == mods);
  CHECK(own.states_revision() == 1);

  auto view = fixture.join();
  REQUIRE(view != nullptr);
  REQUIRE(view->send_json(frame("sub", {{"space", "graceui"}, {"id", 1}, {"topic", "graceui.mods"}})));
  std::optional<Json> state;
  for (int i = 0; i < 300 && !state; ++i) {
    own.serve();
    if (auto json = view->next_json(std::chrono::milliseconds(10)); json && (*json)["t"] == "state") {
      state = json;
    }
  }
  REQUIRE(state);
  CHECK((*state)["topic"] == "graceui.mods");
  CHECK((*state)["body"] == mods);
  // A newer body reaches the watching view.
  const Json quieter = {{"columns", Json::array()}, {"rows", Json::array()}};
  REQUIRE(own.set_state("graceui.mods", quieter));
  std::optional<Json> newer;
  for (int i = 0; i < 300 && !newer; ++i) {
    own.serve();
    if (auto json = view->next_json(std::chrono::milliseconds(10)); json && (*json)["t"] == "state") {
      newer = json;
    }
  }
  REQUIRE(newer);
  CHECK((*newer)["body"] == quieter);

  // A declared command of the host's, from the browser and from the menu, reaches the hook; its refusal carries its words.
  REQUIRE(view->send_json(frame("cmd", {{"space", "graceui"}, {"id", 5}, {"name", "mods.notices"},
                                        {"args", {{"id", "demo"}, {"on", false}}}})));
  std::optional<Json> result;
  for (int i = 0; i < 300 && !result; ++i) {
    own.serve();
    if (auto json = view->next_json(std::chrono::milliseconds(10)); json && (*json)["t"] == "result") {
      result = json;
    }
  }
  REQUIRE(result);
  CHECK((*result)["error"] == "not_now");
  CHECK((*result)["detail"] == "Not now.");
  CHECK(own.apply(protocol::Cmd{"graceui", 0, "mods.notices", Json::object({{"id", "demo"}, {"on", true}})}).ok);
  REQUIRE(taken.size() == 2);
  CHECK(taken[0].args["id"] == "demo");
  // dashboard.disconnect and menu.close never reach the command hook; an undeclared command is refused.
  CHECK_FALSE(own.apply(protocol::Cmd{"graceui", 0, "menu.close", Json::object()}).ok);
  CHECK_FALSE(own.apply(protocol::Cmd{"graceui", 0, "mods.everything", Json::object()}).ok);
  CHECK(taken.size() == 2);
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
  CHECK(listed[0].pages->pages.size() == 6);
  CHECK(listed[0].settings->settings.size() == 13);

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
  // The settings topic is held to the registration: a secret's value never leaves (only whether it is set), and a
  // description other than the registered one is refused (invariants 5 and 12, as framework guards).
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"settings","rev":5,"body":{"values":{"lantern.oil_token":"Zq7-wick-tallow-99","lantern.keeper":"Ivo"}}})",
                         why) == Status::ok);
  REQUIRE(registry.game_state("demo", "settings", 4, body, rev));
  CHECK(body["values"]["lantern.oil_token"] == "set");
  CHECK(body["values"]["lantern.keeper"] == "Ivo");
  CHECK(body.dump().find("Zq7") == std::string::npos);
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"settings","rev":6,"body":{"description":[],"values":{}}})",
                         why) == Status::refused);
  CHECK(why == "its settings description is not the one it registered");

  std::uint64_t ticket = 0;
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 3, "settings.set",
                                                      Json::object({{"key", "lantern.wicks"}, {"value", 6}})},
                                ticket));
  CHECK_FALSE(registry.game_command("elsewhere", protocol::Cmd{}, ticket));
  const Polled command = poll(registry);
  REQUIRE(command.kind == EventKind::command);
  CHECK(Json::parse(command.frame)["name"] == "settings.set");
  CHECK(Json::parse(command.frame)["view"] == "game");  // the in-game menu's (§7.3, D224)
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

TEST_CASE("a family's member through the registry: watched by the menu, published, kept; the family itself refused (D215)") {
  spaces::Registry registry;
  const std::string pages = demo_pages();
  const std::string settings = demo_settings();
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(pages, settings, spaces::kViewGame), why) == Status::ok);
  registry.game_watch("demo", "lantern.lamp/fish-steps", true);
  const Polled watched = poll(registry);
  REQUIRE(watched.kind == EventKind::watched);
  CHECK(Json::parse(watched.frame)["topic"] == "lantern.lamp/fish-steps");
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"lantern.lamp/fish-steps","rev":2,"body":{"sentence":"Low."}})",
                         why) == Status::ok);
  Json body;
  std::uint64_t rev = 0;
  REQUIRE(registry.game_state("demo", "lantern.lamp/fish-steps", 0, body, rev));
  CHECK(body["sentence"] == "Low.");
  // A member nobody watches: taken, and dropped; the family itself, a stream's member and a key with '/': refused.
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"lantern.lamp/old-crane","rev":1,"body":{}})",
                         why) == Status::ok);
  CHECK_FALSE(registry.game_state("demo", "lantern.lamp/old-crane", 0, body, rev));
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"lantern.lamp","rev":1,"body":{}})",
                         why) == Status::refused);
  CHECK(why == "a family: its members are published, each as <family>/<key>");
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"lantern.log/one","rev":1,"body":{}})",
                         why) == Status::refused);
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"lantern.lamp/a/b","rev":1,"body":{}})",
                         why) == Status::refused);
  registry.game_watch("demo", "lantern.lamp/fish-steps", false);
  CHECK(poll(registry).kind == EventKind::unwatched);
  CHECK_FALSE(registry.game_state("demo", "lantern.lamp/fish-steps", 0, body, rev));
}

TEST_CASE("a slow command waits 90 s for its answer, any other 10 s (D216)") {
  spaces::Registry registry;
  const std::string pages = demo_pages();
  const std::string settings = demo_settings();
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(pages, settings, spaces::kViewGame), why) == Status::ok);
  std::uint64_t slow = 0;
  std::uint64_t quick = 0;
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 1, "oil.order", Json::object()}, slow));
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 2, "lantern.trim", Json::object()}, quick));
  const auto now = std::chrono::steady_clock::now();
  CHECK(registry.expire(now + std::chrono::seconds(11)) == 1);  // the trim alone
  auto results = registry.take_game_results();
  REQUIRE(results.size() == 1);
  CHECK(results[0].ticket == quick);
  CHECK(registry.expire(now + std::chrono::seconds(60)) == 0);
  CHECK(registry.expire(now + std::chrono::seconds(91)) == 1);
  results = registry.take_game_results();
  REQUIRE(results.size() == 1);
  CHECK(results[0].ticket == slow);
  CHECK(results[0].result.error == protocol::ErrorCode::internal);
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
  // Published while only the browser watches: the menu, watching it later, has it at once (D184), for the mod
  // publishes again only on a change.
  CHECK(registry.publish(kMod, R"({"v":1,"t":"state","space":"demo","topic":"lantern.status","rev":1,"body":{"tiles":[]}})",
                         why) == Status::ok);
  REQUIRE(view->next_of("state"));
  registry.game_watch("demo", "lantern.status", true);  // the mod builds it already
  CHECK(poll(registry).kind == EventKind::none);
  Json first;
  std::uint64_t first_rev = 0;
  CHECK(registry.game_state("demo", "lantern.status", 0, first, first_rev));
  CHECK(first_rev == 1);
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

TEST_CASE("G34's parts are checked at registration: row actions, a row secret, a form, choices from a topic") {
  const Json demo = Json::parse(demo_pages());
  const Json described = Json::parse(demo_settings());
  // The demo space's Keepers page uses all four: its first section's table, its second's form.
  const auto keepers = [](Json& pages) -> Json& {
    for (Json& page : pages["pages"]) {
      if (page["id"] == "demo.keepers") {
        return page["sections"];
      }
    }
    FAIL("no demo.keepers");
    return pages;
  };
  const auto refused = [&](Json pages, Json settings, const std::string& words) {
    spaces::Registry registry;
    std::string why;
    CHECK(registry.add(kMod, "demo", demo_registration(pages.dump(), settings.dump()), why) == Status::refused);
    CHECK_MESSAGE(why.find(words) != std::string::npos, why);
  };
  {
    spaces::Registry registry;
    std::string why;
    CHECK(registry.add(kMod, "demo", demo_registration(demo.dump(), described.dump()), why) == Status::ok);
  }
  Json pages = demo;
  keepers(pages)[0]["blocks"][0]["row_actions"][0].erase("confirm");
  refused(pages, described, "a destructive row action without its confirm text");
  pages = demo;
  keepers(pages)[0]["blocks"][0]["row_actions"][0]["command"] = "keepers.sack";
  refused(pages, described, "a command the space does not declare");
  pages = demo;
  keepers(pages)[0]["blocks"][0]["row_secret"]["command"] = "keepers.whisper";
  refused(pages, described, "a row secret's command is not one the space declares");
  pages = demo;
  keepers(pages)[0]["blocks"][0]["component"] = "stat_tiles";
  refused(pages, described, "row actions and a row secret go on a table");
  pages = demo;
  keepers(pages)[1]["blocks"][0]["fields"][0]["max"] = 201;
  refused(pages, described, "a max outside 1 to 200");
  pages = demo;
  keepers(pages)[1]["blocks"][0].erase("submit");
  refused(pages, described, "a form block has a declared command, one to four fields and a submit label");
  pages = demo;
  keepers(pages)[2]["blocks"][0]["command"] = "keepers.hire";
  refused(pages, described, "a form block, and only a form block");
  Json settings = described;
  for (Json& s : settings["settings"]) {
    if (s["key"] == "lantern.oil_kind") {
      s["choices_from"] = "lantern.log";  // a stream
    }
  }
  refused(demo, settings, "choices_from is on a text setting, naming a state topic");
}

TEST_CASE("a destructive row action is held to its confirmation from the menu too (D192)") {
  spaces::Registry registry;
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(demo_pages(), demo_settings(), spaces::kViewGame), why) ==
          Status::ok);
  std::uint64_t unconfirmed = 0;
  std::uint64_t confirmed = 0;
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 1, "keepers.dismiss", Json::object({{"id", "k1"}})},
                                unconfirmed));
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 2, "keepers.dismiss",
                                                      Json::object({{"id", "k1"}, {"confirm", true}})}, confirmed));
  const Polled command = poll(registry);
  REQUIRE(command.kind == EventKind::command);
  CHECK(command.reply == confirmed);
  const auto results = registry.take_game_results();
  REQUIRE(results.size() == 1);
  CHECK(results[0].result.error == protocol::ErrorCode::confirm_required);
}

TEST_CASE("a row secret's command reaches its space; its result comes back with no body (D194, invariant 5)") {
  Fixture fixture(no_spaces());
  REQUIRE(fixture.started == serve::StartResult::ok);
  spaces::Registry registry;
  registry.attach(fixture.server);
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(demo_pages(), demo_settings()), why) == Status::ok);
  REQUIRE(fixture.wait_files());
  auto view = fixture.join(nullptr);
  REQUIRE(view != nullptr);
  REQUIRE(view->send_json(frame("cmd", {{"space", "demo"}, {"id", 5}, {"name", "keepers.ward"},
                                        {"args", {{"id", "k2"}, {"value", "invented-word"}}}})));
  const Polled command = wait_event(registry);
  REQUIRE(command.kind == EventKind::command);
  CHECK(Json::parse(command.frame)["args"]["value"] == "invented-word");  // the space alone sees it
  CHECK(registry.answer(kMod, command.reply,
                        R"({"v":1,"t":"result","space":"demo","id":5,"ok":true,"body":{"echo":"invented-word"}})",
                        why) == Status::ok);
  const auto result = view->next_of("result");
  REQUIRE(result);
  CHECK((*result)["ok"] == true);
  CHECK_FALSE(result->contains("body"));
  // Another command's body is passed as before. A view that names itself the game is not believed (§7.3, D224).
  REQUIRE(view->send_json(frame("cmd", {{"space", "demo"}, {"id", 6}, {"name", "lantern.trim"}, {"args", Json::object()},
                                        {"view", "game"}})));
  const Polled trim = wait_event(registry);
  REQUIRE(trim.kind == EventKind::command);
  CHECK(Json::parse(trim.frame)["view"] == "browser");
  CHECK(registry.answer(kMod, trim.reply, R"({"v":1,"t":"result","space":"demo","id":6,"ok":true,"body":{"trimmed":1}})",
                        why) == Status::ok);
  const auto kept = view->next_of("result");
  REQUIRE(kept);
  CHECK((*kept)["body"]["trimmed"] == 1);
}

TEST_CASE("a section's own actions and status are checked at registration; a destructive one needs its confirm (D202)") {
  const Json demo = Json::parse(demo_pages());
  const std::string settings = demo_settings();
  const auto keepers = [](Json& pages) -> Json& {
    for (Json& page : pages["pages"]) {
      if (page["id"] == "demo.keepers") {
        return page["sections"];
      }
    }
    FAIL("no demo.keepers");
    return pages;
  };
  const auto refused = [&](const Json& pages, const std::string& words) {
    spaces::Registry registry;
    std::string why;
    CHECK(registry.add(kMod, "demo", demo_registration(pages.dump(), settings), why) == Status::refused);
    CHECK_MESSAGE(why.find(words) != std::string::npos, why);
  };
  Json pages = demo;
  keepers(pages)[0]["actions"][0].erase("confirm");
  refused(pages, "a destructive section action without its confirm text");
  pages = demo;
  keepers(pages)[2]["actions"][0]["command"] = "oil.steal";
  refused(pages, "a command the space does not declare");
  pages = demo;
  keepers(pages)[2]["status"]["topic"] = "lantern.log";
  refused(pages, "status names no state topic of the space's own");
  // From the menu, a destructive section action is held to its confirmation as the server holds a view's.
  spaces::Registry registry;
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(demo_pages(), settings, spaces::kViewGame), why) == Status::ok);
  std::uint64_t ticket = 0;
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 1, "keepers.dismiss_all", Json::object()}, ticket));
  const auto results = registry.take_game_results();
  REQUIRE(results.size() == 1);
  CHECK(results[0].result.error == protocol::ErrorCode::confirm_required);
}

namespace {
std::atomic<int> g_owner_wakes{0};
std::atomic<std::uint64_t> g_woken_owner{0};
void owner_woken(std::uint64_t owner) noexcept {
  g_owner_wakes.fetch_add(1);
  g_woken_owner.store(owner);
}
}  // namespace

TEST_CASE("the menu's watch, unwatch and command wake the space's owner, for its mod's wait handle (D212)") {
  spaces::Registry registry;
  registry.set_owner_wake(&owner_woken);
  g_owner_wakes.store(0);
  const std::string pages = demo_pages();
  const std::string settings = demo_settings();
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(pages, settings, spaces::kViewGame), why) == Status::ok);
  registry.game_watch("demo", "settings", true);
  CHECK(g_owner_wakes.load() == 1);
  CHECK(g_woken_owner.load() == kMod);
  registry.game_watch("demo", "settings", true);  // already watched: no event, no wake
  CHECK(g_owner_wakes.load() == 1);
  std::uint64_t ticket = 0;
  REQUIRE(registry.game_command("demo", protocol::Cmd{"demo", 3, "settings.set",
                                                      Json::object({{"key", "lantern.enabled"}, {"value", false}})},
                                ticket));
  CHECK(g_owner_wakes.load() == 2);
  registry.game_watch("demo", "settings", false);
  CHECK(g_owner_wakes.load() == 3);
}

namespace {

std::string sha256_hex(std::string_view bytes) {
  serve::Sha256 sha;
  sha.update(bytes);
  const std::array<std::uint8_t, 32> digest = sha.finish();
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string out;
  for (const std::uint8_t b : digest) {
    out += kHex[b >> 4];
    out += kHex[b & 0xF];
  }
  return out;
}

// A browser's upload of `file` for keepers.import: its ticket over the WebSocket, then the POST. The POST's status.
int upload(Fixture& fixture, WsClient& view, std::uint32_t id, const std::string& file) {
  REQUIRE(view.send_json(frame("upload", {{"space", "demo"}, {"id", id}, {"name", "keepers.import"},
                                          {"args", Json::object()}, {"size", file.size()},
                                          {"sha256", sha256_hex(file)}})));
  const std::optional<Json> ticket = view.next_of("upload_ticket");
  REQUIRE(ticket.has_value());
  const std::string cookie = fixture.cookie();
  const std::string head = "POST /upload HTTP/1.1\r\nHost: " + host_of(fixture.port) + "\r\nOrigin: " +
                           origin_of(fixture.port) +
                           "\r\nSec-Fetch-Site: same-origin\r\nContent-Type: application/octet-stream\r\n"
                           "Cookie: en_session=" + cookie + "\r\nGrace-Upload-Ticket: " +
                           (*ticket)["ticket"].get<std::string>() + "\r\nContent-Length: " +
                           std::to_string(file.size()) + "\r\n\r\n";
  return request(fixture.port, head + file, 10s).status;
}

}  // namespace

TEST_CASE("a file command's file reaches the mod beside its frame, once, by ABI 1.6's poll (D226)") {
  Fixture fixture(no_spaces());
  REQUIRE(fixture.started == serve::StartResult::ok);
  spaces::Registry registry;
  registry.attach(fixture.server);
  std::string why;
  REQUIRE(registry.add(kMod, "demo", demo_registration(demo_pages(), demo_settings()), why) == Status::ok);
  REQUIRE(fixture.wait_files());
  auto view = fixture.join(nullptr);
  REQUIRE(view != nullptr);
  const std::string file(70000, 'r');
  REQUIRE(upload(fixture, *view, 5, file) == 202);
  // A file buffer too small: full, both lengths set, the event kept.
  std::string frame_buffer(16384, '\0');
  std::vector<std::uint8_t> small(1024);
  spaces::FileOut out;
  out.buffer = small.data();
  out.capacity = small.size();
  out.takes = true;
  EventKind kind = EventKind::none;
  std::uint64_t reply = 0;
  std::uint64_t length = 0;
  Status status = Status::ok;
  for (int i = 0; i < 200 && kind == EventKind::none; ++i) {
    status = registry.poll(kMod, frame_buffer.data(), frame_buffer.size(), kind, reply, length, out);
    if (kind == EventKind::none) {
      std::this_thread::sleep_for(10ms);
    }
  }
  CHECK(status == Status::full);
  CHECK(kind == EventKind::command);
  CHECK(out.length == file.size());
  // Room for it: the frame and the file, once.
  std::vector<std::uint8_t> room(file.size());
  out.buffer = room.data();
  out.capacity = room.size();
  status = registry.poll(kMod, frame_buffer.data(), frame_buffer.size(), kind, reply, length, out);
  REQUIRE(status == Status::ok);
  CHECK(kind == EventKind::command);
  CHECK(out.length == file.size());
  CHECK(std::string(room.begin(), room.end()) == file);
  const Json cmd = Json::parse(frame_buffer.substr(0, length));
  CHECK(cmd["name"] == "keepers.import");
  CHECK(cmd["view"] == "browser");
  CHECK(registry.answer(kMod, reply, R"({"v":1,"t":"result","space":"demo","id":0,"ok":true})", why) == Status::ok);
  const std::optional<Json> result = view->next_of("result");
  REQUIRE(result.has_value());
  CHECK((*result)["id"] == 5);
  CHECK((*result)["ok"] == true);
  // A mod whose event takes no file: GraceUI answers the command for it, and the mod never sees it.
  REQUIRE(upload(fixture, *view, 6, file) == 202);
  spaces::FileOut none;
  kind = EventKind::none;
  for (int i = 0; i < 100 && kind == EventKind::none; ++i) {
    registry.poll(kMod, frame_buffer.data(), frame_buffer.size(), kind, reply, length, none);
    std::this_thread::sleep_for(10ms);
  }
  CHECK(kind == EventKind::none);
  const std::optional<Json> refused = view->next_of("result");
  REQUIRE(refused.has_value());
  CHECK((*refused)["id"] == 6);
  CHECK((*refused)["error"] == "not_now");
}

TEST_CASE("a space with file commands needs a mod built for ABI 1.6 (D226)") {
  spaces::Registry registry;
  std::string why;
  const std::string pages = demo_pages();  // the registration's views point into these
  const std::string settings = demo_settings();
  spaces::Registration older = demo_registration(pages, settings, spaces::kViewGame);
  older.takes_files = false;
  CHECK(registry.add(kMod, "demo", older, why) == Status::refused);
  CHECK(why.find("ABI 1.6") != std::string::npos);
  older.takes_files = true;
  CHECK(registry.add(kMod, "demo", older, why) == Status::ok);
}
