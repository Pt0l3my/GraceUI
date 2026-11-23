// client.cpp: the suite's browser (client.h).
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

#include "client.h"

namespace grace::serve::iotest {
namespace {

#if defined(_WIN32)
using Native = SOCKET;
constexpr Native kInvalid = ~SOCKET{0};
void close_native(Native s) { ::closesocket(s); }
int poll_native(Native s, bool write, int timeout_ms) {
  WSAPOLLFD item{};
  item.fd = s;
  item.events = write ? POLLWRNORM : POLLRDNORM;
  return ::WSAPoll(&item, 1, timeout_ms);
}
int recv_native(Native s, char* data, std::size_t size) { return ::recv(s, data, static_cast<int>(size), 0); }
int send_native(Native s, const char* data, std::size_t size) { return ::send(s, data, static_cast<int>(size), 0); }
#else
using Native = int;
constexpr Native kInvalid = -1;
void close_native(Native s) { ::close(s); }
int poll_native(Native s, bool write, int timeout_ms) {
  pollfd item{};
  item.fd = s;
  item.events = write ? POLLOUT : POLLIN;
  return ::poll(&item, 1, timeout_ms);
}
int recv_native(Native s, char* data, std::size_t size) { return static_cast<int>(::recv(s, data, size, 0)); }
int send_native(Native s, const char* data, std::size_t size) {
  return static_cast<int>(::send(s, data, size, MSG_NOSIGNAL));
}
#endif

Native native(std::intptr_t handle) { return static_cast<Native>(handle); }

union Address {
  sockaddr any;
  sockaddr_in v4;
  sockaddr_in6 v6;
};

Address loopback(bool ipv6, std::uint16_t port, int& size) {
  Address address{};
  if (ipv6) {
    address.v6.sin6_family = AF_INET6;
    address.v6.sin6_port = htons(port);
    address.v6.sin6_addr.s6_addr[15] = 1;
    size = sizeof address.v6;
  } else {
    address.v4.sin_family = AF_INET;
    address.v4.sin_port = htons(port);
    address.v4.sin_addr.s_addr = htonl(0x7F000001u);
    size = sizeof address.v4;
  }
  return address;
}

std::string lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// A browser's mask: any four bytes do.
constexpr std::array<std::uint8_t, 4> kMask{0x1F, 0x82, 0x4C, 0xD3};

}  // namespace

void start_client_network() {
#if defined(_WIN32)
  static bool started = false;
  if (!started) {
    WSADATA data{};
    started = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
#endif
}

std::uint16_t free_port() {
  start_client_network();
  for (int attempt = 0; attempt < 50; ++attempt) {
    const Native s = ::socket(AF_INET, SOCK_STREAM, 0);
    int size = 0;
    Address address = loopback(false, 0, size);
#if defined(_WIN32)
    int length = size;
#else
    socklen_t length = static_cast<socklen_t>(size);
#endif
    std::uint16_t port = 0;
    if (::bind(s, &address.any, size) == 0 && ::getsockname(s, &address.any, &length) == 0) {
      port = ntohs(address.v4.sin_port);
    }
    close_native(s);
    if (port < 1024) {
      continue;
    }
    // Free on ::1 too, where the system has it, so that the server binds both.
    const Native s6 = ::socket(AF_INET6, SOCK_STREAM, 0);
    bool ok = true;
    if (s6 != kInvalid) {
      Address v6 = loopback(true, port, size);
      ok = ::bind(s6, &v6.any, size) == 0;
      close_native(s6);
    }
    if (ok) {
      return port;
    }
  }
  return 0;
}

PortHolder::PortHolder(std::uint16_t port) {
  start_client_network();
  const Native s = ::socket(AF_INET, SOCK_STREAM, 0);
  int size = 0;
  Address address = loopback(false, port, size);
  if (::bind(s, &address.any, size) == 0 && ::listen(s, 4) == 0) {
    handle_ = static_cast<std::intptr_t>(s);
  } else {
    close_native(s);
  }
}

PortHolder::~PortHolder() {
  if (handle_ != kNone) {
    close_native(native(handle_));
  }
}

Client::~Client() { close(); }

void Client::close() {
  if (handle_ != kNone) {
    close_native(native(handle_));
    handle_ = kNone;
  }
}

bool Client::connect(std::uint16_t port, bool ipv6, int receive_buffer) {
  start_client_network();
  close();
  const Native s = ::socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
  if (s == kInvalid) {
    return false;
  }
  if (receive_buffer > 0) {
    std::array<char, sizeof(int)> value{};
    std::memcpy(value.data(), &receive_buffer, value.size());
    ::setsockopt(s, SOL_SOCKET, SO_RCVBUF, value.data(), static_cast<int>(value.size()));
  }
  int size = 0;
  Address address = loopback(ipv6, port, size);
  if (::connect(s, &address.any, size) != 0) {
    close_native(s);
    return false;
  }
  handle_ = static_cast<std::intptr_t>(s);
  return true;
}

bool Client::connect_ipv4(std::array<std::uint8_t, 4> to, std::uint16_t port) {
  start_client_network();
  close();
  const Native s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s == kInvalid) {
    return false;
  }
  Address address{};
  address.v4.sin_family = AF_INET;
  address.v4.sin_port = htons(port);
  std::memcpy(&address.v4.sin_addr, to.data(), to.size());
  if (::connect(s, &address.any, static_cast<int>(sizeof address.v4)) != 0) {
    close_native(s);
    return false;
  }
  handle_ = static_cast<std::intptr_t>(s);
  return true;
}

bool Client::send(std::string_view bytes) {
  std::size_t sent = 0;
  while (sent < bytes.size()) {
    if (poll_native(native(handle_), true, 2000) <= 0) {
      return false;
    }
    const int n = send_native(native(handle_), bytes.data() + sent, bytes.size() - sent);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

bool Client::receive(std::string& into, Millis limit, std::size_t want) {
  const auto end = std::chrono::steady_clock::now() + limit;
  std::size_t got = 0;
  while (want == 0 || got < want) {
    const auto left = std::chrono::duration_cast<Millis>(end - std::chrono::steady_clock::now()).count();
    if (left <= 0) {
      return true;
    }
    const int ready = poll_native(native(handle_), false, static_cast<int>(left));
    if (ready <= 0) {
      return true;
    }
    std::array<char, 65536> buffer{};
    const std::size_t room = want == 0 ? buffer.size() : std::min(buffer.size(), want - got);
    const int n = recv_native(native(handle_), buffer.data(), room);
    if (n <= 0) {
      return false;
    }
    into.append(buffer.data(), static_cast<std::size_t>(n));
    got += static_cast<std::size_t>(n);
    if (want == 0) {
      return true;  // whatever arrived first
    }
  }
  return true;
}

bool Client::read_to_close(std::string& into, Millis limit) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < end) {
    if (!receive(into, std::chrono::duration_cast<Millis>(end - std::chrono::steady_clock::now()))) {
      return true;
    }
  }
  return false;
}

std::optional<std::string> Reply::field(std::string_view name) const {
  const std::string wanted = lower(name);
  for (const auto& [key, value] : fields) {
    if (key == wanted) {
      return value;
    }
  }
  return std::nullopt;
}

std::size_t Reply::count(std::string_view name) const {
  const std::string wanted = lower(name);
  return static_cast<std::size_t>(
      std::count_if(fields.begin(), fields.end(), [&](const auto& field) { return field.first == wanted; }));
}

Reply parse_reply(std::string_view raw) {
  Reply reply;
  const std::size_t end = raw.find("\r\n\r\n");
  if (end == std::string_view::npos || !raw.starts_with("HTTP/1.1 ")) {
    return reply;
  }
  reply.head = std::string(raw.substr(0, end + 4));
  reply.status = std::stoi(std::string(raw.substr(9, 3)));
  std::size_t at = raw.find("\r\n") + 2;
  while (at < end) {
    const std::size_t line_end = raw.find("\r\n", at);
    const std::string_view line = raw.substr(at, line_end - at);
    const std::size_t colon = line.find(':');
    std::string_view value = line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') {
      value.remove_prefix(1);
    }
    reply.fields.emplace_back(lower(line.substr(0, colon)), std::string(value));
    at = line_end + 2;
  }
  reply.body = std::string(raw.substr(end + 4));
  return reply;
}

