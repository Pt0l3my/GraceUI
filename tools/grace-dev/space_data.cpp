// SPDX-License-Identifier: MIT
// space_data.cpp: the spaces grace-dev serves, loaded and checked (space_data.h).
#include "space_data.h"

#include <set>

#include "grace/pages/builtin.h"
#include "grace/pages/settings_store.h"
#include "grace/view/page_view.h"
#include "json_text.h"

namespace grace::dev {
namespace {

using protocol::Json;

template <class T>
bool decode_text(std::string_view text, std::string_view file, T& out, std::string& why) {
  Json json;
  std::string bad;
  if (!parse_json(text, json, bad)) {
    why = std::string(file) + ": " + bad;
    return false;
  }
  protocol::DecodeError error;
  if (!protocol::decode(json, out, "", error)) {
    why = std::string(file) + ": " + (error.field.empty() ? std::string("the whole file") : error.field) + ": " +
          error.why;
    return false;
  }
  return true;
}

bool is_stream_component(protocol::Component component) noexcept {
  return component == protocol::Component::live_feed || component == protocol::Component::log_tail;
}

// A stream's fixture rows: objects whose seq numbers rise by one, since a stream's rows are numbered from their batch's
// from_seq and a gap starts the stream over (§6.3, D161); no more than its ring holds.
bool check_rows(const Json& rows, const protocol::TopicInfo& topic, std::string& why) {
  if (!rows.is_array() || rows.empty()) {
    why = "a stream topic's fixture is a list of rows, at least one";
    return false;
  }
  if (rows.size() > ring_of(topic)) {
    why = "more rows than its ring holds (" + std::to_string(ring_of(topic)) + ")";
    return false;
  }
  std::uint64_t expected = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const std::string where = "row " + std::to_string(i + 1);
    std::uint64_t seq = 0;
    protocol::DecodeError error;
    if (!rows[i].is_object() || !rows[i].contains("seq") || !protocol::read_u64(rows[i]["seq"], seq, "", error)) {
      why = where + " is not an object with a whole seq number";
      return false;
    }
    if (i > 0 && seq != expected) {
      why = where + "'s seq does not follow on from the row before it by one";
      return false;
    }
    if (protocol::encoded_size(rows[i], kMaxBody) > kMaxBody / protocol::kRowsPerBatch) {
      why = where + " is larger than a row of a full batch may be";
      return false;
    }
    expected = seq + 1;
  }
  return true;
}

bool check_fixtures(const SpaceData& space, std::string& why) {
  if (!space.fixtures.is_object()) {
    why = "topics.json: not an object of topics";
    return false;
  }
  for (auto it = space.fixtures.begin(); it != space.fixtures.end(); ++it) {
    const std::string& id = it.key();
    const std::string where = "topics.json: " + id;
    const protocol::TopicInfo* topic = space.topic(id);
    if (topic == nullptr) {
      why = where + " is not a topic the space declares";
      return false;
    }
    if (id == "settings") {
      why = where + ": the settings topic comes from settings.json and the settings store, not from a fixture";
      return false;
    }
    if (topic->family.value_or(false) && topic->id == id) {
      why = where + ": a family has no fixture of its own; each member's is <family>/<key> (D215)";
      return false;
    }
    if (topic->shape == protocol::TopicShape::state) {
      if (!it.value().is_object()) {
        why = where + ": a state topic's fixture is an object, its body";
        return false;
      }
      if (protocol::encoded_size(it.value(), kMaxBody) > kMaxBody) {
        why = where + " is larger than a state frame holds";
        return false;
      }
    } else if (std::string bad; !check_rows(it.value(), *topic, bad)) {
      why = where + ": " + bad;
      return false;
    }
  }
  return true;
}

// What grace-dev needs of a space to serve it, beyond its decoders: the page lint (tools/pages/build.py --lint) checks
// the rest of an author's space.
bool check_space(SpaceData& space, std::string& why) {
  space.id = space.pages.space;
  std::set<std::string, std::less<>> topics;
  for (const protocol::TopicInfo& topic : space.pages.topics) {
    if (!topics.insert(topic.id).second) {
      why = "pages.json: the topic " + topic.id + " is declared twice";
      return false;
    }
  }
  const protocol::TopicInfo* settings_topic = space.topic("settings");
  if (settings_topic != nullptr && settings_topic->shape != protocol::TopicShape::state) {
    why = "pages.json: the settings topic is a state topic";
    return false;
  }
  if (settings_topic == nullptr && !space.settings.settings.empty()) {
    why = "pages.json: the space has settings but declares no settings topic, so no view would see them";
    return false;
  }
  // GraceUI's own commands are its own space's: another space taking them would answer for GraceUI (D81).
  for (const char* own : {"menu.close", "dashboard.disconnect"}) {
    if (space.id != "graceui" && space.has_command(own)) {
      why = std::string("pages.json: ") + own + " is a command of GraceUI's own space alone";
      return false;
    }
  }
  std::set<std::string, std::less<>> pages;
  for (const protocol::Page& page : space.pages.pages) {
    pages.insert(page.id);
  }
  for (const protocol::Category& category : space.pages.categories) {
    for (const std::string& page : category.pages) {
      if (pages.find(page) == pages.end()) {
        why = "pages.json: the category " + category.id + " lists " + page + ", which is not a page";
        return false;
      }
    }
  }
  for (const protocol::Page& page : space.pages.pages) {
    for (const protocol::PageAction& action : page.actions.value_or(std::vector<protocol::PageAction>{})) {
      if (!space.has_command(action.command)) {
        why = "pages.json: " + page.id + "'s action " + action.id + " sends " + action.command +
              ", which the space does not declare";
        return false;
      }
    }
    std::vector<const protocol::PageSection*> sections;  // pointers into `page` itself, never into a copy
    if (page.sections.has_value()) {
      for (const protocol::PageSection& section : *page.sections) {
        sections.push_back(&section);
      }
    }
    if (page.tabs.has_value()) {
      for (const protocol::PageTab& tab : *page.tabs) {
        for (const protocol::PageSection& section : tab.sections) {
          sections.push_back(&section);
        }
      }
    }
    for (const protocol::PageSection* section : sections) {
      std::vector<protocol::PageBlock> blocks = section->blocks;
      for (const protocol::PageBlock& block : section->advanced.value_or(std::vector<protocol::PageBlock>{})) {
        blocks.push_back(block);
      }
      for (const protocol::PageBlock& block : blocks) {
        if (block.kind != protocol::BlockKind::component || !block.topic.has_value()) {
          continue;
        }
        const protocol::TopicInfo* topic = space.topic(*block.topic);
        const std::string where = "pages.json: " + page.id + ", section " + section->id + ": ";
        if (topic == nullptr) {
          why = where + "the topic " + *block.topic + " is not one the space declares";
          return false;
        }
        if (block.component.has_value() &&
            (topic->shape == protocol::TopicShape::stream) != is_stream_component(*block.component)) {
          why = where + std::string(protocol::to_string(*block.component)) + " cannot draw the " +
                std::string(protocol::to_string(topic->shape)) + " topic " + topic->id;
          return false;
        }
      }
    }
  }
  pages::SettingsStore store;
  std::string bad;
  if (!store.load(space.settings, bad)) {
    why = "settings.json: " + bad;
    return false;
  }
  view::PageView view;
  if (!view.load(space.pages, space.settings, bad)) {
    why = "pages.json and settings.json disagree: " + bad;
    return false;
  }
  return check_fixtures(space, why);
}

bool finish(SpaceData& space, std::string& why) {
  if (!check_space(space, why)) {
    why = space.origin + ": " + why;
    return false;
  }
  return true;
}

}  // namespace

