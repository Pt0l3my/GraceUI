// SPDX-License-Identifier: MIT
// own_space.h: GraceUI's own space (D79, D115, D169): its page data and settings, from grace-pages, held by one settings
// store that both views share. The in-game menu changes settings through apply(); the browser's views reach it through
// grace-serve's port, which serve() answers: settings.set and settings.reset by the store (its one entry point,
// invariant 3), dashboard.disconnect by the host's hook, menu.close refused not_now (the browser has no menu to close).
// The settings topic is published to the views that watch it after every change, whichever view made it. Used from
// one thread only (GraceUI's own, in the host). No Win32.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "grace/pages/settings_store.h"
#include "grace/protocol/messages.h"
#include "grace/serve/server.h"
#include "grace/serve/space.h"

namespace grace::spaces {

class OwnSpace {
 public:
  struct Hooks {
    std::function<void()> disconnect;  // dashboard.disconnect: renew the session (the server's renew_session)
  };

  // GraceUI's own space as grace-pages compiled it: false with `why` if it does not load (never, for the built-in one).
  bool load(std::string& why);
  bool loaded() const noexcept { return loaded_; }

  const protocol::SpacePages& pages() const noexcept { return pages_; }
  const protocol::SettingsDescription& settings() const noexcept { return settings_; }
  const pages::SettingsStore& store() const noexcept { return store_; }
  // Rises with every change of a setting, whichever view made it: a view compares it with the last it saw.
  std::uint64_t revision() const noexcept { return revision_; }

  // GraceUI's settings kept between sessions (D84, D179): what to write, and what an earlier session wrote, taken back
  // before the space is attached (no view has seen the store yet, so nothing is published and the revision stays).
  protocol::Json saved_settings() const { return store_.saved(); }
  pages::SettingsStore::Restored restore_settings(const protocol::Json& saved) { return store_.restore(saved); }

  // A settings command from the in-game menu: the store's outcome; on a change the settings topic goes out to every
  // view watching it.
  pages::Outcome apply(const protocol::Cmd& command);

  // Joins the server (once for the session): `version` and `increment` as welcome names them.
  void attach(serve::Server& server, const std::string& version, Hooks hooks, serve::SpaceWake wake = {});
  // Answers what the browser's views asked since the last call: subscriptions to the settings topic, and commands.
  void serve();

  // The settings topic's body: the description, the values (a secret's as its status), the keys changed.
  protocol::SettingsBody settings_body() const;

 private:
  void publish_settings();

  bool loaded_ = false;
  protocol::SpacePages pages_;
  protocol::SettingsDescription settings_;
  pages::SettingsStore store_;
  std::uint64_t revision_ = 1;
  std::uint64_t published_rev_ = 0;
  bool settings_watched_ = false;
  std::shared_ptr<serve::SpacePort> port_;
  Hooks hooks_;
};

}  // namespace grace::spaces
