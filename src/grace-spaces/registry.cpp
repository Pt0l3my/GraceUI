// SPDX-License-Identifier: MIT
// registry.cpp: the mods' spaces across the ABI (registry.h, D169).
#include "grace/spaces/registry.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <set>
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
  std::shared_ptr<const protocol::SpacePages> pages;
  std::shared_ptr<const protocol::SettingsDescription> settings;
  std::shared_ptr<serve::SpacePort> port;  // while it is a browser space and a server is attached
  // The in-game view's side (D171): what the browser's views and the menu watch, the menu's events for the mod, the
  // newest state of each topic the menu watches.
  std::set<std::string, std::less<>> browser_watched;
  std::set<std::string, std::less<>> game_watched;
  struct Internal {
    EventKind kind;
    std::string frame;
    std::uint64_t reply;
  };
  std::deque<Internal> internal;
  std::map<std::string, std::pair<std::uint64_t, protocol::Json>, std::less<>> game_states;
  std::string held_frame;                  // an event the mod's buffer was too small for, kept for its next poll
  EventKind held_kind = EventKind::none;
  std::uint64_t held_reply = 0;
  protocol::Json described;                // its settings description as it registered it, encoded once
  bool secret_masked = false;              // a secret's value was masked once already (logged once)
};

struct Registry::Pending {
  std::uint64_t reply = 0;
  std::uint64_t owner = 0;
  serve::CommandRef ref;
  std::chrono::steady_clock::time_point deadline;
  bool game = false;    // the menu's: its result waits in game_results_
  std::uint32_t id = 0; // the command's id, as its view numbered it
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
  if (mod_id == "graceui") {
    why = "graceui is GraceUI's own space's id";  // one space an id, in both views (invariant 3, D81)
    return Status::refused;
  }
  for (const protocol::TopicInfo& topic : pages.topics) {
    if (topic.id == "pages") {
      why = "pages is the framework's own topic";  // the server sends a space's page data as it registered them (D169)
      return Status::refused;
    }
  }
  for (const protocol::Page& page : pages.pages) {
    for (const protocol::PageAction& action : page.actions.value_or(std::vector<protocol::PageAction>{})) {
      if (action.tone == protocol::ActionTone::destructive && (!action.confirm || action.confirm->empty())) {
        // The confirmation is the framework's (§7.3, D173): its words are what the second press shows.
        why = "a destructive action without its confirm text";
        return Status::refused;
      }
    }
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
  space->pages = std::make_shared<const protocol::SpacePages>(std::move(pages));
  space->described = protocol::encode(settings);
  space->settings = std::make_shared<const protocol::SettingsDescription>(std::move(settings));

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
    if (space->port == nullptr) {
      why = "the browser view's server does not take it";
      return Status::refused;
    }
  }
  const bool game = (space->views & kViewGame) != 0;
  spaces_.push_back(std::move(space));
  if (game) {
    ++spaces_revision_;
  }
  return Status::ok;
}

