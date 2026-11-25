// bridge_tests.cpp: the server security suite's WebSocket half (§9.1), with two spaces, GraceUI's own and the demo
// space: the upgrade's checks, the ticket (missing, stale, reused, late), welcome, per-space isolation (a page reaches
// only the spaces welcome named, and no topic or command of another space: invariant 3), pushes to the watching
// connections, results to the asking one alone, the limits (20 messages a second, 32 subscriptions, 32 commands queued
// per space, 3 bad messages), pings, a third WebSocket superseding the oldest, and errors that echo no payload.
#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "harness.h"

using namespace grace::serve;
using namespace grace::serve::iotest;
using grace::protocol::Json;

namespace {

Json sub(std::string_view space, std::string_view topic, std::uint32_t id = 1) {
  return frame("sub", {{"space", std::string(space)}, {"id", id}, {"topic", std::string(topic)}});
}

Json cmd(std::string_view space, std::string_view name, std::uint32_t id, Json args = Json::object()) {
  return frame("cmd", {{"space", std::string(space)}, {"id", id}, {"name", std::string(name)}, {"args", args}});
}

Json hello_with(std::optional<std::string> ticket, std::string_view protocol = "1.0") {
  Json hello = frame("hello", {{"protocol", std::string(protocol)}, {"bundle", "test"}, {"caps", Json::array()}});
  if (ticket) {
    hello["ticket"] = *ticket;
  }
  return hello;
}

bool has(const std::vector<std::string>& list, std::string_view item) {
  return std::find(list.begin(), list.end(), item) != list.end();
}

}  // namespace

TEST_CASE("hello with a ticket: welcome names both spaces, the protocol and an opaque session, never the cookie") {
  Fixture fixture;
  Json welcome;
  const std::string cookie = fixture.cookie();
  WsClient ws;
  REQUIRE(ws.join(fixture.port, cookie, fixture.ticket(cookie), &welcome));
  CHECK(welcome["protocol"] == "1.18");
  CHECK(welcome["graceui"] == "0.2.0");
  REQUIRE(welcome["spaces"].size() == 2);
  CHECK(welcome["spaces"][0]["id"] == "graceui");
  CHECK(welcome["spaces"][1]["id"] == "demo");
  CHECK(welcome["spaces"][1]["name"] == "Demo space");
  CHECK(welcome["spaces"][1]["settings_schema"]["hash"].get<std::string>().size() == 16);
  const std::string session = welcome["session"].get<std::string>();
  CHECK_FALSE(session.empty());
  CHECK(session.find(cookie) == std::string::npos);
  CHECK(fixture.server.stats().websockets == 1);
}

TEST_CASE("the framework's pages topic: each space's page data, answered by the server, never by the space (D166)") {
  Fixture fixture;
  auto ws = fixture.join();
  REQUIRE(ws != nullptr);
  REQUIRE(ws->send_json(frame("sub", {{"space", "demo"}, {"id", 3}, {"topic", "pages"}})));
  const auto state = ws->next_of("state");
  REQUIRE(state);
  CHECK((*state)["space"] == "demo");
  CHECK((*state)["topic"] == "pages");
  CHECK((*state)["body"]["space"] == "demo");
  CHECK((*state)["body"]["pages"].size() == 9);
  REQUIRE(ws->send_json(frame("unsub", {{"space", "demo"}, {"id", 4}, {"topic", "pages"}})));
  CHECK(ws->quiet(std::chrono::milliseconds(200)));  // nothing held, nothing refused
  CHECK(fixture.demo->watched().empty());            // the space never saw it
}

TEST_CASE("the upgrade: Origin, Fetch Metadata and the cookie first, then RFC 6455's own checks") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  const std::string cookie = fixture.cookie();
  const auto status = [&](const std::string& raw) { return request(port, raw).status; };
  CHECK(status(upgrade_head(port, cookie, "", false)) == 403);                                       // no Origin
  CHECK(status(upgrade_head(port, cookie, "Origin: http://evil.example\r\n", false)) == 403);
  CHECK(status(upgrade_head(port, cookie, "Origin: http://localhost:" + std::to_string(port) + "\r\n", false)) == 403);
  CHECK(status(upgrade_head(port, "")) == 403);                                                       // no cookie
  CHECK(status(upgrade_head(port, std::string(43, 'C'))) == 403);
  const std::string cross = "GET /api/ws HTTP/1.1\r\nHost: " + host_of(port) + "\r\nOrigin: " + origin_of(port) +
                            "\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-Fetch-Site: cross-site\r\n"
                            "Cookie: en_session=" + cookie + "\r\n\r\n";
  CHECK(status(cross) == 403);
  std::string old_version = upgrade_head(port, cookie);
  old_version.replace(old_version.find("Version: 13"), 11, "Version: 8");
  CHECK(status(old_version) == 426);
  std::string bad_key = upgrade_head(port, cookie);
  bad_key.replace(bad_key.find("dGhlIHNhbXBsZSBub25jZQ=="), 24, "c2hvcnQ=");
  CHECK(status(bad_key) == 400);
  CHECK(status(upgrade_head(port, cookie) + "early bytes") == 400);  // nothing may follow the head before the 101
  CHECK(status(plain_get(port, "/api/ws", "Origin: " + origin_of(port) + "\r\nCookie: en_session=" + cookie +
                                              "\r\n")) == 426);
}

