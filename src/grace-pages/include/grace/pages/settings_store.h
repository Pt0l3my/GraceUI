// SPDX-License-Identifier: MIT
// settings_store.h: one space's settings as its one validating entry point holds them (invariant 3, §6.6, §6.9): the
// values, each checked against its description before it is taken (its type, bounds, step, choices, locked or not
// live), and settings.set and settings.reset answered as the protocol says. GraceUI's own space uses it in game and in
// grace-dev (D167); a space of a mod's may. Secrets are write-only (invariant 5): values() shows a secret as "set" or "",
// and nothing here logs, echoes or answers a secret's value. Pure: no I/O, no Win32.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "grace/protocol/messages.h"

namespace grace::pages {

// What a command came to: ok, with the keys whose values changed (in the description's order), or an error code of the
// protocol's with a detail in words that never quotes a value.
struct Outcome {
  bool ok = false;
  protocol::ErrorCode error = protocol::ErrorCode::internal;
  std::string detail;
  std::vector<std::string> changed;
};

// Whether `value` is one `setting` takes: of its type, within its bounds and on its step, one of its choices; false with
// `why` in words (never the value). Locked and availability are not weighed here.
bool valid_value(const protocol::Setting& setting, const protocol::Json& value, std::string& why);

class SettingsStore {
 public:
  // Takes the description and sets every value to its default: false with `why` if a key comes twice or a default fails
  // its own setting (the page lint checks both, so a built-in space never fails).
  bool load(const protocol::SettingsDescription& description, std::string& why);

  const protocol::SettingsDescription& description() const noexcept { return description_; }
  const protocol::Setting* find(std::string_view key) const noexcept;

  // Every value by key, a secret's as "set" or "" (invariant 5): what a view may be sent.
  protocol::Json values() const;
  // A value as it is held, for the space's own use (a secret's too: never to a view or a log).
  const protocol::Json& held(std::string_view key) const;
  // The keys whose values differ from their defaults, in the description's order (a view's "changed" lozenges).
  std::vector<std::string> changed() const;

  // settings.set: unknown_key, locked (locked, or not live: its availability is not "live"), invalid_value, or ok with
  // the key in `changed` if its value changed.
  Outcome set(std::string_view key, const protocol::Json& value);
  // settings.reset: a key (its target a key), a page (its target a page id: every setting homed there) or all of them;
  // a page or all needs `confirm` (confirm_required). Locked settings keep their values.
  Outcome reset(protocol::ResetScopeKind scope, const std::string* target, bool confirm);

  // A framework command for this space (settings.set or settings.reset), its arguments checked by the protocol first:
  // unknown_command for any other, invalid_value for arguments that fail.
  Outcome apply(const protocol::Cmd& command);

 private:
  std::size_t index_of(std::string_view key) const noexcept;

  protocol::SettingsDescription description_;
  std::vector<protocol::Json> values_;  // by the description's order
};

}  // namespace grace::pages
