// server.cpp: Server (server.h) and its engine's lifecycle, its I/O loop, and the HTTP side of every connection: the
// web files, the ticket, the upgrade and the redirects (§7.2 items 2 to 10, 15, 19 to 24; §6.8; D111, D112). Every
// rule on the wire is grace_serve_core's; this file only reads, routes and writes.
#include <algorithm>
#include <cstring>
#include <exception>
#include <string>
#include <utility>

#include "engine.h"
#include "grace/serve/checks.h"
#include "grace/serve/response.h"

namespace grace::serve {

std::string_view to_string(StartResult result) noexcept {
  switch (result) {
    case StartResult::ok: return "ok";
    case StartResult::already_running: return "already_running";
    case StartResult::bad_port: return "bad_port";
    case StartResult::port_taken: return "port_taken";
    case StartResult::port_reserved: return "port_reserved";
    case StartResult::no_network: return "no_network";
    case StartResult::no_random: return "no_random";
    case StartResult::no_thread: return "no_thread";
  }
  return "unknown";
}

namespace detail {
namespace {

constexpr std::string_view kPlainText = "text/plain; charset=utf-8";
constexpr std::string_view kNotFound = "Not found.\n";
constexpr std::string_view kUnavailable = "The browser view could not answer this; reload the page.\n";

std::string port_text(std::uint16_t port) { return std::to_string(port); }

}  // namespace

void Connection::clear() noexcept {
  phase = Phase::idle;
  socket.close();
  serial = 0;
  exact_peer = false;
  http = false;
  websocket = false;
  parser.reset();
  body_size = 0;
  body_need = 0;
  head_size = 0;
  head_sent = 0;
  response = {};
  response_owner.reset();
  response_sent = 0;
  session.reset();
  final_send = false;
  queue.clear();
  queued = 0;
  spaces.clear();
  subs.clear();
  tokens = 0;
  told_rate = false;
  bad = 0;
  logged_bad = false;
  pings_out = 0;
}

Engine::Engine(std::shared_ptr<Hub> hub, const ServerConfig& config) : hub_(std::move(hub)), config_(config) {
  listener_.network = false;
  listener_.port = config_.port;
}

Engine::~Engine() { stop(); }

void Engine::log(LogLevel level, std::string_view line) const noexcept {
  if (config_.log) {
    try {
      config_.log(level, line);
    } catch (...) {
      // A host's sink that throws loses its line, never the server.
    }
  }
}

bool Engine::new_token(std::array<char, kTokenChars>& out) noexcept {
  std::array<std::uint8_t, kTokenBytes> bytes{};
  if (!io::random_bytes(bytes)) {
    return false;
  }
  encode_token(bytes, out);
  bytes.fill(0);
  return true;
}

StartResult Engine::start() {
  const auto fail = [this](StartResult result) {
    listeners_[0].close();
    listeners_[1].close();
    wake_.close();
    if (network_started_) {
      io::stop_network();
      network_started_ = false;
    }
    cookie_.fill(0);
    return result;
  };
  if (config_.port < kLowestPort) {
    return StartResult::bad_port;
  }
  if (!io::start_network()) {
    return StartResult::no_network;
  }
  network_started_ = true;
  if (!new_token(cookie_)) {
    log(LogLevel::error, "The browser view is off: the system gave no random bytes for its cookie.");
    return fail(StartResult::no_random);
  }
  if (!wake_.create()) {
    log(LogLevel::error, "The browser view is off: its server could not make its own wake.");
    return fail(StartResult::no_network);
  }
  io::BindError error = io::BindError::none;
  listeners_[0] = io::listen_loopback(false, config_.port, error);
  if (!listeners_[0].open()) {
    if (error == io::BindError::in_use || error == io::BindError::denied) {
      // §7.2 items 2 and 24, §2.8: one line naming the port, both causes and where to change it.
      log(LogLevel::error,
          "The browser view is off: port " + port_text(config_.port) +
              " on 127.0.0.1 is taken, by another program or by a range of ports Windows reserves (such as for "
              "Hyper-V or WSL). Choose another in GraceUI > Browser view, or set dashboard.port.");
      return fail(error == io::BindError::denied ? StartResult::port_reserved : StartResult::port_taken);
    }
    log(LogLevel::error, "The browser view is off: its server could not open a socket on 127.0.0.1.");
    return fail(StartResult::no_network);
  }
  listeners_[1] = io::listen_loopback(true, config_.port, error);
  ipv6_ = listeners_[1].open();
  if (!ipv6_ && (error == io::BindError::in_use || error == io::BindError::denied)) {
    log(LogLevel::warning, "[::1] port " + port_text(config_.port) +
                               " is held by another program: the browser view is served on 127.0.0.1 alone.");
  }

  HeadRules rules;
  rules.listener = listener_;
  rules.max_content_length = kMaxTicketBody;  // the one body a route takes (item 9)
  pool_.clear();
  for (std::size_t i = 0; i < kPoolSize; ++i) {
    pool_.push_back(std::make_unique<Connection>(rules));
  }
  ping_frame_ = encode_to_page(protocol::Message{protocol::Ping{}});
  files_.reset();
  files_pending_.reset();
  files_ready_ = false;
  files_ok_ = false;
  tickets_ = {};
  faults_ = 0;
  faulted_ = false;
  stat_faults_ = 0;
  {
    std::lock_guard lock(hub_->mutex);
    hub_->running = true;
    hub_->wake = &wake_;
    hub_->renew = false;
    hub_->spaces_removed = false;
    hub_->inbox.clear();
    for (const auto& port : hub_->spaces) {
      port->topics.assign(port->description.topics.size(), TopicCache{});
    }
  }
  stopping_ = false;
  worker_stop_ = false;
  running_ = true;
  try {
    threads_ = 1;
    worker_thread_ = std::thread(&Engine::worker_main, this);
  } catch (...) {
    threads_ = 0;
    running_ = false;
    {
      std::lock_guard lock(hub_->mutex);
      hub_->running = false;
      hub_->wake = nullptr;
    }
    return fail(StartResult::no_thread);
  }
  try {
    threads_ = 2;
    io_thread_ = std::thread(&Engine::io_main, this);
  } catch (...) {
    threads_ = 1;
    stop();
    return StartResult::no_thread;
  }
  return StartResult::ok;
}

void Engine::stop() noexcept {
  stopping_ = true;
  if (wake_.handle() != io::kNoHandle) {
    wake_.signal();
  }
  try {
    if (io_thread_.joinable()) {
      io_thread_.join();
    }
    {
      std::lock_guard lock(worker_mutex_);
      worker_stop_ = true;
    }
    worker_wake_.notify_all();
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
  } catch (...) {
    // join fails only for a thread that is not joinable, which is checked.
  }
  {
    std::lock_guard lock(hub_->mutex);
    if (hub_->wake == &wake_) {
      hub_->wake = nullptr;
      hub_->running = false;
      hub_->inbox.clear();
    }
  }
  listeners_[0].close();
  listeners_[1].close();
  wake_.close();
  pool_.clear();
  if (network_started_) {
    io::stop_network();
    network_started_ = false;
  }
  cookie_.fill(0);
  tickets_ = {};
  running_ = false;
  ipv6_ = false;
  stat_http_ = 0;
  stat_websockets_ = 0;
  stat_sockets_ = 0;
}

ServerStats Engine::stats() const noexcept {
  ServerStats stats;
  stats.running = running_.load();
  stats.ipv6 = ipv6_.load();
  stats.threads = threads_.load();
  stats.http = stat_http_.load();
  stats.websockets = stat_websockets_.load();
  stats.sockets = stat_sockets_.load();
  stats.files_ready = files_ready_.load();
  stats.files_ok = files_ok_.load();
  stats.faults = stat_faults_.load();
  return stats;
}

void Engine::worker_main() noexcept {
  io::name_thread("grace-serve-wk");
  try {
    std::shared_ptr<const FileTable> table = load_files(config_);
    if (!table->ok) {
      log(LogLevel::warning, "The browser view's web files failed their check (" + table->failure +
                                 "): page loads get the notice page.");
    }
    {
      std::lock_guard lock(worker_mutex_);
      files_pending_ = std::move(table);
    }
    wake_.signal();
    // Later jobs (the upload's SHA-256, §7.2 item 18) come here; until then the worker waits to be stopped.
    std::unique_lock lock(worker_mutex_);
    worker_wake_.wait(lock, [this] { return worker_stop_; });
  } catch (...) {
    log(LogLevel::error, "The browser view's worker stopped after a fault.");
  }
  --threads_;
}

void Engine::io_main() noexcept {
  io::name_thread("grace-serve-io");
  try {
    loop();
  } catch (...) {
    log(LogLevel::error, "The browser view is off for this session: its server stopped after a fault.");
  }
  close_all();
  running_ = false;
  --threads_;
}

void Engine::fault(Connection& c) noexcept {
  ++faults_;
  stat_faults_ = faults_;
  log(LogLevel::error, "A fault while the browser view's server handled a connection closed that connection.");
  try {
    drop(c);
  } catch (...) {
  }
  if (faults_ >= 2) {
    log(LogLevel::error, "The browser view is off for this session: its server met a second fault.");
    faulted_ = true;
  }
}

void Engine::loop() {
  std::array<io::PollItem, io::kMaxPollItems> items{};
  std::array<Connection*, io::kMaxPollItems> owners{};
  std::array<std::uint64_t, io::kMaxPollItems> serials{};
  while (!stopping_.load() && !faulted_) {
    Clock::time_point now = Clock::now();
    take_hub(now);
    take_files(now);
    Clock::time_point next = now + std::chrono::seconds(1);
    std::size_t http = 0;
    bool free_slot = false;
    for (const auto& slot : pool_) {
      Connection& c = *slot;
      if (c.phase != Connection::Phase::idle) {
        try {
          timers(c, now);
        } catch (...) {
          fault(c);
        }
      }
      if (c.phase == Connection::Phase::idle) {
        free_slot = true;
        continue;
      }
      next = std::min(next, next_timer(c, now));
      http += c.http ? 1 : 0;
    }
    if (faulted_ || stopping_.load()) {
      break;
    }
    std::size_t count = 0;
    items[count] = io::PollItem{wake_.handle(), true, false};
    owners[count++] = nullptr;
    const std::size_t first_listener = count;
    const bool accepting = http < kMaxHttpConnections && free_slot;  // a 17th waits in the backlog (D111)
    for (const io::Socket& listener : listeners_) {
      if (listener.open() && accepting) {
        items[count] = io::PollItem{listener.handle(), true, false};
        owners[count++] = nullptr;
      }
    }
    const std::size_t first_connection = count;
    for (const auto& slot : pool_) {
      Connection& c = *slot;
      if (c.phase == Connection::Phase::idle) {
        continue;
      }
      const bool reads = c.phase != Connection::Phase::files && c.phase != Connection::Phase::respond;
      const bool writes = c.phase == Connection::Phase::respond || !c.queue.empty();
      items[count] = io::PollItem{c.socket.handle(), reads, writes};
      owners[count] = &c;
      serials[count++] = c.serial;
    }
    const auto wait = std::chrono::ceil<std::chrono::milliseconds>(next - now).count();
    const int timeout = static_cast<int>(std::clamp<long long>(wait, 0, 1000));
    if (!io::poll_all(std::span(items.data(), count), timeout)) {
      log(LogLevel::error, "The browser view is off for this session: waiting on its sockets failed.");
      faulted_ = true;
      break;
    }
    now = Clock::now();
    if (items[0].readable) {
      wake_.drain();
    }
    for (std::size_t i = first_listener; i < first_connection; ++i) {
      if (items[i].readable || items[i].failed) {
        const io::Socket& listener = items[i].handle == listeners_[0].handle() ? listeners_[0] : listeners_[1];
        accept_from(listener, now);
      }
    }
    for (std::size_t i = first_connection; i < count; ++i) {
      Connection& c = *owners[i];
      if (c.phase == Connection::Phase::idle || c.serial != serials[i]) {
        continue;  // dropped, or dropped and taken again, while this round was handled
      }
      try {
        if (items[i].readable || items[i].failed) {
          readable(c, now);
        }
        if (c.phase != Connection::Phase::idle && c.serial == serials[i] && items[i].writable) {
          writable(c, now);
        }
      } catch (...) {
        fault(c);
      }
    }
    std::size_t open_http = 0;
    std::size_t open_websockets = 0;
    std::size_t sockets = 1 + (listeners_[0].open() ? 1 : 0) + (listeners_[1].open() ? 1 : 0);
    for (const auto& slot : pool_) {
      if (slot->phase != Connection::Phase::idle) {
        ++sockets;
        open_http += slot->http ? 1 : 0;
        open_websockets += slot->websocket ? 1 : 0;
      }
    }
    stat_http_ = open_http;
    stat_websockets_ = open_websockets;
    stat_sockets_ = sockets;
  }
}

void Engine::close_all() noexcept {
  try {
    {
      std::lock_guard lock(hub_->mutex);
      hub_->running = false;
    }
    std::shared_ptr<const std::string> bye = encode_to_page(protocol::Message{protocol::Bye{protocol::ByeReason::shutdown}});
    for (const auto& slot : pool_) {
      Connection& c = *slot;
      if (c.phase == Connection::Phase::idle) {
        continue;
      }
      if ((c.phase == Connection::Phase::hello || c.phase == Connection::Phase::open) && c.session) {
        // One try to finish what is queued, then bye shutdown and a Close (1001): best effort, never a wait.
        bool flushed = true;
        while (!c.queue.empty() && flushed) {
          Chunk& front = c.queue.front();
          std::size_t sent = 0;
          const std::string& bytes = *front.bytes;
          flushed = io::transmit(c.socket, std::span(bytes.data() + front.offset, bytes.size() - front.offset),
                                 sent) == io::Io::done &&
                    sent == bytes.size() - front.offset;
          if (flushed) {
            c.queue.pop_front();
          }
        }
        if (flushed && bye != nullptr) {
          std::size_t sent = 0;
          io::transmit(c.socket, std::span(bye->data(), bye->size()), sent);
          std::array<std::uint8_t, kMaxControlFrame> close{};
          const std::size_t size = c.session->start_close(1001, close);
          std::array<char, kMaxControlFrame> text{};
          for (std::size_t i = 0; i < size; ++i) {
            text[i] = static_cast<char>(close[i]);
          }
          io::transmit(c.socket, std::span(text.data(), size), sent);
        }
      }
      if (c.socket.open()) {
        io::shutdown_send(c.socket);
      }
      c.subs.clear();
      c.clear();
    }
    std::vector<std::shared_ptr<PortState>> ports;
    {
      std::lock_guard lock(hub_->mutex);
      ports = hub_->spaces;
    }
    // Nothing is watched once the server is off: each space hears so and builds nothing more (§2.4).
    for (const auto& port : ports) {
      for (std::size_t t = 0; t < port->topics.size(); ++t) {
        if (port->topics[t].watchers != 0) {
          port->topics[t] = TopicCache{};
          set_watched(*port, t, false);
        }
      }
      std::lock_guard lock(port->mutex);
      port->commands.clear();
    }
  } catch (...) {
  }
  stat_http_ = 0;
  stat_websockets_ = 0;
  stat_sockets_ = 0;
}

void Engine::take_files(Clock::time_point now) {
  if (files_ != nullptr) {
    return;
  }
  {
    std::lock_guard lock(worker_mutex_);
    files_ = files_pending_;
  }
  if (files_ == nullptr) {
    return;
  }
  files_ready_ = true;
  files_ok_ = files_->ok;
  for (const auto& slot : pool_) {
    if (slot->phase == Connection::Phase::files) {
      try {
        serve_get(*slot, now);
      } catch (...) {
        fault(*slot);
      }
    }
  }
}

void Engine::accept_from(const io::Socket& listener, Clock::time_point now) {
  while (true) {
    std::size_t http = 0;
    Connection* free = nullptr;
    for (const auto& slot : pool_) {
      if (slot->phase == Connection::Phase::idle) {
        free = free != nullptr ? free : slot.get();
      } else if (slot->http) {
        ++http;
      }
    }
    if (free == nullptr || http >= kMaxHttpConnections) {
      return;
    }
    io::Accepted accepted;
    if (io::accept_one(listener, accepted) != io::Io::done) {
      return;
    }
    if (!accepted.loopback) {
      continue;  // a loopback listener takes loopback peers alone; the socket closes here
    }
    Connection& c = *free;
    c.clear();
    c.socket = std::move(accepted.socket);
    c.exact_peer = accepted.exact;
    c.serial = next_serial_++;
    c.http = true;
    c.accepted = now;
    c.deadline = now + config_.timings.head;  // absolute, whatever trickles in (D111)
    c.phase = Connection::Phase::head;
  }
}

std::size_t Engine::count_websockets(Connection::Phase phase) const noexcept {
  std::size_t count = 0;
  for (const auto& slot : pool_) {
    count += slot->websocket && slot->phase == phase ? 1 : 0;
  }
  return count;
}

void Engine::timers(Connection& c, Clock::time_point now) {
  using Phase = Connection::Phase;
  switch (c.phase) {
    case Phase::head:
    case Phase::body:
    case Phase::files:
    case Phase::respond:
    case Phase::closing:
    case Phase::draining:
      if (now >= c.deadline) {
        drop(c);  // no answer to a head that never ended (D111), or a drain past its bound (item 24)
      }
      return;
    case Phase::hello:
      if (now >= c.deadline) {
        log(LogLevel::info, "A WebSocket sent no hello within its time, and was closed.");
        close_socket(c, protocol::ByeReason::unauthorised, 1008, now);
      }
      return;
    case Phase::open:
      if (now >= c.next_ping) {
        if (c.pings_out >= 2) {
          close_socket(c, std::nullopt, 1001, now);  // two pings unanswered (§6.3)
          return;
        }
        ++c.pings_out;
        c.next_ping = now + config_.timings.ping;
        if (!enqueue(c, ping_frame_)) {
          return;
        }
      }
      send_states(c, now);
      return;
    case Phase::idle:
      return;
  }
}

Clock::time_point Engine::next_timer(const Connection& c, Clock::time_point now) const noexcept {
  if (c.phase != Connection::Phase::open) {
    return c.deadline;
  }
  Clock::time_point next = c.next_ping;
  for (const Subscription& sub : c.subs) {
    if (sub.stream) {
      continue;
    }
    const TopicCache& cache = sub.port->topics[sub.topic];
    if (cache.has_state && (!sub.sent || sub.sent_rev != cache.rev)) {
      next = std::min(next, std::max(now, sub.sent_at + config_.timings.state_interval));
    }
  }
  return next;
}

void Engine::readable(Connection& c, Clock::time_point now) {
  using Phase = Connection::Phase;
  std::size_t count = 0;
  const io::Io got = io::receive(c.socket, scratch_, count);
  if (got == io::Io::would_block) {
    return;
  }
  if (got != io::Io::done) {
    drop(c);  // the peer's end, or an error: in draining, the drain's end (item 24)
    return;
  }
  const std::string_view data(scratch_.data(), count);
  switch (c.phase) {
    case Phase::head: {
      const HeadParser::Fed fed = c.parser.feed(data);
      if (fed.state == HeadParser::State::refused) {
        refuse(c, c.parser.refusal(), now);
      } else if (fed.state == HeadParser::State::complete) {
        route(c, data.substr(fed.consumed), now);
      }
      return;
    }
    case Phase::body: {
      const std::size_t take = std::min(count, c.body_need - c.body_size);
      std::copy_n(data.data(), take, c.body.data() + c.body_size);
      c.body_size += take;
      if (c.body_size == c.body_need) {
        issue_ticket(c, now);
      }
      return;
    }
    case Phase::hello:
    case Phase::open:
    case Phase::closing:
      for (std::size_t i = 0; i < count; ++i) {
        bytes_[i] = static_cast<std::uint8_t>(data[i]);
      }
      ws_bytes(c, std::span<const std::uint8_t>(bytes_.data(), count), now);
      return;
    case Phase::draining:  // read and dropped, until the peer's end
    case Phase::files:
    case Phase::respond:
    case Phase::idle:
      return;
  }
}

void Engine::writable(Connection& c, Clock::time_point now) {
  if (c.phase == Connection::Phase::respond) {
    while (c.head_sent < c.head_size || c.response_sent < c.response.size()) {
      const bool in_head = c.head_sent < c.head_size;
      const std::span<const char> bytes =
          in_head ? std::span<const char>(c.head.data() + c.head_sent, c.head_size - c.head_sent)
                  : std::span<const char>(c.response.data() + c.response_sent, c.response.size() - c.response_sent);
      std::size_t sent = 0;
      const io::Io result = io::transmit(c.socket, bytes, sent);
      if (result == io::Io::would_block) {
        return;
      }
      if (result != io::Io::done) {
        drop(c);
        return;
      }
      (in_head ? c.head_sent : c.response_sent) += sent;
    }
    begin_drain(c, now);  // Connection: close (§6.8)
    return;
  }
  while (!c.queue.empty()) {
    Chunk& front = c.queue.front();
    const std::string& bytes = *front.bytes;
    std::size_t sent = 0;
    const io::Io result =
        io::transmit(c.socket, std::span(bytes.data() + front.offset, bytes.size() - front.offset), sent);
    if (result == io::Io::would_block) {
      return;
    }
    if (result != io::Io::done) {
      drop(c);
      return;
    }
    front.offset += sent;
    if (front.offset == bytes.size()) {
      c.queued -= bytes.size();
      c.queue.pop_front();
    }
  }
  if (c.final_send) {
    begin_drain(c, now);
  }
}

void Engine::begin_drain(Connection& c, Clock::time_point now) noexcept {
  release(c);
  c.websocket = false;
  io::shutdown_send(c.socket);
  c.phase = Connection::Phase::draining;
  c.deadline = now + config_.timings.close_wait;
}

void Engine::drop(Connection& c) noexcept {
  release(c);
  c.clear();
}

bool Engine::cookie_for(const Connection& c) const noexcept {
  // Item 3: a top-level page load on the loopback listener, from 127.0.0.1 or ::1, with the Host 127.0.0.1:PORT.
  const RequestHead& head = c.parser.head();
  return c.exact_peer && head.host == HostMatch::loopback && is_page_load(head);
}

void Engine::respond(Connection& c, std::size_t head_size, std::string_view body,
                     std::shared_ptr<const std::string> owner, Clock::time_point now) noexcept {
  if (head_size == 0) {
    drop(c);
    return;
  }
  c.head_size = head_size;
  c.head_sent = 0;
  c.response = body;
  c.response_owner = std::move(owner);
  c.response_sent = 0;
  c.phase = Connection::Phase::respond;
  c.deadline = now + config_.timings.write;
}

void Engine::respond_status(Connection& c, std::uint16_t status, std::string_view body, bool page_load,
                            Clock::time_point now) noexcept {
  Response response;
  response.status = status;
  response.content_type = kPlainText;
  response.content_length = body.size();
  response.session_cookie =
      page_load && cookie_for(c) ? std::string_view(cookie_.data(), cookie_.size()) : std::string_view();
  respond(c, write_response_head(response, c.head), body, nullptr, now);
}

void Engine::refuse(Connection& c, Refusal refusal, Clock::time_point now) noexcept {
  const std::string_view body = refusal_body(refusal);
  Response response;
  response.status = status_of(refusal);
  response.content_type = kPlainText;
  response.content_length = body.size();
  respond(c, write_response_head(response, c.head), body, nullptr, now);
}

void Engine::route(Connection& c, std::string_view after, Clock::time_point now) {
  const RequestHead& head = c.parser.head();
  if (head.path == kSocketRoute || head.path == kTicketRoute) {
    if (head.has_query) {
      respond_status(c, 404, kNotFound, false, now);  // nothing goes in a query string (item 4)
    } else if (head.path == kSocketRoute) {
      upgrade(c, after.size(), now);
    } else if (head.method == Method::post) {
      ticket(c, after, now);
    } else {
      respond_status(c, 404, kNotFound, false, now);
    }
    return;
  }
  if (head.method == Method::post) {
    respond_status(c, 404, kNotFound, false, now);  // static files by GET only (item 9)
    return;
  }
  if (files_ == nullptr) {
    c.phase = Connection::Phase::files;  // the worker is still reading them: within the same deadline
    return;
  }
  serve_get(c, now);
}

void Engine::serve_get(Connection& c, Clock::time_point now) {
  const RequestHead& head = c.parser.head();
  const bool page = is_page_load(head);
  if (page && (head.host == HostMatch::localhost || head.host == HostMatch::ipv6_loopback)) {
    // Item 23: to the same path on http://127.0.0.1:PORT, written from the listed file's own name, or "/".
    std::string_view location = "/";
    if (!head.has_query && files_->ok && head.path != "/") {
      if (const WebFile* file = files_->find(head.path); file != nullptr) {
        c.body[0] = '/';
        std::copy(file->name.begin(), file->name.end(), c.body.begin() + 1);
        location = std::string_view(c.body.data(), file->name.size() + 1);
      }
    }
    respond(c, write_redirect(config_.port, location, c.head), {}, nullptr, now);
    return;
  }
  if (!files_->ok) {
    if (page) {
      // Item 15: the built-in notice page in place of an app that failed its check.
      const std::string_view notice = notice_page();
      Response response;
      response.status = 503;
      response.content_type = "text/html; charset=utf-8";
      response.content_length = notice.size();
      respond(c, write_response_head(response, c.head), notice, nullptr, now);
    } else {
      respond_status(c, 404, kNotFound, false, now);
    }
    return;
  }
  const WebFile* file = head.has_query ? nullptr : files_->find(head.path);  // a query string is 404 (item 21)
  if (file == nullptr) {
    respond_status(c, 404, kNotFound, page, now);
    return;
  }
  Response response;
  response.status = 200;
  response.content_type = file->type;
  response.content_length = file->bytes->size();
  response.no_store = file->html;  // the HTML is no-store; hashed scripts, styles and fonts are cached (§6.4)
  response.session_cookie = page && cookie_for(c) ? std::string_view(cookie_.data(), cookie_.size()) : std::string_view();
  respond(c, write_response_head(response, c.head), *file->bytes, file->bytes, now);
}

void Engine::ticket(Connection& c, std::string_view after, Clock::time_point now) {
  const RequestHead& head = c.parser.head();
  // Items 4, 6 and 7: Origin, Fetch Metadata and the cookie, then a JSON body within its cap (item 9).
  const Refusal refusal =
      check_api_request(head, listener_, kSessionCookie, std::string_view(cookie_.data(), cookie_.size()));
  if (refusal != Refusal::none) {
    refuse(c, refusal, now);
    return;
  }
  if (!content_type_is_json(head)) {
    refuse(c, Refusal::not_json, now);
    return;
  }
  c.body_need = static_cast<std::size_t>(head.content_length);  // at most kMaxTicketBody: the parser's cap
  c.body_size = std::min(after.size(), c.body_need);
  std::copy_n(after.data(), c.body_size, c.body.data());
  if (c.body_size == c.body_need) {
    issue_ticket(c, now);
  } else {
    c.phase = Connection::Phase::body;
  }
}

void Engine::issue_ticket(Connection& c, Clock::time_point now) {
  std::array<char, kTokenChars> token{};
  if (!new_token(token)) {
    respond_status(c, 503, kUnavailable, false, now);
    return;
  }
  // A dead or stale slot, or else the oldest: at most kMaxTickets wait (EST).
  Ticket* slot = &tickets_[0];
  for (Ticket& ticket : tickets_) {
    if (!ticket.live || now - ticket.issued >= config_.timings.ticket) {
      slot = &ticket;
      break;
    }
    if (ticket.issued < slot->issued) {
      slot = &ticket;
    }
  }
  slot->token = token;
  slot->issued = now;
  slot->live = true;
  constexpr std::string_view kOpen = "{\"ticket\":\"";
  constexpr std::string_view kClose = "\"}";
  std::size_t size = 0;
  for (const std::string_view part : {kOpen, std::string_view(token.data(), token.size()), kClose}) {
    std::copy(part.begin(), part.end(), c.body.begin() + static_cast<std::ptrdiff_t>(size));
    size += part.size();
  }
  token.fill(0);
  Response response;
  response.status = 200;
  response.content_type = "application/json";
  response.content_length = size;
  respond(c, write_response_head(response, c.head), std::string_view(c.body.data(), size), nullptr, now);
}

void Engine::upgrade(Connection& c, std::size_t after, Clock::time_point now) {
  const RequestHead& head = c.parser.head();
  // Items 6 and 7 on every upgrade (an upgrade without Origin is refused), the cookie, then RFC 6455's own rules.
  Refusal refusal =
      check_api_request(head, listener_, kSessionCookie, std::string_view(cookie_.data(), cookie_.size()));
  std::array<char, kAcceptChars> accept{};
  if (refusal == Refusal::none) {
    refusal = check_upgrade(head, after, accept);
  }
  if (refusal != Refusal::none) {
    refuse(c, refusal, now);
    return;
  }
  const std::size_t size = write_switching_protocols(std::string_view(accept.data(), accept.size()), c.head);
  if (size == 0) {
    drop(c);
    return;
  }
  c.http = false;
  c.websocket = true;
  c.session.emplace();
  c.upgraded = now;
  c.deadline = now + config_.timings.hello;  // the hello's 2 s, counted from the 101 (item 22)
  c.phase = Connection::Phase::hello;
  c.tokens = static_cast<double>(protocol::kCommandsPerSecond);
  c.refilled = now;
  if (!enqueue(c, std::make_shared<const std::string>(c.head.data(), size))) {
    return;
  }
  // Sockets still waiting for their hello have a cap of their own, and one past it closes the oldest of them, never a
  // view that showed its ticket: only a welcomed view supersedes one (§6.8, at the welcome).
  if (Connection* oldest = oldest_websocket(Connection::Phase::hello, &c);
      oldest != nullptr && count_websockets(Connection::Phase::hello) > kMaxWebSockets) {
    close_socket(*oldest, protocol::ByeReason::unauthorised, 1000, now);
  }
}

Connection* Engine::oldest_websocket(Connection::Phase phase, const Connection* except) noexcept {
  Connection* oldest = nullptr;
  for (const auto& slot : pool_) {
    if (slot.get() != except && slot->websocket && slot->phase == phase &&
        (oldest == nullptr || slot->upgraded < oldest->upgraded)) {
      oldest = slot.get();
    }
  }
  return oldest;
}

void Engine::renew(Clock::time_point now) {
  if (!new_token(cookie_)) {
    log(LogLevel::error, "The browser view is off for this session: the system gave no random bytes for its cookie.");
    faulted_ = true;
    return;
  }
  for (Ticket& ticket : tickets_) {
    ticket = Ticket{};
  }
  for (const auto& slot : pool_) {
    Connection& c = *slot;
    if (c.phase == Connection::Phase::hello || c.phase == Connection::Phase::open) {
      close_socket(c, protocol::ByeReason::unauthorised, 1000, now);  // its page reloads once for the new cookie
    } else if (c.phase != Connection::Phase::idle && c.phase != Connection::Phase::closing) {
      drop(c);
    }
  }
  log(LogLevel::info, "The browser view's session was renewed: every browser was disconnected.");
}

}  // namespace detail

Server::Server() : hub_(std::make_shared<detail::Hub>()) {}

Server::~Server() { stop(); }

StartResult Server::start(const ServerConfig& config) {
  if (engine_ != nullptr && engine_->running()) {
    return StartResult::already_running;
  }
  stop();  // a server that stopped itself after a fault is joined first
  if (config.port < kLowestPort) {
    return StartResult::bad_port;
  }
  auto engine = std::make_unique<detail::Engine>(hub_, config);
  const StartResult result = engine->start();
  if (result == StartResult::ok) {
    engine_ = std::move(engine);
  }
  return result;
}

void Server::stop() noexcept {
  if (engine_ != nullptr) {
    engine_->stop();
    engine_.reset();
  }
}

bool Server::running() const noexcept { return engine_ != nullptr && engine_->running(); }

ServerStats Server::stats() const noexcept { return engine_ != nullptr ? engine_->stats() : ServerStats{}; }

std::shared_ptr<SpacePort> Server::add_space(SpaceDescription description, SpaceWake wake) {
  if (!protocol::valid_id(description.info.id)) {
    return nullptr;
  }
  for (std::size_t i = 0; i < description.topics.size(); ++i) {
    for (std::size_t j = 0; j < i; ++j) {
      if (description.topics[i].id == description.topics[j].id) {
        return nullptr;
      }
    }
  }
  auto state = std::make_shared<detail::PortState>();
  const std::size_t topics = description.topics.size();
  state->description = std::move(description);
  state->wake = std::move(wake);
  state->watched.assign(topics, 0);
  state->reported.assign(topics, 0);
  state->topics.assign(topics, detail::TopicCache{});
  {
    std::lock_guard lock(hub_->mutex);
    for (const auto& space : hub_->spaces) {
      if (space->description.info.id == state->description.info.id) {
        return nullptr;
      }
    }
    hub_->spaces.push_back(state);
  }
  return std::shared_ptr<SpacePort>(new SpacePort(hub_, std::move(state)));
}

void Server::remove_space(std::string_view id) noexcept {
  std::lock_guard lock(hub_->mutex);
  for (auto it = hub_->spaces.begin(); it != hub_->spaces.end(); ++it) {
    if ((*it)->description.info.id == id) {
      (*it)->registered = false;
      hub_->spaces.erase(it);
      hub_->spaces_removed = true;
      hub_->signal_locked();
      return;
    }
  }
}

void Server::renew_session() noexcept {
  std::lock_guard lock(hub_->mutex);
  hub_->renew = true;
  hub_->signal_locked();
}

}  // namespace grace::serve
