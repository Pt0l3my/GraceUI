// upload_tests.cpp: the upload route (G28, D226, §7.2 item 18) in the server suite (§9.1): a ticket asked over the
// WebSocket, bound to its session, the space, a file command and the file's size and SHA-256; POST /upload on the
// loopback listener alone with every check of an API call; the bytes to the space with their command, checked and held
// by nothing of the server's after; and each refusal of §9.1's list: no ticket, a reused or expired ticket, another
// session's, a size or hash that differs, an oversized body, a second upload at once, the network listener, a
// cross-site or foreign-origin request, and a command not marked as taking a file.
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "grace/serve/codec.h"
#include "grace/serve/pairing.h"
#include "harness.h"

using namespace grace::serve;
using namespace grace::serve::iotest;
namespace protocol = grace::protocol;
using grace::protocol::Json;

namespace {

constexpr std::array<std::uint8_t, 4> kHome{127, 0, 0, 2};

std::string sha256_hex(std::string_view bytes) {
  Sha256 sha;
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

// An invented roster file of `size` bytes.
std::string roster(std::size_t size, char seed = 'a') {
  std::string out;
  out.reserve(size);
  const std::string line = "[[keeper]]\nname = \"Invented keeper\"\npost = \"North wall\"\n";
  while (out.size() < size) {
    out += line;
    out.back() = seed;
  }
  out.resize(size);
  return out;
}

Json upload_frame(std::uint32_t id, std::string_view command, std::size_t size, const std::string& sha256,
                  Json args = Json::object()) {
  return frame("upload", {{"space", "demo"},
                          {"id", id},
                          {"name", std::string(command)},
                          {"args", std::move(args)},
                          {"size", size},
                          {"sha256", sha256}});
}

std::string post_head(std::uint16_t port, std::string_view cookie, std::string_view ticket, std::size_t length,
                      std::string_view extra = "", std::string_view origin = "",
                      std::string_view type = "application/octet-stream") {
  std::string head = "POST /upload HTTP/1.1\r\nHost: " + host_of(port) + "\r\n";
  head += "Origin: " + (origin.empty() ? origin_of(port) : std::string(origin)) + "\r\n";
  head += "Sec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: cors\r\n";
  if (!type.empty()) {
    head += "Content-Type: " + std::string(type) + "\r\n";
  }
  if (!cookie.empty()) {
    head += "Cookie: en_session=" + std::string(cookie) + "\r\n";
  }
  if (!ticket.empty()) {
    head += "Grace-Upload-Ticket: " + std::string(ticket) + "\r\n";
  }
  return head + std::string(extra) + "Content-Length: " + std::to_string(length) + "\r\n\r\n";
}

// What the demo space took: each command's name, view, args and file (the server's hold on it counted).
struct Taken {
  std::string command;
  std::string view;
  Json args;
  std::string bytes;
  long holders = 0;  // the file's holders as the space took it: 1, the space's alone
};

struct Taker {
  std::mutex mutex;
  std::vector<Taken> taken;
  std::shared_ptr<const FileBytes> kept;  // the last file, held a moment to count its holders, then let go

  void attach(TestSpace& space) {
    space.set_on_command([this, &space](const SpaceEvent& event) {
      Taken one;
      one.command = event.command;
      one.view = std::string(protocol::to_string(event.view));
      one.args = event.args;
      if (event.file) {
        one.bytes.assign(event.file->bytes().begin(), event.file->bytes().end());
        one.holders = event.file.use_count();
      }
      {
        std::lock_guard lock(mutex);
        taken.push_back(std::move(one));
      }
      protocol::Result result;
      result.space = "demo";
      result.id = event.reply.id;
      result.ok = true;
      result.body = Json{{"keepers", 3}};
      space.port->answer(event.reply, std::move(result));
    });
  }
  std::vector<Taken> all() {
    std::lock_guard lock(mutex);
    return taken;
  }
};

std::string ticket_of(WsClient& ws, std::uint32_t id) {
  const std::optional<Json> answer = ws.next_of("upload_ticket");
  if (!answer || (*answer)["id"] != id || !(*answer)["ticket"].is_string()) {
    return {};
  }
  return (*answer)["ticket"].get<std::string>();
}

std::optional<Json> result_of(WsClient& ws, std::uint32_t id) {
  for (int i = 0; i < 10; ++i) {
    std::optional<Json> result = ws.next_of("result");
    if (!result || (*result)["id"] == id) {
      return result;
    }
  }
  return std::nullopt;
}

bool wait_for(const std::function<bool()>& condition, Millis limit = 2s) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (!condition()) {
    if (std::chrono::steady_clock::now() >= end) {
      return false;
    }
    std::this_thread::sleep_for(5ms);
  }
  return true;
}

Fixture::Options upload_options() {
  Fixture::Options options;
  options.timings.upload_ticket = 600ms;
  options.timings.upload = 1500ms;
  return options;
}

}  // namespace

