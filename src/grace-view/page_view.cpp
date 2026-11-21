// SPDX-License-Identifier: MIT
// page_view.cpp: one space's menu in game (page_view.h).
#include "grace/view/page_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace grace::view {
namespace {

using protocol::Json;
using protocol::Setting;
using protocol::SettingType;
using tokens::Colour;
using tokens::TypeRole;
namespace size = tokens::size;

bool numeric(SettingType type) {
  return type == SettingType::integer || type == SettingType::number || type == SettingType::duration_s;
}

// Decimals a step shows: 0.05 two, 0.5 one, 1 none.
int decimals_of(double step) {
  int d = 0;
  while (d < 4 && std::fabs(step * std::pow(10.0, d) - std::round(step * std::pow(10.0, d))) > 1e-9) {
    ++d;
  }
  return d;
}

std::string lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

// A tri-state's three values, its third auto or inherit (D163), and their labels.
constexpr std::size_t kNone = static_cast<std::size_t>(-1);  // no focus index: nothing the pointer can act on

const char* tri_value(const Setting& s, int i) {
  if (i < 2) {
    return i == 0 ? "on" : "off";
  }
  return s.third.value_or(protocol::TriThird::auto_) == protocol::TriThird::inherit ? "inherit" : "auto";
}
const char* tri_label(const Setting& s, int i) {
  if (i < 2) {
    return i == 0 ? "On" : "Off";
  }
  return s.third.value_or(protocol::TriThird::auto_) == protocol::TriThird::inherit ? "Inherit" : "Auto";
}

// The layout's running state: where the next row goes, and the focused element's rectangle once it is placed.
struct Cursor {
  DisplayList& out;
  const Fonts& fonts;
  float s;
  std::uint16_t clip = 0;
  Rect focus{};
  bool focus_found = false;

  void fill(Rect r, Colour c, float radius = 0, std::uint8_t alpha = 255) {
    Item item;
    item.shape = Shape::fill;
    item.colour = c;
    item.alpha = alpha;
    item.clip = clip;
    item.rect = r;
    item.radius = radius;
    out.items.push_back(item);
  }
  void stroke(Rect r, Colour c, float width, float radius = 0, std::uint8_t alpha = 255) {
    Item item;
    item.shape = Shape::stroke;
    item.colour = c;
    item.alpha = alpha;
    item.clip = clip;
    item.rect = r;
    item.radius = radius;
    item.width = width;
    out.items.push_back(item);
  }
  void line(float x, float y, float w, Colour c) {
    Item item;
    item.shape = Shape::line;
    item.colour = c;
    item.clip = clip;
    item.rect = Rect{x, y, w, 1};
    out.items.push_back(item);
  }
  void circle(Rect r, Colour c, std::uint8_t alpha = 255) {
    Item item;
    item.shape = Shape::circle;
    item.colour = c;
    item.alpha = alpha;
    item.clip = clip;
    item.rect = r;
    out.items.push_back(item);
  }
  void diamond(Rect r, Colour c) {
    Item item;
    item.shape = Shape::diamond;
    item.colour = c;
    item.clip = clip;
    item.rect = r;
    item.width = std::max(1.0f, std::round(s));
    out.items.push_back(item);
  }
  float text(std::string_view t, TypeRole role, float x, float y, float max_width, Colour c, std::uint8_t alpha = 255) {
    return place_text(out, fonts, role, s, t, x, y, max_width, c, alpha, clip);
  }
  float width(std::string_view t, TypeRole role) const { return measure(fonts, role, s, t); }
  void focused(Rect r) {
    focus = r;
    focus_found = true;
  }
};

// The parts protocol 1.2 adds (G34, D192 to D195), checked at registration as the page lint checks them: a table's row
// actions and row secret, a form block, a text setting's choices from a topic.
bool check_parts(const protocol::SpacePages& pages, const std::vector<protocol::Setting>& settings, std::string& why) {
  const auto declared = [&](const std::string& command) {
    return std::find(pages.commands.begin(), pages.commands.end(), command) != pages.commands.end();
  };
  const auto block_ok = [&](const protocol::Page& page, const protocol::PageBlock& block) {
    const std::string where = page.id + ": ";
    const bool table = block.kind == protocol::BlockKind::component && block.component == protocol::Component::table;
    if ((block.row_actions || block.row_secret) && !table) {
      why = where + "row actions and a row secret go on a table";
      return false;
    }
    std::vector<std::string> ids;
    for (const protocol::RowAction& action : block.row_actions.value_or(std::vector<protocol::RowAction>{})) {
      if (action.label.empty() || !declared(action.command) ||
          std::find(ids.begin(), ids.end(), action.id) != ids.end()) {
        why = where + "row action " + action.id + " has no label, a command the space does not declare, or a twin";
        return false;
      }
      if (action.tone == protocol::ActionTone::destructive && (!action.confirm || action.confirm->empty())) {
        why = where + "a destructive row action without its confirm text";  // §7.3, D173, D192
        return false;
      }
      ids.push_back(action.id);
    }
    if (block.row_secret && !declared(block.row_secret->command)) {
      why = where + "a row secret's command is not one the space declares";
      return false;
    }
    const bool form = block.kind == protocol::BlockKind::form;
    if (form != (block.command.has_value() || block.fields.has_value() || block.submit.has_value())) {
      why = where + "a form block, and only a form block, has a command, fields and a submit label";
      return false;
    }
    if (form) {
      const std::vector<protocol::FormField>& fields = block.fields.value_or(std::vector<protocol::FormField>{});
      if (!block.command || !declared(*block.command) || fields.empty() || !block.submit || block.submit->empty() ||
          block.keys || block.component || block.topic) {
        why = where + "a form block has a declared command, one to four fields and a submit label, and nothing else";
        return false;
      }
      std::vector<std::string> names;
      for (const protocol::FormField& field : fields) {
        if (field.label.empty() || field.max_ < 1 || field.max_ > 200 ||
            std::find(names.begin(), names.end(), field.id) != names.end()) {
          why = where + "form field " + field.id + " has no label, a twin, or a max outside 1 to 200";
          return false;
        }
        names.push_back(field.id);
      }
    }
    return true;
  };
  for (const protocol::Page& page : pages.pages) {
    std::vector<const protocol::PageSection*> sections;
    if (page.sections) {
      for (const protocol::PageSection& section : *page.sections) {
        sections.push_back(&section);
      }
    }
    if (page.tabs) {
      for (const protocol::PageTab& tab : *page.tabs) {
        for (const protocol::PageSection& section : tab.sections) {
          sections.push_back(&section);
        }
      }
    }
    for (const protocol::PageSection* section : sections) {
      for (const protocol::PageBlock& block : section->blocks) {
        if (!block_ok(page, block)) {
          return false;
        }
      }
      for (const protocol::PageBlock& block : section->advanced.value_or(std::vector<protocol::PageBlock>{})) {
        if (!block_ok(page, block)) {
          return false;
        }
      }
    }
  }
  for (const protocol::Setting& setting : settings) {
    if (!setting.choices_from) {
      continue;
    }
    const std::string& topic = *setting.choices_from;
    const auto found = std::find_if(pages.topics.begin(), pages.topics.end(),
                                    [&](const protocol::TopicInfo& t) { return t.id == topic; });
    if (setting.type != protocol::SettingType::text || setting.choices || found == pages.topics.end() ||
        found->shape != protocol::TopicShape::state || topic == "settings" || topic == "space.state") {
      why = setting.key + ": choices_from is on a text setting, naming a state topic of the space's own (D193)";
      return false;
    }
  }
  return true;
}

}  // namespace

// ---- Loading and focus.

bool PageView::load(const protocol::SpacePages& pages, const protocol::SettingsDescription& settings, std::string& why) {
  pages_ = pages;
  settings_ = settings.settings;
  by_key_.clear();
  for (std::size_t i = 0; i < settings_.size(); ++i) {
    by_key_.emplace(settings_[i].key, i);
  }
  if (pages_.pages.empty()) {
    why = "the space has no page";
    return false;
  }
  for (const protocol::Page& page : pages_.pages) {
    const auto check_sections = [&](const std::vector<protocol::PageSection>& list) {
      for (const protocol::PageSection& section : list) {
        if (section.collapsed.value_or(false)) {
          collapsed_.insert(page.id + "/" + section.id);
        }
        for (const auto* blocks : {&section.blocks, section.advanced ? &*section.advanced : nullptr}) {
          if (blocks == nullptr) {
            continue;
          }
          for (const protocol::PageBlock& block : *blocks) {
            for (const std::string& key : block.keys.value_or(std::vector<std::string>{})) {
              if (by_key_.find(key) == by_key_.end()) {
                why = page.id + " names " + key + ", which is not a setting of the space";
                return false;
              }
            }
          }
        }
      }
      return true;
    };
    if (page.sections && !check_sections(*page.sections)) {
      return false;
    }
    for (const protocol::PageTab& tab : page.tabs.value_or(std::vector<protocol::PageTab>{})) {
      if (!check_sections(tab.sections)) {
        return false;
      }
    }
  }
  if (!check_parts(pages_, settings_, why)) {
    return false;
  }
  page_ = 0;
  tab_ = 0;
  scope_ = Scope::page;
  rebuild();
  focus_first_control();
  return true;
}

void PageView::set_changed(const std::vector<std::string>& keys) {
  changed_.clear();
  changed_.insert(keys.begin(), keys.end());
  changed_known_ = true;
}

void PageView::set_inherited(const std::vector<protocol::InheritedValue>& inherited) {
  inherited_.clear();
  for (const protocol::InheritedValue& one : inherited) {
    inherited_[one.key] = one;
  }
}

void PageView::set_values(const Json& values) {
  if (values.is_object()) {
    for (auto it = values.begin(); it != values.end(); ++it) {
      values_[it.key()] = it.value();
    }
  }
}

namespace {

// "0.6" as (0, 6); false for anything else.
bool parse_increment(std::string_view text, std::pair<unsigned, unsigned>& out) {
  const std::size_t dot = text.find('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= text.size() || text.size() > 16) {
    return false;
  }
  unsigned parts[2] = {0, 0};
  for (int part = 0; part < 2; ++part) {
    const std::string_view digits = part == 0 ? text.substr(0, dot) : text.substr(dot + 1);
    for (const char ch : digits) {
      if (ch < '0' || ch > '9') {
        return false;
      }
      parts[part] = parts[part] * 10 + static_cast<unsigned>(ch - '0');
    }
  }
  out = {parts[0], parts[1]};
  return true;
}

}  // namespace

void PageView::set_increment(std::string_view increment) {
  absent_.clear();
  std::pair<unsigned, unsigned> running{};
  if (parse_increment(increment, running)) {
    for (const protocol::Page& page : pages_.pages) {
      std::pair<unsigned, unsigned> ships{};
      if (page.increment && parse_increment(*page.increment, ships) && ships > running) {
        absent_.insert(page.id);
      }
    }
  }
  if (!pages_.pages.empty() && !present(pages_.pages[page_].id)) {
    for (std::size_t p = 0; p < pages_.pages.size(); ++p) {
      if (present(pages_.pages[p].id)) {
        page_ = p;  // the open page has gone: the first present one
        tab_ = 0;
        page_focus_ = 0;
        break;
      }
    }
  }
  rebuild();
}

bool PageView::present(std::string_view page_id) const { return absent_.find(page_id) == absent_.end(); }

bool PageView::open(std::string_view page_id) {
  for (std::size_t i = 0; i < pages_.pages.size(); ++i) {
    if (pages_.pages[i].id == page_id && present(page_id)) {
      page_ = i;
      tab_ = 0;
      page_focus_ = 0;
      scroll_ = 0;
      confirming_.clear();
      rebuild();
      focus_first_control();
      return true;
    }
  }
  return false;
}

std::string_view PageView::page() const noexcept {
  return pages_.pages.empty() ? std::string_view() : std::string_view(pages_.pages[page_].id);
}

const std::vector<protocol::PageSection>& PageView::sections() const {
  const protocol::Page& page = current();
  if (page.tabs && tab_ < page.tabs->size()) {
    return (*page.tabs)[tab_].sections;
  }
  return page.sections ? *page.sections : no_sections_;
}