void Registry::remove_owner(std::uint64_t owner) noexcept {
  bool game = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = std::find_if(spaces_.begin(), spaces_.end(), [&](const auto& s) { return s->owner == owner; });
    if (it == spaces_.end()) {
      return;
    }
    if (server_ != nullptr && (*it)->port != nullptr) {
      server_->remove_space((*it)->id);
    }
    game = ((*it)->views & kViewGame) != 0;
    if (game) {
      ++spaces_revision_;
    }
    spaces_.erase(it);
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.owner == owner; }),
                   pending_.end());
  }
  if (game) {
    wake_game();
  }
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
  bool for_game = false;
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
    if (shape == protocol::TopicShape::state && *topic == "settings") {
      // The settings topic is held to what the space registered (invariants 5 and 12, as framework guards): its
      // description, if it sends one, is the registered one, and a secret's value never leaves: only whether it is set.
      protocol::Json& body = std::get<protocol::State>(message).body;
      if (!body.is_object()) {
        why = "the settings topic's body is not an object";
        return Status::refused;
      }
      if (body.contains("schema") || body.contains("description")) {
        // Compared as decoded, so that two encodings of one description (an optional field left out) are one.
        protocol::Json sent = protocol::Json::object();
        sent["schema"] = body.contains("schema") ? body["schema"] : space->described["schema"];
        sent["settings"] = body.contains("description") ? body["description"] : space->described["settings"];
        protocol::SettingsDescription decoded;
        protocol::DecodeError error;
        if (!protocol::decode(sent, decoded, "", error) || !(decoded == *space->settings)) {
          why = "its settings description is not the one it registered";
          return Status::refused;
        }
      }
      bool masked = false;
      if (body.contains("values") && body["values"].is_object()) {
        protocol::Json& values = body["values"];
        for (const protocol::Setting& setting : space->settings->settings) {
          const auto found = values.find(setting.key);
          if (setting.type != protocol::SettingType::secret || found == values.end()) {
            continue;
          }
          const bool empty = found->is_null() || (found->is_string() && found->get_ref<const std::string&>().empty());
          const protocol::Json shown = empty ? protocol::Json("") : protocol::Json("set");
          if (*found != shown) {
            *found = shown;
            masked = true;
          }
        }
      }
      if (masked) {
        Space* writable = find(owner);
        if (!writable->secret_masked) {
          writable->secret_masked = true;
          if (log_) {
            log_("space " + space->id + " published a secret's value: only whether it is set was sent");
          }
        }
      }
    }
    port = space->port;
    if (shape == protocol::TopicShape::state && space->game_watched.count(*topic) != 0) {
      const auto& state = std::get<protocol::State>(message);
      Space* writable = find(owner);
      writable->game_states[*topic] = {state.rev, state.body};  // the menu's copy, the newest
      for_game = true;
    }
  }
  if (for_game) {
    wake_game();
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
  if (space->held_kind == EventKind::none && !space->internal.empty()) {
    // The menu's events first (D171): already a frame, with its reply handle.
    Space::Internal next = std::move(space->internal.front());
    space->internal.pop_front();
    space->held_kind = next.kind;
    space->held_frame = std::move(next.frame);
    space->held_reply = next.reply;
  }
  while (space->held_kind == EventKind::none) {
    serve::SpaceEvent event;
    if (space->port == nullptr || !space->port->poll(event)) {
      return Status::ok;
    }
    protocol::Message message;
    switch (event.kind) {
      case serve::SpaceEvent::Kind::watched:
        space->browser_watched.insert(event.topic);
        if (space->game_watched.count(event.topic) != 0) {
          continue;  // the menu watches it already: the mod is building it
        }
        message = protocol::Sub{space->id, 0, event.topic, std::nullopt};
        space->held_kind = EventKind::watched;
        break;
      case serve::SpaceEvent::Kind::unwatched:
        space->browser_watched.erase(event.topic);
        if (space->game_watched.count(event.topic) != 0) {
          continue;  // the menu still watches it
        }
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
          continue;
        }
        message = protocol::Cmd{space->id, event.reply.id, event.command, std::move(event.args)};
        space->held_kind = EventKind::command;
        space->held_reply = next_reply_++;
        Pending pending;
        pending.reply = space->held_reply;
        pending.owner = owner;
        pending.ref = event.reply;
        pending.deadline = std::chrono::steady_clock::now() + kAnswerDeadline;
        pending.id = event.reply.id;
        pending_.push_back(pending);
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
  bool game = false;
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
    game = it->game;
    result->space = space->id;
    result->id = it->id;
    pending_.erase(it);
    port = space->port;
    if (game) {
      game_results_.push_back(GameResult{reply, std::move(*result)});  // the menu's (D171)
    }
  }
  if (game) {
    wake_game();
  } else if (port != nullptr) {
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
  bool game = false;
  for (std::size_t i = 0; i < late.size(); ++i) {
    protocol::Result result;
    result.space = names[i];
    result.id = late[i].second.id;
    result.error = protocol::ErrorCode::internal;
    if (late[i].second.game) {
      std::lock_guard<std::mutex> lock(mutex_);
      game_results_.push_back(GameResult{late[i].second.reply, std::move(result)});
      game = true;
    } else if (late[i].first != nullptr) {
      late[i].first->answer(late[i].second.ref, std::move(result));
    }
    if (log_) {
      log_("space " + names[i] + " left a command unanswered for 10 s: answered internal");
    }
  }
  if (game) {
    wake_game();
  }
  return late.size();
}

std::size_t Registry::pending() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.size();
}