TEST_CASE("an upload: a ticket over the WebSocket, the bytes by POST, the file to its space and its result (G28)") {
  Fixture f(upload_options());
  Taker taker;
  taker.attach(*f.demo);
  const std::unique_ptr<WsClient> ws = f.join();
  REQUIRE(ws != nullptr);
  const std::string cookie = f.cookie();
  // A file larger than one read, so that it arrives in many.
  const std::string file = roster(3 * 1024 * 1024 + 17);
  REQUIRE(ws->send_json(upload_frame(7, "keepers.import", file.size(), sha256_hex(file), Json{{"merge", true}})));
  const std::string ticket = ticket_of(*ws, 7);
  REQUIRE(ticket.size() == kTokenChars);
  const Reply posted = request(f.port, post_head(f.port, cookie, ticket, file.size()) + file, 10s);
  CHECK(posted.status == 202);
  const std::optional<Json> result = result_of(*ws, 7);
  REQUIRE(result.has_value());
  CHECK((*result)["ok"] == true);
  CHECK((*result)["body"]["keepers"] == 3);
  const std::vector<Taken> taken = taker.all();
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].command == "keepers.import");
  CHECK(taken[0].view == "browser");
  CHECK(taken[0].args == Json{{"merge", true}});
  CHECK(taken[0].bytes == file);
  CHECK(taken[0].holders == 1);  // the server let go of it as it handed it over
  // Used once: the same ticket again is refused, and the space sees nothing more.
  const Reply again = request(f.port, post_head(f.port, cookie, ticket, file.size()) + file, 10s);
  CHECK(again.status == 403);
  CHECK(taker.all().size() == 1);
  // Nothing on disk: the web folder holds what it held.
  std::size_t entries = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(f.folder->path)) {
    entries += entry.is_regular_file() ? 1 : 0;
  }
  CHECK(entries == 4);  // graceui.toml, index.html, assets/app.js and the stray file
  for (const std::string& line : f.logs()) {
    CAPTURE(line);
    CHECK(line.find(ticket) == std::string::npos);
  }
}

TEST_CASE("an upload's ticket: none, unknown, expired, another session's, its session gone (G28)") {
  Fixture f(upload_options());
  Taker taker;
  taker.attach(*f.demo);
  const std::unique_ptr<WsClient> ws = f.join();
  REQUIRE(ws != nullptr);
  const std::string cookie = f.cookie();
  const std::string file = roster(4096);
  const std::string hash = sha256_hex(file);
  CHECK(request(f.port, post_head(f.port, cookie, "", file.size()) + file).status == 403);
  CHECK(request(f.port, post_head(f.port, cookie, std::string(kTokenChars, 'A'), file.size()) + file).status == 403);
  CHECK(request(f.port, post_head(f.port, cookie, "short", file.size()) + file).status == 403);
  // Two ticket fields: refused, whichever is right.
  REQUIRE(ws->send_json(upload_frame(1, "keepers.import", file.size(), hash)));
  std::string ticket = ticket_of(*ws, 1);
  REQUIRE_FALSE(ticket.empty());
  CHECK(request(f.port, post_head(f.port, cookie, ticket, file.size(), "Grace-Upload-Ticket: " + ticket + "\r\n") + file)
            .status == 403);
  // A request refused before its ticket is looked at leaves the ticket as it was: it still serves, once.
  CHECK(request(f.port, post_head(f.port, cookie, ticket, file.size()) + file).status == 202);
  CHECK((*result_of(*ws, 1))["ok"] == true);
  // Expired: not used within its time, and the session told.
  REQUIRE(ws->send_json(upload_frame(2, "keepers.import", file.size(), hash)));
  const std::string late = ticket_of(*ws, 2);
  REQUIRE_FALSE(late.empty());
  std::optional<Json> ran_out = result_of(*ws, 2);
  REQUIRE(ran_out.has_value());
  CHECK((*ran_out)["error"] == "not_now");
  CHECK(request(f.port, post_head(f.port, cookie, late, file.size()) + file).status == 403);
  // Another session's: the cookie renewed (Disconnect) after the ticket, its old cookie refused.
  const std::unique_ptr<WsClient> other = f.join();
  REQUIRE(other != nullptr);
  REQUIRE(other->send_json(upload_frame(3, "keepers.import", file.size(), hash)));
  const std::string theirs = ticket_of(*other, 3);
  REQUIRE_FALSE(theirs.empty());
  f.server.renew_session();
  CHECK(ws->gone(2s));
  CHECK(other->gone(2s));
  CHECK(request(f.port, post_head(f.port, cookie, theirs, file.size()) + file).status == 403);
  // Its session gone: the ticket went with it, even with the new cookie.
  const std::string renewed = f.cookie();
  CHECK(request(f.port, post_head(f.port, renewed, theirs, file.size()) + file).status == 403);
  CHECK(taker.all().size() == 1);  // the one that served, and nothing else
}