const Setting* PageView::setting(std::string_view key) const {
  const auto it = by_key_.find(key);
  return it == by_key_.end() ? nullptr : &settings_[it->second];
}

const Json& PageView::value(std::string_view key) const {
  const auto it = values_.find(key);
  if (it != values_.end()) {
    return *it;
  }
  const Setting* s = setting(key);
  return s != nullptr ? s->default_ : null_;
}

void PageView::set_rail(RailContext context) {
  context_ = std::move(context);
  rebuild();
  rail_focus_ = rail_index_of_page();
}

namespace {

// A table body's switch column (D181): {command, key, on, off}; null if it has none, or one not whole.
const Json* switch_of(const Json& column) {
  const auto found = column.find("switch");
  if (found == column.end() || !found->is_object()) {
    return nullptr;
  }
  const Json& sw = *found;
  const auto is_text = [&](const char* name) { return sw.contains(name) && sw[name].is_string(); };
  return is_text("command") && is_text("key") && is_text("on") && is_text("off") ? &sw : nullptr;
}

// A cell's words: a string as it is, a number as JSON writes it, anything else nothing.
std::string cell_words(const Json& row, const std::string& column) {
  const auto found = row.find(column);
  if (found == row.end()) {
    return {};
  }
  if (found->is_string()) {
    return found->get<std::string>();
  }
  return found->is_number() ? found->dump() : std::string();
}

}  // namespace

void PageView::set_topic(std::string_view topic, const Json& body) {
  if (topic == "space.state") {
    // The space's greyed pages (D188): a page named in `greyed` with its words, every other page with `inactive`'s if
    // it has some; each text as the space wrote it, at most 200 bytes.
    greyed_.clear();
    inactive_.clear();
    const auto words = [](const Json& value) {
      return value.is_string() && !value.get_ref<const std::string&>().empty() &&
                     value.get_ref<const std::string&>().size() <= 200
                 ? value.get<std::string>()
                 : std::string();
    };
    if (body.is_object() && body.contains("greyed") && body["greyed"].is_object()) {
      for (const auto& [page, text] : body["greyed"].items()) {
        if (const std::string w = words(text); !w.empty()) {
          greyed_[page] = w;
        }
      }
    }
    if (body.is_object() && body.contains("inactive")) {
      inactive_ = words(body["inactive"]);
    }
    if (!confirming_.empty() && !greyed_words(current().id).empty()) {
      confirming_.clear();  // a destructive action asking for its second press goes inert with its page
    }
    return;
  }
  const std::string was = focused();
  topics_[std::string(topic)] = body;
  for (const std::string& prefix : std::array<std::string, 2>{std::string(topic) + "/", std::string(topic) + "#"}) {
    for (auto at = refusals_.lower_bound(prefix); at != refusals_.end() && at->first.rfind(prefix, 0) == 0;) {
      at = refusals_.erase(at);  // a new body answers what was refused of its rows
    }
  }
  rebuild();
  if (scope_ == Scope::page) {  // the same element, wherever it now is, as rows come and go (D181, D192)
    for (std::size_t i = 0; i < focus_.size(); ++i) {
      if (name_of(Scope::page, i) == was) {
        page_focus_ = i;
        break;
      }
    }
  }
}

bool PageView::drawn_in_game(protocol::Component component) {
  switch (component) {
    case protocol::Component::table:
    case protocol::Component::key_value_list:
    case protocol::Component::check_list:
    case protocol::Component::stat_tiles:
    case protocol::Component::computed_sentence:
      return true;
    default:
      return false;  // the streams (a live feed, a log tail) and anything newer: the browser view's for now
  }
}

namespace {

// A string prop of a block, or empty.
std::string prop_text(const protocol::PageBlock& block, const char* name) {
  if (!block.props || !block.props->is_object()) {
    return {};
  }
  const auto found = block.props->find(name);
  return found != block.props->end() && found->is_string() ? found->get<std::string>() : std::string();
}

}  // namespace

PageView::Drawn PageView::drawn(const protocol::PageBlock& block) const {
  Drawn out;
  if (!block.topic) {
    return out;
  }
  const auto found = topics_.find(*block.topic);
  if (found == topics_.end() || !found->second.is_object()) {
    return out;
  }
  out.published = true;
  const std::string field = prop_text(block, "field");
  if (field.empty()) {
    out.body = &found->second;
  } else if (const auto member = found->second.find(field); member != found->second.end() && member->is_object()) {
    out.body = &*member;  // one topic feeding several blocks (D185)
  }
  return out;
}

std::string PageView::empty_words(const protocol::PageBlock& block, bool published) const {
  const std::string own = prop_text(block, "empty");
  return published && !own.empty() ? own : std::string("Nothing to show yet.");  // the space's words once it spoke (D186)
}

// A row secret's status in words (D194, D197): set, refused (the key was sent but the provider refused it), none (none
// is needed), or else missing.
static const char* secret_status_words(const std::string& status) {
  return status == "set" ? "Set" : status == "refused" ? "Refused" : status == "none" ? "Not needed" : "Missing";
}

std::string PageView::row_action_id(const protocol::PageBlock& block, const Json& row, std::size_t r,
                                    const protocol::RowAction& action) {
  const std::string key = row.is_object() ? cell_words(row, action.key) : std::string();
  return cell_base(block) + "/" + (key.empty() ? std::to_string(r) : key) + "/" + action.id;
}

Action PageView::row_action(const Focus& f) {
  if (f.block == nullptr || !f.block->row_actions || f.sub >= f.block->row_actions->size() ||
      !greyed_words(current().id).empty()) {
    return {};  // a greyed page's row actions are inert, as its header's (D188)
  }
  const protocol::RowAction& a = (*f.block->row_actions)[f.sub];
  const Json* table = table_of(*f.block);
  if (table == nullptr || f.index >= (*table)["rows"].size()) {
    return {};
  }
  const Json& row = (*table)["rows"][f.index];
  const std::string asking = "row:" + f.id;
  if ((a.confirm || a.tone == protocol::ActionTone::destructive) && confirming_ != asking) {
    confirming_ = asking;  // its second press, as a header action's (§7.3, D173, D192)
    return {};
  }
  confirming_.clear();
  refusals_.erase(f.id);
  Action action;
  action.kind = Action::Kind::command;
  action.command.space = pages_.space;
  action.command.name = a.command;
  action.command.args = Json::object({{a.key, row.is_object() && row.contains(a.key) ? row[a.key] : Json()}});
  if (a.tone == protocol::ActionTone::destructive) {
    action.command.args["confirm"] = true;
  }
  return action;
}

std::vector<protocol::Choice> PageView::live_choices(const protocol::Setting& s, bool& published) const {
  std::vector<protocol::Choice> out;
  published = false;
  if (!s.choices_from) {
    return out;
  }
  const auto found = topics_.find(*s.choices_from);
  if (found == topics_.end() || !found->second.is_object()) {
    return out;
  }
  published = true;
  const Json& body = found->second;
  if (!body.contains("choices") || !body["choices"].is_array()) {
    return out;
  }
  for (const Json& choice : body["choices"]) {
    const std::string value = choice.is_object() ? cell_words(choice, "value") : std::string();
    if (value.empty() || value.size() > 200) {
      continue;  // a value the store would refuse (D193)
    }
    const std::string label = cell_words(choice, "label");
    out.push_back(protocol::Choice{value, label.empty() ? value : label});
    if (out.size() >= 256) {
      break;
    }
  }
  return out;
}

std::string PageView::cell_base(const protocol::PageBlock& block) {
  const std::string field = prop_text(block, "field");
  return *block.topic + (field.empty() ? std::string() : "#" + field);
}

std::string PageView::greyed_words(std::string_view page_id) const {
  if (const auto found = greyed_.find(page_id); found != greyed_.end()) {
    return found->second;
  }
  return inactive_;
}

bool PageView::greyed(const protocol::Setting& setting) const {
  return !greyed_words(current().id).empty() || !greyed_words(setting.home).empty();
}

std::vector<std::string> PageView::component_topics() const {
  std::vector<std::string> out;
  for (const protocol::TopicInfo& topic : pages_.topics) {
    if (topic.id == "space.state" && topic.shape == protocol::TopicShape::state) {
      out.push_back(topic.id);  // its greyed pages (D188), watched with every page
    }
  }
  const auto add = [&](const std::vector<protocol::PageBlock>& blocks) {
    for (const protocol::PageBlock& block : blocks) {
      if (block.kind == protocol::BlockKind::component && block.topic && block.component && drawn_in_game(*block.component) &&
          std::find(out.begin(), out.end(), *block.topic) == out.end()) {
        out.push_back(*block.topic);
      }
    }
  };
  for (const protocol::PageSection& section : sections()) {
    add(section.blocks);
    if (section.advanced) {
      add(*section.advanced);
    }
  }
  // A picker's choices (D193), for every setting the page shows, homed here or a shortcut.
  const auto pickers = [&](const std::vector<protocol::PageBlock>& blocks) {
    for (const protocol::PageBlock& block : blocks) {
      for (const std::string& key : block.keys.value_or(std::vector<std::string>{})) {
        const auto at = by_key_.find(key);
        if (at != by_key_.end() && settings_[at->second].choices_from &&
            std::find(out.begin(), out.end(), *settings_[at->second].choices_from) == out.end()) {
          out.push_back(*settings_[at->second].choices_from);
        }
      }
    }
  };
  for (const protocol::PageSection& section : sections()) {
    pickers(section.blocks);
    if (section.advanced) {
      pickers(*section.advanced);
    }
  }
  return out;
}

const Json* PageView::table_of(const protocol::PageBlock& block) const {
  if (block.kind != protocol::BlockKind::component || block.component != protocol::Component::table || !block.topic) {
    return nullptr;
  }
  const Json* body = drawn(block).body;
  return body != nullptr && body->contains("columns") && (*body)["columns"].is_array() && body->contains("rows") &&
                 (*body)["rows"].is_array()
             ? body
             : nullptr;
}

Action PageView::toggle_cell(const Focus& f) const {
  if (!greyed_words(current().id).empty()) {
    return {};  // a greyed page's controls are inert (D188)
  }
  const std::string_view base = std::string_view(f.id).substr(0, f.id.find('/'));
  const std::size_t hash = base.find('#');
  const auto found = topics_.find(base.substr(0, hash));
  if (found == topics_.end() || !found->second.is_object()) {
    return {};
  }
  const Json* table = &found->second;
  if (hash != std::string_view::npos) {
    const auto member = table->find(std::string(base.substr(hash + 1)));
    if (member == table->end() || !member->is_object()) {
      return {};
    }
    table = &*member;  // a table fed by its topic's field (D185)
  }
  const Json& body = *table;
  if (!body.contains("columns") || !body["columns"].is_array() || !body.contains("rows") || !body["rows"].is_array()) {
    return {};
  }
  for (const Json& column : body["columns"]) {
    const Json* sw = switch_of(column);
    if (sw == nullptr || !column.contains("id") || !column["id"].is_string() || f.index >= body["rows"].size()) {
      continue;
    }
    const Json& row = body["rows"][f.index];
    const bool on = cell_words(row, column["id"].get<std::string>()) == (*sw)["on"].get<std::string>();
    Action action;
    action.kind = Action::Kind::command;
    action.command.space = pages_.space;
    action.command.name = (*sw)["command"].get<std::string>();
    const std::string key = (*sw)["key"].get<std::string>();
    action.command.args = Json::object({{key, row.contains(key) ? row[key] : Json()}, {"on", !on}});
    return action;
  }
  return {};
}

void PageView::focus_rail(bool switcher) {
  scope_ = Scope::rail;
  rail_focus_ = rail_index_of_page();
  if (switcher && !rail_.empty() && rail_[0].kind == RailEntry::Kind::switcher) {
    rail_focus_ = 0;
  }
}

Action PageView::follow(const RailLink& link) const {
  Action action;
  if (!link.space.empty()) {
    action.kind = Action::Kind::switch_space;
    action.space = link.space;
    action.page = link.page;
  }
  return action;
}