Reply request(std::uint16_t port, std::string_view raw, Millis limit, bool ipv6) {
  Client client;
  if (!client.connect(port, ipv6) || !client.send(raw)) {
    return {};
  }
  std::string bytes;
  const bool closed = client.read_to_close(bytes, limit);
  Reply reply = parse_reply(bytes);
  reply.closed = closed;
  return reply;
}

Reply request_ipv4(std::array<std::uint8_t, 4> address, std::uint16_t port, std::string_view raw, Millis limit) {
  Client client;
  if (!client.connect_ipv4(address, port) || !client.send(raw)) {
    return {};
  }
  std::string bytes;
  const bool closed = client.read_to_close(bytes, limit);
  Reply reply = parse_reply(bytes);
  reply.closed = closed;
  return reply;
}

std::string host_of(std::uint16_t port) { return "127.0.0.1:" + std::to_string(port); }
std::string origin_of(std::uint16_t port) { return "http://127.0.0.1:" + std::to_string(port); }

std::string page_load(std::uint16_t port, std::string_view path, std::string_view extra, std::string_view host) {
  return "GET " + std::string(path) + " HTTP/1.1\r\nHost: " + (host.empty() ? host_of(port) : std::string(host)) +
         "\r\nSec-Fetch-Site: none\r\nSec-Fetch-Mode: navigate\r\nSec-Fetch-Dest: document\r\n" + std::string(extra) +
         "\r\n";
}