const protocol::TopicInfo* SpaceData::topic(std::string_view id) const noexcept {
  for (const protocol::TopicInfo& t : pages.topics) {
    if (t.id == id) {
      return &t;
    }
  }
  // A family's member (D215): the family's own description, a state.
  const std::size_t slash = id.rfind('/');
  if (slash == std::string_view::npos || slash == 0 || slash + 1 >= id.size()) {
    return nullptr;
  }
  for (const protocol::TopicInfo& t : pages.topics) {
    if (t.id == id.substr(0, slash) && t.family.value_or(false)) {
      return &t;
    }
  }
  return nullptr;
}

bool SpaceData::has_command(std::string_view name) const noexcept {
  for (const std::string& command : pages.commands) {
    if (command == name) {
      return true;
    }
  }
  return false;
}

const protocol::Setting* SpaceData::setting(std::string_view key) const noexcept {
  for (const protocol::Setting& s : settings.settings) {
    if (s.key == key) {
      return &s;
    }
  }
  return nullptr;
}

bool SpaceData::has_page(std::string_view id) const noexcept {
  for (const protocol::Page& page : pages.pages) {
    if (page.id == id) {
      return true;
    }
  }
  return false;
}

std::size_t ring_of(const protocol::TopicInfo& topic) noexcept {
  return topic.ring.has_value() && *topic.ring != 0 ? *topic.ring : kDefaultRing;
}