TEST_CASE("the ticket: missing, stale, reused, late, or from before a renewal: bye unauthorised, then the close") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  const std::string cookie = fixture.cookie();
  const auto refused = [&](const Json& hello, Millis wait_first = 0ms) {
    WsClient ws;
    REQUIRE(ws.open(port, cookie) == 101);
    std::this_thread::sleep_for(wait_first);
    ws.send_json(hello);
    const std::optional<Json> bye = ws.next_json();
    REQUIRE(bye.has_value());
    CHECK((*bye)["t"] == "bye");
    CHECK((*bye)["reason"] == "unauthorised");
    std::uint16_t code = 0;
    CHECK(ws.closed(2s, &code));
    CHECK(code == 1008);
  };
  SUBCASE("missing") { refused(hello_with(std::nullopt)); }
  SUBCASE("not a ticket") { refused(hello_with(std::string(43, 'A'))); }
  SUBCASE("reused") {
    const std::string ticket = fixture.ticket(cookie);
    WsClient first;
    REQUIRE(first.join(port, cookie, ticket));
    refused(hello_with(ticket));
  }
  SUBCASE("stale: unused past its time") {
    const std::string ticket = fixture.ticket(cookie);
    std::this_thread::sleep_for(fast_timings().ticket + 100ms);
    refused(hello_with(ticket));
  }
  SUBCASE("late: the hello past 2 s from the 101 (here, its short form)") {
    WsClient ws;
    REQUIRE(ws.open(port, cookie) == 101);
    const std::optional<Json> bye = ws.next_json(2s);
    REQUIRE(bye.has_value());
    CHECK((*bye)["reason"] == "unauthorised");
    CHECK(ws.closed(2s));
  }
  SUBCASE("a frame before the hello") {
    WsClient ws;
    REQUIRE(ws.open(port, cookie) == 101);
    ws.send_json(sub("demo", "lantern.status"));
    CHECK((*ws.next_json())["reason"] == "unauthorised");
    CHECK(ws.closed(2s));
  }
  SUBCASE("from before the session was renewed") {
    const std::string ticket = fixture.ticket(cookie);
    fixture.server.renew_session();
    std::this_thread::sleep_for(50ms);
    WsClient ws;
    CHECK(ws.open(port, cookie) == 403);  // the cookie itself is stale now
    const std::string fresh = fixture.cookie();
    CHECK(fresh != cookie);
    REQUIRE(ws.open(port, fresh) == 101);
    ws.send_json(hello_with(ticket));
    CHECK((*ws.next_json())["reason"] == "unauthorised");
  }
}

TEST_CASE("the protocol's major: another envelope v or hello protocol answers bye version") {
  Fixture fixture;
  const std::string cookie = fixture.cookie();
  WsClient ws;
  REQUIRE(ws.open(fixture.port, cookie) == 101);
  ws.send_json(hello_with(fixture.ticket(cookie), "2.0"));
  CHECK((*ws.next_json())["reason"] == "version");
  CHECK(ws.closed(2s));
  const std::unique_ptr<WsClient> joined = fixture.join();
  Json future = sub("demo", "lantern.status");
  future["v"] = 2;
  joined->send_json(future);
  CHECK((*joined->next_json())["reason"] == "version");
}

