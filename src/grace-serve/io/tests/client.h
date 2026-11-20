// client.h: the security suite's browser, over real sockets on loopback (§9.1): raw HTTP requests, their replies, and
// a WebSocket client that masks its frames as a browser does. Blocking sockets with deadlines, POSIX or Winsock.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "grace/protocol/codec.h"

namespace grace::serve::iotest {

using namespace std::chrono_literals;
using Millis = std::chrono::milliseconds;

// Sockets for the client side (WSAStartup on Windows), once per process.
void start_client_network();

// A port free on 127.0.0.1 (and on ::1, where there is one) just now: the system's pick, released.
std::uint16_t free_port();

// Holds 127.0.0.1:port as another program would (the port-taken test).
class PortHolder {
 public:
  explicit PortHolder(std::uint16_t port);
  ~PortHolder();
  PortHolder(const PortHolder&) = delete;
  PortHolder& operator=(const PortHolder&) = delete;
  bool held() const { return handle_ != kNone; }

 private:
  static constexpr std::intptr_t kNone = -1;
  std::intptr_t handle_ = kNone;
};

class Client {
 public:
  Client() = default;
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  bool connect(std::uint16_t port, bool ipv6 = false, int receive_buffer = 0);
  bool send(std::string_view bytes);
  // Appends the first bytes to arrive within `limit` (or, with `want`, up to that many): false once the peer has
  // closed or failed.
  bool receive(std::string& into, Millis limit, std::size_t want = 0);
  // Reads until the peer closes (true) or `limit` passes (false).
  bool read_to_close(std::string& into, Millis limit);
  void close();
  bool connected() const { return handle_ != kNone; }

 private:
  static constexpr std::intptr_t kNone = -1;
  std::intptr_t handle_ = kNone;
};

struct Reply {
  int status = 0;
  std::string head;
  std::vector<std::pair<std::string, std::string>> fields;  // names lowercased
  std::string body;
  bool closed = false;  // the server closed the connection after it

  std::optional<std::string> field(std::string_view name) const;
  std::size_t count(std::string_view name) const;
};

// Parses one response; status 0 if none arrived.
Reply parse_reply(std::string_view raw);
// Sends `raw` on a new connection and reads until the server closes it.
Reply request(std::uint16_t port, std::string_view raw, Millis limit = 3s, bool ipv6 = false);

// Request heads, as a browser sends them. `host` defaults to 127.0.0.1:PORT.
std::string host_of(std::uint16_t port);
std::string origin_of(std::uint16_t port);
std::string page_load(std::uint16_t port, std::string_view path = "/", std::string_view extra = "",
                      std::string_view host = "");
std::string plain_get(std::uint16_t port, std::string_view path, std::string_view extra = "",
                      std::string_view host = "");
std::string ticket_post(std::uint16_t port, std::string_view cookie, std::string_view extra = "",
                        std::string_view body = "{}", bool with_origin = true);
std::string upgrade_head(std::uint16_t port, std::string_view cookie, std::string_view extra = "",
                         bool with_origin = true);

// The cookie a page load sets, or empty.
std::string cookie_from(const Reply& reply);
// A ticket from POST /api/ticket with the cookie, or empty.
std::string fetch_ticket(std::uint16_t port, std::string_view cookie);

struct WsFrame {
  std::uint8_t opcode = 0;
  std::string payload;
};

class WsClient {
 public:
  // The upgrade: the 101's status, or the refusal's (and the client is then closed).
  int open(std::uint16_t port, std::string_view cookie, std::string_view extra = "", int receive_buffer = 0);
  // Upgrade, hello with the ticket, and welcome: true with `welcome` set.
  bool join(std::uint16_t port, std::string_view cookie, std::string_view ticket, protocol::Json* welcome = nullptr);

  bool send_frame(std::uint8_t opcode, std::string_view payload, bool fin = true);
  bool send_text(std::string_view text) { return send_frame(1, text); }
  bool send_json(const protocol::Json& json) { return send_text(json.dump()); }

  // The next frame within `limit`.
  std::optional<WsFrame> next(Millis limit = 2s);
  // The next text frame's JSON, skipping pings unless `keep_pings`; nothing if a close or no frame came.
  std::optional<protocol::Json> next_json(Millis limit = 2s, bool keep_pings = false);
  // The next frame of kind `t`, skipping others.
  std::optional<protocol::Json> next_of(std::string_view kind, Millis limit = 2s);
  // Nothing but pings arrives within `limit`.
  bool quiet(Millis limit);
  // A Close frame arrives (its code set) and then the connection ends, within `limit`.
  bool closed(Millis limit, std::uint16_t* code = nullptr);
  // The connection ends within `limit`, whatever frames come first.
  bool gone(Millis limit);

  Client& socket() { return client_; }
  const std::string& response() const { return response_; }

 private:
  Client client_;
  std::string buffer_;
  std::string response_;
};

}  // namespace grace::serve::iotest
