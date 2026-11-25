// network_tests.cpp: other devices at home (G9, §7.2 item 16), over real sockets. The network listener stands on
// 127.0.0.2 (ServerConfig.network_addresses), never on a real address: it is off until switched on and closes with
// every device connection when switched off; an unpaired device sees the pairing page and nothing else; the code pairs
// once, within its time, and is void after five wrong tries; a paired device's cookie opens the page, a ticket and a
// WebSocket on that listener alone; a sensitive command is refused it; Forget disconnects it at once; and no log line
// names an address, a code or a cookie (invariant 4, PV3, PV5).
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <mutex>
#include <thread>

#include "../platform.h"
#include "grace/serve/pairing.h"
#include "harness.h"

using namespace grace::serve;
using namespace grace::serve::iotest;
namespace protocol = grace::protocol;
using grace::protocol::Json;

namespace {

constexpr std::array<std::uint8_t, 4> kHome{127, 0, 0, 2};

std::string home_host(std::uint16_t port) { return "127.0.0.2:" + std::to_string(port); }
std::string home_origin(std::uint16_t port) { return "http://127.0.0.2:" + std::to_string(port); }

std::string device_cookie(std::string_view cookie) {
  return cookie.empty() ? std::string() : "Cookie: en_device=" + std::string(cookie) + "\r\n";
}

std::string home_page(std::uint16_t port, std::string_view path = "/", std::string_view cookie = "") {
  return "GET " + std::string(path) + " HTTP/1.1\r\nHost: " + home_host(port) +
         "\r\nSec-Fetch-Site: none\r\nSec-Fetch-Mode: navigate\r\nSec-Fetch-Dest: document\r\n" +
         device_cookie(cookie) + "\r\n";
}

std::string home_get(std::uint16_t port, std::string_view path, std::string_view cookie = "") {
  return "GET " + std::string(path) + " HTTP/1.1\r\nHost: " + home_host(port) +
         "\r\nSec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: no-cors\r\nSec-Fetch-Dest: script\r\n" +
         device_cookie(cookie) + "\r\n";
}

std::string home_post(std::uint16_t port, std::string_view path, std::string_view body, std::string_view cookie = "",
                      std::string_view origin = "") {
  return "POST " + std::string(path) + " HTTP/1.1\r\nHost: " + home_host(port) +
         "\r\nOrigin: " + (origin.empty() ? home_origin(port) : std::string(origin)) +
         "\r\nSec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: cors\r\nContent-Type: application/json\r\n" +
         device_cookie(cookie) + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

std::string home_upgrade(std::uint16_t port, std::string_view cookie) {
  return "GET /api/ws HTTP/1.1\r\nHost: " + home_host(port) + "\r\nOrigin: " + home_origin(port) +
         "\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
         "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: websocket\r\n" +
         device_cookie(cookie) + "\r\n";
}

std::string pair_body(std::string_view code, std::string_view name = "Phone") {
  return Json{{"code", std::string(code)}, {"name", std::string(name)}}.dump();
}

// The device cookie a reply sets, or empty.
std::string device_from(const Reply& reply) {
  const std::optional<std::string> set = reply.field("set-cookie");
  if (!set || !set->starts_with("en_device=")) {
    return {};
  }
  return set->substr(10, set->find(';') - 10);
}

Json cmd(std::string_view space, std::string_view name, std::uint32_t id, Json args = Json::object()) {
  return frame("cmd", {{"space", std::string(space)}, {"id", id}, {"name", std::string(name)}, {"args", args}});
}

Fixture::Options home_options(std::function<void()> wake = {}) {
  Fixture::Options options;
  options.network = {kHome};
  options.paired_wake = std::move(wake);
  return options;
}

bool wait_until(const std::function<bool()>& condition, Millis limit = 2s) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (!condition()) {
    if (std::chrono::steady_clock::now() >= end) {
      return false;
    }
    std::this_thread::sleep_for(5ms);
  }
  return true;
}

bool network_on(Fixture& f) {
  f.server.set_network(true);
  return wait_until([&] { return !f.server.network_addresses().empty(); });
}

bool listening(std::uint16_t port) {
  Client client;
  return client.connect_ipv4(kHome, port);
}

// Pairs a device with a fresh code: its cookie, or empty.
std::string pair(Fixture& f, std::string_view name = "Phone") {
  const std::string code = f.server.start_pairing();
  REQUIRE(code.size() == kCodeDigits);
  return device_from(request_ipv4(kHome, f.port, home_post(f.port, kPairRoute, pair_body(code, name))));
}

// The paired devices as the host would keep them: taken from the server, then given back.
std::vector<DeviceKey> keys_of(const std::vector<PairedDevice>& devices) {
  std::vector<DeviceKey> keys;
  for (const PairedDevice& device : devices) {
    DeviceKey key;
    std::copy_n(device.id.begin(), key.id.size(), key.id.begin());
    key.hash = device.hash;
    keys.push_back(key);
  }
  return keys;
}

std::string ticket_for(std::uint16_t port, std::string_view cookie) {
  const Reply reply = request_ipv4(kHome, port, home_post(port, "/api/ticket", "{}", cookie));
  if (reply.status != 200) {
    return {};
  }
  const Json json = Json::parse(reply.body, nullptr, false);
  return json.is_object() && json.contains("ticket") ? json["ticket"].get<std::string>() : std::string();
}

}  // namespace