TEST_CASE("per-space isolation: a page reaches only the spaces welcome named, and each space's own topics and "
          "commands (invariant 3)") {
  Fixture fixture;
  const std::unique_ptr<WsClient> ws = fixture.join();
  const auto error_of = [&](const Json& message) {
    ws->send_json(message);
    const std::optional<Json> answer = ws->next_json();
    REQUIRE(answer.has_value());
    return *answer;
  };
  // A demo topic asked of GraceUI's space, a GraceUI topic of the demo space, a space no welcome named.
  Json answer = error_of(sub("graceui", "lantern.status", 4));
  CHECK(answer["t"] == "error");
  CHECK(answer["code"] == "unknown_topic");
  CHECK(answer["space"] == "graceui");
  CHECK(answer["detail"] == "sub 4: not a topic of this space");
  answer = error_of(sub("demo", "graceui.dashboard", 5));
  CHECK(answer["code"] == "unknown_topic");
  CHECK(answer["space"] == "demo");
  answer = error_of(sub("eldennet", "status", 6));
  CHECK(answer["code"] == "unknown_topic");
  CHECK_FALSE(answer.contains("space"));
  CHECK(answer["detail"] == "sub 6: not a topic of a space this view may show");
  answer = error_of(frame("unsub", {{"space", "demo"}, {"id", 7}, {"topic", "graceui.about"}}));
  CHECK(answer["code"] == "unknown_topic");
  // Commands: another space's is unknown_command in this one, and a space not named is unknown_command too.
  answer = error_of(cmd("graceui", "lantern.trim", 8));
  CHECK(answer["t"] == "result");
  CHECK(answer["space"] == "graceui");
  CHECK(answer["id"] == 8);
  CHECK(answer["error"] == "unknown_command");
  CHECK_FALSE(answer.contains("ok"));
  answer = error_of(cmd("demo", "dashboard.disconnect", 9));
  CHECK(answer["error"] == "unknown_command");
  CHECK(answer["space"] == "demo");
  answer = error_of(cmd("eldennet", "secrets.set", 10, {{"provider", "x"}, {"value", "a secret"}}));
  CHECK(answer["t"] == "error");
  CHECK(answer["code"] == "unknown_command");
  CHECK(answer.dump().find("a secret") == std::string::npos);
  // Neither space heard any of it.
  std::this_thread::sleep_for(50ms);
  CHECK(fixture.graceui->events().empty());
  CHECK(fixture.demo->events().empty());
  // Each space's own command reaches it, and it alone.
  answer = error_of(cmd("demo", "lantern.trim", 11));
  CHECK(answer["t"] == "result");
  CHECK(answer["ok"] == true);
  CHECK(answer["space"] == "demo");
  CHECK(fixture.demo->commands() == std::vector<std::string>{"lantern.trim"});
  CHECK(fixture.graceui->commands().empty());
  answer = error_of(cmd("graceui", "menu.close", 12));
  CHECK(answer["ok"] == true);
  CHECK(fixture.graceui->commands() == std::vector<std::string>{"menu.close"});
  CHECK(fixture.demo->commands() == std::vector<std::string>{"lantern.trim"});
  // A destructive action's command without its confirmation is refused, and the space never hears it (§7.3, D173).
  answer = error_of(cmd("demo", "lantern.clear_log", 13));
  CHECK(answer["error"] == "confirm_required");
  answer = error_of(cmd("demo", "lantern.clear_log", 14, {{"confirm", "yes"}}));
  CHECK(answer["error"] == "confirm_required");
  CHECK(fixture.demo->commands() == std::vector<std::string>{"lantern.trim"});
  answer = error_of(cmd("demo", "lantern.clear_log", 15, {{"confirm", true}}));
  CHECK(answer["ok"] == true);
  CHECK(fixture.demo->commands() == std::vector<std::string>{"lantern.trim", "lantern.clear_log"});
}

