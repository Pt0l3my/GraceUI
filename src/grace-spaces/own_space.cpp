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
  loaded_ = true;
  return true;
}

pages::Outcome OwnSpace::apply(const protocol::Cmd& command) {
  pages::Outcome outcome = store_.apply(command);
  if (outcome.ok && !outcome.changed.empty()) {
    ++revision_;
    publish_settings();
  }
  return outcome;
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
        }
        break;  // GraceUI's other topics are published from S11.4, with the components that draw them
      case serve::SpaceEvent::Kind::unwatched:
        if (event.topic == "settings") {
          settings_watched_ = false;
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