TEST_CASE("the network listener is off until switched on, and closes with every device when switched off (G9)") {
  Fixture f(home_options());
  CHECK_FALSE(listening(f.port));
  CHECK(f.server.network_addresses().empty());
  REQUIRE(network_on(f));
  CHECK(f.server.network_addresses() == std::vector<std::string>{"127.0.0.2"});
  CHECK(listening(f.port));

  const std::string cookie = pair(f);
  REQUIRE_FALSE(cookie.empty());
  WsClient ws;
  REQUIRE(ws.open_head(kHome, f.port, home_upgrade(f.port, cookie)) == 101);
  REQUIRE(ws.hello(ticket_for(f.port, cookie)));

  f.server.set_network(false);
  const std::optional<Json> bye = ws.next_of("bye");
  REQUIRE(bye.has_value());
  CHECK((*bye)["reason"] == "shutdown");
  CHECK(ws.gone(2s));
  CHECK(wait_until([&] { return f.server.network_addresses().empty() && !listening(f.port); }));
  // The loopback listener serves on.
  REQUIRE(f.wait_files());
  CHECK(request(f.port, page_load(f.port)).status == 200);
}

TEST_CASE("an unpaired device sees the pairing page and nothing else (G9)") {
  Fixture f(home_options());
  REQUIRE(network_on(f));

  const Reply page = request_ipv4(kHome, f.port, home_page(f.port));
  CHECK(page.status == 200);
  CHECK(page.body == pairing_page());
  CHECK(page.field("content-type") == "text/html; charset=utf-8");
  CHECK(page.count("set-cookie") == 0);  // neither the session's cookie nor a device's
  CHECK(page.field("access-control-allow-origin") == std::nullopt);
  CHECK(page.field("content-security-policy").has_value());
  CHECK(request_ipv4(kHome, f.port, home_page(f.port, "/settings")).body == pairing_page());

  const Reply script = request_ipv4(kHome, f.port, home_get(f.port, kPairScriptRoute));
  CHECK(script.status == 200);
  CHECK(script.body == pairing_script());
  CHECK(script.field("content-type") == "text/javascript; charset=utf-8");
  CHECK(request_ipv4(kHome, f.port, home_get(f.port, kPairStyleRoute)).body == pairing_style());

  // No file of the app, no ticket, no WebSocket.
  CHECK(request_ipv4(kHome, f.port, home_get(f.port, "/assets/app.js")).status == 404);
  CHECK(request_ipv4(kHome, f.port, home_post(f.port, "/api/ticket", "{}")).status == 403);
  WsClient ws;
  CHECK(ws.open_head(kHome, f.port, home_upgrade(f.port, "")) == 403);
  // A session cookie, from this PC's own page load, opens nothing there.
  const std::string session = f.cookie();
  REQUIRE_FALSE(session.empty());
  const std::string with_session =
      "GET /assets/app.js HTTP/1.1\r\nHost: " + home_host(f.port) + "\r\nCookie: en_session=" + session + "\r\n\r\n";
  CHECK(request_ipv4(kHome, f.port, with_session).body == pairing_page());  // no Fetch Metadata: a page load
  // A Host other than the listener's own address is refused (DNS rebinding), and so is the loopback name.
  const std::string rebound = "GET / HTTP/1.1\r\nHost: example.test:" + std::to_string(f.port) + "\r\n\r\n";
  CHECK(request_ipv4(kHome, f.port, rebound).status == 421);
  CHECK(request_ipv4(kHome, f.port, page_load(f.port)).status == 421);
  // A query string is 404, as on loopback.
  CHECK(request_ipv4(kHome, f.port, home_page(f.port, "/?code=1")).status == 404);
}