TEST_CASE("a space's state and rows reach the connections that watch them, from the row each asks") {
  Fixture fixture;
  const std::unique_ptr<WsClient> a = fixture.join();
  a->send_json(sub("demo", "lantern.status", 1));
  const std::optional<Json> state = a->next_of("state");
  REQUIRE(state.has_value());
  CHECK((*state)["space"] == "demo");
  CHECK((*state)["topic"] == "lantern.status");
  CHECK((*state)["rev"] == 1);
  CHECK((*state)["body"] == demo_fixtures()["lantern.status"]);
  CHECK(fixture.demo->wait_for([&] { return has(fixture.demo->watched(), "lantern.status"); }));

  a->send_json(sub("demo", "lantern.log", 2));
  const std::optional<Json> rows = a->next_of("rows");
  REQUIRE(rows.has_value());
  CHECK((*rows)["from_seq"] == 1);
  CHECK((*rows)["rows"].size() == 3);
  CHECK((*rows)["dropped"] == 0);

  // A second view joins the stream from row 3: the ring's tail alone. A third asks from row 9, past the ring.
  const std::unique_ptr<WsClient> b = fixture.join();
  Json from_three = sub("demo", "lantern.log", 3);
  from_three["since"] = 3;
  b->send_json(from_three);
  const std::optional<Json> tail = b->next_of("rows");
  REQUIRE(tail.has_value());
  CHECK((*tail)["from_seq"] == 3);
  CHECK((*tail)["rows"].size() == 1);

  // New rows go to every watcher; a new state revision too, the newest.
  grace::protocol::Rows more;
  more.topic = "lantern.log";
  more.from_seq = 4;
  more.rows = {Json{{"seq", 4}, {"time", "20:01"}, {"line", "The lantern was trimmed again."}}};
  REQUIRE(fixture.demo->port->publish(grace::protocol::Message{more}));
  for (WsClient* client : {a.get(), b.get()}) {
    const std::optional<Json> next = client->next_of("rows");
    REQUIRE(next.has_value());
    CHECK((*next)["from_seq"] == 4);
  }
  for (std::uint64_t rev = 2; rev <= 4; ++rev) {
    grace::protocol::State newer;
    newer.topic = "lantern.status";
    newer.rev = rev;
    newer.body = Json{{"sentence", "Revision " + std::to_string(rev)}};
    REQUIRE(fixture.demo->port->publish(grace::protocol::Message{newer}));
  }
  std::uint64_t last = 0;
  while (const std::optional<Json> next = a->next_of("state", 300ms)) {
    CHECK((*next)["rev"].get<std::uint64_t>() > last);
    last = (*next)["rev"].get<std::uint64_t>();
  }
  CHECK(last == 4);
  CHECK(b->quiet(100ms));  // b watches no state

  // A space's publish of another shape, an undeclared topic or too many rows is refused.
  grace::protocol::State wrong;
  wrong.topic = "lantern.log";
  wrong.body = Json::object();
  CHECK_FALSE(fixture.demo->port->publish(grace::protocol::Message{wrong}));
  wrong.topic = "graceui.about";
  CHECK_FALSE(fixture.demo->port->publish(grace::protocol::Message{wrong}));
  grace::protocol::Rows many;
  many.topic = "lantern.log";
  many.from_seq = 5;
  many.rows.assign(101, Json::object());
  CHECK_FALSE(fixture.demo->port->publish(grace::protocol::Message{many}));

  // Unwatched by all: the space hears it once the last view goes.
  a->send_json(frame("unsub", {{"space", "demo"}, {"id", 2}, {"topic", "lantern.log"}}));
  std::this_thread::sleep_for(50ms);
  CHECK(has(fixture.demo->watched(), "lantern.log"));  // b still watches
  b->socket().close();
  CHECK(fixture.demo->wait_for([&] { return !has(fixture.demo->watched(), "lantern.log"); }));
  CHECK(has(fixture.demo->watched(), "lantern.status"));
}

TEST_CASE("a family's members: each watched as a topic of its own, the family never; a slot given back goes to another (D215)") {
  Fixture fixture;
  const std::unique_ptr<WsClient> a = fixture.join();
  const std::unique_ptr<WsClient> b = fixture.join();
  // A member: its state, and the space hears it watched under its own id.
  a->send_json(sub("demo", "lantern.lamp/harbour-mouth", 1));
  const std::optional<Json> harbour = a->next_of("state");
  REQUIRE(harbour.has_value());
  CHECK((*harbour)["topic"] == "lantern.lamp/harbour-mouth");
  CHECK((*harbour)["body"] == demo_fixtures()["lantern.lamp/harbour-mouth"]);
  CHECK(fixture.demo->wait_for([&] { return has(fixture.demo->watched(), "lantern.lamp/harbour-mouth"); }));
  // The family itself, a key with a '/', and a member of a topic that is no family: unknown.
  for (const char* topic : {"lantern.lamp", "lantern.lamp/a/b", "lantern.status/one", "lantern.lamp/"}) {
    a->send_json(sub("demo", topic, 2));
    const std::optional<Json> refused = a->next_of("error");
    REQUIRE_MESSAGE(refused.has_value(), topic);
    CHECK((*refused)["code"] == "unknown_topic");
  }
  // Another member for another view; a state the space publishes reaches only that member's watchers.
  b->send_json(sub("demo", "lantern.lamp/fish-steps", 3));
  REQUIRE(b->next_of("state").has_value());
  grace::protocol::State newer;
  newer.topic = "lantern.lamp/fish-steps";
  newer.rev = 2;
  newer.body = Json{{"sentence", "The fish-steps lamp is trimmed."}};
  REQUIRE(fixture.demo->port->publish(grace::protocol::Message{newer}));
  const std::optional<Json> fish = b->next_of("state");
  REQUIRE(fish.has_value());
  CHECK((*fish)["rev"] == 2);
  CHECK(a->quiet(150ms));
  // Unwatched: the space hears it; a state published for it now goes nowhere, and is not refused.
  a->send_json(frame("unsub", {{"space", "demo"}, {"id", 4}, {"topic", "lantern.lamp/harbour-mouth"}}));
  CHECK(fixture.demo->wait_for([&] { return !has(fixture.demo->watched(), "lantern.lamp/harbour-mouth"); }));
  // Its slot, given back, goes to another member; the old member's state never reaches the new one's watcher.
  a->send_json(sub("demo", "lantern.lamp/old-crane", 5));
  const std::optional<Json> crane = a->next_of("state");
  REQUIRE(crane.has_value());
  CHECK((*crane)["topic"] == "lantern.lamp/old-crane");
  grace::protocol::State stale;
  stale.topic = "lantern.lamp/harbour-mouth";
  stale.rev = 9;
  stale.body = Json{{"sentence", "Not for the crane."}};
  CHECK(fixture.demo->port->publish(grace::protocol::Message{stale}));
  CHECK(a->quiet(150ms));
  // The family's own id is no topic the space publishes either.
  grace::protocol::State whole;
  whole.topic = "lantern.lamp";
  whole.body = Json::object();
  CHECK_FALSE(fixture.demo->port->publish(grace::protocol::Message{whole}));
}

