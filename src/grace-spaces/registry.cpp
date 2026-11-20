// SPDX-License-Identifier: MIT
// registry.cpp: the mods' spaces across the ABI (registry.h, D169).
#include "grace/spaces/registry.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>
#include <variant>

#include "grace/pages/settings_store.h"
#include "grace/protocol/frame.h"
#include "grace/view/page_view.h"

namespace grace::spaces {

struct Registry::Space {
  std::uint64_t owner = 0;
  std::string id;
  std::uint32_t views = 0;
  serve::SpaceDescription description;
  std::shared_ptr<serve::SpacePort> port;  // while it is a browser space and a server is attached
  std::string held_frame;                  // an event the mod's buffer was too small for, kept for its next poll
  EventKind held_kind = EventKind::none;
  std::uint64_t held_reply = 0;
};

struct Registry::Pending {
  std::uint64_t reply = 0;
  std::uint64_t owner = 0;
  serve::CommandRef ref;
  std::chrono::steady_clock::time_point deadline;
};

namespace {

bool view_flags(std::uint32_t views) noexcept { return views != 0 && (views & ~(kViewGame | kViewBrowser)) == 0; }

template <class T>
bool decode_json(std::string_view text, T& out, std::string& why, const char* what) {
  protocol::Json json = protocol::Json::parse(text.begin(), text.end(), nullptr, false);
  protocol::DecodeError error;
  if (json.is_discarded() || !json.is_object()) {
    why = std::string(what) + " is not a JSON object";
    return false;
  }
  if (!decode(json, out, "", error)) {
    why = std::string(what) + ": " + error.field + ": " + error.why;
    return false;
  }
  return true;
}

}  // namespace

Registry::Registry(Log log) : log_(std::move(log)) {}
Registry::~Registry() = default;

Registry::Space* Registry::find(std::uint64_t owner) noexcept {
  for (const auto& space : spaces_) {
    if (space->owner == owner) {
      return space.get();
    }
  }
  return nullptr;
}

const Registry::Space* Registry::find(std::uint64_t owner) const noexcept {
  for (const auto& space : spaces_) {
    if (space->owner == owner) {
      return space.get();
    }
  }
  return nullptr;
}

Status Registry::add(std::uint64_t owner, std::string_view mod_id, const Registration& in, std::string& why) {
  if (!view_flags(in.views)) {
    why = "its views are neither the game's nor the browser's";
    return Status::refused;
  }
  if (in.version.empty() || in.increment.empty() || in.increment.size() > 16) {
    why = "no version or increment";
    return Status::refused;
  }
  protocol::SpacePages pages;
  protocol::SettingsDescription settings;
  if (!decode_json(in.pages, pages, why, "its page data") ||
      !decode_json(in.settings, settings, why, "its settings description")) {
    return Status::refused;
  }
  if (pages.space != mod_id) {
    why = "its page data names another space than the mod's own id";
    return Status::refused;
  }
  pages::SettingsStore store;  // every default valid, no key twice
  view::PageView view;         // pages and settings agree: every key on a page is described, every page is known
  if (!store.load(settings, why) || !view.load(pages, settings, why)) {
    return Status::refused;
  }
  auto space = std::make_unique<Space>();
  space->owner = owner;
  space->id = std::string(mod_id);
  space->views = in.views;
  space->description = serve::describe_space(pages, settings, in.version, in.increment);

  std::lock_guard<std::mutex> lock(mutex_);
  if (find(owner) != nullptr) {
    why = "the mod has a space already";
    return Status::duplicate;
  }
  for (const auto& other : spaces_) {
    if (other->id == space->id) {
      why = "another mod's space has this id";
      return Status::duplicate;
    }
  }
  if (spaces_.size() >= kMaxSpaces) {
    why = "the session holds as many spaces as it may";
    return Status::full;
  }
  if (server_ != nullptr && (space->views & kViewBrowser) != 0) {
    space->port = server_->add_space(space->description);
  }
  spaces_.push_back(std::move(space));
  return Status::ok;
}

void Registry::remove_owner(std::uint64_t owner) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = std::find_if(spaces_.begin(), spaces_.end(), [&](const auto& s) { return s->owner == owner; });
  if (it == spaces_.end()) {
    return;
  }
  if (server_ != nullptr && (*it)->port != nullptr) {
    server_->remove_space((*it)->id);
  }
  spaces_.erase(it);
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.owner == owner; }),
                 pending_.end());
}

bool Registry::wants_browser() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::any_of(spaces_.begin(), spaces_.end(), [](const auto& s) { return (s->views & kViewBrowser) != 0; });
}

std::size_t Registry::count() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return spaces_.size();
}

void Registry::attach(serve::Server& server) {
  std::lock_guard<std::mutex> lock(mutex_);
  server_ = &server;
  for (const auto& space : spaces_) {
    if ((space->views & kViewBrowser) != 0 && space->port == nullptr) {
      space->port = server.add_space(space->description);
    }
  }
}