TEST_CASE("the code pairs once, within its time, and is void after five wrong tries (G9)") {
  std::atomic<int> woken{0};
  Fixture f(home_options([&] { ++woken; }));
  REQUIRE(network_on(f));
  const auto try_code = [&](std::string_view code, std::string_view name = "Phone") {
    return request_ipv4(kHome, f.port, home_post(f.port, kPairRoute, pair_body(code, name)));
  };

  // No code showing: the same answer as a wrong one, so that the network learns nothing of when a code shows.
  Reply none = try_code("123456");
  CHECK(none.status == 403);
  CHECK(none.body.find("none is showing") != std::string::npos);
  CHECK(device_from(none).empty());

  // The right code: a cookie, HttpOnly and SameSite=Strict, host-only, for 400 days.
  std::string code = f.server.start_pairing();
  REQUIRE(code.size() == kCodeDigits);
  CHECK(f.server.pairing_code() == code);
  const std::string wrong = code == "000000" ? "000001" : "000000";
  Reply missed = try_code(wrong);
  CHECK(missed.status == 403);
  CHECK(missed.body.find("not right") != std::string::npos);
  Reply paired = try_code(code, "Kitchen tablet");
  CHECK(paired.status == 200);
  const std::optional<std::string> set = paired.field("set-cookie");
  REQUIRE(set.has_value());
  CHECK(set->find("; Path=/; Max-Age=34560000; HttpOnly; SameSite=Strict") != std::string::npos);
  CHECK(set->find("Domain") == std::string::npos);
  CHECK(device_from(paired).size() == kTokenChars);
  CHECK(woken.load() == 1);
  const std::vector<PairedDevice> taken = f.server.take_paired();
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].name == "Kitchen tablet");
  CHECK(valid_device_id(taken[0].id));
  CHECK(taken[0].hash == device_hash(device_from(paired)));
  CHECK(f.server.take_paired().empty());

  // Once: the same code again pairs nothing.
  CHECK(f.server.pairing_code().empty());
  CHECK(try_code(code).status == 403);

  // One peer's wrong tries hold that peer, not the code: past three, it is refused even the right code, which still
  // shows for another device (the five tries that void a code take more than one peer: pairing_tests).
  code = f.server.start_pairing();
  const std::string other = code == "000000" ? "000001" : "000000";
  for (std::size_t i = 0; i < kCodeTries; ++i) {
    const Reply refused = try_code(other);
    CHECK(refused.status == 403);
    CHECK(refused.body == missed.body);  // one answer, whether wrong or held
  }
  CHECK(f.server.pairing_code() == code);
  CHECK(try_code(code).status == 403);
  CHECK(f.server.pairing_code() == code);

  // A bad name or body spends nothing: the code still pairs after them.
  code = f.server.start_pairing();
  CHECK(try_code(code, "").status == 400);
  CHECK(try_code(code, std::string(33, 'x')).status == 400);
  CHECK(request_ipv4(kHome, f.port, home_post(f.port, kPairRoute, "[1]")).status == 400);
  CHECK(request_ipv4(kHome, f.port, home_post(f.port, kPairRoute, std::string(kMaxPairBody + 1, ' '))).status == 413);
  // Origin and Fetch Metadata, as every API route (items 6 and 7).
  CHECK(request_ipv4(kHome, f.port, home_post(f.port, kPairRoute, pair_body(code), "", origin_of(f.port))).status ==
        403);
  const std::string cross = "POST /api/pair HTTP/1.1\r\nHost: " + home_host(f.port) + "\r\nOrigin: " +
                            home_origin(f.port) +
                            "\r\nSec-Fetch-Site: cross-site\r\nContent-Type: application/json\r\nContent-Length: " +
                            std::to_string(pair_body(code).size()) + "\r\n\r\n" + pair_body(code);
  CHECK(request_ipv4(kHome, f.port, cross).status == 403);
  CHECK(f.server.pairing_code() == code);
  CHECK(try_code(code).status == 200);
  // Cancelled in game: none showing.
  code = f.server.start_pairing();
  f.server.cancel_pairing();
  CHECK(try_code(code).status == 403);
  // The pairing route is the network listener's alone.
  CHECK(request(f.port, "POST /api/pair HTTP/1.1\r\nHost: " + host_of(f.port) + "\r\nOrigin: " + origin_of(f.port) +
                            "\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}")
            .status == 404);

  // No log line names the address, a code or a cookie.
  for (const std::string& line : f.logs()) {
    CAPTURE(line);
    CHECK(line.find("127.0.0.2") == std::string::npos);
    CHECK(line.find(code) == std::string::npos);
    CHECK(line.find(device_from(paired)) == std::string::npos);
  }
}

