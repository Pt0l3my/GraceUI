// engine.h: the running server (server.h): its sockets, its two threads and every connection's state. The I/O thread
// alone touches a Connection; other threads reach the engine through the Hub (internal.h) and its wake.
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "grace/protocol/frame.h"
#include "grace/serve/head.h"
#include "grace/serve/limits.h"
#include "grace/serve/refusal.h"
#include "grace/serve/server.h"
#include "grace/serve/websocket.h"
#include "internal.h"
#include "platform.h"

namespace grace::serve::detail {

// One view's watch of one topic (§6.3 sub), at most protocol::kSubscriptions a connection (§6.8).
struct Subscription {
  std::shared_ptr<PortState> port;
  std::size_t topic = 0;
  bool stream = false;
  // A state: the revision last sent, and when (at most one each state_interval, the newest, §2.4).
  bool sent = false;
  std::uint64_t sent_rev = 0;
  Clock::time_point sent_at{};
  // A stream: the next row's sequence number this view wants.
  std::uint64_t next_seq = 0;
};

struct Chunk {
  std::shared_ptr<const std::string> bytes;
  std::size_t offset = 0;
};

// One connection, in one fixed slot of the pool, reused: its buffers are the caps' sizes (D158, §7.6).
struct Connection {
  enum class Phase : std::uint8_t {
    idle,      // the slot is free
    head,      // reading the request's head, within its deadline from accept
    body,      // reading the ticket's body, within the same deadline
    files,     // a GET waiting for the worker's web files, within the same deadline
    respond,   // sending the response, then Connection: close
    hello,     // a WebSocket after its 101, waiting for its hello (2 s)
    open,      // a WebSocket after welcome
    closing,   // a WebSocket whose Close the server sent: waiting for the page's, within close_wait
    draining,  // the send side shut down: reading until the peer's end, within close_wait (item 24)
  };

  explicit Connection(const HeadRules& rules) noexcept : parser(rules) {}

  Phase phase = Phase::idle;
  io::Socket socket;
  std::uint64_t serial = 0;  // never reused: what a command's result is routed by
  bool exact_peer = false;   // the peer is exactly 127.0.0.1 or ::1 (item 3)
  bool http = false;         // counted against kMaxHttpConnections
  bool websocket = false;    // counted against kMaxWebSockets (hello or open)
  bool network = false;      // accepted by the network listener (G9): another device at home
  bool paired = false;       // and a paired device's, by its cookie (for a WebSocket, its upgrade's)
  std::array<char, kDeviceIdChars> device{};  // that device's id
  enum class BodyFor : std::uint8_t { ticket, pair };
  BodyFor body_for = BodyFor::ticket;
  Clock::time_point accepted{};
  Clock::time_point upgraded{};
  Clock::time_point deadline{};

  HeadParser parser;
  std::array<char, kMaxTicketBody> body{};  // the ticket's request body, then its response's
  std::size_t body_size = 0;
  std::size_t body_need = 0;
  std::array<char, kMaxResponseHead + kMaxTarget> head{};
  std::size_t head_size = 0;
  std::size_t head_sent = 0;
  std::string_view response;  // the response's body: static text, the ticket's in `body`, or a file's
  std::shared_ptr<const std::string> response_owner;
  std::size_t response_sent = 0;

  std::optional<Session> session;
  bool final_send = false;  // the session is closed: shut down once the queue is sent
  std::deque<Chunk> queue;
  std::size_t queued = 0;
  std::vector<std::shared_ptr<PortState>> spaces;  // the spaces welcome named (invariant 3)
  std::vector<Subscription> subs;
  double tokens = 0;  // messages it may still send (§6.8: 20 a second), counted before parsing (item 22)
  Clock::time_point refilled{};
  Clock::time_point rate_told{};
  bool told_rate = false;
  std::size_t bad = 0;
  bool logged_bad = false;
  std::size_t pings_out = 0;
  Clock::time_point next_ping{};

  void clear() noexcept;
};

class Engine {
 public:
  Engine(std::shared_ptr<Hub> hub, const ServerConfig& config);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  StartResult start();
  void stop() noexcept;
  bool running() const noexcept { return running_.load(); }
  ServerStats stats() const noexcept;

