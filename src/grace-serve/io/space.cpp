// space.cpp: SpacePort (space.h), on the space's own thread: its events from the I/O thread, and what it publishes and
// answers, encoded here once as WebSocket text frames, so that the I/O thread only routes bytes (D55).
#include <utility>

#include "grace/protocol/frame.h"
#include "grace/serve/websocket.h"
#include "internal.h"

namespace grace::serve {
namespace detail {

std::size_t PortState::topic_index(std::string_view id) const noexcept {
  for (std::size_t i = 0; i < description.topics.size(); ++i) {
    if (description.topics[i].id == id) {
      return i;
    }
  }
  return npos;
}

bool PortState::has_command(std::string_view name) const noexcept {
  for (const std::string& command : description.commands) {
    if (command == name) {
      return true;
    }
  }
  return false;
}

std::shared_ptr<const std::string> ws_text_frame(std::string_view json) {
  std::array<std::uint8_t, 10> header{};
  const std::size_t size = encode_frame_header(Opcode::text, json.size(), header);
  auto frame = std::make_shared<std::string>();
  frame->reserve(size + json.size());
  for (std::size_t i = 0; i < size; ++i) {
    frame->push_back(static_cast<char>(header[i]));
  }
  frame->append(json);
  return frame;
}

std::shared_ptr<const std::string> encode_to_page(const protocol::Message& message) {
  std::string json;
  if (!protocol::encode_frame(message, protocol::Direction::to_page, json)) {
    return nullptr;
  }
  return ws_text_frame(json);
}

}  // namespace detail

SpaceDescription describe_space(const protocol::SpacePages& pages, const protocol::SettingsDescription& settings,
                                std::string version, std::string increment) {
  SpaceDescription description;
  description.info.id = pages.space;
  description.info.name = pages.name;
  description.info.version = std::move(version);
  description.info.increment = std::move(increment);
  description.info.settings_schema = settings.schema;
  description.topics = pages.topics;
  description.commands = pages.commands;
  description.pages = protocol::encode(pages);
  return description;
}

SpacePort::SpacePort(std::shared_ptr<detail::Hub> hub, std::shared_ptr<detail::PortState> state) noexcept
    : hub_(std::move(hub)), state_(std::move(state)) {}

SpacePort::~SpacePort() = default;

const SpaceDescription& SpacePort::description() const noexcept { return state_->description; }

bool SpacePort::registered() const noexcept { return state_->registered.load(); }

bool SpacePort::poll(SpaceEvent& event) noexcept {
  try {
    event = SpaceEvent{};
    std::lock_guard lock(state_->mutex);
    for (std::size_t i = 0; i < state_->watched.size(); ++i) {
      if (state_->watched[i] != state_->reported[i]) {
        state_->reported[i] = state_->watched[i];
        event.kind = state_->watched[i] != 0 ? SpaceEvent::Kind::watched : SpaceEvent::Kind::unwatched;
        event.topic = state_->description.topics[i].id;
        return true;
      }
    }
    if (!state_->commands.empty()) {
      event = std::move(state_->commands.front());
      state_->commands.pop_front();
      return true;
    }
    return false;
  } catch (...) {
    event = SpaceEvent{};
    return false;
  }
}

bool SpacePort::publish(protocol::Message message) noexcept {
  try {
    detail::Outgoing out;
    out.port = state_;
    if (auto* state = std::get_if<protocol::State>(&message)) {
      out.topic = state_->topic_index(state->topic);
      if (out.topic == detail::npos || state_->description.topics[out.topic].shape != protocol::TopicShape::state) {
        return false;
      }
      state->space = state_->description.info.id;
      out.kind = detail::Outgoing::Kind::state;
      out.rev = state->rev;
    } else if (auto* rows = std::get_if<protocol::Rows>(&message)) {
      out.topic = state_->topic_index(rows->topic);
      if (out.topic == detail::npos || state_->description.topics[out.topic].shape != protocol::TopicShape::stream ||
          rows->rows.empty() || rows->rows.size() > protocol::kRowsPerBatch) {
        return false;
      }
      for (const protocol::Json& row : rows->rows) {
        if (!row.is_object()) {
          return false;
        }
      }
      rows->space = state_->description.info.id;
      out.kind = detail::Outgoing::Kind::rows;
      out.from_seq = rows->from_seq;
      out.dropped = rows->dropped;
    } else {
      return false;
    }
    out.frame = detail::encode_to_page(message);
    if (out.frame == nullptr) {
      return false;
    }
    if (auto* rows = std::get_if<protocol::Rows>(&message)) {
      out.rows = std::move(rows->rows);
    }
    std::lock_guard lock(hub_->mutex);
    if (!registered()) {
      return false;
    }
    if (!hub_->running) {
      return true;  // nothing watches anything while the server is off
    }
    if (out.kind == detail::Outgoing::Kind::state) {
      // Only the newest state of a topic is worth sending (§2.4).
      for (detail::Outgoing& waiting : hub_->inbox) {
        if (waiting.kind == out.kind && waiting.port == out.port && waiting.topic == out.topic) {
          waiting = std::move(out);
          hub_->signal_locked();
          return true;
        }
      }
    }
    if (hub_->inbox.size() >= detail::kMaxInbox) {
      return false;
    }
    hub_->inbox.push_back(std::move(out));
    hub_->signal_locked();
    return true;
  } catch (...) {
    return false;
  }
}

bool SpacePort::answer(const CommandRef& reply, protocol::Result result) noexcept {
  try {
    const bool ok = result.ok.value_or(false);
    if (ok == result.error.has_value() || (result.ok.has_value() && !ok)) {
      return false;  // ok: true, or an error: exactly one (§6.3)
    }
    result.space = state_->description.info.id;
    result.id = reply.id;
    detail::Outgoing out;
    out.kind = detail::Outgoing::Kind::result;
    out.port = state_;
    out.reply = reply;
    out.frame = detail::encode_to_page(protocol::Message{std::move(result)});
    if (out.frame == nullptr) {
      return false;
    }
    std::lock_guard lock(hub_->mutex);
    if (!registered()) {
      return false;
    }
    if (!hub_->running) {
      return true;
    }
    if (hub_->inbox.size() >= detail::kMaxInbox) {
      return false;
    }
    hub_->inbox.push_back(std::move(out));
    hub_->signal_locked();
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace grace::serve