TEST_CASE("a paired device opens the page, a ticket and a WebSocket on its listener alone (G9)") {
  Fixture f(home_options());
  REQUIRE(network_on(f));
  REQUIRE(f.wait_files());
  const std::string cookie = pair(f);
  REQUIRE_FALSE(cookie.empty());

  // The page, its cookie renewed with the same value, and no session cookie.
  const Reply page = request_ipv4(kHome, f.port, home_page(f.port, "/", cookie));
  CHECK(page.status == 200);
  CHECK(page.body == f.folder->index);
  CHECK(device_from(page) == cookie);
  CHECK(page.count("set-cookie") == 1);
  CHECK(request_ipv4(kHome, f.port, home_get(f.port, "/assets/app.js", cookie)).status == 200);
  // A cookie not paired sees the pairing page.
  CHECK(request_ipv4(kHome, f.port, home_page(f.port, "/", std::string(kTokenChars, 'A'))).body == pairing_page());

  // A device's ticket opens a WebSocket there.
  WsClient ws;
  REQUIRE(ws.open_head(kHome, f.port, home_upgrade(f.port, cookie)) == 101);
  REQUIRE(ws.hello(ticket_for(f.port, cookie)));

  // A ticket opens a WebSocket only on the listener that issued it.
  WsClient crossed;
  REQUIRE(crossed.open_head(kHome, f.port, home_upgrade(f.port, cookie)) == 101);
  CHECK_FALSE(crossed.hello(f.ticket(f.cookie())));
  WsClient back;
  REQUIRE(back.open(f.port, f.cookie()) == 101);
  CHECK_FALSE(back.hello(ticket_for(f.port, cookie)));

  // A device's ticket opens that device's WebSocket alone.
  const std::string second = pair(f, "Laptop");
  REQUIRE_FALSE(second.empty());
  WsClient other;
  REQUIRE(other.open_head(kHome, f.port, home_upgrade(f.port, second)) == 101);
  CHECK_FALSE(other.hello(ticket_for(f.port, cookie)));
}

TEST_CASE("a paired device is refused every sensitive command (G9, D81)") {
  Fixture f(home_options());
  REQUIRE(network_on(f));
  const std::string cookie = pair(f);
  WsClient ws;
  REQUIRE(ws.open_head(kHome, f.port, home_upgrade(f.port, cookie)) == 101);
  REQUIRE(ws.hello(ticket_for(f.port, cookie)));

  // A secret setting and a row secret: refused, and never reach the space.
  REQUIRE(ws.send_json(cmd("demo", "settings.set", 1, {{"key", "lantern.oil_token"}, {"value", "lamp-oil"}})));
  std::optional<Json> result = ws.next_of("result");
  REQUIRE(result.has_value());
  CHECK((*result)["error"] == "not_now");
  CHECK((*result)["detail"] == "Only on the PC the game runs on.");
  REQUIRE(ws.send_json(cmd("demo", "keepers.ward", 2, {{"id", "k1"}, {"value", "ward"}})));
  result = ws.next_of("result");
  REQUIRE(result.has_value());
  CHECK((*result)["error"] == "not_now");
  // An ordinary setting goes through.
  REQUIRE(ws.send_json(cmd("demo", "settings.set", 3, {{"key", "lantern.brightness"}, {"value", 40}})));
  result = ws.next_of("result");
  REQUIRE(result.has_value());
  CHECK((*result)["ok"] == true);
  CHECK(f.demo->commands() == std::vector<std::string>{"settings.set"});

  // On this PC the same secret goes through.
  const std::unique_ptr<WsClient> pc = f.join();
  REQUIRE(pc != nullptr);
  REQUIRE(pc->send_json(cmd("demo", "settings.set", 4, {{"key", "lantern.oil_token"}, {"value", "lamp-oil"}})));
  result = pc->next_of("result");
  REQUIRE(result.has_value());
  CHECK((*result)["ok"] == true);
}