std::string plain_get(std::uint16_t port, std::string_view path, std::string_view extra, std::string_view host) {
  return "GET " + std::string(path) + " HTTP/1.1\r\nHost: " + (host.empty() ? host_of(port) : std::string(host)) +
         "\r\n" + std::string(extra) + "\r\n";
}

std::string ticket_post(std::uint16_t port, std::string_view cookie, std::string_view extra, std::string_view body,
                        bool with_origin) {
  std::string head = "POST /api/ticket HTTP/1.1\r\nHost: " + host_of(port) + "\r\n";
  if (with_origin) {
    head += "Origin: " + origin_of(port) + "\r\n";
  }
  head += "Sec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: cors\r\nContent-Type: application/json\r\n";
  if (!cookie.empty()) {
    head += "Cookie: en_session=" + std::string(cookie) + "\r\n";
  }
  head += std::string(extra);
  head += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
  return head;
}

std::string upgrade_head(std::uint16_t port, std::string_view cookie, std::string_view extra, bool with_origin) {
  std::string head = "GET /api/ws HTTP/1.1\r\nHost: " + host_of(port) +
                     "\r\nConnection: keep-alive, Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
                     "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-Fetch-Site: same-origin\r\n"
                     "Sec-Fetch-Mode: websocket\r\n";
  if (with_origin) {
    head += "Origin: " + origin_of(port) + "\r\n";
  }
  if (!cookie.empty()) {
    head += "Cookie: en_session=" + std::string(cookie) + "\r\n";
  }
  return head + std::string(extra) + "\r\n";
}

std::string cookie_from(const Reply& reply) {
  const std::optional<std::string> set = reply.field("set-cookie");
  if (!set || !set->starts_with("en_session=")) {
    return {};
  }
  const std::size_t end = set->find(';');
  return set->substr(11, end - 11);
}

std::string fetch_ticket(std::uint16_t port, std::string_view cookie) {
  const Reply reply = request(port, ticket_post(port, cookie));
  if (reply.status != 200) {
    return {};
  }
  const protocol::Json json = protocol::Json::parse(reply.body, nullptr, false);
  if (!json.is_object() || !json.contains("ticket") || !json["ticket"].is_string()) {
    return {};
  }
  return json["ticket"].get<std::string>();
}

