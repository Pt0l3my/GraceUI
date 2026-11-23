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
  peer_ipv4 = 0;
  http = false;
  websocket = false;
  network = false;
  paired = false;
  device.fill('\0');
  body_for = BodyFor::ticket;
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
  upload.reset();  // zeroed with its last holder (FileBytes)
  upload_got = 0;
  upload_slot = 0;
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

  loopback_rules_ = HeadRules{};
  loopback_rules_.listener = listener_;
  // The largest body a route takes: an upload's (G28, §7.2 item 18); each route caps its own below it (a ticket's 256 B).
  loopback_rules_.max_content_length = kMaxUpload;
  uploads_ = {};
  {
    std::lock_guard lock(worker_mutex_);
    hash_jobs_.clear();
    hash_done_.clear();
  }
  network_listener_ = Listener{};
  network_listener_.network = true;
  network_listener_.port = config_.port;
  network_rules_ = HeadRules{};
  network_rules_.listener = network_listener_;
  network_rules_.max_content_length = std::max(kMaxTicketBody, kMaxPairBody);  // a ticket's, or the code's (G9)
  devices_.clear();
  network_wanted_ = false;
  network_seen_.clear();
  connected_seen_.clear();
  pair_logged_ = {};
  pair_unlogged_ = 0;
  pool_.clear();
  for (std::size_t i = 0; i < kPoolSize; ++i) {
    pool_.push_back(std::make_unique<Connection>(loopback_rules_));
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
    hub_->network_changed = hub_->network_on;  // a host that turned it on before the start: applied by the loop (G9)
    hub_->devices_changed = true;
    hub_->network_addresses.clear();
    hub_->connected_devices.clear();
    for (const auto& port : hub_->spaces) {
      port->topics.assign(port->declared + kMemberSlots, TopicCache{});
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
  for (io::Socket& socket : network_) {
    socket.close();
  }
  {
    std::lock_guard lock(hub_->mutex);
    hub_->network_addresses.clear();
    hub_->connected_devices.clear();
  }
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
  stat_network_ = 0;
  stat_devices_ = 0;
  stat_device_connections_ = 0;
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
  stats.network = stat_network_.load();
  stats.devices = stat_devices_.load();
  stats.device_connections = stat_device_connections_.load();
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
    // Then each upload's SHA-256 (§7.2 item 18, §7.6), off the I/O thread, until the server stops.
    std::unique_lock lock(worker_mutex_);
    while (true) {
      worker_wake_.wait(lock, [this] { return worker_stop_ || !hash_jobs_.empty(); });
      if (worker_stop_) {
        hash_jobs_.clear();
        break;
      }
      HashJob job = std::move(hash_jobs_.front());
      hash_jobs_.pop_front();
      lock.unlock();
      Sha256 sha;
      sha.update(job.file->bytes());
      const std::array<std::uint8_t, 32> digest = sha.finish();
      job.file.reset();  // the worker's hold on the file ends here
      lock.lock();
      hash_done_.push_back(HashDone{job.serial, digest});
      lock.unlock();
      wake_.signal();
      lock.lock();
    }
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
    take_hashes(now);     // the worker's checked uploads (G28)
    expire_uploads(now);  // and tickets not used in time
    follow_addresses(now);
    Clock::time_point next = now + std::chrono::seconds(1);
    std::size_t http = 0;
    std::size_t network_http = 0;
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
      network_http += c.http && c.network ? 1 : 0;
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
    const std::size_t first_network = count;
    for (const io::Socket& listener : network_) {
      // The network listener's share of the connections is capped, so this PC keeps room whatever it sends (G9).
      if (listener.open() && accepting && network_http < kMaxNetworkHttp) {
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
        if (i >= first_network) {
          for (const io::Socket& listener : network_) {
            if (listener.open() && listener.handle() == items[i].handle) {
              accept_from(listener, true, now);
            }
          }
          continue;
        }
        const io::Socket& listener = items[i].handle == listeners_[0].handle() ? listeners_[0] : listeners_[1];
        accept_from(listener, false, now);
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
    std::size_t network_sockets = 0;
    for (const io::Socket& listener : network_) {
      network_sockets += listener.open() ? 1 : 0;
    }
    sockets += network_sockets;
    std::size_t device_connections = 0;
    std::array<std::array<char, kDeviceIdChars>, kPoolSize> connected{};
    std::size_t connected_count = 0;
    for (const auto& slot : pool_) {
      if (slot->phase != Connection::Phase::idle) {
        ++sockets;
        open_http += slot->http ? 1 : 0;
        open_websockets += slot->websocket ? 1 : 0;
        device_connections += slot->network && slot->paired ? 1 : 0;
        if (slot->network && slot->paired && slot->phase == Connection::Phase::open && connected_count < connected.size()) {
          connected[connected_count++] = slot->device;
        }
      }
    }
    std::sort(connected.begin(), connected.begin() + static_cast<std::ptrdiff_t>(connected_count));
    if (!std::equal(connected.begin(), connected.begin() + static_cast<std::ptrdiff_t>(connected_count),
                    connected_seen_.begin(), connected_seen_.end())) {
      connected_seen_.assign(connected.begin(), connected.begin() + static_cast<std::ptrdiff_t>(connected_count));
      std::vector<std::string> ids;
      for (const auto& id : connected_seen_) {
        if (ids.empty() || ids.back() != std::string_view(id.data(), id.size())) {
          ids.emplace_back(id.data(), id.size());
        }
      }
      std::lock_guard lock(hub_->mutex);
      hub_->connected_devices = std::move(ids);
    }
    stat_network_ = network_sockets;
    stat_devices_ = devices_.size();
    stat_device_connections_ = device_connections;
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

void Engine::accept_from(const io::Socket& listener, bool network, Clock::time_point now) {
  while (true) {
    std::size_t http = 0;
    std::size_t network_http = 0;
    Connection* free = nullptr;
    for (const auto& slot : pool_) {
      if (slot->phase == Connection::Phase::idle) {
        free = free != nullptr ? free : slot.get();
      } else if (slot->http) {
        ++http;
        network_http += slot->network ? 1 : 0;
      }
    }
    if (free == nullptr || http >= kMaxHttpConnections || (network && network_http >= kMaxNetworkHttp)) {
      return;
    }
    io::Accepted accepted;
    if (io::accept_one(listener, accepted) != io::Io::done) {
      return;
    }
    if (!network && !accepted.loopback) {
      continue;  // a loopback listener takes loopback peers alone; the socket closes here
    }
    if (network && !accepted.home && !accepted.loopback) {
      continue;  // the network listener takes a device at home alone: a private or link-local peer (item 25)
    }
    Connection& c = *free;
    c.clear();
    c.network = network;  // another device at home (G9): its own head rules, and a device's cookie for credential
    c.parser.reset(network ? network_rules_ : loopback_rules_);
    c.socket = std::move(accepted.socket);
    c.exact_peer = accepted.exact;
    c.peer_ipv4 = accepted.ipv4;
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
    case Phase::upload:   // an upload's body within its time; dropped, its session hears so (G28)
    case Phase::hashing:
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
  // What was read is wiped once handled, whatever the phase (D229): an upload's bytes pass through this buffer, and
  // GraceUI keeps nothing of a file (§7.2 item 18).
  struct Wipe {
    std::array<char, 16384>& buffer;
    std::size_t count;
    ~Wipe() { std::memset(buffer.data(), 0, std::min(count, buffer.size())); }
  } wipe{scratch_, count};
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
        body_done(c, now);
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
    case Phase::upload:
      upload_bytes(c, data, now);  // into its file, up to the ticket's size (G28)
      return;
    case Phase::hashing:   // anything past the file is not read into it
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
  if (c.network) {
    route_network(c, after, now);  // another device at home (G9)
    return;
  }
  const RequestHead& head = c.parser.head();
  if (head.path == kUploadRoute) {
    if (head.has_query || head.method != Method::post) {
      respond_status(c, 404, kNotFound, false, now);
    } else {
      upload_post(c, after, now);  // G28, §7.2 item 18: on the loopback listener alone
    }
    return;
  }
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
  if (c.network && page) {
    // A paired device's page load renews its cookie's life in the browser, with the value it brought (G9).
    std::string_view token;
    if (match_device(head.cookie, devices_, &token)) {
      response.device_cookie = token;
    }
  }
  respond(c, write_response_head(response, c.head), *file->bytes, file->bytes, now);
}

void Engine::ticket(Connection& c, std::string_view after, Clock::time_point now) {
  const RequestHead& head = c.parser.head();
  // Items 4, 6 and 7: Origin, Fetch Metadata and the cookie (a paired device's on the network listener, G9), then a
  // JSON body within its cap (item 9).
  const Refusal refusal =
      c.network ? check_device(c)
                : check_api_request(head, listener_, kSessionCookie, std::string_view(cookie_.data(), cookie_.size()));
  if (refusal != Refusal::none) {
    refuse(c, refusal, now);
    return;
  }
  if (!content_type_is_json(head) || head.content_length > kMaxTicketBody) {
    refuse(c, content_type_is_json(head) ? Refusal::body_too_large : Refusal::not_json, now);
    return;
  }
  c.body_for = Connection::BodyFor::ticket;
  take_body(c, after, now);
}

void Engine::take_body(Connection& c, std::string_view after, Clock::time_point now) {
  c.body_need = static_cast<std::size_t>(c.parser.head().content_length);  // within the body's cap: the parser's
  c.body_size = std::min(after.size(), c.body_need);
  std::copy_n(after.data(), c.body_size, c.body.data());
  if (c.body_size == c.body_need) {
    body_done(c, now);
  } else {
    c.phase = Connection::Phase::body;
  }
}

void Engine::body_done(Connection& c, Clock::time_point now) {
  if (c.body_for == Connection::BodyFor::pair) {
    finish_pair(c, now);
  } else {
    issue_ticket(c, now);
  }
}

void Engine::issue_ticket(Connection& c, Clock::time_point now) {
  std::array<char, kTokenChars> token{};
  if (!new_token(token)) {
    respond_status(c, 503, kUnavailable, false, now);
    return;
  }
  // A dead or stale slot, or else the oldest: at most kMaxTickets wait (EST). A device's ticket takes the place of a
  // device's alone, never this PC's; this PC's takes a device's first (G9).
  Ticket* slot = nullptr;
  Ticket* device = nullptr;
  Ticket* pc = nullptr;
  for (Ticket& ticket : tickets_) {
    if (!ticket.live || now - ticket.issued >= config_.timings.ticket) {
      slot = &ticket;
      break;
    }
    Ticket*& oldest = ticket.network ? device : pc;
    if (oldest == nullptr || ticket.issued < oldest->issued) {
      oldest = &ticket;
    }
  }
  if (slot == nullptr) {
    slot = c.network ? device : (device != nullptr ? device : pc);
  }
  if (slot == nullptr) {
    token.fill(0);
    respond_status(c, 503, kUnavailable, false, now);  // every place holds this PC's tickets
    return;
  }
  slot->token = token;
  slot->issued = now;
  slot->live = true;
  slot->network = c.network;  // a device's ticket opens that device's WebSocket alone (G9)
  slot->device = c.device;
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
  // Items 6 and 7 on every upgrade (an upgrade without Origin is refused), the cookie (a paired device's on the network
  // listener, G9), then RFC 6455's own rules.
  Refusal refusal =
      c.network ? check_device(c)
                : check_api_request(head, listener_, kSessionCookie, std::string_view(cookie_.data(), cookie_.size()));
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
  if (count_websockets(Connection::Phase::hello) > kMaxWebSockets) {
    // A device's never closes this PC's (G9): then it is the one closed.
    Connection* oldest = displaced(Connection::Phase::hello, c);
    close_socket(oldest != nullptr ? *oldest : c, protocol::ByeReason::unauthorised, 1000, now);
  }
}

Connection* Engine::displaced(Connection::Phase phase, const Connection& fresh) noexcept {
  Connection* device = nullptr;
  Connection* pc = nullptr;
  for (const auto& slot : pool_) {
    if (slot.get() == &fresh || !slot->websocket || slot->phase != phase) {
      continue;
    }
    Connection*& oldest = slot->network ? device : pc;
    if (oldest == nullptr || slot->upgraded < oldest->upgraded) {
      oldest = slot.get();
    }
  }
  return fresh.network ? device : (device != nullptr ? device : pc);
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

// ---- Other devices at home (G9, §7.2 item 16).

namespace {

constexpr std::string_view kPairedWords = "Paired.";
// One answer whether the code was wrong or none was showing, so that the network learns nothing of when one shows.
constexpr std::string_view kNotPaired =
    "That code is not right, or none is showing now. In game, choose Pair a device on GraceUI's Browser view page, "
    "then enter the code it shows.";
constexpr std::string_view kTooMany = "Sixteen devices are paired already: forget one in game first.";
constexpr std::string_view kBadName = "Give this device a name of 1 to 32 characters.";
constexpr std::string_view kBadPair = "Send the code and a name.";

}  // namespace

Refusal Engine::check_device(const Connection& c) const noexcept {
  // As check_api_request's order: Origin (this listener's own address), Fetch Metadata, then the credential, here a
  // paired device's cookie and never the session's.
  const RequestHead& head = c.parser.head();
  switch (check_origin(head, network_listener_)) {
    case OriginCheck::missing:
      return Refusal::origin_missing;
    case OriginCheck::mismatch:
      return Refusal::origin_mismatch;
    case OriginCheck::ok:
      break;
  }
  if (!fetch_site_allowed(head)) {
    return Refusal::cross_site;
  }
  if (head.cookie && head.cookie->size() > kMaxCookie) {
    return Refusal::cookie_too_large;
  }
  return c.paired ? Refusal::none : Refusal::cookie_missing;
}

void Engine::route_network(Connection& c, std::string_view after, Clock::time_point now) {
  const RequestHead& head = c.parser.head();
  const std::optional<std::size_t> device = match_device(head.cookie, devices_);
  c.paired = device.has_value();
  if (device) {
    c.device = devices_[*device].id;
  }
  if (head.has_query) {
    respond_status(c, 404, kNotFound, false, now);  // nothing goes in a query string (items 4 and 21)
    return;
  }
  if (head.path == kPairRoute) {
    if (head.method == Method::post) {
      pair(c, after, now);
    } else {
      respond_status(c, 404, kNotFound, false, now);
    }
    return;
  }
  if (head.method == Method::get && (head.path == kPairScriptRoute || head.path == kPairStyleRoute)) {
    serve_pairing(c, now);
    return;
  }
  if (!c.paired) {
    // An unpaired device sees the pairing page and nothing else: no file, no ticket, no WebSocket.
    if (head.path == kTicketRoute || head.path == kSocketRoute) {
      refuse(c, Refusal::cookie_missing, now);
    } else if (is_page_load(head)) {
      serve_pairing(c, now);
    } else {
      respond_status(c, 404, kNotFound, false, now);
    }
    return;
  }
  // A paired device: this PC's routes, its cookie for credential; never the session cookie, never an upload.
  if (head.path == kSocketRoute) {
    upgrade(c, after.size(), now);
  } else if (head.path == kTicketRoute) {
    if (head.method == Method::post) {
      ticket(c, after, now);
    } else {
      respond_status(c, 404, kNotFound, false, now);
    }
  } else if (head.method == Method::post) {
    respond_status(c, 404, kNotFound, false, now);
  } else if (files_ == nullptr) {
    c.phase = Connection::Phase::files;
  } else {
    serve_get(c, now);
  }
}

void Engine::serve_pairing(Connection& c, Clock::time_point now) noexcept {
  const RequestHead& head = c.parser.head();
  std::string_view body = pairing_page();
  std::string_view type = "text/html; charset=utf-8";
  if (head.path == kPairScriptRoute) {
    body = pairing_script();
    type = "text/javascript; charset=utf-8";
  } else if (head.path == kPairStyleRoute) {
    body = pairing_style();
    type = "text/css; charset=utf-8";
  }
  Response response;
  response.status = 200;
  response.content_type = type;
  response.content_length = body.size();
  respond(c, write_response_head(response, c.head), body, nullptr, now);
}

void Engine::pair(Connection& c, std::string_view after, Clock::time_point now) {
  const RequestHead& head = c.parser.head();
  // Items 6, 7 and 9: Origin, Fetch Metadata, and a JSON body within its cap; no cookie, since none is held yet.
  switch (check_origin(head, network_listener_)) {
    case OriginCheck::missing:
      refuse(c, Refusal::origin_missing, now);
      return;
    case OriginCheck::mismatch:
      refuse(c, Refusal::origin_mismatch, now);
      return;
    case OriginCheck::ok:
      break;
  }
  if (!fetch_site_allowed(head)) {
    refuse(c, Refusal::cross_site, now);
    return;
  }
  if (!content_type_is_json(head)) {
    refuse(c, Refusal::not_json, now);
    return;
  }
  if (head.content_length > kMaxPairBody) {
    refuse(c, Refusal::body_too_large, now);
    return;
  }
  c.body_for = Connection::BodyFor::pair;
  take_body(c, after, now);
}

void Engine::finish_pair(Connection& c, Clock::time_point now) {
  const protocol::Json body = protocol::Json::parse(std::string_view(c.body.data(), c.body_size), nullptr, false);
  const auto text = [&](const char* name) -> std::string {
    const auto it = body.is_object() ? body.find(name) : body.end();
    return body.is_object() && it != body.end() && it->is_string() ? it->get<std::string>() : std::string();
  };
  const std::string code = text("code");
  const std::string name = text("name");
  if (!body.is_object() || code.empty()) {
    respond_status(c, 400, kBadPair, false, now);
    return;
  }
  if (!valid_device_name(name)) {
    respond_status(c, 400, kBadName, false, now);
    return;
  }
  std::array<std::uint8_t, kTokenBytes> secret{};
  std::array<std::uint8_t, kDeviceIdChars / 2> id_bytes{};
  if (!io::random_bytes(secret) || !io::random_bytes(id_bytes)) {
    respond_status(c, 503, kUnavailable, false, now);
    return;
  }
  std::array<char, kTokenChars> token{};
  encode_token(secret, token);
  secret.fill(0);
  DeviceKey key;
  constexpr std::string_view kHex = "0123456789abcdef";
  for (std::size_t i = 0; i < id_bytes.size(); ++i) {
    key.id[2 * i] = kHex[id_bytes[i] >> 4];
    key.id[2 * i + 1] = kHex[id_bytes[i] & 0xFu];
  }
  key.hash = device_hash(std::string_view(token.data(), token.size()));
  PairingCode::Try tried = PairingCode::Try::none;
  bool full = false;
  {
    std::lock_guard lock(hub_->mutex);
    full = devices_.size() >= kMaxDevices;
    if (!full) {
      tried = hub_->pairing.attempt(code, now, c.peer_ipv4);  // a full list never spends the code
    }
    if (tried == PairingCode::Try::paired) {
      hub_->paired.push_back(PairedDevice{std::string(key.id.data(), key.id.size()), name, key.hash});
      hub_->devices.push_back(key);
    }
  }
  if (full) {
    token.fill(0);
    respond_status(c, 403, kTooMany, false, now);
    return;
  }
  if (tried != PairingCode::Try::paired) {
    token.fill(0);
    // At most one line in 10 s, with the count since: a flood of requests cannot push the log's other lines out.
    ++pair_unlogged_;
    if (pair_logged_ == Clock::time_point{} || now - pair_logged_ >= std::chrono::seconds(10)) {
      log(LogLevel::info, pair_unlogged_ == 1 ? std::string("A device's pairing code was refused.")
                                              : "Devices' pairing codes were refused " + std::to_string(pair_unlogged_) +
                                                    " times.");
      pair_logged_ = now;
      pair_unlogged_ = 0;
    }
    respond_status(c, 403, kNotPaired, false, now);
    return;
  }
  devices_.push_back(key);
  c.paired = true;
  c.device = key.id;
  Response response;
  response.status = 200;
  response.content_type = kPlainText;
  response.content_length = kPairedWords.size();
  response.device_cookie = std::string_view(token.data(), token.size());
  respond(c, write_response_head(response, c.head), kPairedWords, nullptr, now);
  token.fill(0);  // its only copy is in the response's head now, and the browser's
  log(LogLevel::info, "A device was paired with the browser view.");
  if (config_.paired_wake) {
    try {
      config_.paired_wake();
    } catch (...) {
    }
  }
}

void Engine::apply_network(bool on, Clock::time_point now) {
  // Whatever it was: every socket and connection of the network listener closes first.
  for (io::Socket& socket : network_) {
    socket.close();
  }
  for (const auto& slot : pool_) {
    Connection& c = *slot;
    if (!c.network || c.phase == Connection::Phase::idle) {
      continue;
    }
    if (c.phase == Connection::Phase::hello || c.phase == Connection::Phase::open) {
      close_socket(c, protocol::ByeReason::shutdown, 1001, now);
    } else if (c.phase != Connection::Phase::closing) {
      drop(c);
    }
  }
  for (Ticket& ticket : tickets_) {
    if (ticket.network) {
      ticket = Ticket{};
    }
  }
  std::size_t count = 0;
  std::vector<std::string> names;
  network_wanted_ = on;
  next_address_check_ = now + std::chrono::seconds(10);
  network_seen_.clear();
  if (on) {
    const std::vector<std::array<std::uint8_t, 4>> candidates = network_candidates();
    for (const std::array<std::uint8_t, 4>& address : candidates) {
      std::array<char, 16> dotted{};
      network_seen_.emplace_back(dotted.data(), format_ipv4(address, dotted));
    }
    std::sort(network_seen_.begin(), network_seen_.end());
    for (const std::array<std::uint8_t, 4>& address : candidates) {
      io::BindError error = io::BindError::none;
      io::Socket socket = io::listen_ipv4(address, config_.port, error);
      if (!socket.open()) {
        continue;
      }
      std::array<char, 16> dotted{};
      network_[count] = std::move(socket);
      network_names_[count] = std::string(dotted.data(), format_ipv4(address, dotted));
      ++count;
    }
    for (std::size_t i = 0; i < count; ++i) {
      network_views_[i] = network_names_[i];
      names.push_back(network_names_[i]);
    }
  }
  network_listener_.addresses = std::span<const std::string_view>(network_views_.data(), count);
  network_rules_.listener = network_listener_;
  {
    std::lock_guard lock(hub_->mutex);
    hub_->network_addresses = std::move(names);
  }
  // Fixed words and a count only: never the address itself (PV3, PV5).
  if (!on) {
    log(LogLevel::info, "Other devices: the browser view no longer listens on the home network.");
  } else if (count == 0) {
    log(LogLevel::warning, "Other devices: no private network address of this PC could be listened on.");
  } else {
    log(LogLevel::info, "Other devices: the browser view listens on " + std::to_string(count) +
                            " of this PC's home network addresses too.");
  }
}

std::vector<std::array<std::uint8_t, 4>> Engine::network_candidates() const {
  std::array<std::array<std::uint8_t, 4>, 16> found{};
  std::size_t listed = 0;
  if (!config_.network_addresses.empty()) {
    for (const auto& address : config_.network_addresses) {
      if (listed < found.size()) {
        found[listed++] = address;  // a test's own, taken as they are
      }
    }
  } else {
    listed = io::local_ipv4_addresses(found);
  }
  std::vector<std::array<std::uint8_t, 4>> out;
  for (std::size_t i = 0; i < listed && out.size() < kMaxNetworkAddresses; ++i) {
    const std::array<std::uint8_t, 4>& address = found[i];
    if (address[0] == 0 || address[0] >= 224) {
      continue;  // never 0.0.0.0 (every interface), a multicast or a reserved address, whatever the source
    }
    if (config_.network_addresses.empty() && !private_ipv4(address)) {
      continue;  // the PC's private addresses alone, never a public one (item 16)
    }
    if (std::find(out.begin(), out.end(), address) == out.end()) {
      out.push_back(address);
    }
  }
  return out;
}

void Engine::follow_addresses(Clock::time_point now) {
  if (!network_wanted_ || now < next_address_check_) {
    return;
  }
  next_address_check_ = now + std::chrono::seconds(10);
  std::vector<std::string> wanted;
  for (const std::array<std::uint8_t, 4>& address : network_candidates()) {
    std::array<char, 16> dotted{};
    wanted.emplace_back(dotted.data(), format_ipv4(address, dotted));
  }
  std::sort(wanted.begin(), wanted.end());
  if (wanted == network_seen_) {
    return;
  }
  // The PC's addresses changed (another network, a new lease): its listeners follow them (item 25). Every device
  // connection closes, as on any change of the listeners, and each device's page reconnects at its new address.
  log(LogLevel::info, "Other devices: this PC's home network addresses changed; the browser view follows them.");
  apply_network(true, now);
}

void Engine::apply_devices(std::vector<DeviceKey> devices, Clock::time_point now) {
  devices_ = std::move(devices);
  // A device no longer listed is disconnected at once: its page reloads and finds the pairing page (item 16).
  for (const auto& slot : pool_) {
    Connection& c = *slot;
    if (!c.network || !c.paired || c.phase == Connection::Phase::idle) {
      continue;
    }
    const bool kept = std::any_of(devices_.begin(), devices_.end(), [&](const DeviceKey& d) { return d.id == c.device; });
    if (kept) {
      continue;
    }
    if (c.phase == Connection::Phase::hello || c.phase == Connection::Phase::open) {
      close_socket(c, protocol::ByeReason::unauthorised, 1000, now);
    } else if (c.phase != Connection::Phase::closing) {
      drop(c);
    }
  }
  for (Ticket& ticket : tickets_) {
    if (ticket.network && std::none_of(devices_.begin(), devices_.end(),
                                       [&](const DeviceKey& d) { return d.id == ticket.device; })) {
      ticket = Ticket{};
    }
  }
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
  state->declared = topics;
  state->watched.assign(topics + detail::kMemberSlots, 0);
  state->reported.assign(topics + detail::kMemberSlots, 0);
  state->members.assign(detail::kMemberSlots, detail::PortState::Member{});
  state->topics.assign(topics + detail::kMemberSlots, detail::TopicCache{});
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

void Server::set_network(bool on) noexcept {
  std::lock_guard lock(hub_->mutex);
  hub_->network_on = on;
  hub_->network_changed = true;
  hub_->signal_locked();
}

std::vector<std::string> Server::network_addresses() const {
  std::lock_guard lock(hub_->mutex);
  return hub_->network_addresses;
}

std::string Server::start_pairing() {
  std::array<std::uint8_t, 64> random{};
  if (!io::random_bytes(random)) {
    return {};
  }
  const auto now = PairingCode::Clock::now();
  std::lock_guard lock(hub_->mutex);
  const bool started = hub_->pairing.start(random, now);
  random.fill(0);
  return started ? std::string(hub_->pairing.code(now)) : std::string();
}

void Server::cancel_pairing() noexcept {
  std::lock_guard lock(hub_->mutex);
  hub_->pairing.cancel();
}

std::string Server::pairing_code() const {
  std::lock_guard lock(hub_->mutex);
  return std::string(hub_->pairing.code(PairingCode::Clock::now()));
}

void Server::set_devices(std::vector<DeviceKey> devices) noexcept {
  std::lock_guard lock(hub_->mutex);
  // A device paired since the host last took it stays, whatever the host's list says.
  for (const PairedDevice& fresh : hub_->paired) {
    const bool listed = std::any_of(devices.begin(), devices.end(), [&](const DeviceKey& d) {
      return std::string_view(d.id.data(), d.id.size()) == fresh.id;
    });
    if (!listed) {
      DeviceKey key;
      std::copy_n(fresh.id.begin(), std::min(fresh.id.size(), key.id.size()), key.id.begin());
      key.hash = fresh.hash;
      devices.push_back(key);
    }
  }
  if (devices.size() > kMaxDevices) {
    devices.resize(kMaxDevices);
  }
  hub_->devices = std::move(devices);
  hub_->devices_changed = true;
  hub_->signal_locked();
}

std::vector<std::string> Server::connected_devices() const {
  std::lock_guard lock(hub_->mutex);
  return hub_->connected_devices;
}

std::vector<PairedDevice> Server::take_paired() {
  std::lock_guard lock(hub_->mutex);
  std::vector<PairedDevice> out;
  out.swap(hub_->paired);
  return out;
}

void Server::renew_session() noexcept {
  std::lock_guard lock(hub_->mutex);
  hub_->renew = true;
  hub_->signal_locked();
}

}  // namespace grace::serve