TEST_CASE("a space learns which view sent a command from its channel, never from the view (§7.3, D224)") {
  Fixture f(home_options());
  REQUIRE(network_on(f));
  std::mutex mutex;
  std::vector<std::string> views;
  f.demo->set_on_command([&](const SpaceEvent& event) {
    {
      std::lock_guard lock(mutex);
      views.emplace_back(protocol::to_string(event.view));
    }
    protocol::Result result;
    result.space = "demo";
    result.id = event.reply.id;
    result.ok = true;
    f.demo->port->answer(event.reply, std::move(result));
  });
  const std::string cookie = pair(f);
  WsClient device;
  REQUIRE(device.open_head(kHome, f.port, home_upgrade(f.port, cookie)) == 101);
  Json welcome;
  REQUIRE(device.hello(ticket_for(f.port, cookie), &welcome));
  CHECK(welcome["view"] == "device");  // what the page offers follows it (1.9: no upload on a device, D226)
  Json claimed = cmd("demo", "lantern.trim", 1);
  claimed["view"] = "game";  // a view's own word is never kept
  REQUIRE(device.send_json(claimed));
  REQUIRE(device.next_of("result").has_value());
  Json pc_welcome;
  const std::unique_ptr<WsClient> pc = f.join(&pc_welcome);
  REQUIRE(pc != nullptr);
  CHECK(pc_welcome["view"] == "browser");
  Json pretending = cmd("demo", "lantern.trim", 2);
  pretending["view"] = "device";
  REQUIRE(pc->send_json(pretending));
  REQUIRE(pc->next_of("result").has_value());
  std::lock_guard lock(mutex);
  CHECK(views == std::vector<std::string>{"device", "browser"});
}

TEST_CASE("a device never takes this PC's place: its views, waiting sockets and tickets (D221's review)") {
  Fixture f(home_options());
  REQUIRE(network_on(f));
  const std::string cookie = pair(f);
  // This PC's two views hold every place: a device's view is the one closed, and this PC's stay.
  const std::unique_ptr<WsClient> first = f.join();
  const std::unique_ptr<WsClient> second = f.join();
  REQUIRE(first != nullptr);
  REQUIRE(second != nullptr);
  WsClient device;
  REQUIRE(device.open_head(kHome, f.port, home_upgrade(f.port, cookie)) == 101);
  const std::string ticket = ticket_for(f.port, cookie);
  REQUIRE_FALSE(ticket.empty());
  Json hello = {{"v", 1}, {"t", "hello"}, {"protocol", "1.0"}, {"bundle", "test"}, {"caps", Json::array()}};
  hello["ticket"] = ticket;
  REQUIRE(device.send_json(hello));
  const std::optional<Json> bye = device.next_of("bye", 2s);
  REQUIRE(bye.has_value());
  CHECK((*bye)["reason"] == "superseded");
  CHECK(first->quiet(300ms));
  CHECK(second->quiet(300ms));
  // A third view of this PC's takes a place as before, its oldest's.
  const std::unique_ptr<WsClient> third = f.join();
  REQUIRE(third != nullptr);
  CHECK((*first->next_of("bye"))["reason"] == "superseded");
  // Every ticket place held by this PC's: a device's ticket is refused, never one of this PC's voided.
  const std::string session = f.cookie();
  std::vector<std::string> mine;
  for (std::size_t i = 0; i < kMaxTickets; ++i) {
    mine.push_back(fetch_ticket(f.port, session));
    REQUIRE_FALSE(mine.back().empty());
  }
  const Reply refused = request_ipv4(kHome, f.port, home_post(f.port, "/api/ticket", "{}", cookie));
  CHECK(refused.status == 503);
  WsClient last;
  REQUIRE(last.open(f.port, session) == 101);
  CHECK(last.hello(mine.front()));  // the oldest of this PC's still opens its view
}

