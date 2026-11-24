// lifecycle_tests.cpp: the server's caps and its life (§6.8, §7.2 items 2 and 24, §7.6; D111): a head's absolute
// deadline, the 17th plain HTTP connection, 100 connections at once, a taken port failing closed, start and stop by
// ordinary calls with both threads joined and nothing left open, and no cookie or ticket in any log line.
#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "harness.h"

using namespace grace::serve;
using namespace grace::serve::iotest;
using grace::protocol::Json;

TEST_CASE("a head must be complete within its deadline of accept, however it trickles in") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  const Millis deadline = fast_timings().head;
  SUBCASE("a connection that sends nothing") {
    Client idle;
    REQUIRE(idle.connect(port));
    const auto start = std::chrono::steady_clock::now();
    std::string bytes;
    CHECK(idle.read_to_close(bytes, 3s));
    CHECK(bytes.empty());  // closed, unanswered
    CHECK(std::chrono::steady_clock::now() - start >= deadline - 50ms);
  }
  SUBCASE("a head sent a byte at a time, never late between bytes") {
    Client slow;
    REQUIRE(slow.connect(port));
    const std::string head = page_load(port);
    const auto start = std::chrono::steady_clock::now();
    bool closed = false;
    for (std::size_t i = 0; i < head.size() && !closed; ++i) {
      closed = !slow.send(head.substr(i, 1));
      std::string bytes;
      closed = closed || !slow.receive(bytes, 40ms);
      if (!bytes.empty()) {
        FAIL("answered: ", bytes);
      }
    }
    CHECK(closed);  // the absolute deadline, not one per read (D111)
    CHECK(std::chrono::steady_clock::now() - start < deadline + 500ms);
  }
}

TEST_CASE("16 plain HTTP connections on loopback; the 17th waits until one closes") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  std::vector<std::unique_ptr<Client>> idle;
  for (int i = 0; i < 16; ++i) {
    idle.push_back(std::make_unique<Client>());
    REQUIRE(idle.back()->connect(port));
  }
  REQUIRE(fixture.demo->wait_for([&] { return fixture.server.stats().http == 16; }));
  Client seventeenth;
  REQUIRE(seventeenth.connect(port));  // the system's backlog takes it
  REQUIRE(seventeenth.send(page_load(port)));
  std::string bytes;
  seventeenth.receive(bytes, 150ms);
  CHECK(bytes.empty());
  CHECK(fixture.server.stats().http == 16);
  idle.front()->close();
  CHECK(seventeenth.read_to_close(bytes, 2s));
  CHECK(parse_reply(bytes).status == 200);
}

TEST_CASE("100 connections at once are each answered") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  std::vector<std::unique_ptr<Client>> clients;
  for (int i = 0; i < 100; ++i) {
    clients.push_back(std::make_unique<Client>());
    REQUIRE(clients.back()->connect(port));
    REQUIRE(clients.back()->send(plain_get(port, "/assets/app.js")));
  }
  int answered = 0;
  for (auto& client : clients) {
    std::string bytes;
    client->read_to_close(bytes, 5s);
    answered += parse_reply(bytes).status == 200 ? 1 : 0;
  }
  CHECK(answered == 100);
}

TEST_CASE("a taken port fails closed: no other port or interface, one line naming both causes") {
  const std::uint16_t port = free_port();
  PortHolder holder(port);
  REQUIRE(holder.held());
  Server server;
  std::vector<std::string> logs;
  ServerConfig config;
  config.port = port;
  config.from_memory = true;
  config.log = [&](LogLevel, std::string_view line) { logs.emplace_back(line); };
  CHECK(server.start(config) == StartResult::port_taken);
  CHECK_FALSE(server.running());
  CHECK(server.stats().threads == 0);
  CHECK(server.stats().sockets == 0);
  REQUIRE(logs.size() == 1);
  CHECK(logs[0].find(std::to_string(port)) != std::string::npos);
  CHECK(logs[0].find("another program") != std::string::npos);
  CHECK(logs[0].find("Windows reserves") != std::string::npos);
  CHECK(logs[0].find("dashboard.port") != std::string::npos);
  config.port = 1023;
  CHECK(server.start(config) == StartResult::bad_port);
}

