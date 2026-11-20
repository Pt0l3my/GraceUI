// SPDX-License-Identifier: MIT
// settings_store.cpp: one space's settings, checked at its one entry point (settings_store.h, D167).
#include "grace/pages/settings_store.h"

#include <cmath>
#include <unordered_set>
#include <utility>

#include "grace/protocol/frame.h"

namespace grace::pages {
namespace {

using protocol::ErrorCode;
using protocol::Json;
using protocol::SettingType;

const Json& null_json() {
  static const Json* const null = new Json();  // never freed: no exit-time destructor in a DLL module
  return *null;
}

Outcome refused(ErrorCode code, std::string detail) {
  Outcome out;
  out.ok = false;
  out.error = code;
  out.detail = std::move(detail);
  return out;
}

// Whether `n` lies on the step's grid from the minimum (or from 0), within a millionth of a step.
bool on_step(const protocol::Setting& s, double n) {
  if (!s.step.has_value() || *s.step <= 0) {
    return true;
  }
  const double from = s.min_.value_or(0.0);
  const double steps = (n - from) / *s.step;
  return std::fabs(steps - std::round(steps)) <= 1e-6;
}

// A key's name as the page lint takes it: (Ctrl+|Shift+|Alt+)* then 1 to 12 of A-Z a-z 0-9.
bool key_name(std::string_view text) {
  for (;;) {
    bool took = false;
    for (std::string_view modifier : {std::string_view("Ctrl+"), std::string_view("Shift+"), std::string_view("Alt+")}) {
      if (text.substr(0, modifier.size()) == modifier) {
        text.remove_prefix(modifier.size());
        took = true;
      }
    }
    if (!took) {
      break;
    }
  }
  if (text.empty() || text.size() > 12) {
    return false;
  }
  for (const char c : text) {
    const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!alnum) {
      return false;
    }
  }
  return true;
}

constexpr std::size_t kTextMax = 1024;  // settings.set's value, as the protocol bounds it

}  // namespace

bool valid_value(const protocol::Setting& s, const Json& v, std::string& why) {
  switch (s.type) {
    case SettingType::bool_:
      if (!v.is_boolean()) {
        why = "not on or off";
        return false;
      }
      return true;
    case SettingType::integer:
    case SettingType::number:
    case SettingType::duration_s: {
      if (!v.is_number()) {
        why = "not a number";
        return false;
      }
      const double n = v.get<double>();
      if (!std::isfinite(n)) {
        why = "not a finite number";
        return false;
      }
      if (s.type == SettingType::integer && !v.is_number_integer() && n != std::round(n)) {
        why = "not a whole number";
        return false;
      }
      if ((s.min_.has_value() && n < *s.min_) || (s.max_.has_value() && n > *s.max_)) {
        why = "outside its range";
        return false;
      }
      if (!on_step(s, n)) {
        why = "not on its step";
        return false;
      }
      return true;
    }
    case SettingType::choice: {
      if (v.is_string() && s.choices.has_value()) {
        for (const protocol::Choice& choice : *s.choices) {
          if (choice.value == v.get_ref<const std::string&>()) {
            return true;
          }
        }
      }
      why = "not one of its choices";
      return false;
    }
    case SettingType::tri_state: {
      const char* third = s.third.value_or(protocol::TriThird::auto_) == protocol::TriThird::inherit ? "inherit" : "auto";
      if (v.is_string() && (v == "on" || v == "off" || v == third)) {
        return true;
      }
      why = std::string("not on, off or ") + third;
      return false;
    }
    case SettingType::key_name:
      if (!v.is_string() || !key_name(v.get_ref<const std::string&>())) {
        why = "not a key's name";
        return false;
      }
      return true;
    case SettingType::text:
    case SettingType::secret:
      if (!v.is_string() || v.get_ref<const std::string&>().size() > kTextMax) {
        why = "not a text of at most 1024 bytes";
        return false;
      }
      return true;
  }
  why = "of an unknown type";
  return false;
}

bool SettingsStore::load(const protocol::SettingsDescription& description, std::string& why) {
  std::unordered_set<std::string_view> keys;
  for (const protocol::Setting& s : description.settings) {
    if (!keys.insert(s.key).second) {
      why = s.key + " comes twice";
      return false;
    }
    std::string bad;
    if (!valid_value(s, s.default_, bad)) {
      why = s.key + "'s default is " + bad;
      return false;
    }
  }
  description_ = description;
  values_.clear();
  values_.reserve(description_.settings.size());
  for (const protocol::Setting& s : description_.settings) {
    values_.push_back(s.default_);
  }
  return true;
}

std::size_t SettingsStore::index_of(std::string_view key) const noexcept {
  for (std::size_t i = 0; i < description_.settings.size(); ++i) {
    if (description_.settings[i].key == key) {
      return i;
    }
  }
  return description_.settings.size();
}

