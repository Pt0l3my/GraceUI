// bridge.cpp: the bridge over the WebSocket (§6, D81): frames from a page parsed by grace-protocol's parse_frame;
// hello and its ticket, welcome naming the spaces the view may show; sub, unsub and cmd routed to the space that owns
// them and to no other (invariant 3); pushes from a space to the connections that watch them; results to the asking
// connection alone; the limits per connection and per space (§6.8); errors that never echo a payload (§6.7).
#include <algorithm>
#include <string>
#include <utility>

#include "engine.h"
#include "grace/serve/checks.h"

namespace grace::serve::detail {
namespace {

constexpr std::size_t kDefaultRing = 200;
constexpr std::size_t kMaxDetail = 256;
// welcome's session: an opaque id for the page's own use, never the cookie (§6.3).
constexpr std::size_t kSessionBytes = 12;

std::string numbered(std::string_view kind, std::uint32_t id, std::string_view why) {
  return std::string(kind) + " " + std::to_string(id) + ": " + std::string(why);
}

std::size_t ring_of(const PortState& port, std::size_t topic) noexcept {
  const auto& ring = port.description.topics[topic].ring;
  return ring.has_value() && *ring != 0 ? *ring : kDefaultRing;
}

}  // namespace

void Engine::take_hub(Clock::time_point now) {
  std::deque<Outgoing> items;
  bool renewing = false;
  bool removed = false;
  std::optional<bool> network;
  std::optional<std::vector<DeviceKey>> devices;
  {
    std::lock_guard lock(hub_->mutex);
    items.swap(hub_->inbox);
    renewing = std::exchange(hub_->renew, false);
    removed = std::exchange(hub_->spaces_removed, false);
    if (std::exchange(hub_->network_changed, false)) {
      network = hub_->network_on;
    }
    if (std::exchange(hub_->devices_changed, false)) {
      devices = hub_->devices;
    }
  }
  if (devices) {
    apply_devices(std::move(*devices), now);  // a device forgotten in game is disconnected at once (G9)
  }
  if (network) {
    apply_network(*network, now);  // dashboard.lan switched: its listeners open, or close with every connection
  }
  if (removed) {
    // A space withdrawn: every subscription to it ends; its topics and commands are unknown from now on (D81).
    for (const auto& slot : pool_) {
      std::erase_if(slot->subs, [](const Subscription& sub) { return !sub.port->registered.load(); });
    }
  }
  for (Outgoing& item : items) {
    dispatch(item);
  }
  if (renewing) {
    renew(now);
  }
}

void Engine::dispatch(Outgoing& item) {
  PortState& port = *item.port;
  if (!port.registered.load()) {
    return;
  }
  if (item.kind == Outgoing::Kind::result) {
    for (const auto& slot : pool_) {
      // To the connection that asked, and to no other (identity bound to the connection, item 11).
      if (slot->serial == item.reply.connection && slot->phase == Connection::Phase::open) {
        enqueue(*slot, std::move(item.frame));
        return;
      }
    }
    return;
  }
  if (item.topic >= port.topics.size() || port.topics[item.topic].watchers == 0) {
    return;  // nobody watches it: nothing is kept (§2.4)
  }
  if (item.topic >= port.declared && port.topics[item.topic].generation != item.generation) {
    return;  // a member's state, its slot since given to another member (D215)
  }
  if (item.kind == Outgoing::Kind::state) {
    TopicCache& cache = port.topics[item.topic];
    cache.has_state = true;
    cache.rev = item.rev;
    cache.frame = std::move(item.frame);
    cache.device_frame = std::move(item.device_frame);
    return;  // sent by send_states, the newest at most each state_interval
  }
  {
    TopicCache& cache = port.topics[item.topic];
    const std::uint64_t next = cache.first_seq + cache.rows.size();
    if (cache.started && item.from_seq < next) {
      return;  // rows already held: a space's stream only moves on
    }
  }
  // Forwarded first, to each view that watches the stream, from the row it wants; then held for later watchers.
  for (const auto& slot : pool_) {
    Connection& c = *slot;
    if (c.phase != Connection::Phase::open) {
      continue;
    }
    for (std::size_t i = 0; i < c.subs.size() && c.phase == Connection::Phase::open; ++i) {
      if (c.subs[i].port.get() == &port && c.subs[i].topic == item.topic) {
        forward_rows(c, c.subs[i], item);
      }
    }
  }
  TopicCache& cache = port.topics[item.topic];
  if (cache.watchers == 0) {
    return;  // the last watcher went while the rows were forwarded
  }
  const std::uint64_t next = cache.first_seq + cache.rows.size();
  if (!cache.started || item.from_seq > next) {
    cache.rows.clear();  // a gap: the stream starts over there
    cache.first_seq = item.from_seq;
    cache.started = true;
  }
  for (protocol::Json& row : item.rows) {
    cache.rows.push_back(std::move(row));
  }
  const std::size_t ring = ring_of(port, item.topic);
  while (cache.rows.size() > ring) {
    cache.rows.pop_front();
    ++cache.first_seq;
  }
}

void Engine::forward_rows(Connection& c, Subscription& sub, const Outgoing& item) {
  const std::uint64_t end = item.from_seq + item.rows.size();
  if (item.rows.empty() && sub.next_seq >= item.from_seq) {
    return;  // an empty batch clears only past a gap (protocol 1.1): for this view there is none
  }
  if (sub.next_seq <= item.from_seq) {
    sub.next_seq = end;
    enqueue(c, item.frame);
    return;
  }
  if (sub.next_seq >= end) {
    return;
  }
  // The view wants a later row than the batch's first: the batch's tail alone, encoded for it.
  protocol::Rows rows;
  rows.space = sub.port->description.info.id;
  rows.topic = sub.port->description.topics[sub.topic].id;
  rows.from_seq = sub.next_seq;
  rows.rows.assign(item.rows.begin() + static_cast<std::ptrdiff_t>(sub.next_seq - item.from_seq), item.rows.end());
  sub.next_seq = end;
  send_message(c, protocol::Message{std::move(rows)});
}

void Engine::send_replay(Connection& c, Subscription& sub, std::optional<std::uint64_t> since) {
  const TopicCache& cache = sub.port->topics[sub.topic];
  const std::uint64_t next = cache.first_seq + cache.rows.size();
  if (!cache.started) {
    sub.next_seq = since.value_or(0);
    return;
  }
  std::uint64_t start = cache.first_seq;
  std::uint64_t dropped = 0;
  if (since.has_value()) {
    if (*since > next) {
      sub.next_seq = *since;
      return;
    }
    if (*since > start) {
      start = *since;
    } else {
      dropped = start - *since;  // rows the ring no longer holds (§6.3)
    }
  }
  sub.next_seq = next;
  for (std::uint64_t from = start; from < next && c.phase == Connection::Phase::open;
       from += protocol::kRowsPerBatch) {
    const std::uint64_t to = std::min<std::uint64_t>(next, from + protocol::kRowsPerBatch);
    protocol::Rows rows;
    rows.space = sub.port->description.info.id;
    rows.topic = sub.port->description.topics[sub.topic].id;
    rows.from_seq = from;
    rows.dropped = from == start ? dropped : 0;
    rows.rows.assign(cache.rows.begin() + static_cast<std::ptrdiff_t>(from - cache.first_seq),
                     cache.rows.begin() + static_cast<std::ptrdiff_t>(to - cache.first_seq));
    if (!send_message(c, protocol::Message{std::move(rows)})) {
      return;
    }
  }
}

void Engine::send_states(Connection& c, Clock::time_point now) {
  for (std::size_t i = 0; i < c.subs.size() && c.phase == Connection::Phase::open; ++i) {
    Subscription& sub = c.subs[i];
    if (sub.stream) {
      continue;
    }
    const TopicCache& cache = sub.port->topics[sub.topic];
    if (!cache.has_state || (sub.sent && sub.sent_rev == cache.rev) ||
        (sub.sent && now - sub.sent_at < config_.timings.state_interval)) {
      continue;
    }
    sub.sent = true;
    sub.sent_rev = cache.rev;
    sub.sent_at = now;
    // A paired device never gets a report preview's shared text (D263): its own frame, where the state has one.
    enqueue(c, c.network && cache.device_frame != nullptr ? cache.device_frame : cache.frame);
  }
}

bool Engine::enqueue(Connection& c, std::shared_ptr<const std::string> bytes) noexcept {
  if (bytes == nullptr || c.phase == Connection::Phase::idle || c.phase == Connection::Phase::draining) {
    return false;
  }
  if (c.queued + bytes->size() > config_.send_queue) {
    // A tab that does not read is closed, not buffered for (item 22).
    log(LogLevel::info, "A browser tab stopped reading its WebSocket, which was closed.");
    drop(c);
    return false;
  }
  c.queued += bytes->size();
  c.queue.push_back(Chunk{std::move(bytes), 0});
  return true;
}

bool Engine::send_message(Connection& c, const protocol::Message& message) {
  return enqueue(c, encode_to_page(message));
}

void Engine::send_error(Connection& c, const PortState* space, protocol::ErrorCode code, std::string detail) {
  protocol::Error error;
  if (space != nullptr) {
    error.space = space->description.info.id;
  }
  error.code = code;
  if (detail.size() > kMaxDetail) {
    detail.resize(kMaxDetail);  // the details are ASCII: fixed words, ids and field names
  }
  error.detail = std::move(detail);
  send_message(c, protocol::Message{std::move(error)});
}

void Engine::send_result_error(Connection& c, const PortState& space, std::uint32_t id, protocol::ErrorCode code,
                               std::string detail) {
  protocol::Result result;
  result.space = space.description.info.id;
  result.id = id;
  result.error = code;
  if (!detail.empty()) {
    result.detail = std::move(detail);
  }
  send_message(c, protocol::Message{std::move(result)});
}

void Engine::close_socket(Connection& c, std::optional<protocol::ByeReason> bye, std::uint16_t code,
                          Clock::time_point now) {
  if ((c.phase != Connection::Phase::hello && c.phase != Connection::Phase::open) || !c.session) {
    return;
  }
  release(c);
  c.websocket = false;
  if (bye.has_value()) {
    if (!send_message(c, protocol::Message{protocol::Bye{*bye}})) {
      return;
    }
  }
  std::array<std::uint8_t, kMaxControlFrame> close{};
  const std::size_t size = c.session->start_close(code, close);
  auto frame = std::make_shared<std::string>();
  for (std::size_t i = 0; i < size; ++i) {
    frame->push_back(static_cast<char>(close[i]));
  }
  if (!enqueue(c, std::move(frame))) {
    return;
  }
  c.phase = Connection::Phase::closing;  // the page's Close, within the bounded wait (item 22)
  c.deadline = now + config_.timings.close_wait;
}

void Engine::release(Connection& c) noexcept {
  release_uploads(c);  // a POST's file let go, a session's ticket with it (G28)
  for (const Subscription& sub : c.subs) {
    if (!sub.port->registered.load()) {
      continue;
    }
    TopicCache& cache = sub.port->topics[sub.topic];
    if (cache.watchers != 0 && --cache.watchers == 0) {
      unwatch(*sub.port, sub.topic);
    }
  }
  c.subs.clear();
}

void Engine::unwatch(PortState& port, std::size_t topic) noexcept {
  const std::uint64_t generation = port.topics[topic].generation;  // a member's slot keeps whose it is (D215)
  port.topics[topic] = TopicCache{};
  port.topics[topic].generation = generation;
  set_watched(port, topic, false);
}

std::size_t Engine::take_member(PortState& port, std::string_view id) noexcept {
  try {
    std::lock_guard lock(port.mutex);
    if (const std::size_t held = port.member_slot_locked(id); held != npos) {
      port.topics[held].generation = port.members[held - port.declared].generation;
      return held;
    }
    // A free slot: never taken, or watched by nobody and its unwatch already told to the space, so that the space
    // hears each member's watch and unwatch under its own id.
    for (std::size_t i = 0; i < port.members.size(); ++i) {
      const std::size_t slot = port.declared + i;
      PortState::Member& member = port.members[i];
      if (member.id.empty() || (port.topics[slot].watchers == 0 && port.watched[slot] == 0 && port.reported[slot] == 0)) {
        member.id = std::string(id);
        member.generation = port.next_generation++;
        port.topics[slot] = TopicCache{};
        port.topics[slot].generation = member.generation;
        return slot;
      }
    }
  } catch (...) {
  }
  return npos;
}

void Engine::set_watched(PortState& port, std::size_t topic, bool on) noexcept {
  {
    std::lock_guard lock(port.mutex);
    port.watched[topic] = on ? 1 : 0;
  }
  if (port.wake) {
    try {
      port.wake();
    } catch (...) {
    }
  }
}

std::shared_ptr<PortState> Engine::space_of(const Connection& c, std::string_view id) const noexcept {
  for (const auto& port : c.spaces) {
    if (port->registered.load() && port->description.info.id == id) {
      return port;
    }
  }
  return nullptr;
}

bool Engine::take_token(Connection& c, Clock::time_point now) noexcept {
  // 20 a second (§6.8), as a bucket that holds 20: counted for every message, before it is parsed (item 22).
  const double per_window = static_cast<double>(protocol::kCommandsPerSecond);
  const double elapsed = std::chrono::duration<double>(now - c.refilled).count() /
                         std::chrono::duration<double>(config_.timings.rate_window).count();
  c.tokens = std::min(per_window, c.tokens + elapsed * per_window);
  c.refilled = now;
  if (c.tokens >= 1.0) {
    c.tokens -= 1.0;
    return true;
  }
  return false;
}

void Engine::ws_bytes(Connection& c, std::span<const std::uint8_t> in, Clock::time_point now) {
  std::size_t at = 0;
  while (at < in.size() && c.session && !c.final_send &&
         (c.phase == Connection::Phase::hello || c.phase == Connection::Phase::open ||
          c.phase == Connection::Phase::closing)) {
    std::size_t consumed = 0;
    std::size_t written = 0;
    std::array<std::uint8_t, kMaxControlFrame> out{};
    const Session::Step step = c.session->on_input(in.subspan(at), consumed, out, written);
    at += consumed;
    switch (step) {
      case Session::Step::need_more:
        return;
      case Session::Step::message:
        message(c, c.session->message_text(), c.session->message(), now);
        break;
      case Session::Step::send:
      case Session::Step::close: {
        auto frame = std::make_shared<std::string>();
        for (std::size_t i = 0; i < written; ++i) {
          frame->push_back(static_cast<char>(out[i]));
        }
        if (written != 0 && !enqueue(c, std::move(frame))) {
          return;
        }
        if (step == Session::Step::close) {
          // The page's Close (echoed) or a failure (answered with its code): send what is queued, then shut down.
          release(c);
          c.websocket = false;
          c.final_send = true;
          c.phase = Connection::Phase::closing;
          c.deadline = now + config_.timings.close_wait;
          if (c.queue.empty()) {
            begin_drain(c, now);
          }
          return;
        }
        break;
      }
    }
  }
}

void Engine::message(Connection& c, bool text, std::span<const std::uint8_t> payload, Clock::time_point now) {
  if (c.phase != Connection::Phase::hello && c.phase != Connection::Phase::open) {
    return;
  }
  if (!take_token(c, now)) {
    // Dropped unparsed; the page is told, at most once a window.
    if (!c.told_rate || now - c.rate_told >= config_.timings.rate_window) {
      c.told_rate = true;
      c.rate_told = now;
      send_error(c, nullptr, protocol::ErrorCode::rate_limited, "too many messages: some were dropped unread");
    }
    return;
  }
  protocol::Message parsed;
  protocol::Refusal refusal;
  bool ok = false;
  if (text) {
    text_.resize(payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i) {
      text_[i] = static_cast<char>(payload[i]);
    }
    ok = protocol::parse_frame(text_, protocol::Direction::from_page, parsed, refusal);
  } else {
    refusal.code = protocol::ErrorCode::bad_message;
    refusal.where.why = "a binary message: frames are JSON text";
  }
  if (!ok) {
    if (refusal.code == protocol::ErrorCode::version) {
      close_socket(c, protocol::ByeReason::version, 1000, now);  // §6.4
    } else if (c.phase == Connection::Phase::hello) {
      close_socket(c, protocol::ByeReason::unauthorised, 1008, now);
    } else {
      bad_message(c, refusal, now);
    }
    return;
  }
  if (c.phase == Connection::Phase::hello) {
    if (const auto* first = std::get_if<protocol::Hello>(&parsed); first != nullptr) {
      hello(c, *first, now);
    } else {
      close_socket(c, protocol::ByeReason::unauthorised, 1008, now);  // the first frame is the hello (item 4)
    }
    return;
  }
  if (const auto* sub = std::get_if<protocol::Sub>(&parsed)) {
    subscribe(c, *sub);
  } else if (const auto* unsub = std::get_if<protocol::Unsub>(&parsed)) {
    unsubscribe(c, *unsub);
  } else if (auto* cmd = std::get_if<protocol::Cmd>(&parsed)) {
    command(c, *cmd);
  } else if (auto* upload = std::get_if<protocol::Upload>(&parsed)) {
    upload_request(c, *upload, now);  // an upload's ticket (G28)
  } else if (std::holds_alternative<protocol::Ping>(parsed)) {
    send_message(c, protocol::Message{protocol::Pong{}});
  } else if (std::holds_alternative<protocol::Pong>(parsed)) {
    c.pings_out = 0;
  } else {
    protocol::Refusal second;
    second.where.field = "t";
    second.where.why = "a second hello";
    bad_message(c, second, now);
  }
}

void Engine::bad_message(Connection& c, const protocol::Refusal& refusal, Clock::time_point now) {
  ++c.bad;
  // The field's path and what was wrong with it: the schema's own words, never the value (§6.7).
  const std::string detail =
      refusal.where.field.empty() ? refusal.where.why : refusal.where.field + ": " + refusal.where.why;
  if (!c.logged_bad) {
    c.logged_bad = true;
    log(LogLevel::info, "A view sent a bad message (" + detail + ").");
  }
  send_error(c, nullptr, protocol::ErrorCode::bad_message, detail);
  if (c.bad >= protocol::kBadMessagesBeforeClose) {
    close_socket(c, std::nullopt, 1008, now);  // §6.7: after 3 the connection closes
  }
}

void Engine::hello(Connection& c, const protocol::Hello& hello, Clock::time_point now) {
  // Item 4: a ticket fetched with the cookie, used once, within its time; every held one compared in constant time.
  bool valid = false;
  if (hello.ticket.has_value() && valid_token(*hello.ticket)) {
    for (Ticket& ticket : tickets_) {
      const bool same =
          ticket.live && constant_time_equal(std::string_view(ticket.token.data(), ticket.token.size()), *hello.ticket);
      if (same) {
        // A ticket opens a WebSocket on the listener that issued it, and a device's only that device's (G9).
        const bool here = ticket.network == c.network && (!c.network || ticket.device == c.device);
        valid = here && now - ticket.issued < config_.timings.ticket;
        ticket = Ticket{};
      }
    }
  }
  if (!valid) {
    log(LogLevel::info, "A WebSocket's hello brought no valid ticket, and was closed.");
    close_socket(c, protocol::ByeReason::unauthorised, 1008, now);
    return;
  }
  // §6.4: the same major.
  const std::string_view version = hello.protocol;
  const std::size_t dot = version.find('.');
  if (version.substr(0, dot) != std::to_string(protocol::kProtocolMajor)) {
    close_socket(c, protocol::ByeReason::version, 1000, now);
    return;
  }
  std::array<std::uint8_t, kSessionBytes> bytes{};
  std::array<char, base64url_size(kSessionBytes)> session{};
  if (!io::random_bytes(bytes)) {
    close_socket(c, protocol::ByeReason::fault, 1011, now);
    return;
  }
  base64url_encode(bytes, session);
  protocol::Welcome welcome;
  welcome.protocol = std::string(protocol::kProtocolVersion);
  welcome.caps = config_.caps;
  welcome.graceui = config_.graceui_version;
  welcome.session = std::string(session.data(), session.size());
  // Which view this is (1.9, D226): this PC's browser, or a paired device, for what the page offers (a file's upload).
  welcome.view = c.network ? protocol::CommandView::device : protocol::CommandView::browser;
  {
    // The spaces this view may show: those registered now (D81).
    std::lock_guard lock(hub_->mutex);
    c.spaces = hub_->spaces;
  }
  for (const auto& port : c.spaces) {
    welcome.spaces.push_back(port->description.info);
  }
  if (!send_message(c, protocol::Message{std::move(welcome)})) {
    return;
  }
  c.phase = Connection::Phase::open;
  c.pings_out = 0;
  c.next_ping = now + config_.timings.ping;
  // §6.8: a third view, once its ticket is shown, supersedes the oldest; a device's never this PC's (G9).
  if (count_websockets(Connection::Phase::open) > kMaxWebSockets) {
    if (Connection* oldest = displaced(Connection::Phase::open, c); oldest != nullptr) {
      log(LogLevel::info, "A third WebSocket superseded the oldest.");
      close_socket(*oldest, protocol::ByeReason::superseded, 1000, now);
    } else {
      log(LogLevel::info, "A device's WebSocket was closed: this PC's views hold every place.");
      close_socket(c, protocol::ByeReason::superseded, 1000, now);
    }
  }
}

void Engine::subscribe(Connection& c, const protocol::Sub& sub) {
  const std::shared_ptr<PortState> port = space_of(c, sub.space);
  if (port == nullptr) {
    send_error(c, nullptr, protocol::ErrorCode::unknown_topic,
               numbered("sub", sub.id, "not a topic of a space this view may show"));
    return;
  }
  if (sub.topic == kPagesTopic && !port->description.pages.is_null() && port->topic_index(sub.topic) == npos) {
    // The framework's own topic (D166, D169): the space's page data, as it registered them, from the server itself.
    protocol::State state;
    state.space = port->description.info.id;
    state.topic = std::string(kPagesTopic);
    state.rev = 1;
    // A paired device's copy leaves out the pages on this PC alone (D264).
    state.body = c.network && !port->description.device_pages.is_null() ? port->description.device_pages
                                                                          : port->description.pages;
    send_message(c, state);
    return;
  }
  std::size_t topic = port->topic_index(sub.topic);
  if (topic != npos && port->description.topics[topic].family.value_or(false)) {
    send_error(c, port.get(), protocol::ErrorCode::unknown_topic,
               numbered("sub", sub.id, "a family: a view watches one of its members"));
    return;
  }
  if (c.network) {
    // On this PC alone (D264): a paired device's sub to a pc_only topic, or to a member of a pc_only family, refused.
    const std::string_view named = sub.topic;
    const std::size_t slash = named.rfind('/');
    std::size_t declared = topic;
    if (declared == npos && slash != std::string_view::npos && port->is_member(sub.topic)) {
      declared = port->topic_index(named.substr(0, slash));
    }
    if (declared != npos && declared < port->declared && port->description.topics[declared].pc_only.value_or(false)) {
      send_error(c, port.get(), protocol::ErrorCode::not_now, numbered("sub", sub.id, "Only on the PC the game runs on."));
      return;
    }
  }
  if (topic == npos && port->is_member(sub.topic)) {
    topic = take_member(*port, sub.topic);  // a family's member (D215): its slot
    if (topic == npos) {
      send_error(c, port.get(), protocol::ErrorCode::not_now,
                 numbered("sub", sub.id, "as many of this space's members are watched as may be"));
      return;
    }
  }
  if (topic == npos) {
    send_error(c, port.get(), protocol::ErrorCode::unknown_topic, numbered("sub", sub.id, "not a topic of this space"));
    return;
  }
  const bool stream = topic < port->declared && port->description.topics[topic].shape == protocol::TopicShape::stream;
  for (Subscription& held : c.subs) {
    if (held.port == port && held.topic == topic) {
      // Watched already: the newest state again, or the stream from where the view now asks.
      if (stream) {
        send_replay(c, held, sub.since);
      } else {
        held.sent = false;
        held.sent_at = Clock::time_point{};
      }
      return;
    }
  }
  if (c.subs.size() >= protocol::kSubscriptions) {
    send_error(c, port.get(), protocol::ErrorCode::not_now,
               numbered("sub", sub.id, "this view watches 32 topics already"));
    return;
  }
  Subscription added;
  added.port = port;
  added.topic = topic;
  added.stream = stream;
  if (port->topics[topic].watchers++ == 0) {
    set_watched(*port, topic, true);
  }
  c.subs.push_back(std::move(added));
  if (stream) {
    send_replay(c, c.subs.back(), sub.since);
  }
}

void Engine::unsubscribe(Connection& c, const protocol::Unsub& unsub) {
  const std::shared_ptr<PortState> port = space_of(c, unsub.space);
  if (port != nullptr && unsub.topic == kPagesTopic && !port->description.pages.is_null() &&
      port->topic_index(unsub.topic) == npos) {
    return;  // the framework's own topic: sent once, nothing held
  }
  std::size_t topic = port != nullptr ? port->topic_index(unsub.topic) : npos;
  if (port != nullptr && topic == npos && port->is_member(unsub.topic)) {
    std::lock_guard lock(port->mutex);
    topic = port->member_slot_locked(unsub.topic);
  }
  if (topic == npos) {
    send_error(c, port.get(), protocol::ErrorCode::unknown_topic,
               numbered("unsub", unsub.id,
                        port == nullptr ? "not a topic of a space this view may show" : "not a topic of this space"));
    return;
  }
  for (auto it = c.subs.begin(); it != c.subs.end(); ++it) {
    if (it->port == port && it->topic == topic) {
      c.subs.erase(it);
      TopicCache& cache = port->topics[topic];
      if (cache.watchers != 0 && --cache.watchers == 0) {
        unwatch(*port, topic);
      }
      return;
    }
  }
}

void Engine::command(Connection& c, protocol::Cmd& cmd) {
  const std::shared_ptr<PortState> port = space_of(c, cmd.space);
  if (port == nullptr) {
    send_error(c, nullptr, protocol::ErrorCode::unknown_command,
               numbered("cmd", cmd.id, "not a command of a space this view may show"));
    return;
  }
  if (!port->has_command(cmd.name)) {
    send_result_error(c, *port, cmd.id, protocol::ErrorCode::unknown_command);
    return;
  }
  if (c.network && sensitive_command(port->description, cmd.name, cmd.args)) {
    // A paired device never sets a secret or a provider, nor what its space marks sensitive (§7.2 item 16, D81): told
    // so before any confirmation is asked of it, which would change nothing.
    send_result_error(c, *port, cmd.id, protocol::ErrorCode::not_now, "Only on the PC the game runs on.");
    return;
  }
  if (file_command(port->description, cmd.name)) {
    // A file command comes with its file, by the upload route alone (D226).
    send_result_error(c, *port, cmd.id, protocol::ErrorCode::invalid_value, "This takes a file: add it with its button.");
    return;
  }
  if (!confirmed(port->description, cmd.name, cmd.args)) {
    send_result_error(c, *port, cmd.id, protocol::ErrorCode::confirm_required);  // §7.3, D173
    return;
  }
  bool full = false;
  {
    std::lock_guard lock(port->mutex);
    if (port->commands.size() >= protocol::kCommandQueue) {
      full = true;  // §6.8: the newest is dropped, and nothing waits
    } else {
      SpaceEvent event;
      event.kind = SpaceEvent::Kind::command;
      event.command = cmd.name;
      event.args = std::move(cmd.args);
      event.view = c.network ? protocol::CommandView::device : protocol::CommandView::browser;  // never the view's word
      event.reply = CommandRef{c.serial, cmd.id,
                               std::find(port->description.secret.begin(), port->description.secret.end(), cmd.name) !=
                                   port->description.secret.end()};
      port->commands.push_back(std::move(event));
    }
  }
  if (full) {
    send_result_error(c, *port, cmd.id, protocol::ErrorCode::rate_limited);
    const Clock::time_point now = Clock::now();
    if (port->full_logged == Clock::time_point{} || now - port->full_logged >= std::chrono::seconds(1)) {
      port->full_logged = now;
      log(LogLevel::warning, "A space's command queue is full (" + port->description.info.id +
                                 "): the newest command was dropped.");
    }
    return;
  }
  if (port->wake) {
    try {
      port->wake();
    } catch (...) {
    }
  }
}

}  // namespace grace::serve::detail