TEST_CASE("an empty batch past a gap clears a stream: it reaches its watchers, and a later watcher gets no old rows (1.1)") {
  Fixture fixture;
  const std::unique_ptr<WsClient> a = fixture.join();
  a->send_json(sub("demo", "lantern.log", 1));
  const std::optional<Json> held = a->next_of("rows");
  REQUIRE(held.has_value());
  const std::uint64_t next = (*held)["from_seq"].get<std::uint64_t>() + (*held)["rows"].size();

  grace::protocol::Rows cleared;  // the space cleared its log: nothing held, its next row past a gap
  cleared.topic = "lantern.log";
  cleared.from_seq = next + 1;
  REQUIRE(fixture.demo->port->publish(grace::protocol::Message{cleared}));
  const std::optional<Json> empty = a->next_of("rows");
  REQUIRE(empty.has_value());
  CHECK((*empty)["from_seq"] == next + 1);
  CHECK((*empty)["rows"].empty());

  const std::unique_ptr<WsClient> b = fixture.join();  // a later watcher: the cleared rows are not replayed
  b->send_json(sub("demo", "lantern.log", 2));
  CHECK(b->quiet(150ms));

  grace::protocol::Rows after;
  after.topic = "lantern.log";
  after.from_seq = next + 1;
  after.rows = {Json{{"seq", next + 1}, {"time", "20:05"}, {"line", "The log begins again."}}};
  REQUIRE(fixture.demo->port->publish(grace::protocol::Message{after}));
  for (WsClient* client : {a.get(), b.get()}) {
    const std::optional<Json> fresh = client->next_of("rows");
    REQUIRE(fresh.has_value());
    CHECK((*fresh)["from_seq"] == next + 1);
    CHECK((*fresh)["rows"].size() == 1);
  }
}

TEST_CASE("a command's result goes to the connection that asked, and to no other") {
  Fixture fixture;
  const std::unique_ptr<WsClient> a = fixture.join();
  const std::unique_ptr<WsClient> b = fixture.join();
  a->send_json(cmd("demo", "lantern.trim", 7));
  const std::optional<Json> result = a->next_of("result");
  REQUIRE(result.has_value());
  CHECK((*result)["id"] == 7);
  CHECK((*result)["ok"] == true);
  CHECK(b->quiet(200ms));
  // An answer for a connection that has gone is dropped.
  std::atomic<bool> answered{false};
  fixture.demo->set_on_command([&](const SpaceEvent& event) {
    std::this_thread::sleep_for(100ms);
    grace::protocol::Result late;
    late.ok = true;
    fixture.demo->port->answer(event.reply, late);
    answered = true;
  });
  b->send_json(cmd("demo", "lantern.trim", 8));
  b->socket().close();
  CHECK(fixture.demo->wait_for([&] { return answered.load(); }));
  CHECK(a->quiet(200ms));
  grace::protocol::Result both;
  both.ok = true;
  both.error = grace::protocol::ErrorCode::internal;
  CHECK_FALSE(fixture.demo->port->answer(CommandRef{1, 1}, both));
}