TEST_CASE("stop closes every socket and joins both threads; the server starts again, its spaces kept") {
  Fixture fixture;
  CHECK(fixture.server.stats().threads == 2);
  const std::unique_ptr<WsClient> ws = fixture.join();
  ws->send_json(frame("sub", {{"space", "demo"}, {"id", 1}, {"topic", "lantern.status"}}));
  REQUIRE(ws->next_of("state").has_value());
  REQUIRE(fixture.demo->wait_for([&] { return !fixture.demo->watched().empty(); }));
  Client idle;
  REQUIRE(idle.connect(fixture.port));
  REQUIRE(fixture.demo->wait_for([&] { return fixture.server.stats().http == 1; }));
  CHECK(fixture.server.stats().sockets >= 4);  // its wake, a listener, the WebSocket and the idle connection

  fixture.server.stop();
  CHECK_FALSE(fixture.server.running());
  CHECK(fixture.server.stats().threads == 0);
  CHECK(fixture.server.stats().sockets == 0);
  const std::optional<Json> bye = ws->next_json();
  REQUIRE(bye.has_value());
  CHECK((*bye)["reason"] == "shutdown");
  CHECK(ws->gone(1s));
  std::string bytes;
  CHECK(idle.read_to_close(bytes, 1s));
  Client refused;
  CHECK_FALSE(refused.connect(fixture.port));
  // Every space hears that nothing is watched any more.
  CHECK(fixture.demo->wait_for([&] { return fixture.demo->watched().empty(); }));

  ServerConfig again;
  again.port = free_port();
  again.from_memory = true;
  again.memory_files.push_back(MemoryFile{"index.html", std::make_shared<const std::string>(fixture.folder->index)});
  REQUIRE(fixture.server.start(again) == StartResult::ok);
  CHECK(fixture.server.start(again) == StartResult::already_running);
  const Reply page = request(again.port, page_load(again.port));
  CHECK(page.status == 200);
  const std::string cookie = cookie_from(page);
  Json welcome;
  WsClient joined;
  REQUIRE(joined.join(again.port, cookie, fetch_ticket(again.port, cookie), &welcome));
  CHECK(welcome["spaces"].size() == 2);
  joined.send_json(frame("sub", {{"space", "demo"}, {"id", 1}, {"topic", "lantern.status"}}));
  CHECK(joined.next_of("state").has_value());
  fixture.server.stop();
  fixture.server.stop();  // idempotent
  CHECK(fixture.server.stats().threads == 0);
}

TEST_CASE("no cookie, ticket, payload or path in any log line") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  const std::string cookie = fixture.cookie();
  std::vector<std::string> secrets = {cookie};
  for (int i = 0; i < 3; ++i) {
    secrets.push_back(fixture.ticket(cookie));
  }
  WsClient ws;
  REQUIRE(ws.join(port, cookie, secrets.back()));
  ws.send_text("{\"v\":1,\"t\":\"cmd\",\"space\":\"demo\",\"id\":1,\"name\":\"nope\",\"args\":{\"key\":\"" +
               secrets[1] + "\"}}");
  ws.send_text("garbage " + cookie);
  WsClient reused;
  REQUIRE(reused.open(port, cookie) == 101);
  reused.send_json(frame("hello", {{"protocol", "1.0"}, {"bundle", "t"}, {"caps", Json::array()},
                                   {"ticket", secrets.back()}}));
  reused.closed(2s);
  request(port, plain_get(port, "/", "", "evil.example"));
  request(port, ticket_post(port, std::string(43, 'Z')));
  fixture.server.renew_session();
  std::this_thread::sleep_for(100ms);
  fixture.server.stop();
  const std::vector<std::string> logs = fixture.logs();
  CHECK_FALSE(logs.empty());
  for (const std::string& line : logs) {
    CAPTURE(line);
    for (const std::string& secret : secrets) {
      CHECK(line.find(secret) == std::string::npos);
    }
    CHECK(line.find("127.0.0.1:" + std::to_string(port)) == std::string::npos);  // no peer or local address
    CHECK(line.find(fixture.folder->path.string()) == std::string::npos);
  }
}

TEST_CASE("a test host's free port: the system's choice on 127.0.0.1, within the players' range, and bindable (D234)") {
  std::uint16_t port = 0;
  REQUIRE(free_loopback_port(port));
  CHECK(port >= kLowestPort);
  CHECK(port != kDefaultPort);
  const PortHolder holder(port);  // free when asked: another program, or the server, may take it next
  CHECK(holder.held());
  std::uint16_t again = 0;
  REQUIRE(free_loopback_port(again));
  CHECK(again != port);  // the held one is not offered again
}