TEST_CASE("an upload's size and hash: checked before and after the bytes arrive (G28)") {
  Fixture f(upload_options());
  Taker taker;
  taker.attach(*f.demo);
  const std::unique_ptr<WsClient> ws = f.join();
  REQUIRE(ws != nullptr);
  const std::string cookie = f.cookie();
  const std::string file = roster(8192);
  // Past 32 MiB: refused at the ticket, and a POST that declares more at once, before a byte is read.
  REQUIRE(ws->send_json(upload_frame(1, "keepers.import", protocol::kUploadMax + 1, sha256_hex(file))));
  std::optional<Json> too_big = result_of(*ws, 1);
  REQUIRE(too_big.has_value());
  CHECK((*too_big)["error"] == "invalid_value");
  CHECK(request(f.port, post_head(f.port, cookie, std::string(kTokenChars, 'A'), protocol::kUploadMax + 1)).status ==
        413);
  REQUIRE(ws->send_json(upload_frame(2, "keepers.import", 0, sha256_hex(""))));
  CHECK((*result_of(*ws, 2))["error"] == "invalid_value");
  REQUIRE(ws->send_json(upload_frame(3, "keepers.import", file.size(), "NOT-HEX")));
  CHECK((*result_of(*ws, 3))["error"] == "bad_message");
  // A Content-Length other than the ticket's: refused, the ticket spent, the session told.
  REQUIRE(ws->send_json(upload_frame(4, "keepers.import", file.size(), sha256_hex(file))));
  std::string ticket = ticket_of(*ws, 4);
  CHECK(request(f.port, post_head(f.port, cookie, ticket, file.size() + 1) + file + "x").status == 413);
  CHECK((*result_of(*ws, 4))["error"] == "invalid_value");
  REQUIRE(ws->send_json(upload_frame(5, "keepers.import", file.size(), sha256_hex(file))));
  ticket = ticket_of(*ws, 5);
  CHECK(request(f.port, post_head(f.port, cookie, ticket, file.size() - 1) + file.substr(1)).status == 400);
  CHECK((*result_of(*ws, 5))["error"] == "invalid_value");
  // The same size, other bytes: the hash differs once they have arrived.
  REQUIRE(ws->send_json(upload_frame(6, "keepers.import", file.size(), sha256_hex(file))));
  ticket = ticket_of(*ws, 6);
  const Reply changed = request(f.port, post_head(f.port, cookie, ticket, file.size()) + roster(8192, 'z'));
  CHECK(changed.status == 400);
  CHECK(changed.body.find("changed on its way") != std::string::npos);
  std::optional<Json> result = result_of(*ws, 6);
  REQUIRE(result.has_value());
  CHECK((*result)["error"] == "invalid_value");
  // A body that stops short: the POST times out, the session is told, the place is free again.
  REQUIRE(ws->send_json(upload_frame(7, "keepers.import", file.size(), sha256_hex(file))));
  ticket = ticket_of(*ws, 7);
  {
    Client client;
    REQUIRE(client.connect(f.port));
    REQUIRE(client.send(post_head(f.port, cookie, ticket, file.size()) + file.substr(0, 100)));
    std::string bytes;
    client.read_to_close(bytes, 4s);
  }
  result = result_of(*ws, 7);
  REQUIRE(result.has_value());
  CHECK((*result)["detail"] == "The file did not arrive whole: add it again.");
  // A wrong media type, and no length.
  REQUIRE(ws->send_json(upload_frame(8, "keepers.import", file.size(), sha256_hex(file))));
  ticket = ticket_of(*ws, 8);
  CHECK(request(f.port, post_head(f.port, cookie, ticket, file.size(), "", "", "multipart/form-data") + file).status ==
        415);
  CHECK(taker.all().empty());
}