void PageView::rebuild() {
  using Kind = RailEntry::Kind;
  rail_.clear();
  if (!context_.switcher.empty()) {
    rail_.push_back({Kind::switcher, 0});
  }
  for (std::size_t h = 0; h < context_.head.size(); ++h) {
    rail_.push_back({Kind::head, h});
  }
  const std::string& category = current().category;
  for (std::size_t c = 0; c < pages_.categories.size(); ++c) {
    const auto& listed = pages_.categories[c].pages;
    if (std::none_of(listed.begin(), listed.end(), [&](const std::string& id) { return present(id); })) {
      continue;  // every page of it ships in a later increment
    }
    rail_.push_back({Kind::category, c});
    if (pages_.categories[c].id == category) {  // the accordion: the open category's pages
      for (const std::string& id : pages_.categories[c].pages) {
        for (std::size_t p = 0; p < pages_.pages.size(); ++p) {
          if (pages_.pages[p].id == id && present(id)) {
            rail_.push_back({Kind::page, p});
          }
        }
      }
    }
  }
  for (std::size_t f = 0; f < context_.foot.size(); ++f) {
    rail_.push_back({Kind::foot, f});
  }
  focus_.clear();
  const protocol::Page& page = current();
  for (std::size_t a = 0; page.actions && a < page.actions->size(); ++a) {
    focus_.push_back({FocusKind::action, (*page.actions)[a].id, a});
  }
  for (std::size_t t = 0; page.tabs && t < page.tabs->size(); ++t) {
    focus_.push_back({FocusKind::tab, (*page.tabs)[t].id, t});
  }
  const auto& list = sections();
  for (std::size_t i = 0; i < list.size(); ++i) {
    const protocol::PageSection& section = list[i];
    focus_.push_back({FocusKind::section, section.id, i});
    if (collapsed_.count(page.id + "/" + section.id) != 0) {
      continue;
    }
    const auto add_blocks = [&](const std::vector<protocol::PageBlock>& blocks) {
      for (const protocol::PageBlock& block : blocks) {
        if (const Json* table = table_of(block); table != nullptr) {
          // A table's rows: each row's switch, if it has a switch column (D181), then its row actions (D192), each a
          // focus stop of its own, by the row's key.
          const Json* sw = nullptr;
          for (const Json& column : (*table)["columns"]) {
            if ((sw = switch_of(column)) != nullptr) {
              break;  // one switch column a table
            }
          }
          const std::size_t actions = block.row_actions ? block.row_actions->size() : 0;
          for (std::size_t r = 0; r < (*table)["rows"].size(); ++r) {
            const Json& row = (*table)["rows"][r];
            if (sw != nullptr) {
              const std::string id = row.is_object() ? cell_words(row, (*sw)["key"].get<std::string>()) : std::string();
              focus_.push_back({FocusKind::cell, cell_base(block) + "/" + (id.empty() ? std::to_string(r) : id), r});
            }
            for (std::size_t a = 0; a < actions; ++a) {
              focus_.push_back({FocusKind::row_action, row_action_id(block, row, r, (*block.row_actions)[a]), r, &block, a});
            }
          }
          continue;
        }
        for (const std::string& key : block.keys.value_or(std::vector<std::string>{})) {
          if (block.kind == protocol::BlockKind::shortcuts && !present(settings_[by_key_.at(key)].home)) {
            continue;  // a shortcut to a page of a later increment
          }
          focus_.push_back({block.kind == protocol::BlockKind::shortcuts ? FocusKind::shortcut : FocusKind::setting, key,
                            by_key_.at(key)});
        }
      }
    };
    add_blocks(section.blocks);
    if (section.advanced && !section.advanced->empty()) {
      focus_.push_back({FocusKind::advanced, section.id, i});
      if (advanced_open_.count(page.id + "/" + section.id) != 0) {
        add_blocks(*section.advanced);
      }
    }
  }
  for (std::size_t r = 0; r < page.related.size(); ++r) {
    if (!present(page.related[r])) {
      continue;
    }
    focus_.push_back({FocusKind::related, page.related[r], r});
  }
  page_focus_ = std::min(page_focus_, focus_.empty() ? std::size_t{0} : focus_.size() - 1);
  rail_focus_ = std::min(rail_focus_, rail_.empty() ? std::size_t{0} : rail_.size() - 1);
}

void PageView::focus_first_control() {
  page_focus_ = 0;
  for (std::size_t i = 0; i < focus_.size(); ++i) {
    if (focus_[i].kind != FocusKind::section && focus_[i].kind != FocusKind::tab) {
      page_focus_ = i;
      return;
    }
  }
}

std::size_t PageView::rail_index_of_page() const {
  for (std::size_t i = 0; i < rail_.size(); ++i) {
    if (rail_[i].own_page() && rail_[i].index == page_) {
      return i;
    }
  }
  return 0;
}

std::string PageView::focused() const {
  return name_of(scope_, scope_ == Scope::rail ? rail_focus_ : page_focus_);
}

std::string PageView::name_of(Scope scope, std::size_t index) const {
  if (scope == Scope::top) {
    return "search";
  }
  if (scope == Scope::rail) {
    if (index >= rail_.size()) {
      return "rail:";
    }
    const RailEntry& e = rail_[index];
    switch (e.kind) {
      case RailEntry::Kind::category: return "rail:" + pages_.categories[e.index].id;
      case RailEntry::Kind::page: return "rail:" + pages_.pages[e.index].id;
      case RailEntry::Kind::head: return "rail:" + context_.head[e.index].space + "/" + context_.head[e.index].page;
      case RailEntry::Kind::foot: return "rail:" + context_.foot[e.index].space + "/" + context_.foot[e.index].page;
      case RailEntry::Kind::switcher: return "rail:switcher";
    }
    return "rail:";
  }
  if (index >= focus_.size()) {
    return "";
  }
  static constexpr const char* kKind[] = {"action", "tab", "section", "advanced", "setting", "shortcut", "related",
                                          "cell", "row"};
  const Focus& f = focus_[index];
  return std::string(kKind[static_cast<int>(f.kind)]) + ":" + f.id;
}

std::vector<std::pair<std::string, PageView::Bounds>> PageView::all_bounds() const {
  std::vector<std::pair<std::string, Bounds>> out;
  out.reserve(hits_.size());
  for (const Hit& h : hits_) {
    out.emplace_back(name_of(h.scope, h.index), Bounds{h.rect, h.control.w > 0 ? h.control : h.rect});
  }
  return out;
}

PageView::Bounds PageView::bounds_of(std::string_view name) const {
  for (const Hit& h : hits_) {
    if (name_of(h.scope, h.index) == name) {
      return Bounds{h.rect, h.control.w > 0 ? h.control : h.rect};
    }
  }
  return {};
}

// ---- Values.

std::string PageView::format(std::string_view key, const Json& v) const {
  const Setting* s = setting(key);
  if (s == nullptr) {
    return "";
  }
  char text[64];
  switch (s->type) {
    case SettingType::bool_:
      return v.is_boolean() && v.get<bool>() ? "On" : "Off";
    case SettingType::integer:
    case SettingType::number:
    case SettingType::duration_s: {
      const double n = v.is_number() ? v.get<double>() : 0.0;
      std::snprintf(text, sizeof text, "%.*f", decimals_of(s->step.value_or(1.0)), n);
      std::string out = text;
      if (s->unit) {
        const bool word = !s->unit->empty() && (*s->unit)[0] >= 'a' && (*s->unit)[0] <= 'z';
        out += (word ? " " : "") + *s->unit;
      }
      return out;
    }
    case SettingType::choice:
      for (const protocol::Choice& c : s->choices.value_or(std::vector<protocol::Choice>{})) {
        if (v.is_string() && c.value == v.get_ref<const std::string&>()) {
          return c.label;
        }
      }
      return "";
    case SettingType::tri_state:
      for (int i = 0; i < 3; ++i) {
        if (v.is_string() && v.get_ref<const std::string&>() == tri_value(*s, i)) {
          return tri_label(*s, i);
        }
      }
      return "";
    case SettingType::secret:
      return v.is_string() && !v.get_ref<const std::string&>().empty() ? "Set" : "Missing";  // §4.2.4's words
    case SettingType::text:
      if (s->choices_from && v.is_string() && v.get_ref<const std::string&>().empty() && s->default_ == Json("")) {
        return "Default";  // a picker whose default is none of its choices: the space's own default (D199)
      }
      if (s->choices_from && v.is_string()) {
        bool published = false;
        for (const protocol::Choice& c : live_choices(*s, published)) {
          if (c.value == v.get_ref<const std::string&>()) {
            return c.label;
          }
        }
      }
      return v.is_string() ? v.get<std::string>() : "";  // a value not in the list is still shown (D193)
    case SettingType::key_name:
      return v.is_string() ? v.get<std::string>() : "";
  }
  return "";
}

Action PageView::set(const Setting& s, Json v) {
  if (s.locked || greyed(s)) {
    return {};  // locked, or on a page its space has greyed (D188)
  }
  values_[s.key] = v;
  refusals_.erase(s.key);
  Action action;
  action.kind = Action::Kind::command;
  action.command.space = pages_.space;
  action.command.name = "settings.set";
  action.command.args = Json::object({{"key", s.key}, {"value", std::move(v)}});
  return action;
}

Action PageView::change(const Setting& s, int direction, bool shift) {
  const Json& v = value(s.key);
  switch (s.type) {
    case SettingType::bool_:
      return set(s, Json(!(v.is_boolean() && v.get<bool>())));
    case SettingType::integer:
    case SettingType::number:
    case SettingType::duration_s: {
      const double step = s.step.value_or(1.0) * (shift ? 10 : 1);
      double n = (v.is_number() ? v.get<double>() : 0.0) + direction * step;
      n = std::clamp(n, s.min_.value_or(n), s.max_.value_or(n));
      const double scale = std::pow(10.0, decimals_of(s.step.value_or(1.0)));
      n = std::round(n * scale) / scale;
      if (s.type == SettingType::integer) {
        return set(s, Json(static_cast<std::int64_t>(std::llround(n))));
      }
      return set(s, Json(n));
    }
    case SettingType::choice: {
      const auto& choices = s.choices.value_or(std::vector<protocol::Choice>{});
      if (choices.empty()) {
        return {};
      }
      std::size_t at = 0;
      for (std::size_t i = 0; i < choices.size(); ++i) {
        if (v.is_string() && choices[i].value == v.get_ref<const std::string&>()) {
          at = i;
        }
      }
      const std::size_t n = choices.size();
      const std::size_t next = (at + (direction > 0 ? 1 : n - 1)) % n;
      return set(s, Json(choices[next].value));
    }
    case SettingType::tri_state: {
      int at = 2;
      for (int i = 0; i < 3; ++i) {
        if (v.is_string() && v.get_ref<const std::string&>() == tri_value(s, i)) {
          at = i;
        }
      }
      return set(s, Json(tri_value(s, (at + (direction > 0 ? 1 : 2)) % 3)));
    }
    case SettingType::text:
      if (s.choices_from) {
        bool published = false;
        std::vector<protocol::Choice> choices = live_choices(s, published);
        if (choices.empty()) {
          return {};  // nothing to choose from yet (D193)
        }
        if (s.default_ == Json("")) {
          choices.insert(choices.begin(), protocol::Choice{"", "Default"});  // back to the space's default (D199)
        }
        const std::size_t n = choices.size();
        std::size_t next = direction > 0 ? 0 : n - 1;  // from a value not in the list: the first, or the last
        for (std::size_t i = 0; i < n; ++i) {
          if (v.is_string() && choices[i].value == v.get_ref<const std::string&>()) {
            next = (i + (direction > 0 ? 1 : n - 1)) % n;
          }
        }
        return set(s, Json(choices[next].value));
      }
      return {};
    case SettingType::key_name:
    case SettingType::secret:
      return {};  // GraceUI's own text field and key capture come with the in-game menu (v0.6, D57); the browser edits them
  }
  return {};
}

// ---- The mouse (§5.5).

const PageView::Hit* PageView::hit_at(float x, float y) const {
  for (std::size_t i = hits_.size(); i-- > 0;) {  // the last drawn is on top
    const Rect& r = hits_[i].rect;
    if (x >= r.x && x < r.right() && y >= r.y && y < r.bottom()) {
      return &hits_[i];
    }
  }
  return nullptr;
}