TEST_CASE("each space's command queue holds 32; past it the newest is refused, and other spaces are untouched") {
  Fixture::Options options;
  options.timings.rate_window = 100ms;  // so that one connection may send 33 quickly
  Fixture fixture(options);
  const std::unique_ptr<WsClient> ws = fixture.join();
  fixture.demo->hold(true);
  for (std::uint32_t id = 1; id <= 32; ++id) {
    ws->send_json(cmd("demo", "lantern.trim", id));
    std::this_thread::sleep_for(6ms);
  }
  CHECK(ws->quiet(100ms));
  ws->send_json(cmd("demo", "lantern.trim", 33));
  const std::optional<Json> full = ws->next_of("result");
  REQUIRE(full.has_value());
  CHECK((*full)["id"] == 33);
  CHECK((*full)["error"] == "rate_limited");
  // GraceUI's space still takes its own.
  ws->send_json(cmd("graceui", "menu.close", 34));
  const std::optional<Json> other = ws->next_of("result");
  REQUIRE(other.has_value());
  CHECK((*other)["ok"] == true);
  fixture.demo->hold(false);
  CHECK(fixture.demo->wait_for([&] { return fixture.demo->commands().size() == 32; }));
  bool logged = false;
  for (const std::string& line : fixture.logs()) {
    logged = logged || line.find("command queue is full (demo)") != std::string::npos;
  }
  CHECK(logged);
}

TEST_CASE("20 messages a second, counted before parsing: past it each is dropped unread, the page told once") {
  Fixture fixture;
  const std::unique_ptr<WsClient> ws = fixture.join();
  std::this_thread::sleep_for(1100ms);  // the hello's token back
  for (int i = 0; i < 25; ++i) {
    ws->send_json(frame("ping"));
  }
  int pongs = 0;
  int limited = 0;
  while (const std::optional<Json> next = ws->next_json(300ms)) {
    if ((*next)["t"] == "pong") {
      ++pongs;
    } else if ((*next)["t"] == "error" && (*next)["code"] == "rate_limited") {
      ++limited;
    }
  }
  CHECK(pongs == 20);
  CHECK(limited == 1);
  std::this_thread::sleep_for(300ms);
  ws->send_json(frame("ping"));
  CHECK((*ws->next_json())["t"] == "pong");  // the bucket refills
}

TEST_CASE("bad messages: each answered bad_message without its payload; the third closes the connection") {
  Fixture fixture;
  const std::unique_ptr<WsClient> ws = fixture.join();
  const std::string marker = "lantern-oil-secret";
  ws->send_text("not json " + marker);
  Json error = *ws->next_json();
  CHECK(error["code"] == "bad_message");
  CHECK(error.dump().find(marker) == std::string::npos);
  ws->send_json(frame("sub", {{"space", "demo"}, {"id", "one"}, {"topic", marker}}));
  error = *ws->next_json();
  CHECK(error["code"] == "bad_message");
  CHECK(error["detail"] == "id: not a whole number");
  CHECK(error.dump().find(marker) == std::string::npos);
  ws->send_frame(2, marker);  // a binary message
  error = *ws->next_json();
  CHECK(error["code"] == "bad_message");
  std::uint16_t code = 0;
  CHECK(ws->closed(2s, &code));
  CHECK(code == 1008);
  for (const std::string& line : fixture.logs()) {
    CHECK(line.find(marker) == std::string::npos);
  }
}

TEST_CASE("32 subscriptions a connection") {
  Fixture fixture;
  SpaceDescription many;
  many.info.id = "many";
  many.info.name = "Many topics";
  for (int i = 0; i < 40; ++i) {
    grace::protocol::TopicInfo topic;
    topic.id = "topic." + std::to_string(i);
    topic.shape = grace::protocol::TopicShape::state;
    many.topics.push_back(topic);
  }
  TestSpace space(fixture.server, many, Json::object());
  const std::unique_ptr<WsClient> ws = fixture.join();
  for (std::uint32_t i = 0; i < 33; ++i) {
    ws->send_json(sub("many", "topic." + std::to_string(i), i));
    std::this_thread::sleep_for(55ms);  // within 20 a second
  }
  int states = 0;
  std::optional<Json> refusal;
  while (const std::optional<Json> next = ws->next_json(300ms)) {
    if ((*next)["t"] == "state") {
      ++states;
    } else if ((*next)["t"] == "error") {
      refusal = next;
    }
  }
  CHECK(states == 32);
  REQUIRE(refusal.has_value());
  CHECK((*refusal)["code"] == "not_now");
  CHECK((*refusal)["detail"] == "sub 32: this view watches 32 topics already");
  fixture.server.stop();  // before `space` goes
}