TEST_CASE("an upload: one at a time a session, a file command alone, this PC alone (G28)") {
  Fixture::Options options = upload_options();
  options.network = {kHome};
  Fixture f(std::move(options));  // moved: GCC 13 at -O3 misreads a copy of the address list (-Warray-bounds)
  Taker taker;
  taker.attach(*f.demo);
  const std::unique_ptr<WsClient> ws = f.join();
  REQUIRE(ws != nullptr);
  const std::string cookie = f.cookie();
  const std::string file = roster(2048);
  // A second ticket while the first is unused: busy.
  REQUIRE(ws->send_json(upload_frame(1, "keepers.import", file.size(), sha256_hex(file))));
  REQUIRE_FALSE(ticket_of(*ws, 1).empty());
  REQUIRE(ws->send_json(upload_frame(2, "keepers.import", file.size(), sha256_hex(file))));
  CHECK((*result_of(*ws, 2))["error"] == "busy");
  // A command not marked as taking a file, and a file command sent as a plain command.
  REQUIRE(ws->send_json(upload_frame(3, "lantern.trim", file.size(), sha256_hex(file))));
  CHECK((*result_of(*ws, 3))["error"] == "unknown_command");
  REQUIRE(ws->send_json(frame("cmd", {{"space", "demo"}, {"id", 4}, {"name", "keepers.import"}, {"args", Json::object()}})));
  std::optional<Json> plain = result_of(*ws, 4);
  REQUIRE(plain.has_value());
  CHECK((*plain)["error"] == "invalid_value");
  // Cross-site, a foreign Origin, no Origin: refused before the ticket is looked at.
  const std::string cross = post_head(f.port, cookie, std::string(kTokenChars, 'A'), file.size());
  std::string cross_site = cross;
  cross_site.replace(cross_site.find("same-origin"), 11, "cross-site");
  CHECK(request(f.port, cross_site + file).status == 403);
  CHECK(request(f.port, post_head(f.port, cookie, std::string(kTokenChars, 'A'), file.size(), "",
                                  "http://example.test:" + std::to_string(f.port)) + file)
            .status == 403);
  std::string no_origin = cross;
  no_origin.erase(no_origin.find("Origin: "), no_origin.find("\r\n", no_origin.find("Origin: ")) - no_origin.find("Origin: ") + 2);
  CHECK(request(f.port, no_origin + file).status == 403);
  // The network listener has no upload route, and a paired device's WebSocket gets no ticket.
  f.server.set_network(true);
  REQUIRE(wait_for([&] { return !f.server.network_addresses().empty(); }));
  const std::array<std::uint8_t, 4> home = kHome;
  const std::string here = "127.0.0.2:" + std::to_string(f.port);
  std::string on_network = cross;
  on_network.replace(on_network.find(host_of(f.port)), host_of(f.port).size(), here);
  on_network.replace(on_network.find(origin_of(f.port)), origin_of(f.port).size(), "http://" + here);
  CHECK(request_ipv4(home, f.port, on_network + file).status == 413);  // no body past 256 bytes there, let alone a file
  const std::string code = f.server.start_pairing();
  const std::string pair_body = Json{{"code", code}, {"name", "Phone"}}.dump();
  const auto device_post = [&](std::string_view path, std::string_view body, std::string_view device) {
    return "POST " + std::string(path) + " HTTP/1.1\r\nHost: " + here + "\r\nOrigin: http://" + here +
           "\r\nSec-Fetch-Site: same-origin\r\nContent-Type: application/json\r\n" +
           (device.empty() ? std::string() : "Cookie: en_device=" + std::string(device) + "\r\n") +
           "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
  };
  const Reply paired = request_ipv4(home, f.port, device_post(kPairRoute, pair_body, ""));
  const std::optional<std::string> set = paired.field("set-cookie");
  REQUIRE(set.has_value());
  const std::string device = set->substr(10, set->find(';') - 10);
  const Json ticket_json = Json::parse(request_ipv4(home, f.port, device_post("/api/ticket", "{}", device)).body);
  WsClient phone;
  REQUIRE(phone.open_head(home, f.port,
                          "GET /api/ws HTTP/1.1\r\nHost: " + here + "\r\nOrigin: http://" + here +
                              "\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
                              "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-Fetch-Site: same-origin\r\n"
                              "Cookie: en_device=" + device + "\r\n\r\n") == 101);
  REQUIRE(phone.hello(ticket_json["ticket"].get<std::string>()));
  REQUIRE(phone.send_json(upload_frame(9, "keepers.import", file.size(), sha256_hex(file))));
  const std::optional<Json> refused = result_of(phone, 9);
  REQUIRE(refused.has_value());
  CHECK((*refused)["error"] == "not_now");
  CHECK(taker.all().empty());
}