Action PageView::slide(float x) {
  const Setting* s = setting(drag_);
  if (s == nullptr || drag_track_.w <= 0 || s->locked) {
    return {};
  }
  const double lo = s->min_.value_or(0);
  const double hi = s->max_.value_or(1);
  const double t = std::clamp(static_cast<double>((x - drag_track_.x) / drag_track_.w), 0.0, 1.0);
  const double step = s->step.value_or(1.0);
  double n = lo + t * (hi - lo);
  n = step > 0 ? lo + std::round((n - lo) / step) * step : n;
  const double scale = std::pow(10.0, decimals_of(step));
  n = std::clamp(std::round(n * scale) / scale, lo, hi);
  const Json now = s->type == SettingType::integer ? Json(static_cast<std::int64_t>(std::llround(n))) : Json(n);
  const Json& was = value(s->key);
  if (was.is_number() && std::fabs(was.get<double>() - n) < 1e-9) {
    return {};  // the same value: nothing to send
  }
  return set(*s, now);
}

Action PageView::pointer(const Pointer& in) {
  pointer_x_ = in.x;
  pointer_y_ = in.y;
  pointer_in_ = in.kind != Pointer::Kind::leave;
  switch (in.kind) {
    case Pointer::Kind::leave:
    case Pointer::Kind::release:
      drag_.clear();
      return {};
    case Pointer::Kind::wheel:
      scroll_ = std::max(0.0f, std::round(scroll_ - in.wheel * 120.0f * scale_));  // three rows a notch; layout bounds it
      follow_ = false;
      return {};
    case Pointer::Kind::move:
      return drag_.empty() ? Action{} : slide(in.x);
    case Pointer::Kind::press:
      break;
  }
  drag_.clear();
  const Hit* hit = hit_at(in.x, in.y);
  if (hit == nullptr) {
    return {};
  }
  const Hit target = *hit;
  follow_ = true;
  if (target.scope == Scope::rail) {
    scope_ = Scope::rail;
    rail_focus_ = target.index;
    return input(Input{Key::activate, false});
  }
  if (target.scope == Scope::top) {
    scope_ = Scope::top;  // search waits for GraceUI's text field (§5.6): focus only
    return {};
  }
  if (target.index >= focus_.size()) {
    return {};
  }
  const Focus& clicked = focus_[target.index];
  if (!confirming_.empty() && !(clicked.kind == FocusKind::action && clicked.id == confirming_) &&
      !(clicked.kind == FocusKind::row_action && "row:" + clicked.id == confirming_)) {
    confirming_.clear();  // a click elsewhere lets a destructive action's confirmation go, as any other key does
  }
  scope_ = Scope::page;
  page_focus_ = target.index;
  const Focus f = focus_[target.index];
  const Rect& c = target.control;
  const bool on_control = c.w > 0 && in.x >= c.x && in.x < c.right() && in.y >= c.y && in.y < c.bottom();
  switch (f.kind) {
    case FocusKind::setting:
    case FocusKind::shortcut: {
      const Setting& st = settings_[f.index];
      if (f.kind == FocusKind::shortcut && !on_control) {
        return input(Input{Key::activate, false});  // the row's link to its home (D189)
      }
      if (!on_control || st.locked || st.availability == "later") {
        return {};  // focus only
      }
      if (st.type == SettingType::bool_) {
        return change(st, 1, false);
      }
      if (numeric(st.type)) {
        drag_ = st.key;  // its moves are the slider's until the button is released
        drag_track_ = target.track;
        return slide(in.x);
      }
      if ((st.type == SettingType::choice || st.type == SettingType::tri_state) && target.segments > 0) {
        const std::size_t at = std::min(
            target.segments - 1,
            static_cast<std::size_t>(std::max(0.0f, (in.x - c.x) / (c.w / static_cast<float>(target.segments)))));
        if (st.type == SettingType::tri_state) {
          return set(st, Json(tri_value(st, static_cast<int>(at))));
        }
        const auto& choices = st.choices.value_or(std::vector<protocol::Choice>{});
        return at < choices.size() ? set(st, Json(choices[at].value)) : Action{};
      }
      if (st.type == SettingType::text && st.choices_from) {
        return change(st, 1, false);  // a picker: the next choice (D193)
      }
      return {};  // a key, a text, a secret: edited in the browser view until GraceUI's own field
    }
    case FocusKind::cell:
      if (!on_control) {
        return {};
      }
      refusals_.erase(f.id);
      return toggle_cell(f);
    default:
      return input(Input{Key::activate, false});  // an action, a tab, a section, a shortcut, a related page: as Enter
  }
}

void PageView::refused(std::string_view key, std::string_view reason) {
  refusals_[std::string(key)] = std::string(reason);
  for (std::size_t i = 0; i < focus_.size(); ++i) {
    if ((focus_[i].kind == FocusKind::setting || focus_[i].kind == FocusKind::cell ||
         focus_[i].kind == FocusKind::row_action) && focus_[i].id == key) {
      scope_ = Scope::page;
      page_focus_ = i;
    }
  }
}

std::string PageView::availability_badge(const Setting& st) const {
  const std::string& a = st.availability;
  if (a.rfind("check:", 0) == 0) {
    return "Waits on a game check";
  }
  if (a.rfind("version:", 0) == 0) {
    return "Takes effect in v" + a.substr(8);
  }
  if (a.rfind("requires:", 0) == 0) {
    const std::string key = a.substr(9);
    const Json& v = value(key);
    const bool off = (v.is_boolean() && !v.get<bool>()) || (v.is_string() && v.get_ref<const std::string&>() == "off");
    const Setting* needed = setting(key);
    return off ? "Off because " + (needed != nullptr ? needed->label : key) + " is off" : std::string();
  }
  return a == "inert" ? "No effect in this version" : std::string();
}

void PageView::restore(std::string_view key, const Json& value) {
  const std::string name(key);
  if (value.is_null()) {
    values_.erase(name);
  } else {
    values_[name] = value;
  }
}

std::vector<SearchHit> PageView::search(std::string_view query) const {
  std::vector<SearchHit> hits;
  const std::string q = lower(query);
  if (q.empty()) {
    return hits;
  }
  for (const Setting& s : settings_) {
    bool match = lower(s.label).find(q) != std::string::npos || s.key.find(q) != std::string::npos;
    for (const std::string& synonym : s.synonyms.value_or(std::vector<std::string>{})) {
      match = match || lower(synonym).find(q) != std::string::npos;
    }
    if (match) {
      if (present(s.home)) {
        hits.push_back({s.key, s.home, s.label});
      }
    }
  }
  std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) { return a.label < b.label; });
  return hits;
}

// ---- Input (§5.3, §5.4, §5.7).

// The first control of the page's focus order: past the section labels where there is one (§5.7).
Action PageView::open_index(std::size_t index, Scope scope) {
  page_ = index;
  tab_ = 0;
  page_focus_ = 0;
  scroll_ = 0;
  confirming_.clear();
  rebuild();
  focus_first_control();
  rail_focus_ = rail_index_of_page();
  scope_ = scope;
  Action action;
  action.kind = Action::Kind::opened_page;
  action.page = current().id;
  return action;
}

Action PageView::input(const Input& in) {
  if (rail_.empty() && focus_.empty()) {
    return {};
  }
  follow_ = true;  // a key moves focus, and the page follows it again
  // A destructive action waits for its second press; any other key lets it go.
  const bool confirming = !confirming_.empty();
  if (confirming && !(in.key == Key::activate && scope_ == Scope::page)) {
    confirming_.clear();
    if (in.key == Key::back) {
      return {};
    }
  }
  // Rail order across categories, for Page Up and Page Down.
  std::vector<std::size_t> order;
  for (const protocol::Category& c : pages_.categories) {
    for (const std::string& id : c.pages) {
      for (std::size_t p = 0; p < pages_.pages.size(); ++p) {
        if (pages_.pages[p].id == id && present(id)) {
          order.push_back(p);
        }
      }
    }
  }
  const auto at_in_order = std::find(order.begin(), order.end(), page_) - order.begin();
  switch (in.key) {
    case Key::previous_page:
      if (at_in_order > 0) {
        return open_index(order[static_cast<std::size_t>(at_in_order - 1)], scope_);
      }
      return {};
    case Key::next_page:
      if (static_cast<std::size_t>(at_in_order + 1) < order.size()) {
        return open_index(order[static_cast<std::size_t>(at_in_order + 1)], scope_);
      }
      return {};
    case Key::previous_tab:
    case Key::next_tab: {
      const auto& tabs = current().tabs;
      if (!tabs || tabs->size() < 2) {
        return {};
      }
      const int step = in.key == Key::next_tab ? 1 : -1;
      const int next = static_cast<int>(tab_) + step;
      if (next < 0 || next >= static_cast<int>(tabs->size())) {
        return {};
      }
      tab_ = static_cast<std::size_t>(next);
      rebuild();
      page_focus_ = 0;
      for (std::size_t i = 0; i < focus_.size(); ++i) {
        if (focus_[i].kind == FocusKind::tab && focus_[i].index == tab_) {
          page_focus_ = i;
        }
      }
      return {};
    }
    default:
      break;
  }
  if (scope_ == Scope::rail) {
    switch (in.key) {
      case Key::up:
        rail_focus_ = rail_focus_ > 0 ? rail_focus_ - 1 : 0;
        return {};
      case Key::down:
        rail_focus_ = std::min(rail_focus_ + 1, rail_.size() - 1);
        return {};
      case Key::left:
        if (rail_[rail_focus_].kind == RailEntry::Kind::switcher) {
          return follow(context_.previous);
        }
        return {};
      case Key::right:
      case Key::next:
        if (in.key == Key::right && rail_[rail_focus_].kind == RailEntry::Kind::switcher) {
          return follow(context_.next);
        }
        scope_ = in.key == Key::next ? Scope::top : Scope::page;
        return {};
      case Key::activate: {
        const RailEntry e = rail_[rail_focus_];
        switch (e.kind) {
          case RailEntry::Kind::switcher: return follow(context_.next);
          case RailEntry::Kind::head: return follow(context_.head[e.index]);
          case RailEntry::Kind::foot: return follow(context_.foot[e.index]);
          case RailEntry::Kind::category:
          case RailEntry::Kind::page: break;
        }
        if (e.category()) {
          // The accordion: open the category and go to its first page, focus staying on the rail.
          for (const std::string& id : pages_.categories[e.index].pages) {
            for (std::size_t p = 0; p < pages_.pages.size(); ++p) {
              if (pages_.pages[p].id == id && present(id)) {
                return open_index(p, Scope::rail);
              }
            }
          }
          return {};
        }
        return open_index(e.index, Scope::page);
      }
      case Key::back: {
        Action action;
        action.kind = Action::Kind::close;
        return action;
      }
      default:
        return {};
    }
  }
  if (scope_ == Scope::top) {
    switch (in.key) {
      case Key::next:
      case Key::down:
        scope_ = Scope::page;
        return {};
      case Key::previous:
      case Key::back:
        scope_ = Scope::rail;
        rail_focus_ = rail_index_of_page();
        return {};
      default:
        return {};  // search's text field comes with GraceUI's own field (D57)
    }
  }
  // The page.
  if (focus_.empty()) {
    if (in.key == Key::back || in.key == Key::left || in.key == Key::previous) {
      scope_ = Scope::rail;
      rail_focus_ = rail_index_of_page();
    }
    return {};
  }
  const Focus f = focus_[page_focus_];
  const Setting* s = f.kind == FocusKind::setting ? &settings_[f.index] : nullptr;
  // A shortcut row's control changes as its home's does (D189); Enter follows its link.
  const Setting* changeable = f.kind == FocusKind::setting || f.kind == FocusKind::shortcut ? &settings_[f.index] : nullptr;
  switch (in.key) {
    case Key::up:
    case Key::previous:
      if (page_focus_ > 0) {
        --page_focus_;
      } else if (in.key == Key::previous) {
        scope_ = Scope::top;
      }
      return {};
    case Key::down:
    case Key::next:
      page_focus_ = std::min(page_focus_ + 1, focus_.size() - 1);
      return {};
    case Key::left:
    case Key::right:
      if (changeable != nullptr && changeable->type != SettingType::key_name &&
          (changeable->type != SettingType::text || changeable->choices_from) && changeable->type != SettingType::secret) {
        return change(*changeable, in.key == Key::right ? 1 : -1, in.shift);
      }
      if (f.kind == FocusKind::cell) {
        refusals_.erase(f.id);  // a new try: the last refusal's line goes
        return toggle_cell(f);  // a switch turns with ← and →, as a setting's does
      }
      if (in.key == Key::left) {
        scope_ = Scope::rail;  // ← at the page's left edge goes back to the rail
        rail_focus_ = rail_index_of_page();
      }
      return {};
    case Key::back:
      scope_ = Scope::rail;
      rail_focus_ = rail_index_of_page();
      return {};
    case Key::reset:
      if (s != nullptr && !s->locked && !greyed(*s)) {
        values_[s->key] = s->default_;
        refusals_.erase(s->key);
        Action action;
        action.kind = Action::Kind::command;
        action.command.space = pages_.space;
        action.command.name = "settings.reset";
        action.command.args = Json::object({{"scope", "key"}, {"target", s->key}});
        return action;
      }
      return {};
    case Key::activate:
      switch (f.kind) {
        case FocusKind::row_action:
          return row_action(f);
        case FocusKind::action: {
          const protocol::PageAction& a = (*current().actions)[f.index];
          if (!greyed_words(current().id).empty()) {
            return {};  // a greyed page's actions are inert (D188)
          }
          if ((a.confirm || a.tone == protocol::ActionTone::destructive) && confirming_ != a.id) {
            confirming_ = a.id;  // the button asks for its second press; a destructive one always (§7.3, D173)
            return {};
          }
          confirming_.clear();
          Action action;
          action.kind = Action::Kind::command;
          action.command.space = pages_.space;
          action.command.name = a.command;
          // A destructive action's command carries its confirmation, after the second press (§7.3, D173).
          action.command.args = a.tone == protocol::ActionTone::destructive ? Json::object({{"confirm", true}})
                                                                            : Json::object();
          return action;
        }
        case FocusKind::tab:
          tab_ = f.index;
          rebuild();
          return {};
        case FocusKind::section: {
          const std::string id = current().id + "/" + f.id;
          if (!collapsed_.erase(id)) {
            collapsed_.insert(id);
          }
          rebuild();
          return {};
        }
        case FocusKind::advanced: {
          const std::string id = current().id + "/" + f.id;
          if (!advanced_open_.erase(id)) {
            advanced_open_.insert(id);
          }
          rebuild();
          return {};
        }
        case FocusKind::setting:
          if (s->type == SettingType::bool_ || s->type == SettingType::choice || s->type == SettingType::tri_state) {
            return change(*s, 1, false);
          }
          return {};
        case FocusKind::cell:
          refusals_.erase(f.id);
          return toggle_cell(f);
        case FocusKind::shortcut:
        case FocusKind::related: {
          const std::string target = f.kind == FocusKind::shortcut ? settings_[f.index].home : f.id;
          for (std::size_t p = 0; p < pages_.pages.size(); ++p) {
            if (pages_.pages[p].id == target && present(target)) {
              Action action = open_index(p, Scope::page);
              if (f.kind == FocusKind::shortcut) {  // the row focused on its home page
                for (std::size_t i = 0; i < focus_.size(); ++i) {
                  if (focus_[i].kind == FocusKind::setting && focus_[i].id == f.id) {
                    page_focus_ = i;
                  }
                }
              }
              return action;
            }
          }
          return {};  // another space's page: the host follows a "<space>/<page>" link
        }
      }
      return {};
    default:
      return {};
  }
}