 private:
  // Threads (server.cpp).
  void io_main() noexcept;
  void worker_main() noexcept;
  void loop();
  void close_all() noexcept;
  void log(LogLevel level, std::string_view line) const noexcept;
  void fault(Connection& c) noexcept;

  // Connections and HTTP (server.cpp).
  void accept_from(const io::Socket& listener, bool network, Clock::time_point now);
  void readable(Connection& c, Clock::time_point now);
  void writable(Connection& c, Clock::time_point now);
  void timers(Connection& c, Clock::time_point now);
  Clock::time_point next_timer(const Connection& c, Clock::time_point now) const noexcept;
  void route(Connection& c, std::string_view after, Clock::time_point now);
  void serve_get(Connection& c, Clock::time_point now);
  void ticket(Connection& c, std::string_view after, Clock::time_point now);
  void issue_ticket(Connection& c, Clock::time_point now);
  // Other devices (G9): a network connection's routes, the code's request and its body, the pairing page's three files,
  // the credential check for a device's ticket and upgrade, and the host's changes applied.
  void route_network(Connection& c, std::string_view after, Clock::time_point now);
  void pair(Connection& c, std::string_view after, Clock::time_point now);
  void finish_pair(Connection& c, Clock::time_point now);
  void serve_pairing(Connection& c, Clock::time_point now) noexcept;
  Refusal check_device(const Connection& c) const noexcept;
  void take_body(Connection& c, std::string_view after, Clock::time_point now);
  void body_done(Connection& c, Clock::time_point now);
  void apply_network(bool on, Clock::time_point now);
  // The PC's addresses the network listener may take: the test's own, else its private ones; deduplicated, at most
  // kMaxNetworkAddresses, never 0.0.0.0, multicast or reserved.
  std::vector<std::array<std::uint8_t, 4>> network_candidates() const;
  // While the network listener is wanted, the PC's addresses again every 10 s; changed, it listens on the new ones.
  void follow_addresses(Clock::time_point now);
  void apply_devices(std::vector<DeviceKey> devices, Clock::time_point now);
  void upgrade(Connection& c, std::size_t after, Clock::time_point now);
  void respond(Connection& c, std::size_t head_size, std::string_view body,
               std::shared_ptr<const std::string> owner, Clock::time_point now) noexcept;
  void respond_status(Connection& c, std::uint16_t status, std::string_view body, bool page_load,
                      Clock::time_point now) noexcept;
  void refuse(Connection& c, Refusal refusal, Clock::time_point now) noexcept;
  void begin_drain(Connection& c, Clock::time_point now) noexcept;
  void drop(Connection& c) noexcept;
  bool cookie_for(const Connection& c) const noexcept;
  bool new_token(std::array<char, kTokenChars>& out) noexcept;
  void renew(Clock::time_point now);
  void take_files(Clock::time_point now);
  // WebSockets in `phase` (hello: waiting for their ticket; open: welcomed views), and the oldest of them but `except`.
  std::size_t count_websockets(Connection::Phase phase) const noexcept;
  Connection* oldest_websocket(Connection::Phase phase, const Connection* except) noexcept;