int WsClient::open(std::uint16_t port, std::string_view cookie, std::string_view extra, int receive_buffer) {
  buffer_.clear();
  response_.clear();
  if (!client_.connect(port, false, receive_buffer) || !client_.send(upgrade_head(port, cookie, extra))) {
    return 0;
  }
  const auto end = std::chrono::steady_clock::now() + 3s;
  while (buffer_.find("\r\n\r\n") == std::string::npos && std::chrono::steady_clock::now() < end) {
    if (!client_.receive(buffer_, 100ms)) {
      break;
    }
  }
  const std::size_t head_end = buffer_.find("\r\n\r\n");
  if (head_end == std::string::npos) {
    return 0;
  }
  response_ = buffer_.substr(0, head_end + 4);
  buffer_.erase(0, head_end + 4);
  const int status = parse_reply(response_).status;
  if (status != 101) {
    client_.close();
  }
  return status;
}

int WsClient::open_head(std::array<std::uint8_t, 4> address, std::uint16_t port, std::string_view head) {
  buffer_.clear();
  response_.clear();
  if (!client_.connect_ipv4(address, port) || !client_.send(head)) {
    return 0;
  }
  const auto end = std::chrono::steady_clock::now() + 3s;
  while (buffer_.find("\r\n\r\n") == std::string::npos && std::chrono::steady_clock::now() < end) {
    if (!client_.receive(buffer_, 100ms)) {
      break;
    }
  }
  const std::size_t head_end = buffer_.find("\r\n\r\n");
  if (head_end == std::string::npos) {
    return 0;
  }
  response_ = buffer_.substr(0, head_end + 4);
  buffer_.erase(0, head_end + 4);
  const int status = parse_reply(response_).status;
  if (status != 101) {
    client_.close();
  }
  return status;
}

bool WsClient::hello(std::string_view ticket, protocol::Json* welcome) {
  protocol::Json message = {{"v", 1}, {"t", "hello"}, {"protocol", "1.0"}, {"bundle", "test"},
                            {"caps", protocol::Json::array()}};
  message["ticket"] = std::string(ticket);
  if (!send_json(message)) {
    return false;
  }
  const std::optional<protocol::Json> answer = next_json();
  if (!answer || (*answer)["t"] != "welcome") {
    return false;
  }
  if (welcome != nullptr) {
    *welcome = *answer;
  }
  return true;
}

bool WsClient::join(std::uint16_t port, std::string_view cookie, std::string_view ticket, protocol::Json* welcome) {
  if (open(port, cookie) != 101) {
    return false;
  }
  protocol::Json hello = {{"v", 1}, {"t", "hello"}, {"protocol", "1.0"}, {"bundle", "test"},
                          {"caps", protocol::Json::array()}};
  hello["ticket"] = std::string(ticket);
  if (!send_json(hello)) {
    return false;
  }
  const std::optional<protocol::Json> answer = next_json();
  if (!answer || (*answer)["t"] != "welcome") {
    return false;
  }
  if (welcome != nullptr) {
    *welcome = *answer;
  }
  return true;
}

bool WsClient::send_frame(std::uint8_t opcode, std::string_view payload, bool fin) {
  std::string frame;
  frame.push_back(static_cast<char>((fin ? 0x80 : 0) | opcode));
  const std::size_t size = payload.size();
  if (size < 126) {
    frame.push_back(static_cast<char>(0x80 | size));
  } else if (size <= 0xFFFF) {
    frame.push_back(static_cast<char>(0x80 | 126));
    frame.push_back(static_cast<char>(size >> 8));
    frame.push_back(static_cast<char>(size & 0xFF));
  } else {
    frame.push_back(static_cast<char>(0x80 | 127));
    for (int shift = 56; shift >= 0; shift -= 8) {
      frame.push_back(static_cast<char>((static_cast<std::uint64_t>(size) >> shift) & 0xFF));
    }
  }
  for (const std::uint8_t byte : kMask) {
    frame.push_back(static_cast<char>(byte));
  }
  for (std::size_t i = 0; i < size; ++i) {
    frame.push_back(static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^ kMask[i % 4]));
  }
  return client_.send(frame);
}