Status Registry::publish(std::uint64_t owner, std::string_view frame, std::string& why) {
  protocol::Message message;
  protocol::Refusal refusal;
  if (!protocol::parse_frame(frame, protocol::Direction::to_page, message, refusal)) {
    why = refusal.where.field.empty() ? "not a frame of the protocol's" : "its " + refusal.where.field + " fails";
    return Status::refused;
  }
  const std::string* space_id = nullptr;
  const std::string* topic = nullptr;
  protocol::TopicShape shape{};
  if (auto* state = std::get_if<protocol::State>(&message)) {
    space_id = &state->space;
    topic = &state->topic;
    shape = protocol::TopicShape::state;
  } else if (auto* rows = std::get_if<protocol::Rows>(&message)) {
    space_id = &rows->space;
    topic = &rows->topic;
    shape = protocol::TopicShape::stream;
  } else {
    why = "not a state or rows frame";
    return Status::refused;
  }
  std::shared_ptr<serve::SpacePort> port;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const Space* space = find(owner);
    if (space == nullptr) {
      why = "the mod has no space";
      return Status::refused;
    }
    if (*space_id != space->id) {
      why = "the frame names another space";
      return Status::refused;
    }
    const auto& topics = space->description.topics;
    const auto declared = std::find_if(topics.begin(), topics.end(), [&](const protocol::TopicInfo& t) {
      return t.id == *topic;
    });
    if (declared == topics.end() || declared->shape != shape) {
      why = declared == topics.end() ? "not a topic the space declares" : "not the topic's shape";
      return Status::refused;
    }
    port = space->port;
  }
  if (port != nullptr && !port->publish(std::move(message))) {
    why = "past the frame's limit";
    return Status::refused;
  }
  return Status::ok;
}

Status Registry::poll(std::uint64_t owner, char* buffer, std::uint64_t capacity, EventKind& kind,
                      std::uint64_t& reply, std::uint64_t& length) {
  kind = EventKind::none;
  reply = 0;
  length = 0;
  std::lock_guard<std::mutex> lock(mutex_);
  Space* space = find(owner);
  if (space == nullptr) {
    return Status::ok;
  }
  if (space->held_kind == EventKind::none) {
    serve::SpaceEvent event;
    if (space->port == nullptr || !space->port->poll(event)) {
      return Status::ok;
    }
    protocol::Message message;
    switch (event.kind) {
      case serve::SpaceEvent::Kind::watched:
        message = protocol::Sub{space->id, 0, event.topic, std::nullopt};
        space->held_kind = EventKind::watched;
        break;
      case serve::SpaceEvent::Kind::unwatched:
        message = protocol::Unsub{space->id, 0, event.topic};
        space->held_kind = EventKind::unwatched;
        break;
      case serve::SpaceEvent::Kind::command: {
        if (pending_.size() >= kMaxPending) {
          protocol::Result busy;
          busy.space = space->id;
          busy.id = event.reply.id;
          busy.error = protocol::ErrorCode::busy;
          space->port->answer(event.reply, std::move(busy));
          return Status::ok;
        }
        message = protocol::Cmd{space->id, event.reply.id, event.command, std::move(event.args)};
        space->held_kind = EventKind::command;
        space->held_reply = next_reply_++;
        pending_.push_back(Pending{space->held_reply, owner, event.reply,
                                   std::chrono::steady_clock::now() + kAnswerDeadline});
        break;
      }
      case serve::SpaceEvent::Kind::none:
        return Status::ok;
    }
    space->held_frame.clear();
    protocol::encode_text(message, space->held_frame);
  }
  length = space->held_frame.size();
  kind = space->held_kind;
  reply = space->held_reply;
  if (buffer == nullptr || capacity < length) {
    return Status::full;
  }
  std::memcpy(buffer, space->held_frame.data(), space->held_frame.size());
  space->held_kind = EventKind::none;
  space->held_reply = 0;
  space->held_frame.clear();
  return Status::ok;
}

Status Registry::answer(std::uint64_t owner, std::uint64_t reply, std::string_view frame, std::string& why) {
  protocol::Message message;
  protocol::Refusal refusal;
  if (!protocol::parse_frame(frame, protocol::Direction::to_page, message, refusal)) {
    why = refusal.where.field.empty() ? "not a frame of the protocol's" : "its " + refusal.where.field + " fails";
    return Status::refused;
  }
  auto* result = std::get_if<protocol::Result>(&message);
  if (result == nullptr || result->ok.has_value() == result->error.has_value()) {
    why = "not a result frame with ok or an error";
    return Status::refused;
  }
  std::shared_ptr<serve::SpacePort> port;
  serve::CommandRef ref;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = std::find_if(pending_.begin(), pending_.end(), [&](const Pending& p) {
      return p.reply == reply && p.owner == owner;
    });
    const Space* space = find(owner);
    if (it == pending_.end() || space == nullptr) {
      why = "no command of the mod's waits for this reply";
      return Status::refused;
    }
    ref = it->ref;
    pending_.erase(it);
    port = space->port;
    result->space = space->id;
    result->id = ref.id;
  }
  if (port != nullptr) {
    port->answer(ref, std::move(*result));
  }
  return Status::ok;
}

std::size_t Registry::expire(std::chrono::steady_clock::time_point now) {
  std::vector<std::pair<std::shared_ptr<serve::SpacePort>, Pending>> late;
  std::vector<std::string> names;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pending_.begin(); it != pending_.end();) {
      if (it->deadline > now) {
        ++it;
        continue;
      }
      const Space* space = find(it->owner);
      late.emplace_back(space != nullptr ? space->port : nullptr, *it);
      names.push_back(space != nullptr ? space->id : std::string());
      it = pending_.erase(it);
    }
  }
  for (std::size_t i = 0; i < late.size(); ++i) {
    if (late[i].first != nullptr) {
      protocol::Result result;
      result.space = names[i];
      result.id = late[i].second.ref.id;
      result.error = protocol::ErrorCode::internal;
      late[i].first->answer(late[i].second.ref, std::move(result));
    }
    if (log_) {
      log_("space " + names[i] + " left a command unanswered for 10 s: answered internal");
    }
  }
  return late.size();
}

std::size_t Registry::pending() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.size();
}

}  // namespace grace::spaces