TEST_CASE("a third WebSocket supersedes the oldest, which is told bye superseded") {
  Fixture fixture;
  const std::unique_ptr<WsClient> first = fixture.join();
  std::this_thread::sleep_for(10ms);
  const std::unique_ptr<WsClient> second = fixture.join();
  std::this_thread::sleep_for(10ms);
  const std::unique_ptr<WsClient> third = fixture.join();
  const std::optional<Json> bye = first->next_json();
  REQUIRE(bye.has_value());
  CHECK((*bye)["t"] == "bye");
  CHECK((*bye)["reason"] == "superseded");
  CHECK(first->closed(2s));
  CHECK(second->quiet(100ms));
  second->send_json(frame("ping"));
  CHECK((*second->next_json())["t"] == "pong");
  third->send_json(frame("ping"));
  CHECK((*third->next_json())["t"] == "pong");
  CHECK(fixture.server.stats().websockets == 2);
}

TEST_CASE("only a welcomed view supersedes one: upgrades that show no ticket close the oldest of themselves") {
  Fixture fixture;
  const std::unique_ptr<WsClient> first = fixture.join();
  std::this_thread::sleep_for(10ms);
  const std::unique_ptr<WsClient> second = fixture.join();
  const std::string cookie = fixture.cookie();
  // Three upgrades with the cookie and no hello: the two views stay; past two, the oldest of the three goes.
  WsClient bare[3];
  for (WsClient& one : bare) {
    REQUIRE(one.open(fixture.port, cookie) == 101);
    std::this_thread::sleep_for(10ms);
  }
  CHECK(bare[0].closed(1s));
  CHECK(first->quiet(100ms));
  first->send_json(frame("ping"));
  CHECK((*first->next_json())["t"] == "pong");
  second->send_json(frame("ping"));
  CHECK((*second->next_json())["t"] == "pong");
  // The two left without a hello are closed at the hello's deadline, and the views are untouched.
  CHECK(bare[1].gone(4s));
  CHECK(bare[2].gone(4s));
  first->send_json(frame("ping"));
  CHECK((*first->next_json())["t"] == "pong");
}

TEST_CASE("a ping every interval; two unanswered close the connection, and a pong keeps it") {
  Fixture::Options options;
  options.timings.ping = 100ms;
  Fixture fixture(options);
  const std::unique_ptr<WsClient> silent = fixture.join();
  const std::unique_ptr<WsClient> lively = fixture.join();
  // The lively one answers every ping, on a thread of its own, while the silent one is watched.
  std::atomic<int> answered{0};
  std::thread answering([&] {
    while (answered < 6) {
      const std::optional<Json> ping = lively->next_json(1s, true);
      if (!ping || (*ping)["t"] != "ping") {
        return;
      }
      lively->send_json(frame("pong"));
      ++answered;
    }
  });
  int pings = 0;
  const auto start = std::chrono::steady_clock::now();
  std::uint16_t code = 0;
  while (std::chrono::steady_clock::now() - start < 2s) {
    const std::optional<WsFrame> next = silent->next(1s);
    if (!next) {
      break;
    }
    if (next->opcode == 1 && Json::parse(next->payload)["t"] == "ping") {
      ++pings;
    } else if (next->opcode == 8) {
      code = static_cast<std::uint16_t>((static_cast<std::uint8_t>(next->payload[0]) << 8) |
                                        static_cast<std::uint8_t>(next->payload[1]));
      break;
    }
  }
  CHECK(pings == 2);
  CHECK(code == 1001);
  answering.join();
  CHECK(answered == 6);  // six pings over 600 ms, each answered: open still
  lively->send_json(frame("ping"));
  std::optional<Json> answer = lively->next_json(1s);
  CHECK((*answer)["t"] == "pong");
}

TEST_CASE("a tab that never reads is closed, not buffered for") {
  Fixture::Options options;
  options.send_queue = 64 * 1024;
  options.timings.state_interval = 1ms;
  Fixture fixture(options);
  const std::string cookie = fixture.cookie();
  WsClient ws;
  REQUIRE(ws.open(fixture.port, cookie, "", 4096) == 101);
  REQUIRE(ws.send_json(hello_with(fixture.ticket(cookie))));
  REQUIRE(ws.next_of("welcome").has_value());
  ws.send_json(sub("demo", "lantern.status"));
  REQUIRE(fixture.demo->wait_for([&] { return has(fixture.demo->watched(), "lantern.status"); }));
  const std::string filler(100 * 1024, 'x');
  bool closed = false;
  for (std::uint64_t rev = 2; rev < 400 && !closed; ++rev) {
    grace::protocol::State big;
    big.topic = "lantern.status";
    big.rev = rev;
    big.body = Json{{"sentence", filler}};
    fixture.demo->port->publish(grace::protocol::Message{big});
    std::this_thread::sleep_for(3ms);
    closed = fixture.server.stats().websockets == 0;
  }
  CHECK(closed);
  bool logged = false;
  for (const std::string& line : fixture.logs()) {
    logged = logged || line.find("stopped reading") != std::string::npos;
  }
  CHECK(logged);
}