  // The bridge (bridge.cpp).
  void take_hub(Clock::time_point now);
  void dispatch(Outgoing& item);
  void ws_bytes(Connection& c, std::span<const std::uint8_t> in, Clock::time_point now);
  void message(Connection& c, bool text, std::span<const std::uint8_t> payload, Clock::time_point now);
  void hello(Connection& c, const protocol::Hello& hello, Clock::time_point now);
  void subscribe(Connection& c, const protocol::Sub& sub);
  void unsubscribe(Connection& c, const protocol::Unsub& unsub);
  void command(Connection& c, protocol::Cmd& cmd);
  void bad_message(Connection& c, const protocol::Refusal& refusal, Clock::time_point now);
  void send_states(Connection& c, Clock::time_point now);
  void send_replay(Connection& c, Subscription& sub, std::optional<std::uint64_t> since);
  void forward_rows(Connection& c, Subscription& sub, const Outgoing& item);
  void send_error(Connection& c, const PortState* space, protocol::ErrorCode code, std::string detail);
  void send_result_error(Connection& c, const PortState& space, std::uint32_t id, protocol::ErrorCode code,
                         std::string detail = {});
  bool send_message(Connection& c, const protocol::Message& message);
  bool enqueue(Connection& c, std::shared_ptr<const std::string> bytes) noexcept;
  void close_socket(Connection& c, std::optional<protocol::ByeReason> bye, std::uint16_t code,
                    Clock::time_point now);
  void release(Connection& c) noexcept;
  void unwatch(PortState& port, std::size_t topic) noexcept;
  // A family member's slot (D215): the one it holds, or a free one taken for it; npos if every slot is in use.
  std::size_t take_member(PortState& port, std::string_view id) noexcept;
  void set_watched(PortState& port, std::size_t topic, bool on) noexcept;
  std::shared_ptr<PortState> space_of(const Connection& c, std::string_view id) const noexcept;
  bool take_token(Connection& c, Clock::time_point now) noexcept;

  std::shared_ptr<Hub> hub_;
  ServerConfig config_;
  Listener listener_;

  io::Wake wake_;
  std::array<io::Socket, 2> listeners_;
  // The network listener's (G9): its sockets and their addresses as a Host names them, and each kind's head rules.
  std::array<io::Socket, kMaxNetworkAddresses> network_;
  std::array<std::string, kMaxNetworkAddresses> network_names_;
  std::array<std::string_view, kMaxNetworkAddresses> network_views_;
  bool network_wanted_ = false;
  Clock::time_point next_address_check_{};
  // The addresses the PC had at the last apply, sorted: a bind that failed is not tried again until they change.
  std::vector<std::string> network_seen_;
  std::vector<std::array<char, kDeviceIdChars>> connected_seen_;  // the devices with an open WebSocket, last published
  Listener network_listener_;
  HeadRules loopback_rules_;
  HeadRules network_rules_;
  std::vector<DeviceKey> devices_;  // the I/O thread's copy
  bool network_started_ = false;
  std::thread io_thread_;
  std::thread worker_thread_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> running_{false};

  std::mutex worker_mutex_;
  std::condition_variable worker_wake_;
  bool worker_stop_ = false;                         // under worker_mutex_
  std::shared_ptr<const FileTable> files_pending_;   // under worker_mutex_

  // The I/O thread's alone.
  std::vector<std::unique_ptr<Connection>> pool_;
  std::shared_ptr<const FileTable> files_;
  std::array<char, kTokenChars> cookie_{};
  struct Ticket {
    std::array<char, kTokenChars> token{};
    Clock::time_point issued{};
    bool live = false;
    bool network = false;                       // fetched on the network listener, by a paired device (G9)
    std::array<char, kDeviceIdChars> device{};  // which: its WebSocket must be that device's
  };
  std::array<Ticket, kMaxTickets> tickets_{};
  std::uint64_t next_serial_ = 1;
  std::size_t faults_ = 0;
  bool faulted_ = false;
  std::shared_ptr<const std::string> ping_frame_;
  std::array<char, 16384> scratch_{};
  std::array<std::uint8_t, 16384> bytes_{};
  std::string text_;

  // What stats() reads from any thread.
  std::atomic<bool> ipv6_{false};
  std::atomic<std::size_t> threads_{0};
  std::atomic<std::size_t> stat_http_{0};
  std::atomic<std::size_t> stat_websockets_{0};
  std::atomic<std::size_t> stat_sockets_{0};
  std::atomic<bool> files_ready_{false};
  std::atomic<bool> files_ok_{false};
  std::atomic<std::size_t> stat_faults_{0};
  std::atomic<std::size_t> stat_network_{0};
  std::atomic<std::size_t> stat_devices_{0};
  std::atomic<std::size_t> stat_device_connections_{0};
};

// The pool: every plain HTTP connection, the welcomed views and as many waiting for their hello, and room for
// WebSockets still closing after a supersede.
inline constexpr std::size_t kPoolSize = kMaxHttpConnections + 2 * kMaxWebSockets + 6;

}  // namespace grace::serve::detail