std::optional<WsFrame> WsClient::next(Millis limit) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (true) {
    if (buffer_.size() >= 2) {
      const auto first = static_cast<std::uint8_t>(buffer_[0]);
      const auto second = static_cast<std::uint8_t>(buffer_[1]);
      std::size_t header = 2;
      std::uint64_t length = second & 0x7F;
      if (length == 126) {
        header = 4;
      } else if (length == 127) {
        header = 10;
      }
      if (buffer_.size() >= header) {
        if (header == 4) {
          length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(buffer_[2])) << 8) |
                   static_cast<std::uint8_t>(buffer_[3]);
        } else if (header == 10) {
          length = 0;
          for (std::size_t i = 2; i < 10; ++i) {
            length = (length << 8) | static_cast<std::uint8_t>(buffer_[i]);
          }
        }
        if (buffer_.size() >= header + length) {
          WsFrame frame;
          frame.opcode = first & 0x0F;
          frame.payload = buffer_.substr(header, static_cast<std::size_t>(length));
          buffer_.erase(0, header + static_cast<std::size_t>(length));
          return frame;
        }
      }
    }
    const auto left = std::chrono::duration_cast<Millis>(end - std::chrono::steady_clock::now());
    if (left.count() <= 0 || !client_.connected()) {
      return std::nullopt;
    }
    if (!client_.receive(buffer_, left, 0)) {
      if (buffer_.empty()) {
        return std::nullopt;
      }
    }
  }
}

std::optional<protocol::Json> WsClient::next_json(Millis limit, bool keep_pings) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (true) {
    const auto left = std::chrono::duration_cast<Millis>(end - std::chrono::steady_clock::now());
    if (left.count() <= 0) {
      return std::nullopt;
    }
    const std::optional<WsFrame> frame = next(left);
    if (!frame || frame->opcode != 1) {
      return std::nullopt;
    }
    protocol::Json json = protocol::Json::parse(frame->payload, nullptr, false);
    if (!keep_pings && json.is_object() && json.value("t", "") == "ping") {
      continue;
    }
    return json;
  }
}

std::optional<protocol::Json> WsClient::next_of(std::string_view kind, Millis limit) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (true) {
    const auto left = std::chrono::duration_cast<Millis>(end - std::chrono::steady_clock::now());
    if (left.count() <= 0) {
      return std::nullopt;
    }
    std::optional<protocol::Json> json = next_json(left);
    if (!json) {
      return std::nullopt;
    }
    if (json->is_object() && json->value("t", "") == kind) {
      return json;
    }
  }
}

bool WsClient::quiet(Millis limit) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (true) {
    const auto left = std::chrono::duration_cast<Millis>(end - std::chrono::steady_clock::now());
    if (left.count() <= 0) {
      return true;
    }
    const std::optional<WsFrame> frame = next(left);
    if (!frame) {
      return client_.connected();
    }
    if (frame->opcode == 1 && protocol::Json::parse(frame->payload, nullptr, false).value("t", "") == "ping") {
      continue;
    }
    return false;
  }
}

bool WsClient::closed(Millis limit, std::uint16_t* code) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (true) {
    const auto left = std::chrono::duration_cast<Millis>(end - std::chrono::steady_clock::now());
    if (left.count() <= 0) {
      return false;
    }
    const std::optional<WsFrame> frame = next(left);
    if (!frame) {
      return false;
    }
    if (frame->opcode == 8) {
      if (code != nullptr && frame->payload.size() >= 2) {
        *code = static_cast<std::uint16_t>((static_cast<std::uint8_t>(frame->payload[0]) << 8) |
                                           static_cast<std::uint8_t>(frame->payload[1]));
      }
      // Answer it, as a browser does, then see the server end the connection.
      send_frame(8, frame->payload.substr(0, 2));
      std::string rest;
      return client_.read_to_close(rest, std::chrono::duration_cast<Millis>(end - std::chrono::steady_clock::now()) +
                                             500ms);
    }
  }
}

bool WsClient::gone(Millis limit) { return client_.read_to_close(buffer_, limit); }

}  // namespace grace::serve::iotest
