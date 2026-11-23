// SPDX-License-Identifier: MIT
// own_space.h: GraceUI's own space (D79, D115, D169): its page data and settings, from grace-pages, held by one settings
// store that both views share. The in-game menu changes settings through apply(); the browser's views reach it through
// grace-serve's port, which serve() answers: settings.set and settings.reset by the store (its one entry point,
// invariant 3), dashboard.disconnect by the host's hook, menu.close refused not_now (the browser has no menu to close),
// and GraceUI's other declared commands (mods.notices, D181) by the host's command hook. The settings topic is published
// to the views that watch it after every change, whichever view made it; GraceUI's other topics (graceui.mods,
// graceui.notices) carry the bodies the host sets, published the same way. Used from one thread only (GraceUI's own,
// in the host). No Win32.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "grace/pages/settings_store.h"
#include "grace/protocol/messages.h"
#include "grace/serve/server.h"
#include "grace/serve/space.h"

namespace grace::spaces {

class OwnSpace {
 public:
  struct Hooks {
    std::function<void()> disconnect;  // dashboard.disconnect: renew the session (the server's renew_session)
    // GraceUI's other declared commands (D181): the host's answer, its one entry point for each (invariant 3).
    std::function<pages::Outcome(const protocol::Cmd&)> command;
    // A setting's own rules past its description's, checked before a settings.set takes it (hotkeys.menu, D208):
    // refused with its words when the host says so.
    std::function<std::optional<pages::Outcome>(const protocol::Cmd&)> check;
  };

  // GraceUI's own space as grace-pages compiled it: false with `why` if it does not load (never, for the built-in one).
  bool load(std::string& why);
  bool loaded() const noexcept { return loaded_; }

  const protocol::SpacePages& pages() const noexcept { return pages_; }
  const protocol::SettingsDescription& settings() const noexcept { return settings_; }
  const pages::SettingsStore& store() const noexcept { return store_; }
  // Rises with every change of a setting, whichever view made it: a view compares it with the last it saw.
  std::uint64_t revision() const noexcept { return revision_; }

  // A topic of GraceUI's own other than settings (D181): the host sets its newest body, which goes to the views
  // watching it; state_of gives it to the in-game view (null before the host set it, or for a topic not declared).
  // set_state answers false for a topic the page data does not declare.
  bool set_state(std::string_view topic, protocol::Json body);
  const protocol::Json* state_of(std::string_view topic) const;
  // Rises with every set_state: the in-game view compares it with the last it took.
  std::uint64_t states_revision() const noexcept { return states_revision_; }

  // GraceUI's settings kept between sessions (D84, D179): what to write, and what an earlier session wrote, taken back
  // before the space is attached (no view has seen the store yet, so nothing is published and the revision stays).
  protocol::Json saved_settings() const { return store_.saved(); }
  pages::SettingsStore::Restored restore_settings(const protocol::Json& saved) { return store_.restore(saved); }

  // A command from the in-game menu: settings.set and settings.reset by the store (on a change the settings topic goes
  // out to every view watching it); another declared command by the host's command hook (D181).
  pages::Outcome apply(const protocol::Cmd& command);

  // Joins the server (once for the session): `version` and `increment` as welcome names them.
  void attach(serve::Server& server, const std::string& version, Hooks hooks, serve::SpaceWake wake = {});
  // Answers what the browser's views asked since the last call: subscriptions to the settings topic, and commands.
  void serve();

  // The settings topic's body: the description, the values (a secret's as its status), the keys changed.
  protocol::SettingsBody settings_body() const;

 private:
  struct Topic {
    protocol::Json body;
    std::uint64_t rev = 0;  // 0 before the host set it
    bool watched = false;
    std::uint64_t published = 0;
  };

  void publish_settings();
  void publish_topic(const std::string& topic, Topic& state);
  bool declared_command(std::string_view name) const;

  bool loaded_ = false;
  protocol::SpacePages pages_;
  protocol::SettingsDescription settings_;
  pages::SettingsStore store_;
  std::uint64_t revision_ = 1;
  std::uint64_t published_rev_ = 0;
  bool settings_watched_ = false;
  std::map<std::string, Topic, std::less<>> topics_;  // the declared topics other than settings
  std::uint64_t states_revision_ = 0;
  std::shared_ptr<serve::SpacePort> port_;
  Hooks hooks_;
};

}  // namespace grace::spaces