// ---- Layout (§3.6, §3.16, §4.2, §4.7).

void PageView::layout(const Viewport& viewport, const Fonts& fonts, DisplayList& out) {
  for (int pass = 0; pass < 2; ++pass) {
    out.clear();
    out.generation = fonts.generation;
    const float s = viewport.scale;
    scale_ = s;
    Cursor c{out, fonts, s};
    const float W = viewport.width;
    const float H = viewport.height;
    out.clips.push_back(Rect{0, 0, W, H});
    // Where the pointer acts (§5.5): each focusable element, within the clip it is drawn in.
    hits_.clear();
    const auto hit = [&](Rect rect, Scope scope, std::size_t index, Rect control = {}, Rect track = {},
                         std::size_t segments = 0) {
      const Rect& clip = out.clips[c.clip];
      const float left = std::max(rect.x, clip.x);
      const float top = std::max(rect.y, clip.y);
      const float right = std::min(rect.right(), clip.right());
      const float bottom = std::min(rect.bottom(), clip.bottom());
      if (right > left && bottom > top && index != kNone) {
        hits_.push_back(Hit{Rect{left, top, right - left, bottom - top}, control, track, scope, index, segments});
      }
    };
    const auto hovered = [&](Rect rect) {
      const Rect& clip = out.clips[c.clip];
      const auto inside = [&](const Rect& r) {
        return pointer_x_ >= r.x && pointer_x_ < r.right() && pointer_y_ >= r.y && pointer_y_ < r.bottom();
      };
      return pointer_in_ && drag_.empty() && inside(rect) && inside(clip);
    };
    const auto focus_index = [&](FocusKind kind, std::string_view id) {
      for (std::size_t i = 0; i < focus_.size(); ++i) {
        if (focus_[i].kind == kind && focus_[i].id == id) {
          return i;
        }
      }
      return kNone;
    };
    // The scrim over the game, then the menu: at most 1.9 × the height wide, centred (§3.6).
    c.fill(Rect{0, 0, W, H}, Colour::scrim);
    const float menu_w = std::min(W, std::round(1.9f * H));
    const float menu_x = std::round((W - menu_w) / 2);
    c.fill(Rect{menu_x, 0, menu_w, H}, Colour::ground_1);

    // ---- The rail.
    const float rail_w = std::round(size::rail * s);
    c.fill(Rect{menu_x, 0, rail_w, H}, Colour::ground_0);
    const float pad = std::round(size::rail_padding * s);
    float y = pad;
    c.text(context_.title.empty() ? pages_.name : context_.title, TypeRole::display_md, menu_x + pad, y,
           rail_w - 2 * pad, Colour::gold);
    y += std::round(line_height(TypeRole::display_md, s) + 16 * s);
    // GraceUI's links at the rail's foot, pinned to its bottom under a rule (§4.2.2's foot, D79).
    float foot_y = H - pad;
    for (const RailLink& link : context_.foot) {
      foot_y -= std::round((link.category ? size::rail_category : size::rail_page) * s);
    }
    for (std::size_t i = 0; i < rail_.size(); ++i) {
      const RailEntry& e = rail_[i];
      const bool link = e.kind == RailEntry::Kind::head || e.kind == RailEntry::Kind::foot;
      const RailLink* other = e.kind == RailEntry::Kind::head   ? &context_.head[e.index]
                              : e.kind == RailEntry::Kind::foot ? &context_.foot[e.index]
                                                                : nullptr;
      const bool is_page = e.own_page() || (other != nullptr && !other->category);
      const float h = std::round((is_page ? size::rail_page : size::rail_category) * s);
      if (e.kind == RailEntry::Kind::foot && (i == 0 || rail_[i - 1].kind != RailEntry::Kind::foot)) {
        y = std::max(y, foot_y);
        c.line(menu_x + pad, y, rail_w - 2 * pad, Colour::rule);
      }
      const Rect row{menu_x, y, rail_w, h};
      hit(row, Scope::rail, i);
      if ((e.category() || (link && other->category && e.kind == RailEntry::Kind::head)) && i != 0 &&
          rail_[i - 1].kind != RailEntry::Kind::switcher) {
        c.line(menu_x + pad, y, rail_w - 2 * pad, Colour::rule);
      }
      if (e.kind == RailEntry::Kind::switcher) {
        if (hovered(row)) {
          c.fill(row, Colour::tint_hover);
        }
        // The content spaces' switcher: ‹ and › around the open one's place among them.
        const std::string label = "\u2039  " + context_.switcher + "  \u203A";
        c.text(label, TypeRole::ui_sm, menu_x + pad, y + std::round((h - line_height(TypeRole::ui_sm, s)) / 2),
               rail_w - 2 * pad, Colour::ink_mute);
        if (scope_ == Scope::rail && i == rail_focus_) {
          c.focused(row);
        }
        y += h;
        continue;
      }
      const bool open_category = e.category() && pages_.categories[e.index].id == current().category;
      const bool current_page = e.own_page() && e.index == page_;
      if (current_page) {
        c.fill(row, Colour::tint_select);
        c.fill(Rect{menu_x, y, std::round(2 * s), h}, Colour::gold);
      } else if (hovered(row)) {
        c.fill(row, Colour::tint_hover);
      }
      const TypeRole role = is_page ? TypeRole::ui_sm : TypeRole::rail;
      const float indent = is_page ? std::round(16 * s) : 0;
      const Colour ink = current_page ? Colour::gold_hi : (open_category ? Colour::ink_hi : Colour::ink_mute);
      const std::string& label = other != nullptr ? other->label
                                 : e.category()  ? pages_.categories[e.index].name
                                                 : pages_.pages[e.index].name;
      c.text(label, role, menu_x + pad + indent, y + std::round((h - line_height(role, s)) / 2),
             rail_w - 2 * pad - indent, ink);
      if (scope_ == Scope::rail && i == rail_focus_) {
        c.focused(row);
      }
      y += h;
    }

    // ---- The top bar: search at the left, the key that closes at the right.
    const float page_x = menu_x + rail_w;
    const float page_w = menu_w - rail_w;
    const float bar_h = std::round(size::topbar * s);
    const float gutter = std::round(size::gutter * s);
    const Rect search{page_x + gutter, std::round(8 * s), std::round(280 * s), bar_h - std::round(16 * s)};
    c.fill(search, Colour::ground_well, std::round(tokens::radius::control * s));
    hit(search, Scope::top, 0);
    c.text("Search", TypeRole::ui_sm, search.x + std::round(10 * s),
           search.y + std::round((search.h - line_height(TypeRole::ui_sm, s)) / 2), search.w - std::round(20 * s),
           Colour::ink_faint);
    if (scope_ == Scope::top) {
      c.focused(search);
    }
    const char* close_hint = "F5  Close";
    const float close_w = c.width(close_hint, TypeRole::ui_sm);
    c.text(close_hint, TypeRole::ui_sm, page_x + page_w - gutter - close_w,
           std::round((bar_h - line_height(TypeRole::ui_sm, s)) / 2), close_w + 1, Colour::ink_faint);
    c.line(page_x, bar_h, page_w, Colour::rule);

    // ---- The page, clipped between the top bar and the key hints, scrolled to keep focus in view.
    const float footer_h = std::round(36 * s);
    const Rect page_area{page_x, bar_h + 1, page_w, H - bar_h - 1 - footer_h};
    out.clips.push_back(page_area);
    c.clip = 1;
    const float content_w = std::min(page_w - 2 * gutter, std::round(size::content_max * s));
    const float x0 = page_x + gutter;
    y = page_area.y + std::round(24 * s) - scroll_;
    const protocol::Page& page = current();
    const auto page_focus = [&](FocusKind kind, std::string_view id) {
      return scope_ == Scope::page && !focus_.empty() && focus_[page_focus_].kind == kind && focus_[page_focus_].id == id;
    };
    // The header: title, description, actions, the lozenge rule.
    c.text(page.name, TypeRole::display_lg, x0, y, content_w / 2, Colour::ink_hi);
    float action_x = x0 + content_w;
    for (std::size_t a = page.actions ? page.actions->size() : 0; a-- > 0;) {
      const protocol::PageAction& act = (*page.actions)[a];
      const bool asking = confirming_ == act.id;
      const std::string label = asking ? "Press again to confirm" : act.label;
      const float w = c.width(label, TypeRole::ui) + std::round(32 * s);
      const Rect button{action_x - w, y + std::round(2 * s), w, std::round(32 * s)};
      const float r = std::round(tokens::radius::control * s);
      hit(button, Scope::page, focus_index(FocusKind::action, act.id));
      Colour ink = Colour::ink_hi;
      if (act.tone == protocol::ActionTone::primary) {
        c.fill(button, Colour::gold, r);
        ink = Colour::on_gold;
      } else if (act.tone == protocol::ActionTone::destructive) {
        c.fill(button, Colour::rot_fill, r);
      } else {
        if (hovered(button)) {
          c.fill(button, Colour::tint_hover, r);
        }
        c.stroke(button, Colour::edge, std::round(tokens::kGrace.size.edge_width * s), r);
      }
      c.text(label, TypeRole::ui, button.x + std::round(16 * s),
             button.y + std::round((button.h - line_height(TypeRole::ui, s)) / 2), w, ink);
      if (page_focus(FocusKind::action, act.id)) {
        c.focused(button);
      }
      action_x = button.x - std::round(12 * s);
    }
    y += std::round(line_height(TypeRole::display_lg, s) + 4 * s);
    c.text(page.description, TypeRole::ui_sm, x0, y, content_w, Colour::ink_mute);
    y += std::round(line_height(TypeRole::ui_sm, s) + 12 * s);
    c.line(x0, y, content_w, Colour::rule_strong);
    c.diamond(Rect{x0, y - std::round(4 * s), std::round(9 * s), std::round(9 * s)}, Colour::gold_deep);
    y += std::round(20 * s);
    // A page its space has greyed (D188): the space's words over it, its controls inert and faint.
    if (const std::string words = greyed_words(page.id); !words.empty()) {
      const Rect band{x0, y, content_w, std::round(44 * s)};
      c.fill(band, Colour::ground_3, std::round(tokens::radius::control * s));
      c.fill(Rect{band.x, band.y, std::round(3 * s), band.h}, Colour::ember);
      c.text(words, TypeRole::ui, band.x + std::round(16 * s), band.y + std::round((band.h - line_height(TypeRole::ui, s)) / 2),
             band.w - std::round(32 * s), Colour::ink_hi);
      y += band.h + std::round(16 * s);
    }
    // Tabs.
    if (page.tabs && !page.tabs->empty()) {
      float tx = x0;
      for (std::size_t t = 0; t < page.tabs->size(); ++t) {
        const protocol::PageTab& tab = (*page.tabs)[t];
        const bool active = t == tab_;
        const float w = c.width(tab.name, TypeRole::tab);
        c.text(tab.name, TypeRole::tab, tx, y, w + 1, active ? Colour::gold_hi : Colour::ink_mute);
        const float under = y + line_height(TypeRole::tab, s) + std::round(6 * s);
        if (active) {
          c.fill(Rect{tx, under, w, std::round(2 * s)}, Colour::gold);
        }
        hit(Rect{tx, y, w, under + std::round(2 * s) - y}, Scope::page, focus_index(FocusKind::tab, tab.id));
        if (page_focus(FocusKind::tab, tab.id)) {
          c.focused(Rect{tx, y, w, under + std::round(2 * s) - y});
        }
        tx += w + std::round(24 * s);
      }
      y += std::round(line_height(TypeRole::tab, s) + 9 * s);
      c.line(x0, y, content_w, Colour::rule);
      y += std::round(20 * s);
    }
    // Sections: cards of rows.
    const float card_pad = std::round(20 * s);
    const float control_w = std::round(size::settings_control * s);
    const float row_h = std::round(size::settings_row * s);
    for (const protocol::PageSection& section : sections()) {
      const float card_top = y;
      const std::size_t card_at = out.items.size();  // the card's ground goes under its rows, inserted below
      y += std::round(16 * s);
      const bool collapsed = collapsed_.count(page.id + "/" + section.id) != 0;
      c.text(section.title, TypeRole::eyebrow, x0 + card_pad, y, content_w - 2 * card_pad - std::round(20 * s),
             Colour::gold);
      c.text(collapsed ? "+" : "-", TypeRole::ui_sm, x0 + content_w - card_pad - std::round(10 * s), y,
             std::round(12 * s), Colour::ink_faint);
      hit(Rect{x0 + card_pad, y, content_w - 2 * card_pad, line_height(TypeRole::eyebrow, s)}, Scope::page,
          focus_index(FocusKind::section, section.id));
      if (page_focus(FocusKind::section, section.id)) {
        c.focused(Rect{x0 + card_pad, y, content_w - 2 * card_pad, line_height(TypeRole::eyebrow, s)});
      }
      y += std::round(line_height(TypeRole::eyebrow, s) + 4 * s);
      if (section.description) {
        c.text(*section.description, TypeRole::ui_sm, x0 + card_pad, y, content_w - 2 * card_pad, Colour::ink_mute);
        y += std::round(line_height(TypeRole::ui_sm, s));
      }
      y += std::round(8 * s);
      const auto draw_blocks = [&](const std::vector<protocol::PageBlock>& blocks) {
        for (const protocol::PageBlock& block : blocks) {
          if (block.kind == protocol::BlockKind::form) {
            // A form (D195): its fields are filled in the browser view until GraceUI has a text field of its own (§5.6).
            std::string labels;
            for (const protocol::FormField& field : block.fields.value_or(std::vector<protocol::FormField>{})) {
              labels += (labels.empty() ? "" : ", ") + field.label;
            }
            c.text("Filled in the browser view: " + labels + ".", TypeRole::ui_sm, x0 + card_pad, y + std::round(12 * s),
                   content_w - 2 * card_pad, Colour::ink_mute);
            y += row_h;
            continue;
          }
          if (block.kind == protocol::BlockKind::component) {
            const float inner_x = x0 + card_pad;
            const float inner_w = content_w - 2 * card_pad;
            const auto one_line = [&](const std::string& words, Colour ink) {
              c.text(words, TypeRole::ui_sm, inner_x, y + std::round(12 * s), inner_w, ink);
              y += row_h;
            };
            if (block.component == protocol::Component::statement) {
              // A statement: its own words, from the page data (§2.5).
              const Json* props = block.props ? &*block.props : nullptr;
              if (props != nullptr && props->is_object() && props->contains("text") && (*props)["text"].is_string()) {
                c.text((*props)["text"].get<std::string>(), TypeRole::ui, inner_x, y + std::round(10 * s), inner_w,
                       Colour::ink_body);
                y += row_h;
              }
              continue;
            }
            if (!block.component || !drawn_in_game(*block.component)) {
              one_line("Shown in the browser view.", Colour::ink_mute);
              continue;
            }
            const Drawn shown = drawn(block);
            const Json* body = shown.body;
            const std::string none = empty_words(block, shown.published);  // D186
            if (block.component != protocol::Component::table) {
              // A component of a state topic (D184): what its topic's body holds, as the browser view draws it, or
              // the empty state while it holds nothing (invariant 2).
              const auto objects = [&](const char* field) {
                std::vector<const Json*> out;
                if (body != nullptr && body->contains(field) && (*body)[field].is_array()) {
                  for (const Json& item : (*body)[field]) {
                    if (item.is_object()) {
                      out.push_back(&item);
                    }
                  }
                }
                return out;
              };
              const auto words = [](const Json& item, const char* field) { return cell_words(item, field); };
              if (block.component == protocol::Component::computed_sentence) {
                const std::string sentence = body != nullptr ? cell_words(*body, "sentence") : std::string();
                one_line(sentence.empty() ? none : sentence,
                         sentence.empty() ? Colour::ink_mute : Colour::ink_body);
                continue;
              }
              if (block.component == protocol::Component::stat_tiles) {
                std::vector<const Json*> tiles;
                for (const Json* t : objects("tiles")) {
                  if (!words(*t, "label").empty() && !words(*t, "value").empty()) {
                    tiles.push_back(t);
                  }
                }
                if (tiles.empty()) {
                  one_line(none, Colour::ink_mute);
                  continue;
                }
                const float gap = std::round(12 * s);
                const float tile_w = std::floor((inner_w - gap * static_cast<float>(tiles.size() - 1)) /
                                                static_cast<float>(tiles.size()));
                const float tile_h = std::round(84 * s);
                for (std::size_t i = 0; i < tiles.size(); ++i) {
                  const Rect tile{inner_x + (tile_w + gap) * static_cast<float>(i), y + std::round(6 * s), tile_w,
                                  tile_h - std::round(12 * s)};
                  c.fill(tile, Colour::ground_3, std::round(tokens::radius::control * s));
                  const float pad = std::round(12 * s);
                  c.text(words(*tiles[i], "label"), TypeRole::ui_sm, tile.x + pad, tile.y + std::round(8 * s),
                         tile.w - 2 * pad, Colour::ink_mute);
                  std::string value = words(*tiles[i], "value");
                  if (const std::string unit = words(*tiles[i], "unit"); !unit.empty()) {
                    value += " " + unit;
                  }
                  c.text(value, TypeRole::display_md, tile.x + pad,
                         tile.y + std::round(8 * s) + line_height(TypeRole::ui_sm, s), tile.w - 2 * pad, Colour::ink_hi);
                }
                y += tile_h;
                continue;
              }
              const bool checks = block.component == protocol::Component::check_list;
              std::vector<const Json*> rows;
              for (const Json* item : objects(checks ? "checks" : "items")) {
                if (!words(*item, "label").empty() && (checks || !words(*item, "value").empty())) {
                  rows.push_back(item);
                }
              }
              if (rows.empty()) {
                one_line(none, Colour::ink_mute);
                continue;
              }
              for (const Json* item : rows) {
                c.line(inner_x, y, inner_w, Colour::rule);
                const float text_y = y + std::round((row_h - line_height(TypeRole::ui, s)) / 2);
                const float small_y = y + std::round((row_h - line_height(TypeRole::ui_sm, s)) / 2);
                if (checks) {
                  // A check: its state in words and its colour, its label, and its detail after them.
                  const std::string state = words(*item, "state");
                  const char* word = state == "pass" ? "Passed" : state == "warn" ? "Warning" : state == "fail" ? "Failed"
                                                                                                                 : "Waiting";
                  const Colour ink = state == "pass" ? Colour::moss : state == "warn" ? Colour::ember
                                     : state == "fail" ? Colour::rot : Colour::ink_faint;
                  const float word_w = std::round(110 * s);
                  c.text(word, TypeRole::ui_sm, inner_x, small_y, word_w - std::round(8 * s), ink);
                  const float label_w = c.text(words(*item, "label"), TypeRole::ui, inner_x + word_w, text_y,
                                               inner_w - word_w, Colour::ink_body);
                  if (const std::string detail = words(*item, "detail"); !detail.empty()) {
                    const float dx = inner_x + word_w + label_w + std::round(16 * s);
                    c.text(detail, TypeRole::ui_sm, dx, small_y, std::max(0.0f, inner_x + inner_w - dx), Colour::ink_mute);
                  }
                } else {
                  // A key and its value: the label at the left, the value after the first third.
                  c.text(words(*item, "label"), TypeRole::ui, inner_x, text_y, inner_w / 3 - std::round(12 * s),
                         Colour::ink_hi);
                  c.text(words(*item, "value"), TypeRole::ui_sm, inner_x + inner_w / 3, small_y, inner_w * 2 / 3,
                         Colour::ink_body);
                }
                y += row_h;
              }
              continue;
            }
            const Json* table = table_of(block);
            if (table == nullptr) {
              one_line(none, Colour::ink_mute);
              continue;
            }
            // A table in game (D181): a row a row, its first column's words, the others' after them, and a switch
            // column's switch at the right, as a setting's.
            std::vector<std::string> text_columns;
            std::string switch_column;
            std::string secret_label;  // a row secret's column (D194): its status in words, edited in the browser
            const Json* sw = nullptr;
            for (const Json& column : (*table)["columns"]) {
              if (!column.is_object() || !column.contains("id") || !column["id"].is_string()) {
                continue;
              }
              if (const Json* found = switch_of(column); found != nullptr && sw == nullptr) {
                sw = found;
                switch_column = column["id"].get<std::string>();
              } else {
                text_columns.push_back(column["id"].get<std::string>());
                if (block.row_secret && column["id"] == block.row_secret->column) {
                  secret_label = cell_words(column, "label");
                }
              }
            }
            const std::vector<protocol::RowAction>& row_actions =
                block.row_actions ? *block.row_actions : std::vector<protocol::RowAction>{};
            const Json& rows = (*table)["rows"];
            if (rows.empty()) {
              c.line(x0 + card_pad, y, content_w - 2 * card_pad, Colour::rule);
              c.text(none, TypeRole::ui_sm, x0 + card_pad, y + std::round(12 * s),
                     content_w - 2 * card_pad, Colour::ink_mute);
              y += row_h;
              continue;
            }
            for (std::size_t r = 0; r < rows.size(); ++r) {
              const Json& line = rows[r];
              if (!line.is_object()) {
                continue;
              }
              const std::string row_key = sw != nullptr ? cell_words(line, (*sw)["key"].get<std::string>()) : std::string();
              const std::string cell_id = cell_base(block) + "/" + (row_key.empty() ? std::to_string(r) : row_key);
              const auto refusal = sw != nullptr ? refusals_.find(cell_id) : refusals_.end();
              const Rect row{x0 + card_pad, y, content_w - 2 * card_pad, row_h};
              if (sw != nullptr && hovered(row)) {
                c.fill(row, Colour::tint_hover);
              }
              c.line(row.x, row.y, row.w, Colour::rule);
              const std::string label = text_columns.empty() ? std::string() : cell_words(line, text_columns[0]);
              std::string rest;
              for (std::size_t k = 1; k < text_columns.size(); ++k) {
                std::string words = cell_words(line, text_columns[k]);
                if (block.row_secret && text_columns[k] == block.row_secret->column) {
                  words = (secret_label.empty() ? "" : secret_label + ": ") + secret_status_words(words);
                }
                if (!words.empty()) {
                  rest += (rest.empty() ? "" : "  ·  ") + words;
                }
              }
              const float label_y = y + std::round((row_h - line_height(TypeRole::ui, s)) / 2);
              c.text(label, TypeRole::ui, row.x, label_y, row.w / 3 - std::round(12 * s), Colour::ink_hi);
              // Its row actions (D192), right-aligned, the last at the row's end; a switch, if any, before them.
              float right = row.right();
              std::string row_refusal_id;
              for (std::size_t a = row_actions.size(); a-- > 0;) {
                const protocol::RowAction& act = row_actions[a];
                const std::string id = row_action_id(block, line, r, act);
                const bool asking = confirming_ == "row:" + id;
                const std::string words = asking ? "Press again to confirm" : act.label;
                const float w = c.width(words, TypeRole::ui_sm) + std::round(24 * s);
                const Rect button{right - w, y + std::round((row_h - 28 * s) / 2), w, std::round(28 * s)};
                const float rr = std::round(tokens::radius::control * s);
                Colour ink = Colour::ink_hi;
                if (act.tone == protocol::ActionTone::primary) {
                  c.fill(button, Colour::gold, rr);
                  ink = Colour::on_gold;
                } else if (act.tone == protocol::ActionTone::destructive) {
                  c.fill(button, Colour::rot_fill, rr);
                } else {
                  c.stroke(button, Colour::edge, std::round(tokens::kGrace.size.edge_width * s), rr);
                }
                c.text(words, TypeRole::ui_sm, button.x + std::round(12 * s),
                       button.y + std::round((button.h - line_height(TypeRole::ui_sm, s)) / 2), w, ink);
                hit(button, Scope::page, focus_index(FocusKind::row_action, id));
                if (page_focus(FocusKind::row_action, id)) {
                  c.focused(button);
                }
                if (refusals_.count(id) != 0) {
                  row_refusal_id = id;
                }
                right = button.x - std::round(12 * s);
              }
              const float actions_w = row.right() - right;
              const float rest_w = (sw != nullptr ? row.w - control_w : row.w) - actions_w - row.w / 3 - std::round(12 * s);
              c.text(rest, TypeRole::ui_sm, row.x + row.w / 3,
                     y + std::round((row_h - line_height(TypeRole::ui_sm, s)) / 2), rest_w, Colour::ink_mute);
              if (sw != nullptr) {
                const std::string words = cell_words(line, switch_column);
                const bool on = words == (*sw)["on"].get<std::string>();
                const Rect control{right - std::round(40 * s), y + std::round((row_h - 22 * s) / 2),
                                   std::round(40 * s), std::round(22 * s)};
                const float word_w = c.width(words, TypeRole::ui_sm);
                c.text(words, TypeRole::ui_sm, control.x - std::round(12 * s) - word_w,
                       y + std::round((row_h - line_height(TypeRole::ui_sm, s)) / 2), word_w + 1, Colour::ink_body);
                if (on) {
                  c.fill(control, Colour::gold, control.h / 2);
                } else {
                  c.fill(control, Colour::ground_well, control.h / 2);
                  c.stroke(control, Colour::edge, std::round(tokens::kGrace.size.edge_width * s), control.h / 2);
                }
                const float knob = std::round(16 * s);
                c.circle(Rect{on ? control.right() - knob - std::round(3 * s) : control.x + std::round(3 * s),
                              control.y + std::round((control.h - knob) / 2), knob, knob},
                         on ? Colour::on_gold : Colour::ink_mute);
                hit(row, Scope::page, focus_index(FocusKind::cell, cell_id), control);
                if (page_focus(FocusKind::cell, cell_id)) {
                  c.focused(control);
                }
              }
              y += row_h;
              const auto said = refusal != refusals_.end() ? refusal : refusals_.find(row_refusal_id);
              if (said != refusals_.end()) {  // the space's reason, under its row (§5.7)
                c.text(said->second, TypeRole::ui_sm, row.x, y - std::round(8 * s), row.w, Colour::rot);
                y += std::round(line_height(TypeRole::ui_sm, s));
              }
            }
            if (!secret_label.empty()) {  // a row secret is set in the browser view until GraceUI's own field (§5.6)
              c.text(secret_label + ": set in the browser view.", TypeRole::ui_sm, x0 + card_pad, y + std::round(8 * s),
                     content_w - 2 * card_pad, Colour::ink_faint);
              y += std::round(line_height(TypeRole::ui_sm, s) + 12 * s);
            }
            continue;
          }
          for (const std::string& key : block.keys.value_or(std::vector<std::string>{})) {
            const Setting& st = settings_[by_key_.at(key)];
            const bool shortcut = block.kind == protocol::BlockKind::shortcuts;
            if (shortcut && !present(st.home)) {
              continue;  // a shortcut to a page of a later increment
            }
            const bool focused_row = page_focus(shortcut ? FocusKind::shortcut : FocusKind::setting, key);
            const auto refusal = refusals_.find(key);
            const float extra = (focused_row ? line_height(TypeRole::ui_sm, s) : 0) +
                                (refusal != refusals_.end() ? line_height(TypeRole::ui_sm, s) : 0);
            const Rect row{x0 + card_pad, y, content_w - 2 * card_pad, row_h + std::round(extra)};
            if (hovered(row)) {
              c.fill(row, Colour::tint_hover);
            }
            c.line(row.x, row.y, row.w, Colour::rule);
            const std::uint8_t alpha = st.locked || greyed(st) ? 128 : 255;  // locked, or greyed by its space (D188)
            // A shortcut row: its home's control, and its link before it (D189).
            std::string link;
            float link_w = 0;
            if (shortcut) {
              link = "Lives in ";
              for (const protocol::Page& p : pages_.pages) {
                if (p.id == st.home) {
                  link += p.name;
                }
              }
              link_w = c.width(link, TypeRole::ui_sm);
            }
            Rect track_rect{};
            std::size_t segment_count = 0;
            const Json& v = value(key);
            const float label_y = y + std::round((row_h - line_height(TypeRole::ui, s)) / 2);
            const bool changed = changed_known_ ? changed_.count(key) != 0 : v != st.default_;  // D198
            if (!shortcut && changed) {  // changed from its default: the lozenge
              c.diamond(Rect{row.x - std::round(14 * s), label_y + std::round(7 * s), std::round(8 * s),
                             std::round(8 * s)},
                        Colour::gold_deep);
            }
            const float label_w = c.text(st.label, TypeRole::ui, row.x, label_y,
                                         row.w - control_w - std::round(24 * s) - (shortcut ? link_w + std::round(24 * s) : 0),
                                         Colour::ink_hi, alpha);
            const auto inherited = inherited_.find(key);
            if (st.type == SettingType::tri_state && v == "inherit" && inherited != inherited_.end()) {
              // What the chain resolves to, faint, with its source (D163).
              const std::string from = (inherited->second.on ? "On, from " : "Off, from ") + inherited->second.source;
              c.text(from, TypeRole::ui_sm, row.x + label_w + std::round(12 * s), label_y + std::round(2 * s),
                     row.w - control_w - label_w - std::round(48 * s), Colour::ink_faint, alpha);
            }
            if (const std::string badge = availability_badge(st); !badge.empty()) {
              c.text(badge, TypeRole::ui_sm, row.x + label_w + std::round(12 * s), label_y + std::round(2 * s),
                     row.w - control_w - label_w - std::round(48 * s), Colour::ink_faint);
            }
            float line_y = y + row_h - std::round(8 * s);
            if (focused_row) {  // the help line opens on focus, and ends with the default (§4.7)
              // Its default, unless it formats to nothing: then the space's help says it, if it must (D198).
              const std::string fallback = format(key, st.default_);
              const std::string help = fallback.empty() ? st.help : st.help + " Default: " + fallback + ".";
              c.text(help, TypeRole::ui_sm, row.x, line_y, row.w, Colour::ink_mute);
              line_y += line_height(TypeRole::ui_sm, s);
            }
            if (refusal != refusals_.end()) {
              c.text(refusal->second, TypeRole::ui_sm, row.x, line_y, row.w, Colour::rot);
            }
            // The control, right-aligned in its fixed column.
            const Rect col{row.right() - control_w, y, control_w, row_h};
            const float r = std::round(tokens::radius::control * s);
            const float edge = std::round(tokens::kGrace.size.edge_width * s);
            Rect control = col;
            if (shortcut) {
              c.text(link, TypeRole::ui_sm, col.x - std::round(24 * s) - link_w,
                     y + std::round((row_h - line_height(TypeRole::ui_sm, s)) / 2), link_w + 1, Colour::gold);
            }
            if (st.availability == "later") {
              const char* badge = "Later version";
              const float w = c.width(badge, TypeRole::ui_sm);
              c.text(badge, TypeRole::ui_sm, col.right() - w, y + std::round((row_h - line_height(TypeRole::ui_sm, s)) / 2),
                     w + 1, Colour::ink_faint);
            } else if (st.type == SettingType::bool_) {
              const bool on = v.is_boolean() && v.get<bool>();
              control = Rect{col.right() - std::round(40 * s), y + std::round((row_h - 22 * s) / 2), std::round(40 * s),
                             std::round(22 * s)};
              if (on) {
                c.fill(control, Colour::gold, control.h / 2, alpha);
              } else {
                c.fill(control, Colour::ground_well, control.h / 2, alpha);
                c.stroke(control, Colour::edge, edge, control.h / 2, alpha);
              }
              const float knob = std::round(16 * s);
              c.circle(Rect{on ? control.right() - knob - std::round(3 * s) : control.x + std::round(3 * s),
                            control.y + std::round((control.h - knob) / 2), knob, knob},
                       on ? Colour::on_gold : Colour::ink_mute, alpha);
            } else if (numeric(st.type)) {
              // Inside its column by a margin on each side, so that neither the value nor the knob at either end
              // meets the focus outline drawn around the column.
              const float inset = std::round(12 * s);
              const std::string shown = format(key, v);
              const float vw = c.width(shown, TypeRole::num);
              c.text(shown, TypeRole::num, col.right() - inset - vw,
                     y + std::round((row_h - line_height(TypeRole::num, s)) / 2), vw + 1, Colour::ink_body, alpha);
              const float track_w = control_w - std::round(64 * s) - 2 * inset;
              const Rect track{col.x + inset, y + std::round((row_h - 4 * s) / 2), track_w, std::round(4 * s)};
              track_rect = track;
              c.fill(track, Colour::ground_well, track.h / 2, alpha);
              const double lo = st.min_.value_or(0), hi = st.max_.value_or(1);
              const double n = v.is_number() ? v.get<double>() : lo;
              const float t = hi > lo ? static_cast<float>(std::clamp((n - lo) / (hi - lo), 0.0, 1.0)) : 0.0f;
              c.fill(Rect{track.x, track.y, std::round(track.w * t), track.h}, Colour::gold, track.h / 2, alpha);
              const float knob = std::round(14 * s);
              c.circle(Rect{track.x + std::round(track.w * t - knob / 2), y + std::round((row_h - knob) / 2), knob, knob},
                       Colour::gold_hi, alpha);
              control = Rect{col.x, y + std::round(10 * s), control_w, row_h - std::round(20 * s)};
            } else if (st.type == SettingType::choice || st.type == SettingType::tri_state) {
              std::vector<std::pair<std::string, bool>> segments;
              if (st.type == SettingType::tri_state) {
                for (int i = 0; i < 3; ++i) {
                  segments.emplace_back(tri_label(st, i), v.is_string() && v.get_ref<const std::string&>() == tri_value(st, i));
                }
              } else {
                for (const protocol::Choice& ch : st.choices.value_or(std::vector<protocol::Choice>{})) {
                  segments.emplace_back(ch.label, v.is_string() && v.get_ref<const std::string&>() == ch.value);
                }
              }
              control = Rect{col.x, y + std::round((row_h - 30 * s) / 2), control_w, std::round(30 * s)};
              segment_count = segments.size();
              c.stroke(control, Colour::edge, edge, r, alpha);
              const float seg_w = control.w / static_cast<float>(std::max<std::size_t>(segments.size(), 1));
              for (std::size_t i = 0; i < segments.size(); ++i) {
                const Rect seg{control.x + std::round(seg_w * static_cast<float>(i)), control.y, std::round(seg_w),
                               control.h};
                if (segments[i].second) {
                  c.fill(Rect{seg.x + edge, seg.y + edge, seg.w - 2 * edge, seg.h - 2 * edge}, Colour::ground_3, r, alpha);
                }
                const float lw = std::min(c.width(segments[i].first, TypeRole::ui_sm), seg.w - std::round(8 * s));
                c.text(segments[i].first, TypeRole::ui_sm, seg.x + std::round((seg.w - lw) / 2),
                       seg.y + std::round((seg.h - line_height(TypeRole::ui_sm, s)) / 2), seg.w - std::round(8 * s),
                       segments[i].second ? Colour::gold_hi : Colour::ink_mute, alpha);
              }
            } else if (st.type == SettingType::text && st.choices_from) {
              // A picker (D193): the chosen value's label in a field, ← and → at its ends while there are choices.
              bool published = false;
              const bool any = !live_choices(st, published).empty();
              control = Rect{col.x, y + std::round((row_h - 28 * s) / 2), control_w, std::round(28 * s)};
              c.fill(control, Colour::ground_well, r, alpha);
              c.stroke(control, Colour::edge, edge, r, alpha);
              const std::string shown = format(key, v);
              const float pad = std::round(10 * s);
              const float arrow_w = c.width("→", TypeRole::ui_sm);
              c.text(shown, TypeRole::ui_sm, control.x + pad + (any ? arrow_w + pad : 0),
                     control.y + std::round((control.h - line_height(TypeRole::ui_sm, s)) / 2),
                     control.w - 2 * pad - (any ? 2 * (arrow_w + pad) : 0), Colour::ink_body, alpha);
              if (any) {
                const float ay = control.y + std::round((control.h - line_height(TypeRole::ui_sm, s)) / 2);
                c.text("←", TypeRole::ui_sm, control.x + pad, ay, arrow_w + 1, Colour::ink_mute, alpha);
                c.text("→", TypeRole::ui_sm, control.right() - pad - arrow_w, ay, arrow_w + 1, Colour::ink_mute, alpha);
              } else {
                const char* none = "Nothing to choose from yet";
                c.text(none, TypeRole::ui_sm, row.x + label_w + std::round(12 * s), label_y + std::round(2 * s),
                       row.w - control_w - label_w - std::round(48 * s), Colour::ink_faint);
              }
            } else {  // a key, a text, a secret: shown in a field; edited in the browser until GraceUI's own field
              const std::string shown = format(key, v);
              const TypeRole role = st.type == SettingType::key_name ? TypeRole::num : TypeRole::ui_sm;
              const float w = st.type == SettingType::key_name ? c.width(shown, role) + std::round(20 * s) : control_w;
              control = Rect{col.right() - w, y + std::round((row_h - 28 * s) / 2), w, std::round(28 * s)};
              c.fill(control, Colour::ground_well, r, alpha);
              c.stroke(control, Colour::edge, edge, r, alpha);
              const Colour ink = st.type == SettingType::secret && shown == "Set" ? Colour::moss : Colour::ink_body;
              c.text(shown, role, control.x + std::round(10 * s),
                     control.y + std::round((control.h - line_height(role, s)) / 2), control.w - std::round(20 * s), ink,
                     alpha);
            }
            hit(row, Scope::page, focus_index(shortcut ? FocusKind::shortcut : FocusKind::setting, key), control, track_rect,
                segment_count);
            if (focused_row) {
              c.focused(control);
            }
            y += row.h;
          }
        }
      };
      if (!collapsed) {
        draw_blocks(section.blocks);
        if (section.advanced && !section.advanced->empty()) {
          const bool open = advanced_open_.count(page.id + "/" + section.id) != 0;
          c.line(x0 + card_pad, y, content_w - 2 * card_pad, Colour::rule);
          c.text(open ? "Advanced  -" : "Advanced  +", TypeRole::ui_sm, x0 + card_pad, y + std::round(12 * s),
                 std::round(160 * s), Colour::gold);
          hit(Rect{x0 + card_pad, y + std::round(8 * s), std::round(120 * s), std::round(28 * s)}, Scope::page,
              focus_index(FocusKind::advanced, section.id));
          if (page_focus(FocusKind::advanced, section.id)) {
            c.focused(Rect{x0 + card_pad, y + std::round(8 * s), std::round(120 * s), std::round(28 * s)});
          }
          y += std::round(40 * s);
          if (open) {
            draw_blocks(*section.advanced);
          }
        }
      }
      y += std::round(12 * s);
      // The card under what it holds: its ground and its rule border, inserted before its contents.
      const Rect card{x0, card_top, content_w, y - card_top};
      Item ground;
      ground.shape = Shape::fill;
      ground.colour = Colour::ground_2;
      ground.clip = 1;
      ground.rect = card;
      ground.radius = std::round(tokens::radius::card * s);
      Item border = ground;
      border.shape = Shape::stroke;
      border.colour = Colour::rule;
      border.width = std::max(1.0f, std::round(s));
      out.items.insert(out.items.begin() + static_cast<std::ptrdiff_t>(card_at), {ground, border});
      y += std::round(16 * s);
    }
    // The Related card at the foot.
    if (std::any_of(page.related.begin(), page.related.end(), [&](const std::string& id) { return present(id); })) {
      const float top = y;
      const std::size_t card_at = out.items.size();
      y += std::round(16 * s);
      c.text("Related", TypeRole::eyebrow, x0 + card_pad, y, content_w, Colour::gold);
      y += std::round(line_height(TypeRole::eyebrow, s) + 8 * s);
      for (const std::string& link : page.related) {
        if (!present(link)) {
          continue;  // a page of a later increment
        }
        const protocol::Page* target = nullptr;
        for (const protocol::Page& p : pages_.pages) {
          if (p.id == link) {
            target = &p;
          }
        }
        const Rect row{x0 + card_pad, y, content_w - 2 * card_pad, std::round(44 * s)};
        if (hovered(row)) {
          c.fill(row, Colour::tint_hover);
        }
        hit(row, Scope::page, focus_index(FocusKind::related, link));
        c.line(row.x, row.y, row.w, Colour::rule);
        c.text(target != nullptr ? target->name : link, TypeRole::ui, row.x, y + std::round(4 * s), row.w / 3,
               Colour::ink_hi);
        if (target != nullptr) {
          c.text(target->description, TypeRole::ui_sm, row.x + row.w / 3, y + std::round(6 * s),
                 row.w * 2 / 3 - std::round(30 * s), Colour::ink_mute);
        }
        c.text("→", TypeRole::ui, row.right() - std::round(16 * s), y + std::round(4 * s), std::round(16 * s),
               Colour::gold);
        if (page_focus(FocusKind::related, link)) {
          c.focused(row);
        }
        y += row.h;
      }
      y += std::round(12 * s);
      Item ground;
      ground.shape = Shape::fill;
      ground.colour = Colour::ground_2;
      ground.clip = 1;
      ground.rect = Rect{x0, top, content_w, y - top};
      ground.radius = std::round(tokens::radius::card * s);
      Item border = ground;
      border.shape = Shape::stroke;
      border.colour = Colour::rule;
      border.width = std::max(1.0f, std::round(s));
      out.items.insert(out.items.begin() + static_cast<std::ptrdiff_t>(card_at), {ground, border});
      y += std::round(16 * s);
    }
    const float content_bottom = y + scroll_;

    // ---- The key hints for the focused control, and the focus ring.
    c.clip = 0;
    c.line(page_x, H - footer_h, page_w, Colour::rule);
    std::string hints = "Esc  Back";
    if (scope_ == Scope::page && !focus_.empty() && !greyed_words(page.id).empty() &&
        focus_[page_focus_].kind != FocusKind::shortcut && focus_[page_focus_].kind != FocusKind::related &&
        focus_[page_focus_].kind != FocusKind::tab && focus_[page_focus_].kind != FocusKind::section) {
      hints = "Not available now    Esc  Back";  // a greyed page's controls (D188)
    } else if (scope_ == Scope::page && !focus_.empty()) {
      const Focus& f = focus_[page_focus_];
      if (f.kind == FocusKind::shortcut) {
        // Its home's control, and its link (D189).
        const Setting& st = settings_[f.index];
        const bool changes = !st.locked && st.availability != "later" && st.type != SettingType::key_name &&
                             (st.type != SettingType::text || st.choices_from) && st.type != SettingType::secret;
        hints = changes ? "← →  Change    Enter  Go to its page    Esc  Back" : "Enter  Go to its page    Esc  Back";
      } else if (f.kind == FocusKind::setting) {
        const Setting& st = settings_[f.index];
        hints = numeric(st.type) ? "← →  Change    Shift+← →  By ten    R  Reset    Esc  Back"
                : st.type == SettingType::bool_ ? "Enter  Toggle    R  Reset    Esc  Back"
                : (st.type == SettingType::choice || st.type == SettingType::tri_state ||
                   (st.type == SettingType::text && st.choices_from))
                    ? "← →  Choose    R  Reset    Esc  Back"
                    : "Edited in the browser view    Esc  Back";
        if (st.locked) {
          hints = "Locked in this version    Esc  Back";
        }
      } else if (f.kind == FocusKind::cell) {
        hints = "Enter  Toggle    Esc  Back";  // a table's switch, as a setting's but with no default to reset to
      } else if (f.kind == FocusKind::row_action) {
        hints = confirming_ == "row:" + f.id ? "Enter  Confirm    Esc  Back" : "Enter  Press    Esc  Back";
      } else {
        hints = "Enter  Open    Esc  Back";
      }
    } else if (scope_ == Scope::rail) {
      hints = "↑ ↓  Move    Enter  Open    →  Page    Esc  Close";
    }
    c.text(hints, TypeRole::ui_sm, page_x + gutter, H - footer_h + std::round((footer_h - line_height(TypeRole::ui_sm, s)) / 2),
           page_w - 2 * gutter, Colour::ink_faint);
    if (c.focus_found) {
      const float off = std::round(tokens::kGrace.size.focus_offset * s);
      const Rect ring{c.focus.x - off, c.focus.y - off, c.focus.w + 2 * off, c.focus.h + 2 * off};
      c.clip = scope_ == Scope::page ? 1 : 0;
      c.stroke(ring, Colour::gold_hi, std::round(tokens::kGrace.size.focus_width * s),
               std::round((tokens::radius::control + tokens::kGrace.size.focus_offset) * s));
    }
    // Keep the focused element in view, unless the wheel has scrolled since a key last moved focus; within the page's
    // length either way. Then lay out once more.
    if (pass == 0) {
      const float margin = std::round(24 * s);
      float next = scroll_;
      if (follow_ && scope_ == Scope::page && c.focus_found) {
        if (c.focus.bottom() + margin > page_area.bottom()) {
          next += c.focus.bottom() + margin - page_area.bottom();
        } else if (c.focus.y - margin < page_area.y) {
          next -= page_area.y - (c.focus.y - margin);
        }
      }
      next = std::clamp(next, 0.0f, std::max(0.0f, content_bottom - page_area.bottom() + margin));
      if (std::round(next) != scroll_) {
        scroll_ = std::round(next);
        continue;
      }
    }
    break;
  }
}

}  // namespace grace::view
