// SPDX-License-Identifier: MIT
// own_space.cpp: GraceUI's own space, shared by both views (own_space.h, D169).
#include "grace/spaces/own_space.h"

#include <utility>

#include "grace/pages/builtin.h"

namespace grace::spaces {
namespace {

template <class T>
bool decode_json(std::string_view text, T& out) {
  protocol::Json json = protocol::Json::parse(text.begin(), text.end(), nullptr, false);
  protocol::DecodeError error;
  return !json.is_discarded() && decode(json, out, "", error);
}

}  // namespace

bool OwnSpace::load(std::string& why) {
  const pages::BuiltinSpace& own = pages::builtin_spaces()[0];
  if (!decode_json(own.pages, pages_) || !decode_json(own.settings, settings_)) {
    why = "GraceUI's own space does not decode";
    return false;
  }
  if (!store_.load(settings_, why)) {
    return false;
  }
  for (const protocol::TopicInfo& topic : pages_.topics) {
    if (topic.id != "settings") {
      topics_.emplace(topic.id, Topic{});
    }
  }
  loaded_ = true;
  return true;
}

bool OwnSpace::declared_command(std::string_view name) const {
  for (const std::string& command : pages_.commands) {
    if (command == name) {
      return true;
    }
  }
  return false;
}

pages::Outcome OwnSpace::apply(const protocol::Cmd& command) {
  if (command.name != "settings.set" && command.name != "settings.reset") {
    if (hooks_.command && declared_command(command.name) && command.name != "dashboard.disconnect" &&
        command.name != "menu.close") {
      return hooks_.command(command);
    }
    pages::Outcome refused;
    refused.error = protocol::ErrorCode::unknown_command;
    refused.detail = "Not available in this version.";
    return refused;
  }
  pages::Outcome outcome = store_.apply(command);
  if (outcome.ok && !outcome.changed.empty()) {
    ++revision_;
    publish_settings();
  }
  return outcome;
}

bool OwnSpace::set_state(std::string_view topic, protocol::Json body) {
  const auto found = topics_.find(topic);
  if (found == topics_.end()) {
    return false;
  }
  found->second.body = std::move(body);
  found->second.rev += 1;
  ++states_revision_;
  publish_topic(found->first, found->second);
  return true;
}

const protocol::Json* OwnSpace::state_of(std::string_view topic) const {
  const auto found = topics_.find(topic);
  return found == topics_.end() || found->second.rev == 0 ? nullptr : &found->second.body;
}

void OwnSpace::publish_topic(const std::string& topic, Topic& state) {
  if (port_ == nullptr || !state.watched || state.rev == 0 || state.published == state.rev) {
    return;
  }
  protocol::State frame;
  frame.space = pages_.space;
  frame.topic = topic;
  frame.rev = state.rev;
  frame.body = state.body;
  if (port_->publish(std::move(frame))) {
    state.published = state.rev;
  }
}

void OwnSpace::attach(serve::Server& server, const std::string& version, Hooks hooks, serve::SpaceWake wake) {
  hooks_ = std::move(hooks);
  // Its increment is GraceUI's own, the version's first two numbers ("0.2.0": "0.2").
  const std::size_t dot = version.find('.');
  const std::size_t second = dot == std::string::npos ? std::string::npos : version.find('.', dot + 1);
  port_ = server.add_space(serve::describe_space(pages_, settings_, version, version.substr(0, second)),
                           std::move(wake));
}

protocol::SettingsBody OwnSpace::settings_body() const {
  protocol::SettingsBody body;
  body.schema = settings_.schema;
  body.description = settings_.settings;
  body.values = store_.values();
  body.changed = store_.changed();
  return body;
}

void OwnSpace::publish_settings() {
  if (port_ == nullptr || !settings_watched_ || published_rev_ == revision_) {
    return;
  }
  protocol::State state;
  state.space = pages_.space;
  state.topic = "settings";
  state.rev = revision_;
  state.body = encode(settings_body());
  if (port_->publish(std::move(state))) {
    published_rev_ = revision_;
  }
}

void OwnSpace::serve() {
  if (port_ == nullptr) {
    return;
  }
  serve::SpaceEvent event;
  for (int i = 0; i < 64 && port_->poll(event); ++i) {
    switch (event.kind) {
      case serve::SpaceEvent::Kind::watched:
        if (event.topic == "settings") {
          settings_watched_ = true;
          published_rev_ = 0;  // a new view: the whole state again
          publish_settings();
        } else if (const auto found = topics_.find(event.topic); found != topics_.end()) {
          found->second.watched = true;
          found->second.published = 0;  // a new view: the newest body again
          publish_topic(found->first, found->second);
        }
        break;
      case serve::SpaceEvent::Kind::unwatched:
        if (event.topic == "settings") {
          settings_watched_ = false;
        } else if (const auto found = topics_.find(event.topic); found != topics_.end()) {
          found->second.watched = false;
        }
        break;
      case serve::SpaceEvent::Kind::command: {
        protocol::Result result;
        result.space = pages_.space;
        result.id = event.reply.id;
        if (event.command == "settings.set" || event.command == "settings.reset") {
          protocol::Cmd command{pages_.space, event.reply.id, event.command, std::move(event.args)};
          const pages::Outcome outcome = apply(command);
          if (outcome.ok) {
            result.ok = true;
          } else {
            result.error = outcome.error;
            result.detail = outcome.detail;  // its reason in words (protocol 1.1)
          }
        } else if (event.command == "dashboard.disconnect") {
          result.ok = true;
          port_->answer(event.reply, std::move(result));
          if (hooks_.disconnect) {
            hooks_.disconnect();  // after the answer: renewing the session closes the view that asked
          }
          continue;
        } else if (event.command != "menu.close" && hooks_.command && declared_command(event.command)) {
          protocol::Cmd command{pages_.space, event.reply.id, event.command, std::move(event.args)};
          const pages::Outcome outcome = hooks_.command(command);
          if (outcome.ok) {
            result.ok = true;
          } else {
            result.error = outcome.error;
            result.detail = outcome.detail;
          }
        } else {
          result.error = event.command == "menu.close" ? protocol::ErrorCode::not_now
                                                       : protocol::ErrorCode::unknown_command;
        }
        port_->answer(event.reply, std::move(result));
        break;
      }
      case serve::SpaceEvent::Kind::none:
        break;
    }
  }
}

}  // namespace grace::spaces