TEST_CASE("dashboard.disconnect: the space's answer, then a new cookie and every connection closed") {
  Fixture fixture;
  fixture.graceui->set_on_command([&](const SpaceEvent& event) {
    grace::protocol::Result result;
    result.ok = true;
    fixture.graceui->port->answer(event.reply, result);
    if (event.command == "dashboard.disconnect") {
      fixture.server.renew_session();
    }
  });
  const std::string old_cookie = fixture.cookie();
  const std::unique_ptr<WsClient> other = fixture.join();
  WsClient ws;
  REQUIRE(ws.join(fixture.port, old_cookie, fixture.ticket(old_cookie)));
  ws.send_json(cmd("graceui", "dashboard.disconnect", 3, {{"confirm", true}}));
  const std::optional<Json> result = ws.next_of("result");
  REQUIRE(result.has_value());
  CHECK((*result)["ok"] == true);
  const std::optional<Json> bye = ws.next_of("bye");
  REQUIRE(bye.has_value());
  CHECK((*bye)["reason"] == "unauthorised");
  CHECK(ws.closed(2s));
  CHECK((*other->next_of("bye"))["reason"] == "unauthorised");
  CHECK(request(fixture.port, ticket_post(fixture.port, old_cookie)).status == 403);
  const std::string fresh = fixture.cookie();
  CHECK(fresh.size() == 43);
  CHECK(fresh != old_cookie);
  CHECK(fixture.ticket(fresh).size() == 43);
}

TEST_CASE("a space withdrawn: its subscriptions end, and its topics and commands are unknown from then on") {
  Fixture fixture;
  const std::unique_ptr<WsClient> ws = fixture.join();
  ws->send_json(sub("demo", "lantern.status", 1));
  REQUIRE(ws->next_of("state").has_value());
  fixture.server.remove_space("demo");
  CHECK_FALSE(fixture.demo->port->registered());
  std::this_thread::sleep_for(50ms);
  ws->send_json(sub("demo", "lantern.status", 2));
  Json answer = *ws->next_json();
  CHECK(answer["code"] == "unknown_topic");
  ws->send_json(cmd("demo", "lantern.trim", 3));
  answer = *ws->next_json();
  CHECK(answer["code"] == "unknown_command");
  grace::protocol::State state;
  state.topic = "lantern.status";
  state.body = Json::object();
  CHECK_FALSE(fixture.demo->port->publish(grace::protocol::Message{state}));
  // A new view's welcome names GraceUI's space alone.
  Json welcome;
  const std::unique_ptr<WsClient> later = fixture.join(&welcome);
  REQUIRE(welcome["spaces"].size() == 1);
  CHECK(welcome["spaces"][0]["id"] == "graceui");
}

TEST_CASE("a sensitive command, which a paired device may never send (D221)") {
  using grace::protocol::Json;
  SpaceDescription d;
  d.commands = {"settings.set", "settings.reset", "secrets.set", "keepers.ward", "lamps.light"};
  d.sensitive = {"secrets.set"};
  d.secret = {"keepers.ward"};
  d.sensitive_keys = {{"dashboard.lan", "graceui.browser"}, {"provider.key", "ai.providers"}};
  CHECK(sensitive_command(d, "secrets.set", Json::object()));
  CHECK(sensitive_command(d, "keepers.ward", Json::object()));  // a row secret's
  CHECK_FALSE(sensitive_command(d, "lamps.light", Json::object()));
  CHECK(sensitive_command(d, "settings.set", Json::parse(R"({"key": "dashboard.lan", "value": true})")));
  CHECK_FALSE(sensitive_command(d, "settings.set", Json::parse(R"({"key": "ui.scale", "value": 1.2})")));
  CHECK(sensitive_command(d, "settings.reset", Json::parse(R"({"scope": "key", "target": "provider.key"})")));
  CHECK_FALSE(sensitive_command(d, "settings.reset", Json::parse(R"({"scope": "key", "target": "ui.scale"})")));
  CHECK(sensitive_command(d, "settings.reset", Json::parse(R"({"scope": "page", "target": "graceui.browser"})")));
  CHECK_FALSE(sensitive_command(d, "settings.reset", Json::parse(R"({"scope": "page", "target": "graceui.menu"})")));
  CHECK(sensitive_command(d, "settings.reset", Json::parse(R"({"scope": "all", "confirm": true})")));
  SpaceDescription plain;
  CHECK_FALSE(sensitive_command(plain, "settings.reset", Json::parse(R"({"scope": "all", "confirm": true})")));
}
