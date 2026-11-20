// The WebSocket handshake (RFC 6455 §4.2): Sec-WebSocket-Accept from the RFC's own example, every refusal of an upgrade
// request with its status, and the 101.
#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>

#include "grace/serve/response.h"
#include "grace/serve/websocket.h"
#include "support.h"

using namespace grace::serve;
using namespace grace::serve::test;

namespace {

constexpr std::string_view kKey = "dGhlIHNhbXBsZSBub25jZQ==";  // RFC 6455 §1.3's "the sample nonce"
constexpr std::string_view kAccept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

const std::string kUpgradeFields = "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n";

struct Checked {
  Refusal refusal;
  std::string accept;
};

Checked upgrade(std::string_view fields, std::string_view method = "GET", std::size_t after = 0) {
  HeadParser parser(loopback_rules());
  const std::string head = std::string(method) + " /ws HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n" +
                           (method == "POST" ? "Content-Length: 0\r\n" : "") + std::string(fields) + "\r\n";
  REQUIRE(parser.feed(head).state == HeadParser::State::complete);
  std::array<char, kAcceptChars> accept{};
  const Refusal refusal = check_upgrade(parser.head(), after, accept);
  return Checked{refusal, std::string(accept.data(), accept.size())};
}

std::string with_key(std::string_view key) {
  return kUpgradeFields + "Sec-WebSocket-Key: " + std::string(key) + "\r\n";
}

}  // namespace

TEST_CASE("Sec-WebSocket-Accept: RFC 6455's example") {
  std::array<char, kAcceptChars> accept{};
  compute_accept(kKey, accept);
  CHECK(std::string_view(accept.data(), accept.size()) == kAccept);
}