// ---- The in-game view's side (D171).

void Registry::set_game_wake(std::function<void()> wake) {
  std::lock_guard<std::mutex> lock(mutex_);
  game_wake_ = std::move(wake);
}

void Registry::wake_game() const {
  std::function<void()> wake;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    wake = game_wake_;
  }
  if (wake) {
    wake();
  }
}

std::vector<Registry::GameSpace> Registry::game_spaces() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<GameSpace> out;
  for (const auto& space : spaces_) {
    if ((space->views & kViewGame) != 0) {
      out.push_back(GameSpace{space->id, space->description.info.name, space->description.info.increment, space->pages,
                              space->settings});
    }
  }
  return out;
}

std::uint64_t Registry::spaces_revision() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return spaces_revision_;
}

void Registry::game_watch(std::string_view space_id, std::string_view topic, bool on) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& space : spaces_) {
    if (space->id != space_id) {
      continue;
    }
    const std::string name(topic);
    const bool browser = space->browser_watched.count(name) != 0;
    if (on && space->game_watched.insert(name).second && !browser) {
      std::string frame;
      protocol::encode_text(protocol::Message(protocol::Sub{space->id, 0, name, std::nullopt}), frame);
      space->internal.push_back(Space::Internal{EventKind::watched, std::move(frame), 0});
    } else if (!on && space->game_watched.erase(name) != 0) {
      space->game_states.erase(name);
      if (!browser) {
        std::string frame;
        protocol::encode_text(protocol::Message(protocol::Unsub{space->id, 0, name}), frame);
        space->internal.push_back(Space::Internal{EventKind::unwatched, std::move(frame), 0});
      }
    }
    return;
  }
}

bool Registry::game_state(std::string_view space_id, std::string_view topic, std::uint64_t seen, protocol::Json& body,
                          std::uint64_t& rev) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& space : spaces_) {
    if (space->id != space_id) {
      continue;
    }
    const auto it = space->game_states.find(topic);
    if (it == space->game_states.end() || it->second.first == seen) {
      return false;
    }
    rev = it->second.first;
    body = it->second.second;
    return true;
  }
  return false;
}

bool Registry::game_command(std::string_view space_id, protocol::Cmd command, std::uint64_t& ticket) {
  bool answered = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& space : spaces_) {
      if (space->id != space_id) {
        continue;
      }
      const auto& declared = space->description.commands;
      const bool known = std::find(declared.begin(), declared.end(), command.name) != declared.end();
      if (!known || !serve::confirmed(space->description, command.name, command.args)) {
        protocol::Result refused;  // as the server answers a view's (invariant 3, §7.3): the mod never sees it
        refused.space = space->id;
        refused.id = command.id;
        refused.error = known ? protocol::ErrorCode::confirm_required : protocol::ErrorCode::unknown_command;
        ticket = next_reply_++;
        game_results_.push_back(GameResult{ticket, std::move(refused)});
        answered = true;
        break;
      }
      if (pending_.size() >= kMaxPending) {
        return false;
      }
      command.space = space->id;
      Pending pending;
      pending.reply = next_reply_++;
      pending.owner = space->owner;
      pending.deadline = std::chrono::steady_clock::now() + kAnswerDeadline;
      pending.game = true;
      pending.id = command.id;
      pending_.push_back(pending);
      std::string frame;
      protocol::encode_text(protocol::Message(std::move(command)), frame);
      space->internal.push_back(Space::Internal{EventKind::command, std::move(frame), pending.reply});
      ticket = pending.reply;
      return true;
    }
  }
  if (answered) {
    wake_game();
  }
  return answered;
}

std::vector<Registry::GameResult> Registry::take_game_results() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<GameResult> out;
  out.swap(game_results_);
  return out;
}

}  // namespace grace::spaces