std::vector<std::string> builtin_ids() {
  std::vector<std::string> out;
  for (const pages::BuiltinSpace& space : pages::builtin_spaces()) {
    out.emplace_back(space.id);
  }
  return out;
}

bool load_builtin(std::string_view id, SpaceData& out, std::string& why) {
  for (const pages::BuiltinSpace& space : pages::builtin_spaces()) {
    if (space.id != id) {
      continue;
    }
    out = SpaceData{};
    out.builtin = true;
    out.origin = "the built-in space " + std::string(id);
    if (!decode_text(space.pages, "pages.json", out.pages, why) ||
        !decode_text(space.settings, "settings.json", out.settings, why)) {
      why = out.origin + ": " + why;
      return false;
    }
    for (const EmbeddedFixture& file : embedded_fixtures()) {
      if (file.space != id) {
        continue;
      }
      if (file.file == "topics.json") {
        std::string bad;
        if (!parse_json(file.text, out.fixtures, bad)) {
          why = out.origin + ": topics.json: " + bad;
          return false;
        }
      } else if (file.file == "scenario.json") {
        out.scenario_name = "fixtures/" + std::string(id) + "/scenario.json";
        out.scenario_text = file.text;
      }
    }
    return finish(out, why);
  }
  why = "there is no built-in space " + std::string(id) + " (there are:";
  for (const std::string& known : builtin_ids()) {
    why += " " + known;
  }
  why += ")";
  return false;
}

bool load_folder(const std::filesystem::path& folder, SpaceData& out, std::string& why) {
  out = SpaceData{};
  out.origin = "the space folder '" + name_of(folder) + "'";
  std::error_code error;
  if (!std::filesystem::is_directory(folder, error) || error) {
    why = out.origin + ": not a folder";
    return false;
  }
  std::string text;
  std::string bad;
  for (const char* file : {"pages.json", "settings.json"}) {
    if (!read_file(folder / file, kMaxPagesFile, text, bad)) {
      why = out.origin + ": " + file + ": " + bad;
      return false;
    }
    const bool decoded = std::string_view(file) == "pages.json" ? decode_text(text, file, out.pages, bad)
                                                                 : decode_text(text, file, out.settings, bad);
    if (!decoded) {
      why = out.origin + ": " + bad;
      return false;
    }
  }
  if (std::filesystem::exists(folder / "topics.json", error)) {
    if (!read_file(folder / "topics.json", kMaxFixtureFile, text, bad) || !parse_json(text, out.fixtures, bad)) {
      why = out.origin + ": topics.json: " + bad;
      return false;
    }
  }
  if (std::filesystem::exists(folder / "scenario.json", error)) {
    if (!read_file(folder / "scenario.json", kMaxFixtureFile, out.scenario_text, bad)) {
      why = out.origin + ": scenario.json: " + bad;
      return false;
    }
    out.scenario_name = "scenario.json";
  }
  return finish(out, why);
}

const protocol::Setting& graceui_setting(std::string_view key) {
  static const protocol::SettingsDescription description = [] {
    protocol::SettingsDescription out;
    protocol::DecodeError error;
    protocol::decode(Json::parse(pages::builtin_spaces()[0].settings), out, "", error);
    return out;
  }();
  for (const protocol::Setting& s : description.settings) {
    if (s.key == key) {
      return s;
    }
  }
  static const protocol::Setting none{};
  return none;
}

std::uint16_t default_port() {
  const protocol::Json& value = graceui_setting("dashboard.port").default_;
  const std::uint64_t port = value.is_number_unsigned() ? value.get<std::uint64_t>() : 0;
  return port >= 1024 && port <= 65535 ? static_cast<std::uint16_t>(port) : std::uint16_t{0};
}

double default_ui_scale() {
  const protocol::Json& value = graceui_setting("ui.scale").default_;
  return value.is_number() ? value.get<double>() : 1.0;
}

}  // namespace grace::dev
