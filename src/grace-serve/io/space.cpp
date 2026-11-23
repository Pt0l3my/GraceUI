// space.cpp: SpacePort (space.h), on the space's own thread: its events from the I/O thread, and what it publishes and
// answers, encoded here once as WebSocket text frames, so that the I/O thread only routes bytes (D55).
#include <algorithm>
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

bool PortState::is_member(std::string_view id) const noexcept { return family_member(description, id); }

std::size_t PortState::member_slot_locked(std::string_view id) const noexcept {
  for (std::size_t i = 0; i < members.size(); ++i) {
    if (members[i].id == id) {
      return declared + i;
    }
  }
  return npos;
}

std::string_view PortState::topic_id_locked(std::size_t index) const noexcept {
  return index < declared ? std::string_view(description.topics[index].id)
                          : std::string_view(members[index - declared].id);
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

bool family_member(const SpaceDescription& description, std::string_view topic) noexcept {
  const std::size_t slash = topic.rfind('/');
  if (slash == std::string_view::npos || slash == 0 || slash + 1 >= topic.size()) {
    return false;
  }
  const std::string_view family = topic.substr(0, slash);
  for (const protocol::TopicInfo& t : description.topics) {
    if (t.id == family) {
      return t.family.value_or(false) && t.shape == protocol::TopicShape::state;
    }
  }
  return false;
}

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
  for (const std::string& name : pages.slow_commands.value_or(std::vector<std::string>{})) {
    if (std::find(pages.commands.begin(), pages.commands.end(), name) != pages.commands.end()) {
      description.slow.push_back(name);  // a declared command's alone (D216)
    }
  }
  const auto add = [](std::vector<std::string>& list, const std::string& command) {
    if (std::find(list.begin(), list.end(), command) == list.end()) {
      list.push_back(command);
    }
  };
  const auto blocks_of = [&](const std::vector<protocol::PageSection>& sections) {
    for (const protocol::PageSection& section : sections) {
      for (const protocol::PageAction& action : section.actions.value_or(std::vector<protocol::PageAction>{})) {
        if (action.tone == protocol::ActionTone::destructive) {
          add(description.destructive, action.command);  // a section's own action (D202)
        }
      }
      for (const auto* blocks : {&section.blocks, section.advanced ? &*section.advanced : nullptr}) {
        if (blocks == nullptr) {
          continue;
        }
        for (const protocol::PageBlock& block : *blocks) {
          for (const protocol::RowAction& action : block.row_actions.value_or(std::vector<protocol::RowAction>{})) {
            if (action.tone == protocol::ActionTone::destructive) {
              add(description.destructive, action.command);
            }
          }
          if (block.row_secret) {
            add(description.secret, block.row_secret->command);
          }
        }
      }
    }
  };
  for (const protocol::Page& page : pages.pages) {
    for (const protocol::PageAction& action : page.actions.value_or(std::vector<protocol::PageAction>{})) {
      if (action.tone == protocol::ActionTone::destructive) {
        add(description.destructive, action.command);
      }
    }
    blocks_of(page.sections.value_or(std::vector<protocol::PageSection>{}));
    for (const protocol::PageTab& tab : page.tabs.value_or(std::vector<protocol::PageTab>{})) {
      blocks_of(tab.sections);
    }
  }
  for (const std::string& name : pages.sensitive_commands.value_or(std::vector<std::string>{})) {
    if (std::find(pages.commands.begin(), pages.commands.end(), name) != pages.commands.end()) {
      add(description.sensitive, name);  // a declared command's alone (D221)
    }
  }
  for (const protocol::Setting& setting : settings.settings) {
    if (setting.sensitive.value_or(false) || setting.type == protocol::SettingType::secret) {
      description.sensitive_keys.push_back({setting.key, setting.home});  // a secret is never set from a device
    }
  }
  description.pages = protocol::encode(pages);
  return description;
}

bool confirmed(const SpaceDescription& description, std::string_view command, const protocol::Json& args) noexcept {
  if (std::find(description.destructive.begin(), description.destructive.end(), command) ==
      description.destructive.end()) {
    return true;
  }
  const auto it = args.is_object() ? args.find("confirm") : args.end();
  return args.is_object() && it != args.end() && it->is_boolean() && it->get<bool>();
}

bool sensitive_command(const SpaceDescription& description, std::string_view command,
                       const protocol::Json& args) noexcept {
  const auto listed = [&](const std::vector<std::string>& list) {
    return std::find(list.begin(), list.end(), command) != list.end();
  };
  if (listed(description.sensitive) || listed(description.secret)) {
    return true;
  }
  const auto text = [&](const char* name) -> std::string_view {
    const auto it = args.is_object() ? args.find(name) : args.end();
    return args.is_object() && it != args.end() && it->is_string() ? std::string_view(it->get_ref<const std::string&>())
                                                                   : std::string_view();
  };
  const auto any = [&](auto&& match) {
    return std::any_of(description.sensitive_keys.begin(), description.sensitive_keys.end(), match);
  };
  if (command == "settings.set") {
    return any([&](const SpaceDescription::SensitiveKey& k) { return k.key == text("key"); });
  }
  if (command == "settings.reset") {
    const std::string_view scope = text("scope");
    const std::string_view target = text("target");
    if (scope == "key") {
      return any([&](const SpaceDescription::SensitiveKey& k) { return k.key == target; });
    }
    if (scope == "page") {
      return any([&](const SpaceDescription::SensitiveKey& k) { return k.home == target; });
    }
    return !description.sensitive_keys.empty();  // all, or a scope the space will refuse anyway
  }
  return false;
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
        event.topic = std::string(state_->topic_id_locked(i));  // a member's slot keeps its id until this is said
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
      if (out.topic == detail::npos && state_->is_member(state->topic)) {
        // A family's member (D215): to its slot while a view watches it, else nowhere (as for any unwatched topic).
        std::lock_guard lock(state_->mutex);
        out.topic = state_->member_slot_locked(state->topic);
        if (out.topic == detail::npos || state_->watched[out.topic] == 0) {
          return true;
        }
        out.generation = state_->members[out.topic - state_->declared].generation;
      } else if (out.topic == detail::npos ||
                 state_->description.topics[out.topic].shape != protocol::TopicShape::state ||
                 state_->description.topics[out.topic].family.value_or(false)) {
        return false;  // not a topic of the space's; or a family itself, whose members alone are published
      }
      state->space = state_->description.info.id;
      out.kind = detail::Outgoing::Kind::state;
      out.rev = state->rev;
    } else if (auto* rows = std::get_if<protocol::Rows>(&message)) {
      out.topic = state_->topic_index(rows->topic);
      // An empty batch is a stream's clearing, past a gap (protocol 1.1).
      if (out.topic == detail::npos || state_->description.topics[out.topic].shape != protocol::TopicShape::stream ||
          rows->rows.size() > protocol::kRowsPerBatch) {
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
    if (reply.secret) {
      result.body.reset();  // a row secret's result is ok or a refusal's words, never a body (D194, invariant 5)
    }
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