TEST_CASE("Forget disconnects a device at once, and keeps the others (G9)") {
  Fixture f(home_options());
  REQUIRE(network_on(f));
  const std::string phone = pair(f, "Phone");
  const std::string tablet = pair(f, "Tablet");
  const std::vector<PairedDevice> devices = f.server.take_paired();
  REQUIRE(devices.size() == 2);
  // The host's list, as it would keep it: both.
  f.server.set_devices(keys_of(devices));
  WsClient a;
  REQUIRE(a.open_head(kHome, f.port, home_upgrade(f.port, phone)) == 101);
  REQUIRE(a.hello(ticket_for(f.port, phone)));
  WsClient b;
  REQUIRE(b.open_head(kHome, f.port, home_upgrade(f.port, tablet)) == 101);
  REQUIRE(b.hello(ticket_for(f.port, tablet)));

  // Which devices are connected, by id (the Browser view page's "Now" column).
  std::vector<std::string> both = {devices[0].id, devices[1].id};
  std::sort(both.begin(), both.end());
  CHECK(wait_until([&] { return f.server.connected_devices() == both; }));
  // The phone forgotten: its WebSocket ends with unauthorised, so its page reloads to the pairing page.
  f.server.set_devices(keys_of({devices[1]}));
  const std::optional<Json> bye = a.next_of("bye");
  REQUIRE(bye.has_value());
  CHECK((*bye)["reason"] == "unauthorised");
  CHECK(a.gone(2s));
  CHECK(b.quiet(300ms));
  CHECK(wait_until([&] { return f.server.connected_devices() == std::vector<std::string>{devices[1].id}; }));
  CHECK(request_ipv4(kHome, f.port, home_page(f.port, "/", phone)).body == pairing_page());
  CHECK(request_ipv4(kHome, f.port, home_post(f.port, "/api/ticket", "{}", phone)).status == 403);
  CHECK_FALSE(ticket_for(f.port, tablet).empty());

  // A device paired after the host's last take stays, whatever list the host gives.
  const std::string laptop = pair(f, "Laptop");
  f.server.set_devices({});
  CHECK(wait_until([&] { return request_ipv4(kHome, f.port, home_page(f.port, "/", tablet)).body == pairing_page(); }));
  CHECK_FALSE(ticket_for(f.port, laptop).empty());
  CHECK(f.server.take_paired().size() == 1);
}

TEST_CASE("the PC's own IPv4 addresses are listed, within the room given (G9)") {
  // The network listener takes the private ones among them (pairing_tests); here only that the system's list is read,
  // bounded, and holds no empty address. Nothing is bound and no address is printed.
  std::array<std::array<std::uint8_t, 4>, 16> found{};
  const std::size_t count = io::local_ipv4_addresses(found);
  CHECK(count <= found.size());
  for (std::size_t i = 0; i < count; ++i) {
    CHECK(found[i] != std::array<std::uint8_t, 4>{});
  }
  std::array<std::array<std::uint8_t, 4>, 1> one{};
  CHECK(io::local_ipv4_addresses(one) <= 1);
  CHECK(io::local_ipv4_addresses(std::span<std::array<std::uint8_t, 4>>()) == 0);
}