const protocol::Setting* SettingsStore::find(std::string_view key) const noexcept {
  const std::size_t i = index_of(key);
  return i < description_.settings.size() ? &description_.settings[i] : nullptr;
}

Json SettingsStore::values() const {
  Json out = Json::object();
  for (std::size_t i = 0; i < description_.settings.size(); ++i) {
    const protocol::Setting& s = description_.settings[i];
    if (s.type == SettingType::secret) {
      const bool set = values_[i].is_string() && !values_[i].get_ref<const std::string&>().empty();
      out[s.key] = set ? "set" : "";
    } else {
      out[s.key] = values_[i];
    }
  }
  return out;
}

const Json& SettingsStore::held(std::string_view key) const {
  const std::size_t i = index_of(key);
  return i < values_.size() ? values_[i] : null_json();
}

std::vector<std::string> SettingsStore::changed() const {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < description_.settings.size(); ++i) {
    if (values_[i] != description_.settings[i].default_) {
      out.push_back(description_.settings[i].key);
    }
  }
  return out;
}

Json SettingsStore::saved() const {
  Json out = Json::object();
  for (std::size_t i = 0; i < description_.settings.size(); ++i) {
    const protocol::Setting& s = description_.settings[i];
    if (s.type != protocol::SettingType::secret && values_[i] != s.default_) {
      out[s.key] = values_[i];
    }
  }
  return out;
}

SettingsStore::Restored SettingsStore::restore(const Json& saved) {
  Restored out;
  if (!saved.is_object()) {
    return out;
  }
  for (const auto& [key, value] : saved.items()) {
    const std::size_t i = index_of(key);
    const bool secret = i < description_.settings.size() && description_.settings[i].type == protocol::SettingType::secret;
    if (!secret && set(key, value).ok) {
      ++out.taken;
    } else {
      ++out.left_out;
    }
  }
  return out;
}

Outcome SettingsStore::set(std::string_view key, const Json& value) {
  const std::size_t i = index_of(key);
  if (i == description_.settings.size()) {
    return refused(ErrorCode::unknown_key, "no such setting");
  }
  const protocol::Setting& s = description_.settings[i];
  if (s.availability == "later") {
    return refused(ErrorCode::locked, "Only in a later version.");
  }
  if (s.locked) {
    return refused(ErrorCode::locked, "This setting is locked.");
  }
  if (s.availability != "live") {
    return refused(ErrorCode::not_now, "Not available now.");  // a check, a version or a dependency holds it (§6.9)
  }
  std::string why;
  if (!valid_value(s, value, why)) {
    return refused(ErrorCode::invalid_value, std::string("The value is ") + why + ".");
  }
  Outcome out;
  out.ok = true;
  if (values_[i] != value) {
    values_[i] = value;
    out.changed.push_back(s.key);
  }
  return out;
}

Outcome SettingsStore::reset(protocol::ResetScopeKind scope, const std::string* target, bool confirm) {
  using protocol::ResetScopeKind;
  if (scope != ResetScopeKind::key && !confirm) {
    return refused(ErrorCode::confirm_required, "Resetting a page or everything asks first.");
  }
  if (scope != ResetScopeKind::all && target == nullptr) {
    return refused(ErrorCode::invalid_value, "No target to reset.");
  }
  if (scope == ResetScopeKind::key && index_of(*target) == description_.settings.size()) {
    return refused(ErrorCode::unknown_key, "no such setting");
  }
  Outcome out;
  out.ok = true;
  for (std::size_t i = 0; i < description_.settings.size(); ++i) {
    const protocol::Setting& s = description_.settings[i];
    const bool in_scope = scope == ResetScopeKind::all || (scope == ResetScopeKind::key ? s.key == *target
                                                                                          : s.home == *target);
    if (!in_scope) {
      continue;
    }
    if (s.locked) {
      if (scope == ResetScopeKind::key) {
        return refused(ErrorCode::locked, "This setting is locked.");
      }
      continue;
    }
    if (values_[i] != s.default_) {
      values_[i] = s.default_;
      out.changed.push_back(s.key);
    }
  }
  return out;
}

Outcome SettingsStore::apply(const protocol::Cmd& command) {
  protocol::Command which{};
  protocol::CommandArgs args;
  protocol::Refusal refusal;
  if (!protocol::parse_command(command, which, args, refusal)) {
    return refused(refusal.code, refusal.where.field.empty() ? "The command's arguments fail."
                                                              : "The command's " + refusal.where.field + " fails.");
  }
  if (const auto* set_args = std::get_if<protocol::SettingsSetArgs>(&args)) {
    return set(set_args->key, set_args->value);
  }
  if (const auto* reset_args = std::get_if<protocol::SettingsResetArgs>(&args)) {
    return reset(reset_args->scope, reset_args->target.has_value() ? &*reset_args->target : nullptr,
                 reset_args->confirm.value_or(false));
  }
  return refused(ErrorCode::unknown_command, "Not a settings command.");
}

}  // namespace grace::pages
