// SPDX-License-Identifier: MIT
// scenario.cpp: a scenario read and checked against its space (scenario.h).
#include "scenario.h"

#include <algorithm>
#include <set>

#include "json_text.h"

namespace grace::dev {
namespace {

using protocol::Json;

// A scenario's bounds (EST): far past any demo's, so that a runaway file fails at once rather than fills memory.
constexpr std::size_t kMaxSteps = 10000;
constexpr std::size_t kMaxActions = 16;  // a reaction's
constexpr std::size_t kMaxDetail = 256;  // a refusal's words, as the protocol's error detail
constexpr std::size_t kMaxBody = 256 * 1024;  // an answer's body, as the protocol's result body (§4 of proposal 0014)
constexpr std::uint64_t kMaxTime = 7ull * 24 * 3600 * 1000;  // a step's time and a pass's length: a week at most

bool only_keys(const Json& object, std::initializer_list<std::string_view> keys, std::string& why) {
  for (auto it = object.begin(); it != object.end(); ++it) {
    bool known = false;
    for (std::string_view key : keys) {
      known = known || it.key() == key;
    }
    if (!known) {
      why = "it has a field \"" + it.key() + "\", which a scenario does not take";
      return false;
    }
  }
  return true;
}

bool is_framework_command(std::string_view name) {
  protocol::Command command{};
  return protocol::from_string(name, command);
}

// One action: a step's (with `at`) or a reaction's.
bool read_action(const Json& item, const SpaceData& space, bool step, Action& out, std::string& why) {
  if (!item.is_object()) {
    why = "not an object";
    return false;
  }
  int kinds = 0;
  for (const char* kind : {"state", "rows", "clear"}) {
    kinds += item.contains(kind) ? 1 : 0;
  }
  if (kinds != 1) {
    why = "it names exactly one of state, rows and clear";
    return false;
  }
  const char* kind = item.contains("state") ? "state" : item.contains("rows") ? "rows" : "clear";
  out.kind = item.contains("state") ? Action::Kind::state : item.contains("rows") ? Action::Kind::rows
                                                                                 : Action::Kind::clear;
  const bool fields_ok = out.kind == Action::Kind::state  ? only_keys(item, {"at", "state", "body"}, why)
                         : out.kind == Action::Kind::rows ? only_keys(item, {"at", "rows", "add"}, why)
                                                          : only_keys(item, {"at", "clear"}, why);
  if (!fields_ok) {
    return false;
  }
  if (!step && item.contains("at")) {
    why = "a reaction's action has no time: it plays when its command arrives";
    return false;
  }
  if (!item[kind].is_string()) {
    why = std::string(kind) + " names a topic";
    return false;
  }
  out.topic = item[kind].get<std::string>();
  const protocol::TopicInfo* topic = space.topic(out.topic);
  if (topic == nullptr) {
    why = out.topic + " is not a topic of the space " + space.id;
    return false;
  }
  if (out.topic == "settings") {
    why = "the settings topic changes only through settings.set and settings.reset (invariant 3)";
    return false;
  }
  const bool wants_stream = out.kind != Action::Kind::state;
  if ((topic->shape == protocol::TopicShape::stream) != wants_stream) {
    why = out.topic + " is a " + std::string(protocol::to_string(topic->shape)) + " topic: " +
          (wants_stream ? "rows and clear are for streams" : "state is for a state topic");
    return false;
  }
  if (out.kind == Action::Kind::state) {
    if (!item.contains("body") || !item["body"].is_object()) {
      why = "state takes a body, an object";
      return false;
    }
    if (protocol::encoded_size(item["body"], kMaxBody) > kMaxBody) {
      why = "its body is larger than a state frame holds";
      return false;
    }
    out.body = item["body"];
  } else if (out.kind == Action::Kind::rows) {
    if (!item.contains("add") || !item["add"].is_array() || item["add"].empty() ||
        item["add"].size() > protocol::kRowsPerBatch) {
      why = "rows takes add, a list of 1 to " + std::to_string(protocol::kRowsPerBatch) + " rows";
      return false;
    }
    std::size_t bytes = 0;
    for (const Json& row : item["add"]) {
      if (!row.is_object()) {
        why = "each row is an object";
        return false;
      }
      if (row.contains("seq")) {
        why = "a row has no seq of its own: grace-dev numbers the rows";
        return false;
      }
      bytes += protocol::encoded_size(row, kMaxBody + 1);
      out.rows.push_back(row);
    }
    if (bytes > kMaxBody) {
      why = "its rows are larger than one batch's frame holds";
      return false;
    }
  }
  return true;
}

bool read_answer(const Json& answer, Reaction& out, std::string& why) {
  if (answer.is_string() && answer.get_ref<const std::string&>() == "ok") {
    return true;
  }
  if (answer.is_object() && answer.size() == 1 && answer.contains("body")) {
    // Ok with a body, as a space answers a page command (D254): {"columns", "rows", "next", "total"}.
    if (!answer["body"].is_object() || answer["body"].dump().size() > kMaxBody) {
      why = "answer's body is an object of at most " + std::to_string(kMaxBody / 1024) + " KiB";
      return false;
    }
    out.body = answer["body"];
    return true;
  }
  if (!answer.is_object() || !only_keys(answer, {"error", "detail"}, why) || !answer.contains("error")) {
    why = "answer is \"ok\", {\"body\": {...}}, or an object with an error code and its detail in words";
    return false;
  }
  protocol::DecodeError error;
  if (!protocol::read_enum(answer["error"], out.error, "", error)) {
    why = "answer's error is not one of the protocol's codes";
    return false;
  }
  if (answer.contains("detail") && !protocol::read_string(answer["detail"], kMaxDetail, out.detail, "", error)) {
    why = "answer's detail is text of at most " + std::to_string(kMaxDetail) + " bytes";
    return false;
  }
  out.ok = false;
  return true;
}

bool read_inherited(const Json& list, const SpaceData& space, Scenario& out, std::string& why) {
  if (!list.is_array()) {
    why = "inherited is a list";
    return false;
  }
  std::set<std::string, std::less<>> seen;
  for (std::size_t i = 0; i < list.size(); ++i) {
    protocol::InheritedValue value;
    protocol::DecodeError error;
    const std::string where = "inherited[" + std::to_string(i) + "]";
    if (!protocol::decode(list[i], value, where, error)) {
      why = error.field + ": " + error.why;
      return false;
    }
    const protocol::Setting* setting = space.setting(value.key);
    if (setting == nullptr || setting->type != protocol::SettingType::tri_state ||
        setting->third != protocol::TriThird::inherit) {
      why = where + ": " + value.key + " is not an inherit tri-state of the space (D163)";
      return false;
    }
    if (!seen.insert(value.key).second) {
      why = where + ": " + value.key + " comes twice";
      return false;
    }
    if (value.page.has_value() && !space.has_page(*value.page)) {
      why = where + ": " + *value.page + " is not a page of the space";
      return false;
    }
    out.inherited.push_back(std::move(value));
  }
  return true;
}

std::string at_line(std::string_view name, const std::vector<std::size_t>& lines, std::size_t index) {
  return std::string(name) + (index < lines.size() ? " line " + std::to_string(lines[index]) : std::string()) + ": ";
}

}  // namespace

const Reaction* Scenario::reaction(std::string_view command) const noexcept {
  for (const Reaction& r : reactions) {
    if (r.command == command) {
      return &r;
    }
  }
  return nullptr;
}

bool scenario_space(std::string_view text, std::string_view name, std::string& space, std::string& why) {
  Json json;
  std::string bad;
  if (!parse_json(text, json, bad)) {
    why = std::string(name) + " " + bad;
    return false;
  }
  if (!json.is_object() || !json.contains("space") || !json["space"].is_string()) {
    why = std::string(name) + ": it names its space (\"space\": \"<id>\")";
    return false;
  }
  space = json["space"].get<std::string>();
  return true;
}

bool load_scenario(std::string_view text, std::string_view name, const SpaceData& space, Scenario& out,
                   std::string& why) {
  out = Scenario{};
  out.name = name;
  const std::string top = std::string(name) + ": ";
  Json json;
  std::string bad;
  if (!parse_json(text, json, bad)) {
    why = std::string(name) + " " + bad;
    return false;
  }
  if (!json.is_object()) {
    why = top + "a scenario is an object";
    return false;
  }
  if (!only_keys(json, {"scenario", "space", "about", "length_ms", "loop", "inherited", "steps", "reactions"}, bad)) {
    why = top + bad;
    return false;
  }
  protocol::DecodeError error;
  std::uint32_t format = 0;
  if (!json.contains("scenario") || !protocol::read_u32(json["scenario"], format, "", error) ||
      format != kScenarioFormat) {
    why = top + "\"scenario\" is its format, " + std::to_string(kScenarioFormat);
    return false;
  }
  if (!json.contains("space") || !json["space"].is_string() || json["space"].get<std::string>() != space.id) {
    why = top + "\"space\" names the space it plays in, " + space.id;
    return false;
  }
  out.space = space.id;
  if (json.contains("about") && !json["about"].is_string()) {
    why = top + "\"about\" is text";
    return false;
  }
  std::uint64_t length = 0;
  if (json.contains("length_ms") && (!protocol::read_u64(json["length_ms"], length, "", error) || length > kMaxTime)) {
    why = top + "\"length_ms\" is a whole number of milliseconds, a week at most";
    return false;
  }
  if (json.contains("loop") && !protocol::read_bool(json["loop"], out.loop, "", error)) {
    why = top + "\"loop\" is true or false";
    return false;
  }
  if (json.contains("inherited") && !read_inherited(json["inherited"], space, out, bad)) {
    why = top + bad;
    return false;
  }
  if (json.contains("steps")) {
    const Json& steps = json["steps"];
    if (!steps.is_array() || steps.size() > kMaxSteps) {
      why = top + "\"steps\" is a list of at most " + std::to_string(kMaxSteps) + " steps";
      return false;
    }
    const std::vector<std::size_t> lines = element_lines(text, "steps");
    for (std::size_t i = 0; i < steps.size(); ++i) {
      const std::string where = at_line(name, lines, i) + "step " + std::to_string(i + 1) + ": ";
      Step step;
      step.line = i < lines.size() ? lines[i] : 0;
      if (!steps[i].is_object() || !steps[i].contains("at") ||
          !protocol::read_u64(steps[i]["at"], step.at, "", error) || step.at > kMaxTime) {
        why = where + "it has \"at\", its time in whole milliseconds, a week at most";
        return false;
      }
      if (!out.steps.empty() && step.at < out.steps.back().at) {
        why = where + "it comes before the step above it: steps are in time order";
        return false;
      }
      if (!read_action(steps[i], space, true, step.action, bad)) {
        why = where + bad;
        return false;
      }
      out.steps.push_back(std::move(step));
    }
  }
  if (json.contains("reactions")) {
    const Json& reactions = json["reactions"];
    if (!reactions.is_array()) {
      why = top + "\"reactions\" is a list";
      return false;
    }
    const std::vector<std::size_t> lines = element_lines(text, "reactions");
    for (std::size_t i = 0; i < reactions.size(); ++i) {
      const std::string where = at_line(name, lines, i) + "reaction " + std::to_string(i + 1) + ": ";
      const Json& item = reactions[i];
      Reaction reaction;
      reaction.line = i < lines.size() ? lines[i] : 0;
      bad.clear();
      if (!item.is_object() || !only_keys(item, {"on", "do", "answer"}, bad) || !item.contains("on") ||
          !item["on"].is_string()) {
        why = where + (bad.empty() ? std::string("it names its command in \"on\"") : bad);
        return false;
      }
      reaction.command = item["on"].get<std::string>();
      if (!space.has_command(reaction.command)) {
        why = where + reaction.command + " is not a command of the space " + space.id;
        return false;
      }
      if (is_framework_command(reaction.command)) {
        why = where + reaction.command +
              " is GraceUI's own: settings go through the settings store (invariant 3), and the others are "
              "grace-dev's";
        return false;
      }
      if (out.reaction(reaction.command) != nullptr) {
        why = where + reaction.command + " has a reaction already";
        return false;
      }
      if (item.contains("do")) {
        if (!item["do"].is_array() || item["do"].size() > kMaxActions) {
          why = where + "\"do\" is a list of at most " + std::to_string(kMaxActions) + " actions";
          return false;
        }
        for (std::size_t a = 0; a < item["do"].size(); ++a) {
          Action action;
          if (!read_action(item["do"][a], space, false, action, bad)) {
            why = where + "action " + std::to_string(a + 1) + ": " + bad;
            return false;
          }
          reaction.actions.push_back(std::move(action));
        }
      }
      if (item.contains("answer") && !read_answer(item["answer"], reaction, bad)) {
        why = where + bad;
        return false;
      }
      out.reactions.push_back(std::move(reaction));
    }
  }
  const std::uint64_t last = out.steps.empty() ? 0 : out.steps.back().at;
  if (json.contains("length_ms") && length < last) {
    why = top + "\"length_ms\" is shorter than its last step's time";
    return false;
  }
  out.length = std::max(length, last);
  return true;
}

}  // namespace grace::dev