TEST_CASE("a browser's upgrade request is taken, and its Accept computed") {
  const Checked plain = upgrade(with_key(kKey) + "Origin: http://127.0.0.1:8642\r\nSec-WebSocket-Extensions: "
                                                 "permessage-deflate; client_max_window_bits\r\n");
  CHECK(plain.refusal == Refusal::none);
  CHECK(plain.accept == kAccept);
  // Firefox's Connection, the token's case, Upgrade's case, and Connection split over two fields.
  CHECK(upgrade("Upgrade: WebSocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                "Sec-WebSocket-Key: " + std::string(kKey) + "\r\n")
            .refusal == Refusal::none);
  CHECK(upgrade("Upgrade: websocket\r\nConnection: keep-alive\r\nConnection: upgrade\r\nSec-WebSocket-Version: 13\r\n"
                "Sec-WebSocket-Key: " + std::string(kKey) + "\r\n")
            .refusal == Refusal::none);
}

TEST_CASE("every refusal of an upgrade request, with its status") {
  const std::string key = "Sec-WebSocket-Key: " + std::string(kKey) + "\r\n";
  CHECK(upgrade(with_key(kKey), "POST").refusal == Refusal::upgrade_not_get);
  CHECK(upgrade("Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n" + key).refusal == Refusal::upgrade_required);
  CHECK(upgrade("Upgrade: h2c\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n" + key).refusal ==
        Refusal::bad_upgrade);
  CHECK(upgrade("Upgrade: websocket, h2c\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n" + key).refusal ==
        Refusal::bad_upgrade);
  CHECK(upgrade("Upgrade: websocket\r\nConnection: keep-alive\r\nSec-WebSocket-Version: 13\r\n" + key).refusal ==
        Refusal::no_connection_upgrade);
  CHECK(upgrade("Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n" + key).refusal ==
        Refusal::no_connection_upgrade);
  CHECK(upgrade("Upgrade: websocket\r\nConnection: Upgradeish\r\nSec-WebSocket-Version: 13\r\n" + key).refusal ==
        Refusal::no_connection_upgrade);
  CHECK(upgrade("Upgrade: websocket\r\nConnection: Upgrade\r\n" + key).refusal == Refusal::ws_version_missing);
  CHECK(upgrade("Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 8\r\n" + key).refusal ==
        Refusal::ws_version);
  CHECK(upgrade("Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13, 8\r\n" + key).refusal ==
        Refusal::ws_version);
  CHECK(upgrade(kUpgradeFields).refusal == Refusal::bad_ws_key);
  CHECK(upgrade(with_key("")).refusal == Refusal::bad_ws_key);
  CHECK(upgrade(with_key("dGhlIHNhbXBsZSBub25jZQ")).refusal == Refusal::bad_ws_key);       // no padding
  CHECK(upgrade(with_key("dGhlIHNhbXBsZSBub25jZR==")).refusal == Refusal::bad_ws_key);     // unused bits set
  CHECK(upgrade(with_key("dGhlIHNhbXBsZSBub25jZWE=")).refusal == Refusal::bad_ws_key);     // 17 bytes
  CHECK(upgrade(with_key("dGhlIHNhbXBsZSBub25j")).refusal == Refusal::bad_ws_key);         // 15 bytes
  CHECK(upgrade(with_key("dGhlIHNhbXBsZSBub25jZQ==dGhl")).refusal == Refusal::bad_ws_key);
  CHECK(upgrade(with_key("dGhlIHNhbXBsZSBub25jZQ-_")).refusal == Refusal::bad_ws_key);
  CHECK(upgrade(with_key(kKey), "GET", 1).refusal == Refusal::upgrade_with_body);  // a frame sent before the 101

  CHECK(status_of(Refusal::upgrade_not_get) == 400);
  CHECK(status_of(Refusal::upgrade_required) == 426);
  CHECK(status_of(Refusal::bad_upgrade) == 400);
  CHECK(status_of(Refusal::no_connection_upgrade) == 400);
  CHECK(status_of(Refusal::ws_version_missing) == 400);
  CHECK(status_of(Refusal::ws_version) == 426);
  CHECK(status_of(Refusal::bad_ws_key) == 400);
  CHECK(status_of(Refusal::upgrade_with_body) == 400);

  // A body is the parser's 400 already, before the upgrade is looked at; a second key is a duplicate.
  HeadParser body(loopback_rules());
  CHECK(body.feed("GET /ws HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nContent-Length: 4\r\n" + with_key(kKey) + "\r\n")
            .state == HeadParser::State::refused);
  CHECK(body.refusal() == Refusal::get_with_body);
  HeadParser twice(loopback_rules());
  CHECK(twice.feed("GET /ws HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n" + with_key(kKey) + key + "\r\n").state ==
        HeadParser::State::refused);
  CHECK(twice.refusal() == Refusal::duplicate_field);
}

TEST_CASE("the 101 carries Accept and the security headers, and no Content-Length") {
  std::array<char, kMaxResponseHead> out{};
  const std::size_t size = write_switching_protocols(kAccept, out);
  REQUIRE(size != 0);
  const std::string head(out.data(), size);
  CHECK(head.rfind("HTTP/1.1 101 Switching Protocols\r\n", 0) == 0);
  CHECK(head.find("\r\nUpgrade: websocket\r\n") != std::string::npos);
  CHECK(head.find("\r\nConnection: Upgrade\r\n") != std::string::npos);
  CHECK(head.find("\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != std::string::npos);
  CHECK(head.find(std::string(security_headers())) != std::string::npos);
  CHECK(head.find("Content-Length") == std::string::npos);
  CHECK(head.find("Sec-WebSocket-Extensions") == std::string::npos);
  CHECK(head.find("Sec-WebSocket-Protocol") == std::string::npos);
  CHECK(head.size() >= 4);
  CHECK(head.substr(head.size() - 4) == "\r\n\r\n");

  CHECK(write_switching_protocols("short", out) == 0);
  CHECK(write_switching_protocols("s3pPLMBiTxaQ9kYGzzhZRbK+xO\r\n", out) == 0);
  std::array<char, 64> small{};
  CHECK(write_switching_protocols(kAccept, small) == 0);
}