TEST_CASE("a report preview's shared text never reaches a paired device; this PC's view gets it (D263)") {
  Fixture f(home_options());
  REQUIRE(network_on(f));
  const std::string cookie = pair(f);
  WsClient device;
  REQUIRE(device.open_head(kHome, f.port, home_upgrade(f.port, cookie)) == 101);
  REQUIRE(device.hello(ticket_for(f.port, cookie)));
  const Json watch = {{"v", 1}, {"t", "sub"}, {"space", "demo"}, {"id", 1}, {"topic", "lantern.report"}};
  REQUIRE(device.send_json(watch));
  const std::optional<Json> seen = device.next_of("state");
  REQUIRE(seen.has_value());
  CHECK((*seen)["body"].contains("parts"));  // the preview's parts, as before
  CHECK_FALSE((*seen)["body"].contains("share"));
  const std::unique_ptr<WsClient> pc = f.join();
  REQUIRE(pc != nullptr);
  REQUIRE(pc->send_json(watch));
  const std::optional<Json> here = pc->next_of("state");
  REQUIRE(here.has_value());
  CHECK((*here)["body"]["share"] == demo_fixtures()["lantern.report"]["share"]);
  // A newer revision: the same again, each its own way.
  grace::protocol::State newer;
  newer.topic = "lantern.report";
  newer.rev = 7;
  newer.body = demo_fixtures()["lantern.report"];
  newer.body["share"] = Json{{"blocked", "Remove the path in Your notes before sharing."}};
  REQUIRE(f.demo->port->publish(grace::protocol::Message{newer}));
  const std::optional<Json> device_next = device.next_of("state");
  REQUIRE(device_next.has_value());
  CHECK((*device_next)["rev"] == 7);
  CHECK_FALSE((*device_next)["body"].contains("share"));
  const std::optional<Json> pc_next = pc->next_of("state");
  REQUIRE(pc_next.has_value());
  CHECK((*pc_next)["body"]["share"]["blocked"] == "Remove the path in Your notes before sharing.");
}

TEST_CASE("a page and a topic on this PC alone: a paired device never gets the page, and its sub is refused (D264)") {
  Fixture f(home_options());
  REQUIRE(network_on(f));
  const std::string cookie = pair(f);
  WsClient device;
  REQUIRE(device.open_head(kHome, f.port, home_upgrade(f.port, cookie)) == 101);
  REQUIRE(device.hello(ticket_for(f.port, cookie)));
  const auto sub_of = [](std::string_view topic, std::uint32_t id) {
    return Json{{"v", 1}, {"t", "sub"}, {"space", "demo"}, {"id", id}, {"topic", topic}};
  };
  const auto names = [](const Json& pages, std::string_view id) {
    const std::string dump = pages.dump();
    return dump.find("\"" + std::string(id) + "\"") != std::string::npos;
  };
  // Its page data without the Workbench: not as a page, in a category or a Related link.
  REQUIRE(device.send_json(sub_of("pages", 1)));
  const std::optional<Json> seen = device.next_of("state");
  REQUIRE(seen.has_value());
  CHECK_FALSE(names((*seen)["body"], "demo.workbench"));
  CHECK(names((*seen)["body"], "demo.log"));
  // Its readings refused, in the words a sensitive command's refusal has.
  REQUIRE(device.send_json(sub_of("lantern.readings", 2)));
  const std::optional<Json> refused = device.next_of("error");
  REQUIRE(refused.has_value());
  CHECK((*refused)["code"] == "not_now");
  CHECK((*refused)["detail"].get<std::string>().find("Only on the PC the game runs on.") != std::string::npos);
  CHECK_FALSE(f.demo->wait_for([&] {
    const std::vector<std::string> watched = f.demo->watched();
    return std::find(watched.begin(), watched.end(), "lantern.readings") != watched.end();
  }, 300ms));
  // Another topic as before.
  REQUIRE(device.send_json(sub_of("lantern.status", 3)));
  CHECK(device.next_of("state").has_value());
  // This PC's view: the page, and the readings.
  const std::unique_ptr<WsClient> pc = f.join();
  REQUIRE(pc != nullptr);
  REQUIRE(pc->send_json(sub_of("pages", 4)));
  const std::optional<Json> here = pc->next_of("state");
  REQUIRE(here.has_value());
  CHECK(names((*here)["body"], "demo.workbench"));
  REQUIRE(pc->send_json(sub_of("lantern.readings", 5)));
  const std::optional<Json> readings = pc->next_of("state");
  REQUIRE(readings.has_value());
  CHECK((*readings)["topic"] == "lantern.readings");
}
