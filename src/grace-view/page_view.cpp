// SPDX-License-Identifier: MIT
// page_view.cpp: one space's menu in game (page_view.h).
#include "grace/view/page_view.h"
#include "grace/view/typed.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>

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
  void triangle(Rect r, Facing facing, Colour c) {
    Item item;
    item.shape = Shape::triangle;
    item.colour = c;
    item.clip = clip;
    item.rect = r;
    item.facing = facing;
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

// ---- Pad mode's key hints (§3.8, §5.4, D242). The keyboard's hints are the one source of the words: each "keys
// label" entry, four spaces apart, is drawn in pad mode with the pad's button for its keys, as the host maps them.

enum class PadGlyph : std::uint8_t { none, a, b, x, y, dpad_lr, dpad_ud, dpad_right, dpad_down };

struct HintPart {
  PadGlyph button = PadGlyph::none;
  std::string_view keys;  // the keyboard's keys, drawn as they are when no button stands for them
  std::string_view label;
};

// The button the menu maps to a hint's keys (the host's pad_inputs): A for Enter, B for Esc, X for R, the D-pad for the
// arrows. `dropped` for keys a pad has no button for (Shift+← →, Ctrl+V, Backspace): pad mode leaves those hints out.
PadGlyph pad_button_of(std::string_view keys, bool& dropped) noexcept {
  dropped = false;
  if (keys == "Esc") {
    return PadGlyph::b;
  }
  if (keys == "Enter") {
    return PadGlyph::a;
  }
  if (keys == "R") {
    return PadGlyph::x;
  }
  if (keys == "← →") {
    return PadGlyph::dpad_lr;
  }
  if (keys == "↑ ↓") {
    return PadGlyph::dpad_ud;
  }
  if (keys == "→") {
    return PadGlyph::dpad_right;
  }
  if (keys == "↓") {
    return PadGlyph::dpad_down;
  }
  dropped = keys == "Shift+← →" || keys == "Ctrl+V" || keys == "Backspace";
  return PadGlyph::none;
}

// A hints line's parts; an entry with no two spaces in it is words alone ("Not available now"). For a pad, the hints it
// has no button for are left out.
std::vector<HintPart> hint_parts(std::string_view hints, bool pad) {
  std::vector<HintPart> parts;
  while (!hints.empty()) {
    const std::size_t end = hints.find("    ");
    const std::string_view entry = hints.substr(0, end);
    hints = end == std::string_view::npos ? std::string_view() : hints.substr(end + 4);
    HintPart part;
    const std::size_t gap = entry.find("  ");
    if (gap == std::string_view::npos) {
      part.label = entry;
    } else {
      part.keys = entry.substr(0, gap);
      part.label = entry.substr(gap + 2);
      bool dropped = false;
      part.button = pad ? pad_button_of(part.keys, dropped) : PadGlyph::none;
      if (dropped) {
        continue;
      }
    }
    parts.push_back(part);
  }
  return parts;
}

std::string_view letter_of(PadGlyph button) noexcept {
  switch (button) {
    case PadGlyph::a: return "A";
    case PadGlyph::b: return "B";
    case PadGlyph::x: return "X";
    case PadGlyph::y: return "Y";
    default: return {};
  }
}

std::string_view dpad_words(PadGlyph button) noexcept {
  switch (button) {
    case PadGlyph::dpad_lr: return "D-pad ← →";
    case PadGlyph::dpad_ud: return "D-pad ↑ ↓";
    case PadGlyph::dpad_right: return "D-pad →";
    case PadGlyph::dpad_down: return "D-pad ↓";
    default: return {};
  }
}

// The words pad mode's footer stands for, as the tests and the dump read it: "A  Toggle    X  Reset    B  Back".
std::string pad_words(std::string_view hints) {
  std::string out;
  for (const HintPart& part : hint_parts(hints, true)) {
    out += out.empty() ? "" : "    ";
    const std::string_view name = !letter_of(part.button).empty() ? letter_of(part.button)
                                  : !dpad_words(part.button).empty() ? dpad_words(part.button)
                                                                     : part.keys;
    if (!name.empty()) {
      out.append(name).append("  ");
    }
    out.append(part.label);
  }
  return out;
}

// A face button (§3.8): a diamond of four dots, the button's own filled, the others rings.
void face_button(Cursor& c, Rect r, PadGlyph button) {
  const float d = std::max(3.0f, std::round(r.w * 0.36f));
  const float ring = std::max(1.0f, std::round(c.s));
  const float cx = std::round(r.x + r.w / 2 - d / 2);
  const float cy = std::round(r.y + r.h / 2 - d / 2);
  const struct {
    PadGlyph button;
    float x, y;
  } dots[] = {{PadGlyph::y, cx, r.y}, {PadGlyph::b, r.right() - d, cy}, {PadGlyph::a, cx, r.bottom() - d},
              {PadGlyph::x, r.x, cy}};
  for (const auto& dot : dots) {
    const Rect at{dot.x, dot.y, d, d};
    if (dot.button == button) {
      c.circle(at, Colour::ink_hi);
    } else {
      c.stroke(at, Colour::ink_faint, ring, d / 2);
    }
  }
}

// The D-pad: a cross, dim, with the arms of the hint's directions marked.
void dpad(Cursor& c, Rect r, PadGlyph button) {
  const float t = std::floor(r.w / 3);  // within the glyph's square
  const float corner = std::max(1.0f, std::round(c.s));
  c.fill(Rect{r.x, r.y + t, 3 * t, t}, Colour::ink_faint, corner, 110);
  c.fill(Rect{r.x + t, r.y, t, 3 * t}, Colour::ink_faint, corner, 110);
  const auto arm = [&](float x, float y) { c.fill(Rect{x, y, t, t}, Colour::ink_hi, corner); };
  if (button == PadGlyph::dpad_lr) {
    arm(r.x, r.y + t);
  }
  if (button == PadGlyph::dpad_lr || button == PadGlyph::dpad_right) {
    arm(r.x + 2 * t, r.y + t);
  }
  if (button == PadGlyph::dpad_ud) {
    arm(r.x + t, r.y);
  }
  if (button == PadGlyph::dpad_ud || button == PadGlyph::dpad_down) {
    arm(r.x + t, r.y + 2 * t);
  }
}

// The arrow a key's name starts with at `at` (← ↑ → ↓, U+2190 to U+2193, three bytes in UTF-8), or none.
std::optional<Facing> arrow_at(std::string_view keys, std::size_t at) noexcept {
  if (at + 3 > keys.size() || keys[at] != '\xE2' || keys[at + 1] != '\x86') {
    return std::nullopt;
  }
  switch (keys[at + 2]) {
    case '\x90': return Facing::left;
    case '\x91': return Facing::up;
    case '\x92': return Facing::right;
    case '\x93': return Facing::down;
    default: return std::nullopt;
  }
}

// A drawn arrow's shaft, as thick as the mono face's strokes about.
float arrow_shaft(float s) noexcept { return std::max(1.0f, std::round(1.5f * s)); }

// A drawn arrow's box (D247): 12 by 10 at a scale of 1 for ← and →, 10 by 12 for ↑ and ↓; its breadth a pixel more
// where that centres the shaft on the head's point.
Rect arrow_box(Facing facing, float s) noexcept {
  const float along = std::round(12 * s);
  float across = std::round(10 * s);
  if (std::fmod(across - arrow_shaft(s), 2.0f) != 0) {
    across += 1;
  }
  return facing == Facing::left || facing == Facing::right ? Rect{0, 0, along, across} : Rect{0, 0, across, along};
}

// An arrow (D247): a shaft and a head, the head as long as half the box is broad, its point a right angle.
void arrow(Cursor& c, Rect r, Facing facing, Colour ink) {
  const bool flat = facing == Facing::left || facing == Facing::right;
  const float broad = flat ? r.h : r.w;
  const float head = std::round(broad / 2);
  const float t = arrow_shaft(c.s);
  const float mid = std::round((broad - t) / 2);
  const float shaft = (flat ? r.w : r.h) - head + 1;  // into the head by a pixel, so no seam shows between them
  switch (facing) {
    case Facing::right:
      c.fill(Rect{r.x, r.y + mid, shaft, t}, ink);
      c.triangle(Rect{r.right() - head, r.y, head, r.h}, facing, ink);
      break;
    case Facing::left:
      c.fill(Rect{r.x + head - 1, r.y + mid, shaft, t}, ink);
      c.triangle(Rect{r.x, r.y, head, r.h}, facing, ink);
      break;
    case Facing::down:
      c.fill(Rect{r.x + mid, r.y, t, shaft}, ink);
      c.triangle(Rect{r.x, r.bottom() - head, r.w, head}, facing, ink);
      break;
    case Facing::up:
      c.fill(Rect{r.x + mid, r.y + head - 1, t, shaft}, ink);
      c.triangle(Rect{r.x, r.y, r.w, head}, facing, ink);
      break;
  }
}

// The keys' names as a cap draws them (D247): runs of text in the mono face, and each arrow drawn in its box. Calls
// `run(text, x)` and `drawn(facing, x)` from `x` left to right, if given; answers the width.
template <typename Run, typename Drawn>
float key_names(Cursor& c, std::string_view keys, float x, Run&& run, Drawn&& drawn) {
  const float x0 = x;
  const float join = std::round(2 * c.s);  // between an arrow and text with no space between them ("Shift+←")
  std::size_t from = 0;
  bool after_arrow = false;
  for (std::size_t i = 0; i <= keys.size(); ++i) {
    const std::optional<Facing> facing = i < keys.size() ? arrow_at(keys, i) : std::nullopt;
    if (!facing && i < keys.size()) {
      continue;
    }
    if (i > from) {
      const std::string_view text = keys.substr(from, i - from);
      x += after_arrow && text.front() != ' ' ? join : 0.0f;
      run(text, x);
      x += c.width(text, TypeRole::num);
      after_arrow = false;
    }
    if (facing) {
      x += i > from && keys[i - 1] != ' ' ? join : 0.0f;
      after_arrow = true;
      drawn(*facing, x);
      x += arrow_box(*facing, c.s).w;
      i += 2;
      from = i + 1;
    }
  }
  return x - x0;
}

float key_names_width(Cursor& c, std::string_view keys) {
  return key_names(c, keys, 0, [](std::string_view, float) {}, [](Facing, float) {});
}

// A key cap (§3.8, D246): the keys' names in the mono face, its arrows drawn (D247), on ground.well in an edge border
// with the control's radius, `h` high from `y`; answers its width.
float key_cap(Cursor& c, std::string_view keys, float x, float y, float h) {
  const float s = c.s;
  const float pad = std::round(6 * s);
  const float w = key_names_width(c, keys) + 2 * pad;
  const Rect cap{x, y, w, h};
  const float radius = std::round(tokens::radius::control * s);
  c.fill(cap, Colour::ground_well, radius);
  c.stroke(cap, Colour::edge, std::max(1.0f, std::round(tokens::kGrace.size.edge_width * s)), radius);
  const float ty = y + std::round((h - line_height(TypeRole::num, s)) / 2);
  key_names(
      c, keys, x + pad, [&](std::string_view text, float at) { c.text(text, TypeRole::num, at, ty, w, Colour::ink_body); },
      [&](Facing facing, float at) {
        Rect box = arrow_box(facing, s);
        box.x = at;
        box.y = y + std::round((h - box.h) / 2);
        arrow(c, box, facing, Colour::ink_body);
      });
  return w;
}

// The key hints, left to right from `x` within `max_w`, centred in `height` from `top` (§3.8): each part's keys in a key
// cap, or in pad mode its glyph (and a face button's letter, D242), then its label in the hints' ink; words alone as
// they are. A part that would not fit is left out, with those after it.
void hints_line(Cursor& c, std::string_view hints, bool pad, float x, float top, float height, float max_w) {
  const float s = c.s;
  const float g = std::round(16 * s);
  const float cap_h = std::round(22 * s);
  const float gy = top + std::round((height - g) / 2);
  const float cy = top + std::round((height - cap_h) / 2);
  const float ty = top + std::round((height - line_height(TypeRole::ui_sm, s)) / 2);
  const float right = x + max_w;
  const float gap = std::round(6 * s);
  for (const HintPart& part : hint_parts(hints, pad)) {
    const std::string_view letter = letter_of(part.button);
    const bool cross = !dpad_words(part.button).empty();
    const float lead = !letter.empty() ? g + std::round(3 * s) + c.width(letter, TypeRole::ui_sm) + gap
                       : cross         ? g + gap
                       : !part.keys.empty() ? key_names_width(c, part.keys) + std::round(12 * s) + gap
                                            : 0.0f;
    if (x + lead + c.width(part.label, TypeRole::ui_sm) > right) {
      break;
    }
    if (!letter.empty()) {
      face_button(c, Rect{x, gy, g, g}, part.button);
      c.text(letter, TypeRole::ui_sm, x + g + std::round(3 * s), ty, right - x, Colour::ink_mute);
    } else if (cross) {
      dpad(c, Rect{x, gy, g, g}, part.button);
    } else if (!part.keys.empty()) {
      key_cap(c, part.keys, x, cy, cap_h);
    }
    x += lead;
    x += c.text(part.label, TypeRole::ui_sm, x, ty, right - x, Colour::ink_faint) + std::round(20 * s);
  }
}

// The parts protocol 1.2 adds (G34, D192 to D195), checked at registration as the page lint checks them: a table's row
// actions and row secret, a form block, a text setting's choices from a topic.
bool check_parts(const protocol::SpacePages& pages, const std::vector<protocol::Setting>& settings, std::string& why) {
  const auto declared = [&](const std::string& command) {
    return std::find(pages.commands.begin(), pages.commands.end(), command) != pages.commands.end();
  };
  // A file command is sent only by an upload block (D229): never by an action, a row's, a form or an editor.
  const std::vector<std::string>& files = pages.file_commands.value_or(std::vector<std::string>{});
  const auto is_file = [&](const std::string& command) {
    return std::find(files.begin(), files.end(), command) != files.end();
  };
  const auto usable = [&](const std::string& command) { return declared(command) && !is_file(command); };
  const auto file_sent = [&](const std::string& where, const std::string& command) {
    if (!is_file(command)) {
      return false;
    }
    why = where + command + " is a file command, which only an upload block sends";
    return true;
  };
  // A file command (D226): the space's own, not a settings command, sensitive (never from a paired device), not slow.
  const std::vector<std::string>& sensitive = pages.sensitive_commands.value_or(std::vector<std::string>{});
  const std::vector<std::string>& slow = pages.slow_commands.value_or(std::vector<std::string>{});
  for (const std::string& name : pages.file_commands.value_or(std::vector<std::string>{})) {
    if (!declared(name) || name == "settings.set" || name == "settings.reset" ||
        std::find(sensitive.begin(), sensitive.end(), name) == sensitive.end() ||
        std::find(slow.begin(), slow.end(), name) != slow.end()) {
      why = "file command " + name + " is a declared command of the space's own, marked sensitive and not slow";
      return false;
    }
  }
  // A filter bar (G43, D250), as the page lint checks it: a part at least; columns, keys and search columns names.
  const auto name_ok = [](const std::string& n) {
    return !n.empty() && n.size() <= 32 &&
           std::all_of(n.begin(), n.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'; });
  };
  const auto bar_ok = [&](const protocol::FilterBar& bar, const std::string& where, bool paged) {
    const bool parts = (bar.filters && !bar.filters->empty()) || (bar.sort && !bar.sort->empty()) || bar.search;
    bool names = true;
    for (const protocol::FilterOption& f : bar.filters.value_or(std::vector<protocol::FilterOption>{})) {
      names = names && name_ok(f.column) && !f.label.empty();
      // Its own choices on rows read a page at a time, one source of them (D254); anywhere else its own choices or
      // none, never a state topic's (D262: the choices word the values the rows hold).
      const int sources = static_cast<int>(f.choices.has_value()) + static_cast<int>(f.choices_from.has_value());
      names = names && (paged ? sources == 1 : !f.choices_from.has_value());
    }
    for (const protocol::SortOption& o : bar.sort.value_or(std::vector<protocol::SortOption>{})) {
      names = names && name_ok(o.key) && !o.label.empty();
    }
    if (bar.search) {
      names = names && !bar.search->columns.empty() && !bar.search->label.empty() &&
              std::all_of(bar.search->columns.begin(), bar.search->columns.end(), name_ok);
    }
    if (!parts || !names) {
      why = where + "a filter bar has a filter, a sort option or a search, each naming its columns, and its filters "
            "their own choices where its rows come a page at a time, and a state topic's nowhere else";
      return false;
    }
    return true;
  };
  // A state topic of the space's own, not a family (the quick switches', the scope's, a page source's changes).
  const auto own_state = [&](const std::string& id) {
    return id != "settings" && id != "space.state" &&
           std::any_of(pages.topics.begin(), pages.topics.end(), [&](const protocol::TopicInfo& t) {
             return t.id == id && t.shape == protocol::TopicShape::state && !t.family.value_or(false);
           });
  };
  // A space's scope and its scoped pages, and rows read a page at a time (G45, D254), as the page lint checks them.
  if (pages.scope && (pages.scope->label.empty() || !own_state(pages.scope->topic) ||
                      (pages.scope->field && !name_ok(*pages.scope->field)))) {
    why = "the space's scope has a label and is on a state topic of its own, not a family";
    return false;
  }
  const auto source_ok = [&](const protocol::PageSource& src) {
    return usable(src.command) && src.command != "settings.set" && src.command != "settings.reset" &&
           std::find(slow.begin(), slow.end(), src.command) == slow.end() && (!src.changes || own_state(*src.changes));
  };
  for (const protocol::Page& page : pages.pages) {
    if (page.scoped.value_or(false) && !pages.scope) {
      why = page.id + " follows the space's scope, which the space does not declare";
      return false;
    }
    if (page.master && page.master->topic.has_value() == page.master->page.has_value()) {
      why = page.id + ": its master list reads a topic or a page source, one of the two";
      return false;
    }
    if (page.master && page.master->page && !source_ok(*page.master->page)) {
      why = page.id + ": its master list's rows come from a command of the space's own, neither a settings, a file nor "
            "a slow command, and its changes topic is a state topic of its own";
      return false;
    }
    if (page.master && page.master->filter_bar &&
        !bar_ok(*page.master->filter_bar, page.id + ": its master list: ", page.master->page.has_value())) {
      return false;
    }
  }
  // The top bar's Quick switches (G45, D252), as the page lint checks them: a shortcut to a switch or a choice of the
  // space's, by its key alone; or a session switch of the space's own, its command neither a settings, a file nor a slow
  // command, its state a member of a state topic of the space's own, its home a page of the space's.
  for (const protocol::QuickSwitch& q : pages.quick.value_or(std::vector<protocol::QuickSwitch>{})) {
    if (q.key) {
      const auto setting = std::find_if(settings.begin(), settings.end(),
                                        [&](const protocol::Setting& one) { return one.key == *q.key; });
      if (q.id || q.label || q.command || q.state || q.home || setting == settings.end() ||
          (setting->type != protocol::SettingType::bool_ && setting->type != protocol::SettingType::choice)) {
        why = "quick switch " + *q.key + " is a shortcut to a switch or a choice of the space's, by its key alone";
        return false;
      }
      continue;
    }
    const bool session = q.id && q.label && !q.label->empty() && q.command && q.state && q.home &&
                         usable(*q.command) && *q.command != "settings.set" && *q.command != "settings.reset" &&
                         std::find(slow.begin(), slow.end(), *q.command) == slow.end() && own_state(q.state->topic) &&
                         name_ok(q.state->field) &&
                         std::any_of(pages.pages.begin(), pages.pages.end(), [&](const protocol::Page& p) {
                           return p.id == *q.home && !p.browser_only.value_or(false);  // a home in game too (D258)
                         });
    if (!session) {
      why = "quick switch " + q.id.value_or("") + " is a session switch: an id, a label, a command of the space's own "
            "(not a settings, file or slow command), its state a member of a state topic of the space's and its home a "
            "page of the space's";
      return false;
    }
  }
  // A page in the browser alone, shown with a setting, gated, with a ribbon (D258), as the page lint checks them.
  for (const protocol::Page& page : pages.pages) {
    const auto setting = page.shown_with ? std::find_if(settings.begin(), settings.end(),
                                                        [&](const protocol::Setting& one) { return one.key == *page.shown_with; })
                                         : settings.end();
    if (page.shown_with && (setting == settings.end() || setting->type != protocol::SettingType::bool_)) {
      why = page.id + " is shown with a switch of the space's settings";
      return false;
    }
    if (page.gate && (!page.browser_only.value_or(false) || page.gate->words.empty() || page.gate->confirm.empty())) {
      why = page.id + ": a gate is a browser-only page's, with its words and a confirm label";
      return false;
    }
    if (page.ribbon && page.ribbon->empty()) {
      why = page.id + ": its ribbon has words";
      return false;
    }
  }
  // A header switch (D256): a session switch's form, as a quick switch's, drawn in the page's header; never on a
  // section, never destructive or confirmed, since it sends {on} at once.
  for (const protocol::Page& page : pages.pages) {
    for (const protocol::PageAction& a : page.actions.value_or(std::vector<protocol::PageAction>{})) {
      if (a.state && (!usable(a.command) || a.command == "settings.set" || a.command == "settings.reset" ||
                      std::find(slow.begin(), slow.end(), a.command) != slow.end() || !own_state(a.state->topic) ||
                      !name_ok(a.state->field) || a.tone == protocol::ActionTone::destructive || a.confirm)) {
        why = page.id + ": action " + a.id + " is a switch: a command of the space's own (not a settings, file or slow "
              "command), its state a member of a state topic of the space's, neither destructive nor confirmed";
        return false;
      }
    }
    const auto sections = [&](const std::vector<protocol::PageSection>& list) {
      return std::any_of(list.begin(), list.end(), [](const protocol::PageSection& sec) {
        const auto& acts = sec.actions.value_or(std::vector<protocol::PageAction>{});
        return std::any_of(acts.begin(), acts.end(), [](const protocol::PageAction& a) { return a.state.has_value(); });
      });
    };
    bool on_section = sections(page.sections.value_or(std::vector<protocol::PageSection>{}));
    for (const protocol::PageTab& tab : page.tabs.value_or(std::vector<protocol::PageTab>{})) {
      on_section = on_section || sections(tab.sections);
    }
    if (on_section) {
      why = page.id + ": a section's action has a state; a switch is a header action's alone";
      return false;
    }
  }
  const auto block_ok = [&](const protocol::Page& page, const protocol::PageBlock& block) {
    const std::string where = page.id + ": ";
    const bool table = block.kind == protocol::BlockKind::component && block.component == protocol::Component::table;
    if ((block.row_actions || block.row_secret) && !table) {
      why = where + "row actions and a row secret go on a table";
      return false;
    }
    if (block.page && (!table || block.topic || !source_ok(*block.page))) {
      why = where + "a page source belongs to a table with no topic, its rows from a command of the space's own, "
            "neither a settings, a file nor a slow command, and its changes topic a state topic of its own";
      return false;
    }
    // A stream's bar (D257): its filters and a search over the rows it holds, never a sort (its order is time's).
    const bool stream = block.kind == protocol::BlockKind::component &&
                        (block.component == protocol::Component::log_tail ||
                         block.component == protocol::Component::live_feed);
    if (block.filter_bar && stream && block.filter_bar->sort && !block.filter_bar->sort->empty()) {
      why = where + "a stream's filter bar has filters and a search, and no sort: its rows come in time's order";
      return false;
    }
    if (block.filter_bar && ((!table && !stream) || !bar_ok(*block.filter_bar, where, block.page.has_value()))) {
      why = table || stream ? why : where + "a filter bar goes on a table, a stream or a page's master list";
      return false;
    }
    if (block.kind == protocol::BlockKind::component && block.component == protocol::Component::filter_bar) {
      why = where + "a filter bar is a table's own field, filter_bar, not a block of its own";
      return false;
    }
    std::vector<std::string> ids;
    for (const protocol::RowAction& action : block.row_actions.value_or(std::vector<protocol::RowAction>{})) {
      if (file_sent(where, action.command)) {
        return false;
      }
      if (action.label.empty() || !usable(action.command) ||
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
    if (block.row_secret && file_sent(where, block.row_secret->command)) {
      return false;
    }
    if (block.row_secret && !usable(block.row_secret->command)) {
      why = where + "a row secret's command is not one the space declares";
      return false;
    }
    const bool form = block.kind == protocol::BlockKind::form;
    const bool upload = block.kind == protocol::BlockKind::upload;
    if (upload) {
      // An upload block (D226): one file command of the space's, sensitive, with its button's label; nothing else.
      if (!block.command || std::find(files.begin(), files.end(), *block.command) == files.end() || !block.submit ||
          block.submit->empty() || block.fields || block.keys || block.component || block.topic || block.props ||
          block.editor) {
        why = where + "an upload block has a file command of the space's and a submit label, and nothing else";
        return false;
      }
      return true;
    }
    if (block.accept) {
      why = where + "accept belongs to an upload block";
      return false;
    }
    if (block.editor) {
      const protocol::EditorBlock& ed = *block.editor;
      for (const std::optional<std::string>& name : {std::optional<std::string>(ed.save), ed.check, ed.reset, ed.restore,
                                                     ed.draft}) {
        if (name && file_sent(where, *name)) {
          return false;
        }
      }
    }
    // A document (G44, D251): a family the space declares, on a page with a master list, and nothing else.
    if (block.kind == protocol::BlockKind::document) {
      const bool family = block.topic && std::any_of(pages.topics.begin(), pages.topics.end(), [&](const auto& tp) {
        return tp.id == *block.topic && tp.family.value_or(false);
      });
      if (!family || !page.master || block.keys || block.component || block.props || block.row_actions ||
          block.row_secret || block.editor || block.command || block.fields || block.submit || block.filter_bar) {
        why = where + "a document block draws a family on a page with a master list, and has nothing else";
        return false;
      }
      return true;
    }
    // A report preview's switches send its command (D251): the space's own, not a file command.
    const bool preview = block.kind == protocol::BlockKind::component &&
                         block.component == protocol::Component::report_preview;
    if (block.share) {
      // A report preview's Copy and Save (D263): one or both; its Save the space's own command, sensitive (never from a
      // paired device, D221) and not a file command.
      const protocol::ReportShare& share = *block.share;
      const bool save_ok = !share.save || (usable(*share.save) && share.save->rfind("settings.", 0) != 0 &&
                                           std::find(sensitive.begin(), sensitive.end(), *share.save) != sensitive.end());
      const bool family = block.topic && std::any_of(pages.topics.begin(), pages.topics.end(), [&](const protocol::TopicInfo& t) {
        return t.id == *block.topic && t.family.value_or(false);
      });
      if (!preview || (!share.copy.value_or(false) && !share.save) || !save_ok || family) {
        why = where + "a report preview's share offers Copy, Save or both, its Save a sensitive command of the "
              "space's own and not a file command, on a topic of its own and not a family (D263)";
        return false;
      }
    }
    if (preview && block.command) {
      if (block.fields || block.submit || !usable(*block.command)) {
        why = where + "a report preview's command is one the space declares, and not a file command";
        return false;
      }
      return true;
    }
    if (form != (block.command.has_value() || block.fields.has_value() || block.submit.has_value())) {
      why = where + "a form block, and only a form block, has a command, fields and a submit label";
      return false;
    }
    if (form && block.command && file_sent(where, *block.command)) {
      return false;
    }
    if (form) {
      const std::vector<protocol::FormField>& fields = block.fields.value_or(std::vector<protocol::FormField>{});
      if (!block.command || !usable(*block.command) || fields.empty() || !block.submit || block.submit->empty() ||
          block.keys || block.component || block.topic) {
        why = where + "a form block has a declared command, one to four fields and a submit label, and nothing else";
        return false;
      }
      std::vector<std::string> names;
      for (const protocol::FormField& field : fields) {
        const std::uint32_t most = field.prose.value_or(false) ? 2000 : 200;  // a prose field's several lines (D251)
        if (field.label.empty() || field.max_ < 1 || field.max_ > most ||
            std::find(names.begin(), names.end(), field.id) != names.end()) {
          why = where + "form field " + field.id + " has no label, a twin, or a max outside 1 to " + std::to_string(most);
          return false;
        }
        names.push_back(field.id);
      }
    }
    return true;
  };
  for (const protocol::Page& page : pages.pages) {
    for (const protocol::PageAction& action : page.actions.value_or(std::vector<protocol::PageAction>{})) {
      if (file_sent(page.id + ": ", action.command)) {
        return false;
      }
    }
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
      // A section's own actions and status (D202), held as header actions and components are.
      std::vector<std::string> action_ids;
      for (const protocol::PageAction& action : section->actions.value_or(std::vector<protocol::PageAction>{})) {
        if (file_sent(page.id + ": ", action.command)) {
          return false;
        }
        if (action.label.empty() || !usable(action.command) ||
            std::find(action_ids.begin(), action_ids.end(), action.id) != action_ids.end()) {
          why = page.id + ": section " + section->id + "'s action " + action.id +
                " has no label, a command the space does not declare, or a twin";
          return false;
        }
        if (action.tone == protocol::ActionTone::destructive && (!action.confirm || action.confirm->empty())) {
          why = page.id + ": a destructive section action without its confirm text";
          return false;
        }
        action_ids.push_back(action.id);
      }
      if (section->chip && section->chip->empty()) {
        why = page.id + ": section " + section->id + "'s chip is empty";
        return false;
      }
      if (section->status) {
        const std::string& topic = section->status->topic;
        const auto found = std::find_if(pages.topics.begin(), pages.topics.end(),
                                        [&](const protocol::TopicInfo& t) { return t.id == topic; });
        if (found == pages.topics.end() || found->shape != protocol::TopicShape::state || topic == "settings" ||
            topic == "space.state") {
          why = page.id + ": section " + section->id + "'s status names no state topic of the space's own";
          return false;
        }
      }
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
  // This PC alone (D264), as the page lint checks it: a pc_only topic drawn on pc_only pages alone, never on a page,
  // the scope or a quick switch a paired device sees; a setting homed on a pc_only page sensitive.
  std::set<std::string, std::less<>> pc_topics;
  std::set<std::string, std::less<>> pc_pages;
  for (const protocol::TopicInfo& t : pages.topics) {
    if (t.pc_only.value_or(false)) {
      pc_topics.insert(t.id);
    }
  }
  for (const protocol::Page& page : pages.pages) {
    if (page.pc_only.value_or(false)) {
      pc_pages.insert(page.id);
    }
  }
  // Whether a part of page data names a pc_only topic: as a "topic" or "changes" id, or a "choices_from" id or topic.
  const std::function<bool(const Json&)> names_pc = [&](const Json& node) {
    if (node.is_object()) {
      for (const auto& [key, value] : node.items()) {
        if ((key == "topic" || key == "changes" || key == "choices_from") && value.is_string() &&
            pc_topics.contains(value.get_ref<const std::string&>())) {
          return true;
        }
        if (names_pc(value)) {
          return true;
        }
      }
    } else if (node.is_array()) {
      return std::any_of(node.begin(), node.end(), names_pc);
    }
    return false;
  };
  if (!pc_topics.empty()) {
    for (const protocol::Page& page : pages.pages) {
      if (!page.pc_only.value_or(false) && names_pc(protocol::encode(page))) {
        why = page.id + " draws a topic on this PC alone: such a topic is drawn on pc_only pages alone (D264)";
        return false;
      }
    }
    if ((pages.scope && names_pc(protocol::encode(*pages.scope))) ||
        (pages.quick && std::any_of(pages.quick->begin(), pages.quick->end(),
                                    [&](const protocol::QuickSwitch& q) { return names_pc(protocol::encode(q)); }))) {
      why = "the top bar names a topic on this PC alone, which a paired device sees (D264)";
      return false;
    }
  }
  // A setting homed on a pc_only page is shown on no page, and by no quick switch, a paired device sees.
  std::set<std::string, std::less<>> pc_keys;
  for (const protocol::Setting& setting : settings) {
    if (pc_pages.contains(setting.home)) {
      pc_keys.insert(setting.key);
    }
  }
  const std::function<bool(const Json&)> shows_pc = [&](const Json& node) {
    if (node.is_object()) {
      for (const auto& [key, value] : node.items()) {
        if (key == "keys" && value.is_array() &&
            std::any_of(value.begin(), value.end(), [&](const Json& k) {
              return k.is_string() && pc_keys.contains(k.get_ref<const std::string&>());
            })) {
          return true;
        }
        if (shows_pc(value)) {
          return true;
        }
      }
    } else if (node.is_array()) {
      return std::any_of(node.begin(), node.end(), shows_pc);
    }
    return false;
  };
  if (!pc_keys.empty() || !pc_pages.empty()) {
    for (const protocol::Page& page : pages.pages) {
      if (!page.pc_only.value_or(false) && shows_pc(protocol::encode(page))) {
        why = page.id + " shows a setting that lives on a pc_only page a paired device never sees (D264)";
        return false;
      }
    }
    for (const protocol::QuickSwitch& q : pages.quick.value_or(std::vector<protocol::QuickSwitch>{})) {
      if ((q.key && pc_keys.contains(*q.key)) || (q.home && pc_pages.contains(*q.home))) {
        why = "a quick switch is a pc_only page's, which a paired device's top bar would show (D264)";
        return false;
      }
    }
  }
  for (const protocol::Setting& setting : settings) {
    const bool pc_home = pc_pages.contains(setting.home);
    if (setting.choices_from && pc_topics.contains(*setting.choices_from) && !pc_home) {
      why = setting.key + " takes its choices from a topic on this PC alone, on a page a paired device sees (D264)";
      return false;
    }
    if (pc_home && !setting.sensitive.value_or(false) && setting.type != protocol::SettingType::secret) {
      why = setting.key + " lives on a pc_only page: it is sensitive too, so that no paired device changes it (D264)";
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
  // The rail's foot (D248): its categories after every other, so that the order given is the order drawn.
  const std::string* foot = nullptr;
  for (const protocol::Category& category : pages_.categories) {
    if (category.foot.value_or(false)) {
      foot = &category.id;
    } else if (foot != nullptr) {
      why = "category " + category.id + " follows " + *foot + ", at the rail's foot; the foot's categories come last";
      return false;
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
  // A page shown with a setting comes and goes with it (D258).
  if (std::any_of(pages_.pages.begin(), pages_.pages.end(), [](const protocol::Page& p) { return p.shown_with.has_value(); })) {
    leave_absent_page();
    rebuild();
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
  leave_absent_page();
  rebuild();
}

bool PageView::present(std::string_view page_id) const {
  if (absent_.find(page_id) != absent_.end()) {
    return false;  // of a later increment (D173)
  }
  // In the browser alone, or shown with a switch that is off (D258): absent in game, from the rail, search and Related.
  for (const protocol::Page& page : pages_.pages) {
    if (page.id != page_id) {
      continue;
    }
    if (page.browser_only.value_or(false)) {
      return false;
    }
    if (page.shown_with) {
      const auto value = values_.find(*page.shown_with);
      return value != values_.end() && *value == true;
    }
    return true;
  }
  return true;
}

bool PageView::has_page(std::string_view page_id) const {
  return std::any_of(pages_.pages.begin(), pages_.pages.end(),
                     [&](const protocol::Page& page) { return page.id == page_id; });
}

std::vector<std::string> PageView::page_ids() const {
  std::vector<std::string> out;
  out.reserve(pages_.pages.size());
  for (const protocol::Page& page : pages_.pages) {
    out.push_back(page.id);
  }
  return out;
}

void PageView::leave_absent_page() {
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
}

bool PageView::open(std::string_view page_id) {
  for (std::size_t i = 0; i < pages_.pages.size(); ++i) {
    if (pages_.pages[i].id == page_id && present(page_id)) {
      release_held();  // what the last page held is applied (D252)
      quick_open_ = false;
      paged_.clear();  // nothing a page answered is kept past leaving it (D254)
  drawer_open_.clear();  // nor an open row drawer (D256)
      page_ = i;
      tab_ = 0;
      page_focus_ = 0;
      scroll_ = 0;
      confirming_.clear();
      rebuild();
      focus_first_control();
      focus_master_on_open();
      settle_expected();
      follow_master();  // a master list's first row, chosen once the list has one (D215)
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
  if (hold(topic)) {
    pending_[std::string(topic)] = body;  // held by Live updates (D252): the newest of each topic
    ++held_new_;
    return;
  }
  const std::string was = focused();
  const bool had = topics_.find(topic) != topics_.end();
  topics_[std::string(topic)] = body;
  for (const std::string& prefix : std::array<std::string, 2>{std::string(topic) + "/", std::string(topic) + "#"}) {
    for (auto at = refusals_.lower_bound(prefix); at != refusals_.end() && at->first.rfind(prefix, 0) == 0;) {
      at = refusals_.erase(at);  // a new body answers what was refused of its rows
    }
  }
  rebuild();
  if (scope_ == Scope::page) {  // the same element, wherever it now is, as rows come and go (D181, D192)
    // Or the one a new row's choice took focus from, once the new member draws it (D218).
    const std::string want = refocus_name_.empty() ? was : refocus_name_;
    for (std::size_t i = 0; i < focus_.size(); ++i) {
      if (name_of(Scope::page, i) == want) {
        page_focus_ = i;
        refocus_name_.clear();
        break;
      }
    }
  }
  // A page source's changes topic (D254): new rows. A list showing its first page asks for it again; one showing more
  // says so at its top and moves nothing until asked. (Its first body is the topic's own, not a change.)
  if (had) {
    for (const Source& src : page_sources()) {
      if (src.source->changes && *src.source->changes == topic) {
        Paged& p = paged_[src.key];
        if (p.answered && p.pages <= 1) {
          p.signature.clear();
        } else if (p.answered) {
          p.fresh = true;
        }
      }
    }
  }
  if (const protocol::PageMaster* m = master(); m != nullptr && !master_topic().empty() && topic == master_topic()) {
    master_changed();
  }
}

void PageView::master_changed() {
  if (const protocol::PageMaster* m = master(); m != nullptr) {
    // The list changed: a chosen row it no longer has is let go, and the first row chosen if none is (D215).
    const auto rows = master_rows();
    const auto at = chosen_.find(current().id);
    if (at != chosen_.end() &&
        std::none_of(rows.begin(), rows.end(), [&](const auto& row) { return row.first == at->second; })) {
      topics_.erase(m->detail + "/" + at->second);
      chosen_.erase(at);
    }
    if (master_focus_pending_ && scope_ == Scope::page) {
      // Opened before the list had a row, and no key has moved focus since: the first row, as an open with rows gives.
      for (std::size_t i = 0; i < focus_.size(); ++i) {
        if (focus_[i].kind == FocusKind::master_row) {
          page_focus_ = i;
          master_focus_pending_ = false;
          break;
        }
      }
    }
    settle_expected();  // a save's choice, once the list has its row (D218)
    follow_master();
  }
}

bool PageView::drawn_in_game(protocol::Component component) {
  switch (component) {
    case protocol::Component::table:
    case protocol::Component::key_value_list:
    case protocol::Component::check_list:
    case protocol::Component::stat_tiles:
    case protocol::Component::computed_sentence:
    case protocol::Component::report_preview:  // D251
    case protocol::Component::live_feed:       // the streams, their newest rows (D257)
    case protocol::Component::log_tail:
      return true;
    default:
      return false;  // anything newer: the browser view's for now
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
  if (block.page) {
    // Rows read a page at a time (D254): what the space has answered, once it has.
    if (const Paged* p = paged_of(cell_base(block)); p != nullptr && p->answered) {
      out.published = true;
      out.body = &p->body;
    }
    return out;
  }
  if (!block.topic) {
    return out;
  }
  const std::string topic = topic_of(block);  // a family's block: the chosen row's member (D215)
  if (topic.empty()) {
    return out;
  }
  const auto found = topics_.find(topic);
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
  return published && !own.empty() ? own : std::string("Nothing to show.");  // the space's words once it spoke (D186)
}

// A row secret's status in words (D194, D197): set, refused (the key was sent but the provider refused it), none (none
// is needed), or else missing.
static const char* secret_status_words(const std::string& status) {
  return status == "set" ? "Set" : status == "refused" ? "Refused" : status == "none" ? "Not needed" : "Missing";
}

// ---- Capture (G37, D209).

namespace {

// XInput's fourteen buttons as a chord's pickers offer them, in the order a player reads the pad.
struct PadButton {
  std::uint16_t bit;
  std::string_view name;
};
constexpr PadButton kPadButtons[] = {
    {0x0001, "D-pad Up"}, {0x0002, "D-pad Down"}, {0x0004, "D-pad Left"}, {0x0008, "D-pad Right"},
    {0x1000, "A"},        {0x2000, "B"},          {0x4000, "X"},          {0x8000, "Y"},
    {0x0100, "LB"},       {0x0200, "RB"},         {0x0040, "LS"},         {0x0080, "RS"},
    {0x0020, "View"},     {0x0010, "Menu"},
};
constexpr std::size_t kPadButtonCount = sizeof kPadButtons / sizeof kPadButtons[0];

}  // namespace

Action PageView::capture_command(bool confirm) const {
  Action action;
  action.kind = Action::Kind::command;
  action.command.space = pages_.space;
  action.command.name = capture_.command;
  action.command.args = Json::object({{capture_.key_field, capture_.key_value}, {"value", capture_.value}});
  if (confirm) {
    action.command.args["confirm"] = true;  // the space asked first: move it (D208)
  }
  return action;
}

Action PageView::captured(std::string_view key) {
  if (!awaiting_key()) {
    return {};
  }
  capture_.value = capture_.row && key == "Backspace" ? std::string() : std::string(key);
  capture_.words.clear();
  capture_.stage = CaptureStage::sent;
  return capture_command(false);
}

void PageView::capture_answered(bool ok, bool confirm_required, std::string_view words) {
  if (!capture_open() || capture_.stage != CaptureStage::sent) {
    return;
  }
  if (ok) {
    capture_ = Capture{};
    return;
  }
  capture_.words = words.empty() ? std::string("Not possible right now.") : std::string(words);
  capture_.stage = confirm_required ? CaptureStage::asking : CaptureStage::waiting;
}

Action PageView::capture_input(const Input& in) {
  if (in.key == Key::back) {
    capture_ = Capture{};  // Escape, or B: nothing changes
    return {};
  }
  if (capture_.stage == CaptureStage::asking) {
    if (in.key != Key::activate) {
      return {};
    }
    capture_.stage = CaptureStage::sent;
    capture_.words.clear();
    return capture_command(true);
  }
  if (capture_.stage == CaptureStage::sent || capture_.kind == CaptureKind::key) {
    return {};  // a key capture's key comes through captured(), not as the menu's own keys
  }
  switch (in.key) {
    case Key::up:
    case Key::previous:
      capture_.picker = 0;
      return {};
    case Key::down:
    case Key::next:
      capture_.picker = 1;
      return {};
    case Key::left:
    case Key::right: {
      std::size_t& index = capture_.picker == 0 ? capture_.anchor : capture_.key;
      index = (index + (in.key == Key::right ? 1 : kPadButtonCount - 1)) % kPadButtonCount;
      capture_.words.clear();
      return {};
    }
    case Key::reset:
      capture_.value = 0;  // no pad chord
      capture_.stage = CaptureStage::sent;
      return capture_command(false);
    case Key::activate:
      if (capture_.anchor == capture_.key) {
        capture_.words = "Choose two different buttons.";
        return {};
      }
      capture_.value = (std::uint32_t{kPadButtons[capture_.anchor].bit} << 16) | kPadButtons[capture_.key].bit;
      capture_.words.clear();
      capture_.stage = CaptureStage::sent;
      return capture_command(false);
    default:
      return {};
  }
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
  if (a.capture) {
    // A key or a pad chord first (D209): the modal takes it, then sends the row's key with it as "value".
    capture_ = Capture{};
    capture_.kind = *a.capture == protocol::RowCapture::key ? CaptureKind::key : CaptureKind::chord;
    capture_.row = true;
    capture_.command = a.command;
    capture_.key_field = a.key;
    capture_.key_value = row.is_object() && row.contains(a.key) ? row[a.key] : Json();
    std::string name;
    for (const Json& column : (*table)["columns"]) {
      const std::string words = column.is_object() && column.contains("id") && column["id"].is_string()
                                    ? cell_words(row, column["id"].get<std::string>()) : std::string();
      if (!words.empty() && !column.contains("switch") && !(column.contains("numeric") && column["numeric"] == true)) {
        name += (name.empty() ? "" : ", ") + words;
        if (name.find(", ") != std::string::npos) {
          break;  // the first two columns name it
        }
      }
    }
    capture_.title = a.label + (name.empty() ? "" : ": " + name);
    confirming_.clear();
    refusals_.erase(f.id);
    return {};
  }
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

Action PageView::section_action(const Focus& f) {
  const auto& list = sections();
  if (f.index >= list.size() || !list[f.index].actions || f.sub >= list[f.index].actions->size() ||
      !greyed_words(current().id).empty()) {
    return {};  // a greyed page's section actions are inert, as its header's (D188)
  }
  const protocol::PageAction& a = (*list[f.index].actions)[f.sub];
  const std::string asking = "section:" + f.id;
  if ((a.confirm || a.tone == protocol::ActionTone::destructive) && confirming_ != asking) {
    confirming_ = asking;  // its second press, as a header action's (§7.3, D173, D202)
    return {};
  }
  confirming_.clear();
  refusals_.erase(f.id);
  Action action;
  action.kind = Action::Kind::command;
  action.command.space = pages_.space;
  action.command.name = a.command;
  action.command.args = a.tone == protocol::ActionTone::destructive ? Json::object({{"confirm", true}}) : Json::object();
  return action;
}

std::pair<std::string, std::string> PageView::section_status(const protocol::PageSection& section) const {
  if (!section.status) {
    return {};
  }
  const auto found = topics_.find(section.status->topic);
  if (found == topics_.end() || !found->second.is_object()) {
    return {};  // nothing published: nothing drawn (invariant 2)
  }
  const Json* body = &found->second;
  if (section.status->field) {
    const auto member = body->find(*section.status->field);
    if (member == body->end() || !member->is_object()) {
      return {};
    }
    body = &*member;
  }
  std::string word = cell_words(*body, "word");
  std::string tone = cell_words(*body, "tone");
  if (word.empty() || word.size() > 64) {
    return {};
  }
  if (tone != "moss" && tone != "ember" && tone != "glint" && tone != "rot") {
    tone = "glint";  // as the lamp's
  }
  return {word, tone};
}

std::string PageView::cell_base(const protocol::PageBlock& block) {
  if (block.page) {
    // A table read a page at a time (D254): its command, and its args' view if they name one.
    const auto& args = block.page->args;
    const std::string view = args && args->is_object() && args->contains("view") && (*args)["view"].is_string()
                                 ? (*args)["view"].get<std::string>()
                                 : std::string();
    return "page." + block.page->command + (view.empty() ? std::string() : "." + view);
  }
  const std::string field = prop_text(block, "field");
  return *block.topic + (field.empty() ? std::string() : "#" + field);
}

std::string PageView::greyed_words(std::string_view page_id) const {
  if (const auto found = greyed_.find(page_id); found != greyed_.end()) {
    return found->second;
  }
  if (inactive_.empty() && pages_.scope) {
    // A scoped page on a scope marked read only (D254): its writing controls greyed, the scope's words.
    for (const protocol::Page& p : pages_.pages) {
      if (p.id == page_id && p.scoped.value_or(false)) {
        const std::string id = scope_id();
        for (const ScopeChoice& c : scope_choices()) {
          if (c.id == id && c.read_only) {
            return c.label + " is read only.";
          }
        }
      }
    }
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
  for (const protocol::QuickSwitch& q : pages_.quick.value_or(std::vector<protocol::QuickSwitch>{})) {
    if (q.state && std::find(out.begin(), out.end(), q.state->topic) == out.end()) {
      out.push_back(q.state->topic);  // a session switch's state (D252), on every page: Start opens them anywhere
    }
  }
  for (const protocol::PageAction& a : current().actions.value_or(std::vector<protocol::PageAction>{})) {
    if (a.state && std::find(out.begin(), out.end(), a.state->topic) == out.end()) {
      out.push_back(a.state->topic);  // a header switch's state (D256)
    }
  }
  const auto want = [&](const std::string& topic) {
    if (!topic.empty() && std::find(out.begin(), out.end(), topic) == out.end()) {
      out.push_back(topic);
    }
  };
  if (pages_.scope && current().scoped.value_or(false)) {
    want(pages_.scope->topic);  // the scope's choices, on a page that follows it (D254)
  }
  for (const Source& src : page_sources()) {
    // A page source's changes topic, and the topics its filters take their choices from (D254).
    want(src.source->changes.value_or(std::string()));
    for (const protocol::FilterOption& f :
         src.bar != nullptr ? src.bar->filters.value_or(std::vector<protocol::FilterOption>{})
                            : std::vector<protocol::FilterOption>{}) {
      if (f.choices_from) {
        want(f.choices_from->topic);
      }
    }
  }
  if (const protocol::PageMaster* m = master(); m != nullptr) {
    if (const std::string list = master_topic(); !list.empty()) {
      out.push_back(list);  // its list (D215); the chosen row's member comes with the blocks that draw it
    }
  }
  const auto add = [&](const std::vector<protocol::PageBlock>& blocks) {
    for (const protocol::PageBlock& block : blocks) {
      if (block.kind == protocol::BlockKind::component && block.topic && block.component && drawn_in_game(*block.component)) {
        if (const std::string topic = topic_of(block); !topic.empty() &&
                                                       std::find(out.begin(), out.end(), topic) == out.end()) {
          out.push_back(topic);
        }
      }
      if (block.kind == protocol::BlockKind::document && block.topic) {
        // A document block's chosen member (D251): the reader's body, watched as any member is.
        if (const std::string topic = topic_of(block); !topic.empty() &&
                                                       std::find(out.begin(), out.end(), topic) == out.end()) {
          out.push_back(topic);
        }
      }
      if (block.kind == protocol::BlockKind::editor && block.topic) {
        // An editor's document (D217), and the topics its fields take their choices from, once it has come.
        const std::string topic = topic_of(block);
        if (topic.empty()) {
          continue;
        }
        if (std::find(out.begin(), out.end(), topic) == out.end()) {
          out.push_back(topic);
        }
        EditorDocument doc;
        if (const auto body = topics_.find(topic); body != topics_.end() && EditorDocument::parse(body->second, doc)) {
          for (const EditorField& field : doc.fields) {
            if (!field.choices_from.empty() && std::find(out.begin(), out.end(), field.choices_from) == out.end()) {
              out.push_back(field.choices_from);
            }
          }
        }
      }
    }
  };
  for (const protocol::PageSection& section : sections()) {
    add(section.blocks);
    if (section.advanced) {
      add(*section.advanced);
    }
  }
  for (const protocol::PageSection& section : sections()) {  // a section's live status (D202)
    if (section.status && std::find(out.begin(), out.end(), section.status->topic) == out.end()) {
      out.push_back(section.status->topic);
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
  if (block.kind != protocol::BlockKind::component || block.component != protocol::Component::table ||
      (!block.topic && !block.page)) {
    return nullptr;
  }
  const Json* body = drawn(block).body;
  return body != nullptr && body->contains("columns") && (*body)["columns"].is_array() && body->contains("rows") &&
                 (*body)["rows"].is_array()
             ? body
             : nullptr;
}

// ---- Filter bars (G43, D250): done here on the rows the space published, never sent to it.

PageView::Bar PageView::bar_of_table(const protocol::PageBlock& block) const {
  Bar out;
  if (block.filter_bar && is_stream(block)) {
    // A stream's bar (D257): over the rows the menu holds of it; drawn with none yet too (D262), so that a page's
    // focus does not depend on whether another view already watched the stream.
    const Json* body = drawn(block).body;
    static const Json* const no_rows = new Json(Json::array());  // made once, never freed
    out.key = cell_base(block);
    out.bar = &*block.filter_bar;
    out.rows = body != nullptr && body->contains("rows") && (*body)["rows"].is_array() ? &(*body)["rows"] : no_rows;
    return out;
  }
  const Json* table = block.filter_bar ? table_of(block) : nullptr;
  if (table == nullptr || ((*table)["rows"].empty() && !block.page)) {
    return out;  // (a table read a page at a time keeps its bar with no rows: its filters chose none, D254)
  }
  out.key = cell_base(block);
  out.bar = &*block.filter_bar;
  out.rows = &(*table)["rows"];
  return out;
}

PageView::Bar PageView::bar_of_master() const {
  Bar out;
  const protocol::PageMaster* m = master();
  if (m == nullptr || !m->filter_bar) {
    return out;
  }
  const Json* body = master_body();
  if (body == nullptr || !body->is_object() || !body->contains("rows") || !(*body)["rows"].is_array() ||
      ((*body)["rows"].empty() && !m->page)) {
    return out;  // (a list read a page at a time keeps its bar with no rows: its filters chose none, D254)
  }
  out.key = "master:" + master_name(*m);
  out.bar = &*m->filter_bar;
  out.rows = &(*body)["rows"];
  return out;
}

std::vector<std::pair<std::string, std::string>> PageView::bar_choices(const Bar& bar, std::size_t i) const {
  std::vector<std::pair<std::string, std::string>> out;
  const protocol::FilterOption& f = (*bar.bar->filters)[i];
  if (f.choices) {
    for (const protocol::Choice& c : *f.choices) {
      out.emplace_back(c.value, c.label.empty() ? c.value : c.label);
    }
    return out;
  }
  if (f.choices_from) {
    const Json* body = latest(f.choices_from->topic);
    if (body != nullptr && f.choices_from->field) {
      body = body->is_object() && body->contains(*f.choices_from->field) ? &(*body)[*f.choices_from->field] : nullptr;
    }
    for (std::size_t k = 0; body != nullptr && body->is_array() && k < body->size() && out.size() < 32; ++k) {
      const Json& v = (*body)[k];
      if (v.is_string() && !v.get_ref<const std::string&>().empty()) {
        out.emplace_back(v.get<std::string>(), v.get<std::string>());
      } else if (v.is_object() && !cell_words(v, "value").empty()) {
        out.emplace_back(cell_words(v, "value"), cell_words(v, "label").empty() ? cell_words(v, "value") : cell_words(v, "label"));
      }
    }
    return out;
  }
  for (const std::string& v : filter_choices(*bar.rows, f.column)) {
    out.emplace_back(v, v);
  }
  return out;
}

PageView::Bar PageView::bar_of(const Focus& f) const {
  return f.block != nullptr ? bar_of_table(*f.block) : bar_of_master();
}

std::vector<std::size_t> PageView::shown_rows(const protocol::PageBlock& block, const Json& rows) const {
  if (const Bar bar = bar_of_table(block); bar && !block.page) {  // a page at a time: the space filtered (D254)
    const auto state = filters_.find(bar.key);
    return visible_rows(rows, *bar.bar, state != filters_.end() ? state->second : FilterState{});
  }
  std::vector<std::size_t> all;
  for (std::size_t r = 0; r < rows.size(); ++r) {
    all.push_back(r);
  }
  return all;
}

std::vector<std::string> PageView::bar_parts(const Bar& bar) const {
  std::vector<std::string> parts;
  const std::size_t filters = bar.bar->filters ? bar.bar->filters->size() : 0;
  for (std::size_t i = 0; i < filters; ++i) {
    parts.push_back("f" + std::to_string(i));
  }
  if (bar.bar->sort && !bar.bar->sort->empty()) {
    parts.emplace_back("sort");
    parts.emplace_back("dir");
  }
  if (bar.bar->search) {
    parts.emplace_back("search");
  }
  const auto state = filters_.find(bar.key);
  const FilterState none;
  const FilterState& st = state != filters_.end() ? state->second : none;
  if (narrowed(*bar.bar, st) && visible_rows(*bar.rows, *bar.bar, st).empty()) {
    parts.emplace_back("clear");
  }
  return parts;
}

void PageView::push_bar(const Bar& bar, const protocol::PageBlock* block) {
  if (!bar) {
    return;
  }
  const std::vector<std::string> parts = bar_parts(bar);
  for (std::size_t i = 0; i < parts.size(); ++i) {
    focus_.push_back({FocusKind::filter, bar.key + "/" + parts[i], i, block});
  }
}

void PageView::refocus(const std::string& id) {
  for (std::size_t i = 0; i < focus_.size(); ++i) {
    if (focus_[i].id == id) {
      page_focus_ = i;
      return;
    }
  }
}

Action PageView::filter_input(const Focus& f, Key key) {
  const Bar bar = bar_of(f);
  if (!bar) {
    return {};
  }
  const std::string part = f.id.substr(f.id.rfind('/') + 1);
  FilterState& st = filters_[bar.key];
  const int step = key == Key::left ? -1 : 1;
  const bool sideways = key == Key::left || key == Key::right;
  if (part[0] == 'f' && sideways) {
    // A filter's choices: All, then the values its rows hold, and the player's own though no row holds it now.
    const std::size_t i = static_cast<std::size_t>(std::stoul(part.substr(1)));
    std::vector<std::string> values;
    for (const auto& [value, words] : bar_choices(bar, i)) {
      values.push_back(value);
    }
    if (st.chosen.size() <= i) {
      st.chosen.resize(i + 1);
    }
    if (st.chosen[i] && std::find(values.begin(), values.end(), *st.chosen[i]) == values.end()) {
      values.push_back(*st.chosen[i]);
    }
    const std::ptrdiff_t at = st.chosen[i] ? std::find(values.begin(), values.end(), *st.chosen[i]) - values.begin() + 1
                                           : 0;
    const std::ptrdiff_t next = std::clamp<std::ptrdiff_t>(at + step, 0, static_cast<std::ptrdiff_t>(values.size()));
    st.chosen[i] = next == 0 ? std::nullopt : std::optional<std::string>(values[static_cast<std::size_t>(next - 1)]);
  } else if (part == "sort" && sideways) {
    const std::ptrdiff_t next = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(st.sort) + step, 0,
                                                           static_cast<std::ptrdiff_t>(bar.bar->sort->size()) - 1);
    if (static_cast<std::size_t>(next) != st.sort) {
      st.sort = static_cast<std::size_t>(next);
      st.reversed = false;
    }
  } else if (part == "dir" && (sideways || key == Key::activate)) {
    st.reversed = !st.reversed;
  } else if (part == "search" && key == Key::activate) {
    start_edit(EditKind::filter_search, f.id, st.search, 64, false, f.block);
    return {};
  } else if (part == "clear" && key == Key::activate) {
    st.chosen.clear();
    st.search.clear();
    const std::string to = bar.key + "/" + bar_parts(bar).front();
    rebuild();
    refocus(to);
    return {};
  } else {
    return {};
  }
  const std::string id = f.id;
  rebuild();
  refocus(id);
  return {};
}

// ---- Documents and report previews (G44, D251).

namespace {
bool member_key(std::string_view family, std::string_view key) noexcept;  // below, with the master list's
}  // namespace

bool PageView::listed(const std::string& key) const {
  // A document the page's list holds, as published, whatever its filter bar shows (invariant 2).
  const protocol::PageMaster* m = master();
  if (m == nullptr) {
    return false;
  }
  const Json* body = master_body();
  if (body == nullptr || !body->is_object() || !body->contains("rows") || !(*body)["rows"].is_array()) {
    return false;
  }
  for (const Json& row : (*body)["rows"]) {
    if (row.is_object() && cell_words(row, m->key) == key && member_key(m->detail, key)) {
      return true;
    }
  }
  return false;
}

void PageView::push_doc_links(const protocol::PageBlock& block) {
  const std::string topic = topic_of(block);
  const auto found = topics_.find(topic);
  if (found == topics_.end()) {
    return;
  }
  const DocumentView doc = parse_document(found->second);
  std::size_t n = 0;
  const auto spans = [&](const std::vector<DocSpan>& list) {
    for (const DocSpan& sp : list) {
      if (sp.style == DocSpan::Style::link && listed(sp.doc)) {
        doc_links_.push_back({sp.doc, sp.heading});
        focus_.push_back({FocusKind::link, topic + "/" + std::to_string(n++), doc_links_.size() - 1, &block});
      }
    }
  };
  for (const DocBlock& b : doc.blocks) {
    spans(b.spans);
    for (const auto& item : b.items) {
      spans(item);
    }
  }
}

void PageView::push_parts(const protocol::PageBlock& block) {
  const Json* body = drawn(block).body;
  if (body == nullptr || !body->contains("parts") || !(*body)["parts"].is_array()) {
    return;
  }
  const Json& parts = (*body)["parts"];
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const Json& part = parts[i];
    if (part.is_object() && part.contains("optional") && part["optional"] == true &&
        !cell_words(part, "id").empty() && !cell_words(part, "label").empty()) {
      focus_.push_back({FocusKind::part, *block.topic + "/" + cell_words(part, "id"), i, &block});
    }
  }
}

Action PageView::follow_link(const Focus& f) {
  if (f.index >= doc_links_.size()) {
    return {};
  }
  const DocLink to = doc_links_[f.index];
  doc_heading_ = to.heading;  // scrolled to once its document is laid out
  scroll_ = 0;
  choose(to.doc);
  // Focus on the chosen document's row, where the player now is; the page shows what the link asked, the document's
  // top or its heading, as after the wheel, until a key moves focus again.
  refocus_name_.clear();
  if (const protocol::PageMaster* m = master(); m != nullptr && scope_ == Scope::page) {
    for (std::size_t i = 0; i < focus_.size(); ++i) {
      if (focus_[i].kind == FocusKind::master_row && focus_[i].id == master_name(*m) + "/" + to.doc) {
        page_focus_ = i;
        break;
      }
    }
  }
  follow_ = false;
  return {};
}

namespace {

// A file name a report's share may give (D263): 1 to 64 of a-z, 0-9, -, _ and ., ending .txt, so no path.
bool share_file_ok(std::string_view name) {
  if (name.size() < 5 || name.size() > 64 || !name.ends_with(".txt")) {
    return false;
  }
  return std::all_of(name.begin(), name.end(), [](char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
  });
}

constexpr std::size_t kShareTextMax = 262144;  // a share's text: at most 256 KiB, a frame's bound (D263)
constexpr std::size_t kShareWordsMax = 400;    // the space's blocked words, or its answer to a Save

}  // namespace

const Json* PageView::shared(const protocol::PageBlock& block, std::string& blocked) const {
  blocked.clear();
  const Json* body = drawn(block).body;
  if (body == nullptr || !body->is_object() || !body->contains("share") || !(*body)["share"].is_object()) {
    return nullptr;  // nothing to share yet: Copy and Save greyed (invariant 2)
  }
  const Json& share = (*body)["share"];
  if (share.contains("blocked")) {
    const std::string words = cell_words(share, "blocked");
    blocked = words.size() <= kShareWordsMax ? words : std::string();
    return nullptr;
  }
  if (!share.contains("text") || !share["text"].is_string() || share["text"].get_ref<const std::string&>().empty() ||
      share["text"].get_ref<const std::string&>().size() > kShareTextMax || !share_file_ok(cell_words(share, "file"))) {
    return nullptr;  // a share GraceUI does not take is dropped
  }
  return &share;
}

Action PageView::press_share(const Focus& f) {
  if (!greyed_words(current().id).empty() || f.block == nullptr || !f.block->share) {
    return {};  // a greyed page's controls are inert (D188)
  }
  std::string blocked;
  const Json* share = shared(*f.block, blocked);
  if (share == nullptr) {
    return {};
  }
  if (f.index == 0) {
    // Copy: the space's text on the clipboard, as it gave it, on this press alone (PV7); GraceUI keeps no copy.
    clipboard_ = (*share)["text"].get<std::string>();
    has_clipboard_ = true;
    copied_until_[f.id] = now_ms_ + 2000;
    return {};
  }
  if (share_busy_.contains(f.id)) {
    return {};  // its answer is awaited
  }
  refusals_.erase(f.id);
  share_said_.erase(f.id);
  share_refused_.erase(f.id);
  share_busy_.insert(f.id);
  Action action;
  action.kind = Action::Kind::command;
  action.command.space = pages_.space;
  action.command.name = *f.block->share->save;
  action.command.args = Json::object();  // the space writes the file where it keeps its own (D263)
  return action;
}

Action PageView::flip_part(const Focus& f) const {
  if (!greyed_words(current().id).empty() || f.block == nullptr || !f.block->command) {
    return {};  // a greyed page's switches do nothing (D188)
  }
  const Json* body = drawn(*f.block).body;
  if (body == nullptr || !body->contains("parts") || !(*body)["parts"].is_array() ||
      f.index >= (*body)["parts"].size()) {
    return {};
  }
  const Json& part = (*body)["parts"][f.index];
  const bool included = !(part.contains("included") && part["included"] == false);
  Action action;
  action.kind = Action::Kind::command;
  action.command.space = pages_.space;
  action.command.name = *f.block->command;
  action.command.args = Json::object({{"id", cell_words(part, "id")}, {"on", !included}});
  return action;  // the space's next body shows it (invariant 1)
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
  doc_links_.clear();
  const protocol::Page& page = current();
  for (std::size_t a = 0; page.actions && a < page.actions->size(); ++a) {
    focus_.push_back({FocusKind::action, (*page.actions)[a].id, a});
  }
  if (const protocol::PageMaster* m = master(); m != nullptr) {
    // The master list's filter bar (D250), then its rows (D215), before its detail: by the list's topic and each row's
    // key.
    push_bar(bar_of_master(), nullptr);
    const Paged* paged = m->page ? paged_of(master_name(*m)) : nullptr;
    if (paged != nullptr && paged->fresh) {
      focus_.push_back({FocusKind::paging, master_name(*m) + "/fresh", 0});  // "New rows · Show" (D254)
    }
    const auto rows = master_rows();
    for (std::size_t r = 0; r < rows.size(); ++r) {
      focus_.push_back({FocusKind::master_row, master_name(*m) + "/" + rows[r].first, r});
    }
    if (paged != nullptr && !paged->next.empty() && !paged->loading) {
      focus_.push_back({FocusKind::paging, master_name(*m) + "/more", 0});  // Show more (D254)
    }
  }
  // A master page with no row chosen draws no detail: its tabs and sections are no focus stops then (D215).
  const bool detail = master() == nullptr || !chosen().empty();
  for (std::size_t t = 0; detail && page.tabs && t < page.tabs->size(); ++t) {
    focus_.push_back({FocusKind::tab, (*page.tabs)[t].id, t});
  }
  const auto& list = detail ? sections() : no_sections_;
  for (std::size_t i = 0; i < list.size(); ++i) {
    const protocol::PageSection& section = list[i];
    focus_.push_back({FocusKind::section, section.id, i});
    for (std::size_t a = 0; section.actions && a < section.actions->size(); ++a) {  // its own actions (D202)
      focus_.push_back({FocusKind::section_action, section.id + "/" + (*section.actions)[a].id, i, nullptr, a});
    }
    if (collapsed_.count(page.id + "/" + section.id) != 0) {
      continue;
    }
    const auto add_blocks = [&](const std::vector<protocol::PageBlock>& blocks, bool advanced) {
      for (std::size_t b = 0; b < blocks.size(); ++b) {
        const protocol::PageBlock& block = blocks[b];
        if (block.kind == protocol::BlockKind::editor && block.editor) {
          // An editor (D217): its buttons, its draft's notes and button, then its fields, a list's items and its Add, and
          // a draft's Use this and Keep mine under each field it proposes for.
          const std::string eid = form_id(section, b, advanced);
          const std::string topic = topic_of(block);
          const auto body = topics_.find(topic);
          EditorDocument doc;
          if (topic.empty() || body == topics_.end() || !EditorDocument::parse(body->second, doc)) {
            continue;  // nothing published yet: nothing to focus
          }
          const EditorState& st = editors_[topic];
          const auto stop = [&](EditorPart part, const std::string& rest, std::size_t index = 0, std::size_t sub = 0) {
            Focus f{FocusKind::editor, eid + "/" + rest, index, &block, sub};
            f.part = static_cast<std::uint8_t>(part);
            focus_.push_back(std::move(f));
          };
          const protocol::EditorBlock& ed = *block.editor;
          if (st.conflict) {
            stop(EditorPart::reload, "reload");
          }
          stop(EditorPart::save, "save");
          if (ed.check) stop(EditorPart::check, "check");
          if (ed.restore) stop(EditorPart::restore, "restore");
          if (ed.reset) stop(EditorPart::reset, "reset");
          if (ed.draft) {
            stop(EditorPart::notes, "notes");
            stop(EditorPart::draft, "draft");
            if (!st.draft.empty()) {
              stop(EditorPart::use_all, "use_all");
            }
          }
          for (std::size_t k = 0; k < doc.fields.size(); ++k) {
            const EditorField& field = doc.fields[k];
            if (field.kind == FieldKind::shown) {
              continue;
            }
            if (field.kind == FieldKind::list) {
              const Json& items = st.value(field);
              const std::size_t n = items.is_array() ? items.size() : 0;
              for (std::size_t i = 0; i < n; ++i) {
                stop(EditorPart::item, "i/" + field.id + "/" + std::to_string(i), k, i);
              }
              if (field.locked.empty() && (field.max_items == 0 || n < field.max_items)) {
                stop(EditorPart::add, "add/" + field.id, k);
              }
            } else {
              stop(EditorPart::field, "f/" + field.id, k);
            }
            if (st.draft.count(field.id) != 0) {
              stop(EditorPart::use, "use/" + field.id, k);
              stop(EditorPart::keep, "keep/" + field.id, k);
            }
          }
          continue;
        }
        if (block.kind == protocol::BlockKind::document) {
          push_doc_links(block);  // its links, in reading order (D251)
          continue;
        }
        if (block.kind == protocol::BlockKind::component && block.component == protocol::Component::report_preview &&
            (block.command || block.share)) {
          if (block.command) {
            push_parts(block);  // its optional parts' switches (D251)
          }
          std::string blocked;
          if (block.share && block.topic && shared(block, blocked) != nullptr) {
            // Its Copy and Save (D263), once the space has published a text to share; greyed and no stop before.
            if (block.share->copy.value_or(false)) {
              focus_.push_back({FocusKind::share, *block.topic + "/copy", 0, &block});
            }
            if (block.share->save) {
              focus_.push_back({FocusKind::share, *block.topic + "/save", 1, &block});
            }
          }
          continue;
        }
        if (block.kind == protocol::BlockKind::form) {
          // A form (D195): each of its fields a focus stop, then its button (G38 (a)).
          const std::string form = form_id(section, b, advanced);
          for (std::size_t k = 0; block.fields && k < block.fields->size(); ++k) {
            focus_.push_back({FocusKind::form_field, form + "/" + (*block.fields)[k].id, k, &block});
          }
          focus_.push_back({FocusKind::form_submit, form, 0, &block});
          continue;
        }
        if (is_stream(block)) {
          push_bar(bar_of_table(block), &block);  // a stream's filter bar (D257): its rows themselves take no focus
          continue;
        }
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
          push_bar(bar_of_table(block), &block);  // its filter bar first (D250), then its rows as they show
          const Paged* paged = block.page ? paged_of(cell_base(block)) : nullptr;
          if (paged != nullptr && paged->fresh) {
            focus_.push_back({FocusKind::paging, cell_base(block) + "/fresh", 0, &block});  // "New rows · Show" (D254)
          }
          const bool drawers = has_drawer(*table);
          for (const std::size_t r : shown_rows(block, (*table)["rows"])) {
            const Json& row = (*table)["rows"][r];
            if (drawers && row.is_object() && row_has_drawer(*table, row)) {
              // The row itself first, where its table has drawer columns and it fills one: Enter opens its drawer
              // (D256, D262).
              focus_.push_back({FocusKind::drawer, cell_base(block) + "/" + drawer_key(row, r), r, &block});
            }
            if (sw != nullptr) {
              const std::string id = row.is_object() ? cell_words(row, (*sw)["key"].get<std::string>()) : std::string();
              focus_.push_back({FocusKind::cell, cell_base(block) + "/" + (id.empty() ? std::to_string(r) : id), r});
            }
            // Its row secret (D194), set in game since G38 (a): a row that takes one and has a key.
            if (block.row_secret && row.is_object() && cell_words(row, block.row_secret->column) != "none" &&
                !cell_words(row, block.row_secret->key).empty()) {
              focus_.push_back({FocusKind::row_secret, secret_id(block, row, r), r, &block});
            }
            for (std::size_t a = 0; a < actions; ++a) {
              const protocol::RowAction& act = (*block.row_actions)[a];
              if (row.is_object() && !cell_words(row, act.key).empty()) {  // a row with no key takes no actions (D209)
                focus_.push_back({FocusKind::row_action, row_action_id(block, row, r, act), r, &block, a});
              }
            }
          }
          if (paged != nullptr && !paged->next.empty() && !paged->loading) {
            focus_.push_back({FocusKind::paging, cell_base(block) + "/more", 0, &block});  // Show more (D254)
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
    add_blocks(section.blocks, false);
    if (section.advanced && !section.advanced->empty()) {
      focus_.push_back({FocusKind::advanced, section.id, i});
      if (advanced_open_.count(page.id + "/" + section.id) != 0) {
        add_blocks(*section.advanced, true);
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
  // The page's first control past its section labels and tabs (§5.7), and past its header's actions, which ↑ reaches,
  // so that a stray Enter after the menu opens does not act (D262). With none above Related (a page whose controls
  // come with a topic, or that has none), its first section label or tab, at its top, rather than the Related card at
  // its foot (D256), and never a header action, where focus would stay once the controls came; a header action only
  // on a page with no other stop above Related.
  page_focus_ = 0;
  std::size_t first_action = focus_.size();
  std::size_t first_label = focus_.size();
  for (std::size_t i = 0; i < focus_.size(); ++i) {
    if (focus_[i].kind == FocusKind::related) {
      break;
    }
    if (focus_[i].kind == FocusKind::action) {
      first_action = std::min(first_action, i);
      continue;
    }
    if (focus_[i].kind != FocusKind::section && focus_[i].kind != FocusKind::tab) {
      page_focus_ = i;
      return;
    }
    first_label = std::min(first_label, i);
  }
  if (first_label < focus_.size()) {
    page_focus_ = first_label;
  } else if (first_action < focus_.size()) {
    page_focus_ = first_action;
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
  return name_of(scope_, scope_ == Scope::rail ? rail_focus_ : scope_ == Scope::top ? top_focus_ : page_focus_);
}

std::string PageView::name_of(Scope scope, std::size_t index) const {
  if (scope == Scope::top) {
    return index == kTopLive    ? "top:live"
           : index == kTopQuick ? "top:quick"
           : index == kTopScope ? "top:scope"
                                : "search";  // the top bar's (D252, D254)
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
                                          "cell", "row", "section-action", "field", "submit", "secret", "item", "editor",
                                          "filter", "link", "part", "paging", "drawer", "share"};
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
  switch (s->type) {
    case SettingType::bool_:
      return v.is_boolean() && v.get<bool>() ? "On" : "Off";
    case SettingType::integer:
    case SettingType::number:
    case SettingType::duration_s:
      return format_number(*s, v.is_number() ? v.get<double>() : 0.0);
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
      const double scale = std::pow(10.0, grid_decimals(s));  // on the store's grid (D266)
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
  const double scale = std::pow(10.0, grid_decimals(*s));  // on the store's grid (D266)
  n = std::clamp(std::round(n * scale) / scale, lo, hi);
  const Json now = s->type == SettingType::integer ? Json(static_cast<std::int64_t>(std::llround(n))) : Json(n);
  const Json& was = value(s->key);
  if (was.is_number() && std::fabs(was.get<double>() - n) < 1e-9) {
    return {};  // the same value: nothing to send
  }
  return set(*s, now);
}

Action PageView::pointer_body(const Pointer& in) {
  pointer_x_ = in.x;
  pointer_y_ = in.y;
  pointer_in_ = in.kind != Pointer::Kind::leave;
  if (sheet_open_) {
    // Only its Close answers the pointer (G9): a press elsewhere does nothing, so a stray click keeps the code.
    const Rect& r = sheet_close_;
    if (in.kind == Pointer::Kind::press && in.x >= r.x && in.x < r.right() && in.y >= r.y && in.y < r.bottom()) {
      close_sheet();
      Action closed;
      closed.kind = Action::Kind::sheet_closed;
      return closed;
    }
    return {};
  }
  if (capture_open()) {
    return {};  // the page under the modal does nothing; its keys and the pad answer it (D209)
  }
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
  if (editing()) {
    const Rect& box = edit_box_;
    if (box.w > 0 && in.x >= box.x && in.x < box.right() && in.y >= box.y && in.y < box.bottom()) {
      // In the edited field: the caret to the boundary nearest the pointer (in a prose field, on the line under it).
      std::size_t best = edit_.field.caret();
      float nearest = 1e30f;
      if (edit_.prose) {
        for (const auto& [at, x, top] : prose_stops_) {
          if (in.y >= top && in.y < top + prose_line_ && std::fabs(x - in.x) < nearest) {
            nearest = std::fabs(x - in.x);
            best = at;
          }
        }
        edit_.field.place(best);
        return {};
      }
      for (const auto& [at, x] : edit_stops_) {
        if (std::fabs(x - in.x) < nearest) {
          nearest = std::fabs(x - in.x);
          best = at;
        }
      }
      edit_.field.place(best);
      return {};
    }
    if (!(edit_.kind == EditKind::search && hit != nullptr && hit->scope == Scope::top && hit->index > 0)) {
      // A click elsewhere: a text's or a form field's text kept, as leaving a field in the browser keeps it; a
      // secret's edit dropped; search's results closed.
      const bool keep = (edit_.kind == EditKind::setting && !edit_.masked) || edit_.kind == EditKind::form_field ||
                        edit_.kind == EditKind::editor_text || edit_.kind == EditKind::editor_notes;
      const Setting* typed_into = edit_.kind == EditKind::setting ? setting(edit_.id) : nullptr;
      const bool slider = typed_into != nullptr && numeric(typed_into->type);
      Action left = edit_.kind == EditKind::search ? edit(EditInput{EditKey::tab}) : finish_edit(keep, 0);
      if (left.kind == Action::Kind::command || slider) {
        // The edit's command, or a slider's typed entry ended (D266): the click did only that. (The hits it would
        // test were laid out around the field, the track shorter for it.)
        return left;
      }
      hit = hit_at(in.x, in.y);
    }
  }
  if (quick_open_ && (hit == nullptr || hit->scope != Scope::top || hit->index < kTopQuick)) {
    quick_open_ = false;  // a press outside the Quick switches menu closes it, and does nothing else (D252)
    return {};
  }
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
  if (target.scope == Scope::top && target.index >= kTopLive) {
    // The top bar's Live updates and Quick switches, and the menu's rows (D252).
    if (target.index == kQuickRow - 1) {
      return {};  // the menu's ground, between its rows
    }
    if (target.index >= kQuickRow) {
      quick_focus_ = target.index - kQuickRow;
      return flip_quick(quick_focus_, 1);
    }
    scope_ = Scope::top;
    top_focus_ = target.index;
    if (target.index == kTopQuick && quick_open_) {
      quick_open_ = false;
      return {};
    }
    return input(Input{Key::activate, false});
  }
  if (target.scope == Scope::top) {
    scope_ = Scope::top;
    top_focus_ = kTopSearch;
    if (target.index > 0 && target.index - 1 < search_hits_.size()) {
      // One of search's results (§4.3): its home page, the row focused.
      const SearchHit picked = search_hits_[target.index - 1];
      search_query_.clear();
      search_hits_.clear();
      end_edit();
      return open_hit(picked);
    }
    if (!editing()) {
      return input(Input{Key::activate, false});  // the search box: its field
    }
    return {};
  }
  if (target.index >= focus_.size()) {
    return {};
  }
  const Focus& clicked = focus_[target.index];
  if (!confirming_.empty() && !(clicked.kind == FocusKind::action && clicked.id == confirming_) &&
      !(clicked.kind == FocusKind::row_action && "row:" + clicked.id == confirming_) &&
      !(clicked.kind == FocusKind::section_action && "section:" + clicked.id == confirming_) &&
      !(clicked.kind == FocusKind::editor && "editor:" + clicked.id == confirming_)) {
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
      if (numeric(st.type) && target.value.w > 0 && in.x >= target.value.x) {
        return input(Input{Key::activate, false});  // its value: typed entry (D266)
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
      if (f.kind == FocusKind::setting && (st.type == SettingType::text || st.type == SettingType::secret)) {
        return input(Input{Key::activate, false});  // GraceUI's own field (G38 (a))
      }
      return {};  // a key: Enter captures it (D209)
    }
    case FocusKind::cell:
      if (!on_control) {
        return {};
      }
      refusals_.erase(f.id);
      return toggle_cell(f);
    case FocusKind::master_row:
      return {};  // focused: the row is chosen (D215); Enter would go into the detail
    case FocusKind::filter:
      // A filter bar's select (D250): a press takes its next choice; its direction, search and Clear filters, as Enter.
      return filter_input(f, f.id.ends_with("/search") || f.id.ends_with("/clear") || f.id.ends_with("/dir")
                                 ? Key::activate : Key::right);
    default:
      return input(Input{Key::activate, false});  // an action, a tab, a section, a shortcut, a related page: as Enter
  }
}

void PageView::refused(std::string_view key, std::string_view reason) {
  refusals_[std::string(key)] = std::string(reason);
  if (const auto busy = share_busy_.find(key); busy != share_busy_.end()) {
    share_busy_.erase(busy);  // a report's Save refused: its words under it until the next press (D263)
    share_said_.erase(std::string(key));
    share_refused_[std::string(key)] = std::string(reason.size() <= kShareWordsMax ? reason : std::string_view());
  }
  for (std::size_t i = 0; i < focus_.size(); ++i) {
    if ((focus_[i].kind == FocusKind::setting || focus_[i].kind == FocusKind::cell ||
         focus_[i].kind == FocusKind::row_action || focus_[i].kind == FocusKind::section_action ||
         focus_[i].kind == FocusKind::form_submit || focus_[i].kind == FocusKind::row_secret ||
         focus_[i].kind == FocusKind::share) &&
        focus_[i].id == key) {
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
        SearchHit hit{s.key, s.home, s.label, std::string(), std::string(), std::string()};
        for (const protocol::Page& p : pages_.pages) {
          if (p.id == s.home) {
            hit.where = p.name;
          }
        }
        hits.push_back(std::move(hit));
      }
    }
  }
  std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) { return a.label < b.label; });
  return hits;
}

void PageView::set_search_others(std::vector<SearchOther> others) {
  search_others_ = std::move(others);
  if (edit_.kind == EditKind::search) {
    edited();  // results open while the spaces change: found again
  }
}

std::vector<SearchHit> PageView::search_all(std::string_view query, std::size_t limit) const {
  std::vector<SearchHit> hits = search(query);
  for (const SearchOther& other : search_others_) {
    if (other.view == nullptr || other.view == this) {
      continue;
    }
    for (SearchHit hit : other.view->search(query)) {
      hit.space = other.id;
      hit.space_name = other.name;
      hits.push_back(std::move(hit));
    }
  }
  // By label, then the view's own space first, then by the other space's name: as the browser orders them (D219).
  std::stable_sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
    if (a.label != b.label) {
      return a.label < b.label;
    }
    return a.space.empty() != b.space.empty() ? a.space.empty() : a.space_name < b.space_name;
  });
  if (hits.size() > limit) {
    hits.resize(limit);
  }
  return hits;
}

void PageView::reveal(std::string_view key) {
  if (setting(key) == nullptr || !present(setting(key)->home)) {
    return;
  }
  for (std::size_t p = 0; p < pages_.pages.size(); ++p) {
    if (pages_.pages[p].id == setting(key)->home) {
      if (p != page_) {
        open_index(p, Scope::page);
      }
      focus_setting(key);
      scope_ = Scope::page;
      return;
    }
  }
}

// ---- Master and detail (D215, D36).

namespace {

// A row's key as a member's: 1 or more of the id characters but '/', the member's whole id at most 64 bytes.
bool member_key(std::string_view family, std::string_view key) noexcept {
  if (key.empty() || family.size() + 1 + key.size() > 64) {
    return false;
  }
  return std::all_of(key.begin(), key.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
  });
}

}  // namespace

const protocol::PageMaster* PageView::master() const {
  if (pages_.pages.empty()) {
    return nullptr;
  }
  const auto& m = current().master;
  return m ? &*m : nullptr;
}

std::string PageView::master_name(const protocol::PageMaster& m) const {
  return m.topic ? *m.topic : current().id + ".list";  // a list read a page at a time has no topic: its page names it
}

std::string PageView::master_topic() const {
  const protocol::PageMaster* m = master();
  if (m == nullptr || !m->topic) {
    return {};  // none for a list read a page at a time (D254)
  }
  if (current().scoped.value_or(false) && is_family(*m->topic)) {
    const std::string id = scope_id();  // a scoped page's list: the scope's member (D254)
    return id.empty() ? std::string() : *m->topic + "/" + id;
  }
  return *m->topic;
}

const Json* PageView::master_body() const {
  if (const protocol::PageMaster* m = master(); m != nullptr && m->page) {
    const Paged* p = paged_of(master_name(*m));  // rows read a page at a time (D254)
    return p != nullptr && p->answered ? &p->body : nullptr;
  }
  const std::string topic = master_topic();
  if (topic.empty()) {
    return nullptr;
  }
  const auto found = topics_.find(topic);
  return found != topics_.end() ? &found->second : nullptr;
}

std::vector<std::pair<std::string, const Json*>> PageView::master_rows() const {
  std::vector<std::pair<std::string, const Json*>> out;
  const protocol::PageMaster* m = master();
  if (m == nullptr) {
    return out;
  }
  const Json* body = master_body();
  if (body == nullptr || !body->is_object() || !body->contains("rows") || !(*body)["rows"].is_array()) {
    return out;
  }
  // Through its filter bar, if it has one (G43, D250): the rows that show, in the order they show.
  const Json& rows = (*body)["rows"];
  std::vector<std::size_t> order;
  if (const Bar bar = bar_of_master(); bar && !m->page) {  // a page at a time: the space filtered (D254)
    const auto state = filters_.find(bar.key);
    order = visible_rows(rows, *bar.bar, state != filters_.end() ? state->second : FilterState{});
  } else {
    for (std::size_t r = 0; r < rows.size(); ++r) {
      order.push_back(r);
    }
  }
  for (const std::size_t r : order) {
    const Json& row = rows[r];
    if (!row.is_object()) {
      continue;
    }
    const std::string key = cell_words(row, m->key);
    if (member_key(m->detail, key)) {
      out.emplace_back(key, &row);  // a row whose key names no member is not shown: it could open nothing
    }
  }
  return out;
}

std::string PageView::topic_of(const protocol::PageBlock& block) const {
  if (!block.topic) {
    return {};
  }
  if (!is_family(*block.topic)) {
    return *block.topic;
  }
  const bool scoped = current().scoped.value_or(false);
  const protocol::PageMaster* m = master();
  if (m != nullptr && !(scoped && *block.topic != m->detail)) {
    // A family on a master page draws the chosen row's member: <family>/<the row's key> (D215, D217).
    const std::string key = chosen();
    return key.empty() ? std::string() : *block.topic + "/" + key;
  }
  if (scoped) {
    // Any other family on a scoped page draws the scope's member: <family>/<the scope in view> (D254).
    const std::string id = scope_id();
    return id.empty() ? std::string() : *block.topic + "/" + id;
  }
  return *block.topic;
}

bool PageView::is_family(std::string_view topic) const {
  return std::any_of(pages_.topics.begin(), pages_.topics.end(), [&](const protocol::TopicInfo& t) {
    return t.id == topic && t.family.value_or(false);
  });
}

std::vector<PageView::ScopeChoice> PageView::scope_choices() const {
  std::vector<ScopeChoice> out;
  if (!pages_.scope) {
    return out;
  }
  const Json* body = latest(pages_.scope->topic);
  if (body != nullptr && pages_.scope->field) {
    body = body->is_object() && body->contains(*pages_.scope->field) ? &(*body)[*pages_.scope->field] : nullptr;
  }
  if (body == nullptr || !body->is_object() || !body->contains("choices") || !(*body)["choices"].is_array()) {
    return out;
  }
  for (const Json& c : (*body)["choices"]) {
    const std::string id = c.is_object() ? cell_words(c, "id") : std::string();
    if (id.empty() || id.size() > 64 || id.find('/') != std::string::npos || out.size() >= 32) {
      continue;  // an id names a family's member: no slash
    }
    ScopeChoice one;
    one.id = id;
    one.label = cell_words(c, "label").empty() ? id : cell_words(c, "label");
    one.read_only = c.contains("read_only") && c["read_only"] == true;
    out.push_back(std::move(one));
  }
  return out;
}

std::string PageView::scope_id() const {
  const std::vector<ScopeChoice> choices = scope_choices();
  const auto listed = [&](const std::string& id) {
    return std::any_of(choices.begin(), choices.end(), [&](const ScopeChoice& c) { return c.id == id; });
  };
  if (!scope_choice_.empty() && listed(scope_choice_)) {
    return scope_choice_;
  }
  const Json* body = pages_.scope ? latest(pages_.scope->topic) : nullptr;
  if (body != nullptr && pages_.scope->field) {
    body = body->is_object() && body->contains(*pages_.scope->field) ? &(*body)[*pages_.scope->field] : nullptr;
  }
  const std::string current = body != nullptr && body->is_object() ? cell_words(*body, "current") : std::string();
  return listed(current) ? current : (choices.empty() ? std::string() : choices.front().id);
}

std::string PageView::chosen() const {
  if (master() == nullptr) {
    return {};
  }
  const auto at = chosen_.find(current().id);
  return at != chosen_.end() ? at->second : std::string();
}

void PageView::choose(const std::string& key) {
  const protocol::PageMaster* m = master();
  if (m == nullptr || chosen() == key) {
    return;
  }
  if (const std::string was = chosen(); !was.empty()) {
    topics_.erase(m->detail + "/" + was);  // the last row's member: not drawn again from what it was
  }
  chosen_[current().id] = key;
  const std::string focused_name = focused();
  rebuild();  // the detail's tables have the new member's rows
  bool found = false;
  for (std::size_t i = 0; scope_ == Scope::page && i < focus_.size(); ++i) {
    if (name_of(Scope::page, i) == focused_name) {
      page_focus_ = i;
      found = true;
      break;
    }
  }
  if (!found && scope_ == Scope::page) {
    refocus_name_ = focused_name;  // drawn again once the new member's state comes
  }
}

void PageView::follow_master() {
  const protocol::PageMaster* m = master();
  if (m == nullptr) {
    return;
  }
  if (scope_ == Scope::page && page_focus_ < focus_.size() && focus_[page_focus_].kind == FocusKind::master_row) {
    choose(focus_[page_focus_].id.substr(master_name(*m).size() + 1));
    return;
  }
  if (chosen().empty()) {
    const auto rows = master_rows();
    if (!rows.empty()) {
      choose(rows.front().first);
    }
  }
}

bool PageView::on_master_row() const {
  return scope_ == Scope::page && page_focus_ < focus_.size() && focus_[page_focus_].kind == FocusKind::master_row;
}

void PageView::settle_expected() {
  const protocol::PageMaster* m = master();
  const auto wanted = expected_.find(current().id);
  if (m == nullptr || wanted == expected_.end()) {
    return;
  }
  const auto rows = master_rows();
  if (std::none_of(rows.begin(), rows.end(), [&](const auto& row) { return row.first == wanted->second; })) {
    return;  // not in the list yet: kept for its next state
  }
  const std::string key = wanted->second;
  expected_.erase(wanted);
  const bool on_row = on_master_row();
  choose(key);
  if (on_row) {
    // Focus on the list goes with the choice, so that the row focused stays the row chosen.
    const std::string name = "item:" + master_name(*m) + "/" + key;
    for (std::size_t i = 0; i < focus_.size(); ++i) {
      if (name_of(Scope::page, i) == name) {
        page_focus_ = i;
      }
    }
  }
}

Action PageView::input(const Input& in) {
  pad_mode_ = in.pad;             // the key hints follow the last device used (§3.8, D242)
  focus_shown_ = true;            // a key or the pad: the ring shows where they act (G42)
  if (in.key == Key::toggle_hints) {
    hints_shown_ = !hints_shown_;  // View (§5.4): focus and the page stay as they are
    return {};
  }
  if (in.key == Key::scroll_up || in.key == Key::scroll_down) {
    // The right stick (§5.4): a step of the page, as a third of the wheel's notch; the page stays where it is left
    // until a key moves focus, as after the wheel.
    if (!capture_open()) {
      const float step = std::round(40.0f * scale_);
      scroll_ = std::max(0.0f, in.key == Key::scroll_up ? scroll_ - step : scroll_ + step);
      follow_ = false;
    }
    return {};
  }
  if (in.repeat && (!repeat_stays_ || !repeat_acts(in))) {
    // A held key's repeat that would do more than move along or step a number, or whose press took focus into another
    // part of the menu (the rail's → into the page): a fresh press does it (D270).
    return {};
  }
  master_focus_pending_ = false;  // a key moves focus on its own from now on
  refocus_name_.clear();
  if (in.key == Key::activate && !editing() && !capture_open() && scope_ == Scope::page && running(focused())) {
    return {};  // a control waiting for its slow command's answer takes no press (D216)
  }
  const std::string sender = focused();
  const Region from = region();
  // A repeat's step at a bound changes nothing: nothing is sent (a mod's queue would fill at the stick's pace).
  Json held;
  if (in.repeat && scope_ == Scope::page && page_focus_ < focus_.size() &&
      (focus_[page_focus_].kind == FocusKind::setting || focus_[page_focus_].kind == FocusKind::shortcut)) {
    held = value(settings_[focus_[page_focus_].index].key);
  }
  Action action = input_body(in);
  if (!in.repeat) {
    repeat_stays_ = region() == from;
  } else if (!(region() == from)) {
    repeat_stays_ = false;  // a repeat that took focus elsewhere after all: the rest of the hold does nothing
  }
  if (in.repeat && action.kind == Action::Kind::command && action.command.name == "settings.set" &&
      action.command.args.is_object() && action.command.args.contains("value") &&
      action.command.args.at("value") == held) {
    return {};
  }
  note_running(action, sender);
  note_sent(action);
  if (on_master_row() && master() != nullptr) {
    expected_.erase(current().id);  // the player chooses by hand: a save's choice waiting for its row goes (D218)
  }
  follow_master();
  return action;
}

Action PageView::pointer(const Pointer& in) {
  if (in.kind != Pointer::Kind::leave) {
    focus_shown_ = false;  // the mouse: the ring goes, the focus stays (G42)
    pad_mode_ = false;     // and the key hints are the keyboard's again (D242)
  }
  if (in.kind == Pointer::Kind::press) {
    master_focus_pending_ = false;
    refocus_name_.clear();
    repeat_stays_ = false;  // a held key's repeats stop at a press of the mouse (D270)
    if (const Hit* hit = hit_at(in.x, in.y); hit != nullptr && !editing() && !capture_open() &&
                                               running(name_of(hit->scope, hit->index))) {
      return {};
    }
  }
  Action action = pointer_body(in);
  if (action.kind == Action::Kind::command) {
    note_running(action, focused());  // a click focuses what it acts on
  }
  note_sent(action);  // a command the mouse sent: its answer is never held (D252)
  if (in.kind == Pointer::Kind::press) {
    if (on_master_row() && master() != nullptr) {
      expected_.erase(current().id);  // as a key's (D218)
    }
    follow_master();
  }
  return action;
}

void PageView::note_running(const Action& action, const std::string& sender) {
  if (action.kind != Action::Kind::command) {
    return;
  }
  const auto& slow = pages_.slow_commands;
  if (slow && std::find(slow->begin(), slow->end(), action.command.name) != slow->end()) {
    running_[action.command.name].push_back(sender);
  }
}

void PageView::finished(std::string_view command) {
  const auto at = running_.find(command);
  if (at != running_.end() && !at->second.empty()) {
    at->second.erase(at->second.begin());
    if (at->second.empty()) {
      running_.erase(at);
    }
  }
}

bool PageView::running(std::string_view element) const {
  for (const auto& [command, senders] : running_) {
    if (std::find(senders.begin(), senders.end(), element) != senders.end()) {
      return true;
    }
  }
  return false;
}

void PageView::focus_master_on_open() {
  const protocol::PageMaster* m = master();
  master_focus_pending_ = false;
  if (m == nullptr) {
    return;
  }
  const std::string key = chosen();
  bool any = false;
  for (std::size_t i = 0; i < focus_.size(); ++i) {
    if (focus_[i].kind == FocusKind::master_row) {
      if (!any || focus_[i].id == master_name(*m) + "/" + key) {
        page_focus_ = i;  // the row it chose last time, or the first (D215)
      }
      any = true;
    }
  }
  master_focus_pending_ = !any;
}

// ---- The editor block (D217).

namespace {

constexpr std::size_t kNoItem = static_cast<std::size_t>(-1);

// A state topic's choices ({"choices": [{value, label}]}), at most 256, as a picker's (D193).
std::vector<protocol::Choice> choices_in(const std::map<std::string, Json, std::less<>>& topics, std::string_view topic) {
  std::vector<protocol::Choice> out;
  const auto found = topics.find(topic);
  if (found == topics.end() || !found->second.is_object() || !found->second.contains("choices") ||
      !found->second["choices"].is_array()) {
    return out;
  }
  for (const Json& c : found->second["choices"]) {
    if (out.size() < 256 && c.is_object() && c.contains("value") && c["value"].is_string() &&
        !c["value"].get_ref<const std::string&>().empty()) {
      out.push_back(protocol::Choice{c["value"].get<std::string>(), c.contains("label") && c["label"].is_string()
                                                                         ? c["label"].get<std::string>()
                                                                         : c["value"].get<std::string>()});
    }
  }
  return out;
}

}  // namespace

std::string PageView::editor_part_name(EditorPart part) {
  switch (part) {
    case EditorPart::save: return "save";
    case EditorPart::check: return "check";
    case EditorPart::reset: return "reset";
    case EditorPart::restore: return "restore";
    case EditorPart::reload: return "reload";
    case EditorPart::notes: return "notes";
    case EditorPart::draft: return "draft";
    case EditorPart::use_all: return "use_all";
    default: return "field";
  }
}

std::string PageView::editor_hints(const Focus& f) {
  EditorAt at;
  if (!editor_of(f, at)) {
    return "Esc  Back";
  }
  const auto part = static_cast<EditorPart>(f.part);
  const std::string name = name_of(Scope::page, page_focus_);
  if (running(name)) {
    return "Working…    Esc  Back";
  }
  if (confirming_ == name) {
    return "Enter  Confirm    Esc  Back";
  }
  if (part == EditorPart::notes) {
    return "Enter  Type    Esc  Back";
  }
  if (part == EditorPart::add) {
    return "Enter  Add an item    Esc  Back";
  }
  if (part != EditorPart::field && part != EditorPart::item) {
    return part == EditorPart::draft && !at.doc.draft_blocked.empty() ? "Not available now    Esc  Back"
                                                                      : "Enter  Press    Esc  Back";
  }
  if (f.index >= at.doc.fields.size()) {
    return "Esc  Back";
  }
  const EditorField& field = at.doc.fields[f.index];
  if (!field.locked.empty()) {
    return "Locked    Esc  Back";
  }
  if (part == EditorPart::item) {
    const Json& items = at.state->value(field);
    if (items.is_array() && f.sub < items.size() && !list_item(items[f.sub]).locked.empty()) {
      return "Shift+↑ ↓  Move    Esc  Back";
    }
    return !field.choices.empty() || !field.choices_from.empty()
               ? "Enter  Edit    ← →  Choose    Shift+↑ ↓  Move    R  Remove    Esc  Back"
               : "Enter  Edit    Shift+↑ ↓  Move    R  Remove    Esc  Back";
  }
  const char* undo = field.nullable ? "R  Undo, then not set here" : "R  Undo";
  switch (field.kind) {
    case FieldKind::line:
    case FieldKind::prose:
      return std::string("Enter  Edit    ") + undo + "    Esc  Back";
    case FieldKind::number:
      return std::string("← →  Change    Shift+← →  By ten    ") + undo + "    Esc  Back";
    case FieldKind::toggle:
      return std::string("Enter  Toggle    ") + undo + "    Esc  Back";
    default:
      return std::string("← →  Choose    ") + undo + "    Esc  Back";
  }
}

bool PageView::editor_of(const Focus& f, EditorAt& out) {
  if (f.kind != FocusKind::editor || f.block == nullptr || !f.block->editor) {
    return false;
  }
  out.block = f.block;
  out.topic = topic_of(*f.block);
  const auto body = topics_.find(out.topic);
  if (out.topic.empty() || body == topics_.end() || !EditorDocument::parse(body->second, out.doc)) {
    return false;
  }
  out.state = &editors_[out.topic];
  out.eid = f.id.substr(0, f.id.find('/', f.id.find('/') + 1));  // "<section>/<n>"
  return true;
}

const EditorState* PageView::editor_state(std::string_view topic) const {
  const auto found = editors_.find(topic);
  return found != editors_.end() ? &found->second : nullptr;
}

void PageView::editor_set(EditorAt& at, const EditorField& field, Json value) {
  at.state->set(field, std::move(value));
  if (at.state->dirty() && at.state->base_rev == 0) {
    at.state->base_rev = at.doc.rev;  // the revision the edits are over (a save against another answers conflict)
  } else if (!at.state->dirty()) {
    at.state->base_rev = 0;
  }
}

void PageView::start_editor_edit(EditKind kind, std::string id, std::string_view text, std::size_t chars,
                                 const EditorAt& at, std::string field, std::size_t item, bool prose) {
  // A field's room: its characters at four bytes each, or 4 KiB with no cap of its own.
  start_edit(kind, std::move(id), std::string_view{}, 0, false, at.block);
  edit_.field = TextField(text, chars != 0 ? chars * 4 : 4096, chars);
  edit_.topic = at.topic;
  edit_.editor_field = std::move(field);
  edit_.item = item;
  edit_.prose = prose;
}

Action PageView::editor_command(EditorAt& at, EditorPart part, const std::string& sender) {
  if (!greyed_words(current().id).empty()) {
    return {};  // a greyed page's editor is inert (D188)
  }
  const protocol::EditorBlock& ed = *at.block->editor;
  EditorState& st = *at.state;
  Action action;
  action.kind = Action::Kind::command;
  action.command.space = pages_.space;
  const auto rev = [&] { return st.dirty() && st.base_rev != 0 ? st.base_rev : at.doc.rev; };
  switch (part) {
    case EditorPart::save:
      action.command.name = ed.save;
      action.command.args = Json::object({{"target", at.doc.target}, {"rev", rev()}, {"doc", st.doc(at.doc)}});
      break;
    case EditorPart::check:
      action.command.name = *ed.check;
      action.command.args = Json::object({{"target", at.doc.target}, {"rev", rev()}, {"doc", st.doc(at.doc)}});
      break;
    case EditorPart::reset:
      if (confirming_ != sender) {
        confirming_ = sender;  // every field back to the space's own: asks first (§7.3)
        return {};
      }
      confirming_.clear();
      action.command.name = *ed.reset;
      action.command.args = Json::object({{"target", at.doc.target}, {"all", true}, {"confirm", true}});
      break;
    case EditorPart::restore:
      action.command.name = *ed.restore;
      action.command.args = Json::object({{"target", at.doc.target}});
      break;
    case EditorPart::draft:
      if (!at.doc.draft_blocked.empty()) {
        return {};  // the space says why not, beside the button
      }
      action.command.name = *ed.draft;
      action.command.args = Json::object({{"target", at.doc.target}, {"notes", st.notes}});
      break;
    case EditorPart::reload:
      if (st.dirty() && confirming_ != sender) {
        confirming_ = sender;  // the player's unsaved edits go: asks first
        return {};
      }
      confirming_.clear();
      st.clear();
      rebuild();
      return {};
    case EditorPart::use_all:
      for (const EditorField& field : at.doc.fields) {
        if (st.draft.count(field.id) != 0) {
          at.state->use(field);
        }
      }
      if (st.dirty() && st.base_rev == 0) {
        st.base_rev = at.doc.rev;  // the revision the edits are over
      }
      rebuild();
      return {};
    default:
      return {};
  }
  editor_sent_[sender] = EditorSent{at.topic, current().id};  // where its answer goes, whatever is open when it comes
  return action;
}

Action PageView::editor_input(const Focus& f, const Input& in, bool& handled) {
  handled = false;
  EditorAt at;
  if (!editor_of(f, at)) {
    return {};
  }
  const auto part = static_cast<EditorPart>(f.part);
  const std::string sender = name_of(Scope::page, page_focus_);
  const bool on_field = part == EditorPart::field || part == EditorPart::item || part == EditorPart::add ||
                        part == EditorPart::use || part == EditorPart::keep;
  if (on_field && f.index >= at.doc.fields.size()) {
    return {};
  }
  const EditorField* field = on_field ? &at.doc.fields[f.index] : nullptr;
  const bool inert = !greyed_words(current().id).empty() || (field != nullptr && !field->locked.empty());
  const auto refocus = [&](const std::string& name) {
    rebuild();
    for (std::size_t i = 0; i < focus_.size(); ++i) {
      if (name_of(Scope::page, i) == name) {
        page_focus_ = i;
      }
    }
  };
  const auto text_of_value = [](const Json& v) { return v.is_string() ? v.get<std::string>() : std::string(); };
  // A choice, a pick, a number's or a list item's next or previous value.
  const auto step = [&](int direction, bool shift) -> bool {
    if (field == nullptr || inert) {
      return false;
    }
    const Json& v = at.state->value(*field);
    if (field->kind == FieldKind::choice || field->kind == FieldKind::pick) {
      std::vector<protocol::Choice> choices =
          field->kind == FieldKind::pick ? choices_in(topics_, field->choices_from) : field->choices;
      if (field->nullable) {
        choices.insert(choices.begin(), protocol::Choice{"", "Not set here"});
      }
      if (choices.empty()) {
        return true;
      }
      std::size_t at_index = choices.size();
      for (std::size_t i = 0; i < choices.size(); ++i) {
        if ((v.is_string() && choices[i].value == v.get_ref<const std::string&>()) ||
            (v.is_null() && choices[i].value.empty())) {
          at_index = i;
        }
      }
      const std::size_t n = choices.size();
      const std::size_t next = at_index == n ? (direction > 0 ? 0 : n - 1) : (at_index + (direction > 0 ? 1 : n - 1)) % n;
      editor_set(at, *field, choices[next].value.empty() && field->nullable ? Json() : Json(choices[next].value));
      return true;
    }
    if (field->kind == FieldKind::number) {
      double n = v.is_number() ? v.get<double>() : field->min.value_or(0);
      n += direction * (shift ? 10 : 1);
      const double lo = field->min.value_or(n);
      const double hi = field->max.value_or(n);
      if (lo <= hi) {
        n = std::clamp(n, lo, hi);  // a space's min above its max: no bounds of GraceUI's
      }
      editor_set(at, *field, Json(static_cast<std::int64_t>(std::llround(n))));
      return true;
    }
    if (field->kind == FieldKind::toggle) {
      editor_set(at, *field, Json(!(v.is_boolean() && v.get<bool>())));
      return true;
    }
    if (part == EditorPart::item && (!field->choices.empty() || !field->choices_from.empty())) {
      Json items = v.is_array() ? v : Json::array();
      if (f.sub >= items.size()) {
        return true;
      }
      ListItem item = list_item(items[f.sub]);
      if (!item.locked.empty()) {
        return true;
      }
      const std::vector<protocol::Choice> choices =
          !field->choices.empty() ? field->choices : choices_in(topics_, field->choices_from);
      if (choices.empty()) {
        return true;
      }
      std::size_t idx = choices.size();
      for (std::size_t i = 0; i < choices.size(); ++i) {
        if (choices[i].value == item.choice) {
          idx = i;
        }
      }
      const std::size_t n = choices.size();
      item.choice = choices[idx == n ? 0 : (idx + (direction > 0 ? 1 : n - 1)) % n].value;
      items[f.sub] = list_json(item, true);
      editor_set(at, *field, items);
      return true;
    }
    return false;
  };
  switch (in.key) {
    case Key::activate:
      handled = true;
      switch (part) {
        case EditorPart::save:
        case EditorPart::check:
        case EditorPart::reset:
        case EditorPart::restore:
        case EditorPart::reload:
        case EditorPart::draft:
        case EditorPart::use_all:
          return editor_command(at, part, sender);
        case EditorPart::notes:
          start_editor_edit(EditKind::editor_notes, f.id, at.state->notes, at.block->editor->notes_max.value_or(400), at,
                            std::string(), kNoItem, true);
          return {};
        case EditorPart::field:
          if (inert) {
            return {};
          }
          if (field->kind == FieldKind::line || field->kind == FieldKind::prose) {
            start_editor_edit(EditKind::editor_text, f.id, text_of_value(at.state->value(*field)), field->max_chars, at,
                              field->id, kNoItem, field->kind == FieldKind::prose);
            return {};
          }
          step(1, false);
          rebuild();
          return {};
        case EditorPart::item: {
          const Json& items = at.state->value(*field);
          if (inert || !items.is_array() || f.sub >= items.size() || !list_item(items[f.sub]).locked.empty()) {
            return {};
          }
          start_editor_edit(EditKind::editor_text, f.id, list_item(items[f.sub]).text, field->max_chars, at, field->id,
                            f.sub, false);
          return {};
        }
        case EditorPart::add: {
          if (inert) {
            return {};
          }
          Json items = at.state->value(*field).is_array() ? at.state->value(*field) : Json::array();
          ListItem fresh;
          const bool with_choice = !field->choices.empty() || !field->choices_from.empty();
          if (with_choice) {
            const auto choices = !field->choices.empty() ? field->choices : choices_in(topics_, field->choices_from);
            fresh.choice = choices.empty() ? std::string() : choices.front().value;
          }
          items.push_back(list_json(fresh, with_choice));
          const std::size_t added = items.size() - 1;
          editor_set(at, *field, items);
          const std::string name = "editor:" + at.eid + "/i/" + field->id + "/" + std::to_string(added);
          refocus(name);
          start_editor_edit(EditKind::editor_text, name.substr(7), std::string(), field->max_chars, at, field->id, added,
                            false);
          return {};
        }
        case EditorPart::use:
        case EditorPart::keep: {
          if (part == EditorPart::use) {
            at.state->use(*field);
            editor_set(at, *field, at.state->value(*field));
          } else {
            at.state->keep(field->id);
          }
          // Back to the field: its own stop, or a list's first item (or its Add).
          const Json& items = at.state->value(*field);
          refocus(field->kind != FieldKind::list ? "editor:" + at.eid + "/f/" + field->id
                  : items.is_array() && !items.empty() ? "editor:" + at.eid + "/i/" + field->id + "/0"
                                                       : "editor:" + at.eid + "/add/" + field->id);
          return {};
        }
      }
      return {};
    case Key::left:
    case Key::right:
      if ((part == EditorPart::field || part == EditorPart::item) && step(in.key == Key::right ? 1 : -1, in.shift)) {
        handled = true;
        rebuild();
      }
      return {};
    case Key::up:
    case Key::down:
      if (part == EditorPart::item && in.shift && !inert) {
        // Shift+↑ ↓: the item moves, a locked one too (its place is the player's), focus with it.
        Json items = at.state->value(*field);
        const std::size_t to = in.key == Key::up ? f.sub - 1 : f.sub + 1;
        if (items.is_array() && f.sub < items.size() && to < items.size()) {
          std::swap(items[f.sub], items[to]);
          editor_set(at, *field, items);
          refocus("editor:" + at.eid + "/i/" + field->id + "/" + std::to_string(to));
        }
        handled = true;
      }
      return {};
    case Key::reset:
      handled = true;
      if (field == nullptr || inert) {
        return {};
      }
      if (part == EditorPart::item) {
        Json items = at.state->value(*field);
        if (items.is_array() && f.sub < items.size() && list_item(items[f.sub]).locked.empty()) {
          items.erase(items.begin() + static_cast<std::ptrdiff_t>(f.sub));
          editor_set(at, *field, items);
          refocus(f.sub > 0 ? "editor:" + at.eid + "/i/" + field->id + "/" + std::to_string(f.sub - 1)
                            : "editor:" + at.eid + "/add/" + field->id);
        }
        return {};
      }
      if (at.state->edits.count(field->id) != 0) {
        at.state->edits.erase(field->id);  // R: the edit undone, back to the space's value
        editor_set(at, *field, at.state->value(*field));
      } else if (field->nullable && !field->value.is_null()) {
        editor_set(at, *field, Json());  // R again: left to the space, "not set here"
      }
      rebuild();
      return {};
    default:
      return {};
  }
}

void PageView::editor_answer(std::string_view where, const protocol::Result& result) {
  const auto sent = editor_sent_.find(where);
  if (sent == editor_sent_.end()) {
    return;
  }
  const std::string topic = sent->second.topic;
  const std::string page = sent->second.page;
  editor_sent_.erase(sent);
  EditorState& st = editors_[topic];
  EditorDocument doc;
  const auto body = topics_.find(topic);
  const bool have = body != topics_.end() && EditorDocument::parse(body->second, doc);
  const std::string part = std::string(where.substr(where.rfind('/') + 1));
  const bool ok = !result.error.has_value();
  const Json answer = result.body ? Json(*result.body) : Json::object();
  const std::string words = result.detail ? *result.detail : std::string();
  if (part == "save") {
    if (ok) {
      st.clear();  // saved: the space publishes the document again, with its new revision
      // The row to choose next (D218): a member's key of the page's master's, taken once the list has it.
      const protocol::Page* on = nullptr;
      for (const protocol::Page& p : pages_.pages) {
        if (p.id == page) {
          on = &p;
        }
      }
      if (on != nullptr && on->master && answer.is_object() && answer.contains("chosen") && answer["chosen"].is_string() &&
          member_key(on->master->detail, answer["chosen"].get_ref<const std::string&>())) {
        expected_[page] = answer["chosen"].get<std::string>();
        settle_expected();
      }
    } else if (*result.error == protocol::ErrorCode::conflict) {
      st.conflict = true;
      st.top = words.empty() ? "Changed elsewhere since you opened it." : words;
    } else {
      st.take_messages(answer);
      st.top = words.empty() ? "Not saved: see the messages below." : words;
    }
  } else if (part == "check") {
    st.take_messages(answer);
    st.top = !ok ? (words.empty() ? "Not checked." : words) : st.messages.empty() ? "No problems found." : "";
  } else if (part == "reset" || part == "restore") {
    if (ok) {
      st.clear();
    } else {
      st.top = words.empty() ? "Not done." : words;
    }
  } else if (part == "draft") {
    if (ok && have) {
      st.take_draft(doc, answer);
      st.top = st.draft.empty() && st.notice.empty() ? "The draft proposed nothing." : "";
    } else {
      st.top = words.empty() ? "No draft came." : words;
    }
  }
  const std::string was = focused();
  rebuild();
  for (std::size_t i = 0; scope_ == Scope::page && i < focus_.size(); ++i) {
    if (name_of(Scope::page, i) == was) {
      page_focus_ = i;
    }
  }
}

// ---- Text entry (§5.6, D57, G38 (a)).

namespace {

// A string's bytes overwritten, then emptied: a secret's copy, once it is in its command.
void wipe(std::string& text) noexcept {
  volatile char* bytes = text.data();
  for (std::size_t i = 0; i < text.size(); ++i) {
    bytes[i] = '\0';
  }
  text.clear();
}

constexpr std::size_t kSearchHits = 8;       // search's results in game (§4.3)
constexpr std::size_t kSearchMax = 128;      // its query's bytes
constexpr std::size_t kSettingTextMax = 1024;  // a text's or a secret's value, as the settings store bounds it

}  // namespace

std::string PageView::form_id(const protocol::PageSection& section, std::size_t index, bool advanced) {
  return section.id + "/" + (advanced ? "a" : "") + std::to_string(index);
}

std::string PageView::secret_id(const protocol::PageBlock& block, const Json& row, std::size_t r) {
  const std::string key = row.is_object() && block.row_secret ? cell_words(row, block.row_secret->key) : std::string();
  return cell_base(block) + "/" + (key.empty() ? std::to_string(r) : key);
}

void PageView::start_edit(EditKind kind, std::string id, std::string_view text, std::size_t max, bool masked,
                          const protocol::PageBlock* block, Json row_key) {
  end_edit();
  edit_.kind = kind;
  edit_.id = std::move(id);
  edit_.field = TextField(text, max);
  edit_.masked = masked;
  edit_.offset = 0;
  edit_.block = block;
  edit_.row_key = std::move(row_key);
  refusals_.erase(edit_.id);  // a new try: the last refusal's line goes
}

void PageView::end_edit() {
  if (edit_.masked) {
    edit_.field.wipe();
  }
  edit_ = Edit{};
  caret_rect_ = {};
  edit_box_ = {};
  edit_stops_.clear();
  prose_stops_.clear();
}

void PageView::editor_store() {
  EditorState& st = editors_[edit_.topic];
  if (edit_.kind == EditKind::editor_notes) {
    st.notes = edit_.field.text();
    return;
  }
  EditorAt at;
  const auto body = topics_.find(edit_.topic);
  if (body == topics_.end() || !EditorDocument::parse(body->second, at.doc)) {
    return;
  }
  const EditorField* field = at.doc.field(edit_.editor_field);
  if (field == nullptr) {
    return;
  }
  at.block = edit_.block;
  at.topic = edit_.topic;
  at.state = &st;
  if (edit_.item == kNoItem) {
    editor_set(at, *field, Json(edit_.field.text()));
    return;
  }
  Json items = st.value(*field);
  if (!items.is_array() || edit_.item >= items.size()) {
    return;
  }
  ListItem item = list_item(items[edit_.item]);
  if (!item.locked.empty()) {
    return;
  }
  item.text = edit_.field.text();
  items[edit_.item] = list_json(item, !field->choices.empty() || !field->choices_from.empty());
  editor_set(at, *field, std::move(items));
}

void PageView::edited() {
  if (edit_.kind == EditKind::form_field) {
    form_values_[edit_.id] = edit_.field.text();  // kept in the view until the space accepts the form (G38 (2))
  } else if (edit_.kind == EditKind::editor_text || edit_.kind == EditKind::editor_notes) {
    editor_store();  // kept at each change, so that no close path loses it (G38 (2))
  } else if (edit_.kind == EditKind::search) {
    search_query_ = edit_.field.text();
    search_hits_ = search_all(search_query_, kSearchHits);  // every space's the menu shows (D219)
    search_pick_ = 0;
  } else if (edit_.kind == EditKind::filter_search) {
    // A filter bar's search (D250), applied as it is typed; its rows and the bar's own stops follow.
    const std::string key = edit_.id.substr(0, edit_.id.rfind('/'));  // "<key>/search"
    filters_[key].search = edit_.field.text();
    const std::string id = edit_.id;
    rebuild();
    refocus(id);
  }
}

Action PageView::finish_edit(bool keep, int move_on) {
  Action action;
  std::string text = edit_.field.text();
  switch (edit_.kind) {
    case EditKind::setting:
      if (const Setting* st = setting(edit_.id); keep && st != nullptr) {
        if (edit_.masked) {
          if (!text.empty()) {
            action = set(*st, Json(text));
            values_[st->key] = "set";  // the view keeps whether it is set, never the secret (invariant 5)
          }
        } else if (numeric(st->type)) {
          // A slider's typed entry (D266): a value on its grid within its bounds, or the bounds in words on its row.
          Typed typed = typed_value(*st, text);
          if (typed.ok && value(st->key) != typed.value) {
            action = set(*st, std::move(typed.value));
          } else if (!typed.ok && !typed.words.empty()) {
            refusals_[st->key] = std::move(typed.words);
          }
        } else if (value(st->key) != Json(text)) {
          action = set(*st, Json(text));
        }
      }
      break;
    case EditKind::form_field:
      form_values_[edit_.id] = text;
      break;
    case EditKind::row_secret:
      if (keep && !text.empty() && edit_.block != nullptr && edit_.block->row_secret) {
        action.kind = Action::Kind::command;
        action.command.space = pages_.space;
        action.command.name = edit_.block->row_secret->command;
        action.command.args = Json::object({{edit_.block->row_secret->key, edit_.row_key}, {"value", text}});
      }
      break;
    case EditKind::editor_text:
      if (text.empty() && edit_.item != kNoItem) {
        // A new item left empty goes again: an item the player added has no key of the space's.
        const auto body = topics_.find(edit_.topic);
        EditorAt at;
        if (body != topics_.end() && EditorDocument::parse(body->second, at.doc)) {
          if (const EditorField* field = at.doc.field(edit_.editor_field); field != nullptr) {
            at.block = edit_.block;
            at.topic = edit_.topic;
            at.state = &editors_[edit_.topic];
            Json items = at.state->value(*field);
            if (items.is_array() && edit_.item < items.size() && list_item(items[edit_.item]).key.is_null() &&
                list_item(items[edit_.item]).locked.empty()) {
              items.erase(items.begin() + static_cast<std::ptrdiff_t>(edit_.item));
              editor_set(at, *field, std::move(items));
              end_edit();
              rebuild();
              return action;
            }
          }
        }
      }
      break;
    case EditKind::filter_search:
      if (!keep) {
        // Escape empties the search, as the browser's search field does (D250).
        const std::string key = edit_.id.substr(0, edit_.id.rfind('/'));
        const std::string id = edit_.id;
        filters_[key].search.clear();
        end_edit();
        rebuild();
        refocus(id);
        return action;
      }
      break;  // its text is applied at each change (edited)
    case EditKind::editor_notes:
    case EditKind::search:
    case EditKind::none:
      break;  // an editor's text is kept at each change (editor_store)
  }
  if (edit_.masked) {
    wipe(text);
  }
  end_edit();
  if (move_on != 0 && !focus_.empty()) {
    page_focus_ = move_on > 0 ? std::min(page_focus_ + 1, focus_.size() - 1) : (page_focus_ > 0 ? page_focus_ - 1 : 0);
  }
  return action;
}

void PageView::focus_setting(std::string_view key) {
  // Its tab, its section opened and its Advanced disclosure if it is under one, then its row focused.
  const protocol::Page& page = current();
  const auto holds = [&](const std::vector<protocol::PageBlock>& blocks) {
    for (const protocol::PageBlock& block : blocks) {
      if (block.kind == protocol::BlockKind::settings) {
        for (const std::string& k : block.keys.value_or(std::vector<std::string>{})) {
          if (k == key) {
            return true;
          }
        }
      }
    }
    return false;
  };
  const auto open_in = [&](const std::vector<protocol::PageSection>& list) {
    for (const protocol::PageSection& section : list) {
      const bool plain = holds(section.blocks);
      const bool advanced = section.advanced && holds(*section.advanced);
      if (plain || advanced) {
        collapsed_.erase(page.id + "/" + section.id);
        if (advanced) {
          advanced_open_.insert(page.id + "/" + section.id);
        }
        return true;
      }
    }
    return false;
  };
  if (page.tabs) {
    for (std::size_t t = 0; t < page.tabs->size(); ++t) {
      if (open_in((*page.tabs)[t].sections)) {
        tab_ = t;
        break;
      }
    }
  } else if (page.sections) {
    open_in(*page.sections);
  }
  rebuild();
  for (std::size_t i = 0; i < focus_.size(); ++i) {
    if (focus_[i].kind == FocusKind::setting && focus_[i].id == key) {
      page_focus_ = i;
      scope_ = Scope::page;
    }
  }
}

Action PageView::open_hit(const SearchHit& hit) {
  if (!hit.space.empty()) {
    // Another space's (D219): the host switches to it, opens the home page and reveals the row.
    Action action;
    action.kind = Action::Kind::switch_space;
    action.space = hit.space;
    action.page = hit.page;
    action.key = hit.key;
    scope_ = Scope::page;
    return action;
  }
  for (std::size_t p = 0; p < pages_.pages.size(); ++p) {
    if (pages_.pages[p].id == hit.page && present(hit.page)) {
      Action action = open_index(p, Scope::page);
      focus_setting(hit.key);  // the row focused on its home page, on its tab (§4.3)
      return action;
    }
  }
  return {};
}

Action PageView::edit(const EditInput& in) {
  if (!editing()) {
    return {};
  }
  using Move = TextField::Move;
  TextField& f = edit_.field;
  switch (in.key) {
    case EditKey::left:
      f.move(in.ctrl ? Move::word_left : Move::left, in.shift);
      return {};
    case EditKey::right:
      f.move(in.ctrl ? Move::word_right : Move::right, in.shift);
      return {};
    case EditKey::home:
      f.move(Move::home, in.shift);
      return {};
    case EditKey::end:
      f.move(Move::end, in.shift);
      return {};
    case EditKey::backspace:
      if (f.backspace(in.ctrl)) {
        edited();
      }
      return {};
    case EditKey::erase:
      if (f.erase(in.ctrl)) {
        edited();
      }
      return {};
    case EditKey::select_all:
      f.select_all();
      return {};
    case EditKey::copy:
    case EditKey::cut:
      if (!edit_.masked && f.selected()) {  // never a secret's (invariant 5)
        clipboard_ = in.key == EditKey::cut ? f.cut() : std::string(f.selection());
        has_clipboard_ = true;
        if (in.key == EditKey::cut) {
          edited();
        }
      }
      return {};
    case EditKey::up:
    case EditKey::down:
      if (edit_.prose && !prose_stops_.empty()) {
        // By line, as the last layout wrapped it: the boundary nearest the caret's x on the line above or below, or the
        // field's start or end from its first or last line.
        float cx = 0;
        float top = 0;
        for (const auto& [at, x, line_top] : prose_stops_) {
          if (at == f.caret()) {
            cx = x;
            top = line_top;
          }
        }
        const float want = top + (in.key == EditKey::up ? -prose_line_ : prose_line_);
        std::size_t best = in.key == EditKey::up ? 0 : f.text().size();
        float nearest = 1e30f;
        for (const auto& [at, x, line_top] : prose_stops_) {
          if (std::fabs(line_top - want) < 0.5f && std::fabs(x - cx) < nearest) {
            nearest = std::fabs(x - cx);
            best = at;
          }
        }
        f.place(best, in.shift);
        return {};
      }
      if (edit_.kind == EditKind::search && !search_hits_.empty()) {
        search_pick_ = in.key == EditKey::up ? (search_pick_ > 0 ? search_pick_ - 1 : 0)
                                             : std::min(search_pick_ + 1, search_hits_.size() - 1);
      }
      return {};
    case EditKey::enter:
      if (edit_.kind == EditKind::search) {
        if (search_hits_.empty()) {
          return {};
        }
        const SearchHit hit = search_hits_[search_pick_];
        search_query_.clear();
        search_hits_.clear();
        end_edit();
        return open_hit(hit);
      }
      return finish_edit(true, edit_.kind == EditKind::form_field ? 1 : 0);  // an editor's text: one paragraph, done
    case EditKey::tab:
      // Tab finishes the entry where it is and moves no focus (G41): in search, the query stays in its box and the
      // results close; in a field, its text is kept as Enter would keep it, the focus staying on the field.
      if (edit_.kind == EditKind::search) {
        end_edit();
        return {};
      }
      return finish_edit(true, 0);
    case EditKey::escape:
      if (edit_.kind == EditKind::search) {
        search_query_.clear();
        search_hits_.clear();
        end_edit();
        return {};
      }
      return finish_edit(edit_.kind == EditKind::form_field, 0);  // a form's or an editor's text stays; an edit is dropped
  }
  return {};
}

Action PageView::typed(char32_t code_point) {
  if (capture_open() || sheet_open_) {
    return {};
  }
  if (!editing()) {
    return code_point == U'/' ? input(Input{Key::search, false}) : Action{};  // `/` opens search (§4.3)
  }
  if (edit_.field.type(code_point)) {
    edited();
  }
  return {};
}

Action PageView::pasted(std::string_view utf8) {
  if (editing() && !sheet_open_ && edit_.field.paste(utf8)) {
    edited();
  }
  return {};
}

bool PageView::take_clipboard(std::string& out) {
  if (!has_clipboard_) {
    return false;
  }
  out = std::move(clipboard_);
  clipboard_.clear();
  has_clipboard_ = false;
  return true;
}

void PageView::accepted(std::string_view where, std::string_view words) {
  if (const auto busy = share_busy_.find(where); busy != share_busy_.end()) {
    share_busy_.erase(busy);
    share_said_[std::string(where)] = std::string(words.size() <= kShareWordsMax ? words : std::string_view());
  }
  const std::string prefix = std::string(where) + "/";
  for (auto at = form_values_.lower_bound(prefix); at != form_values_.end() && at->first.rfind(prefix, 0) == 0;) {
    at = form_values_.erase(at);  // the form the space took: its fields empty again (D195)
  }
  refusals_.erase(std::string(where));
}

std::string PageView::form_value(std::string_view field) const {
  const auto found = form_values_.find(field);
  return found != form_values_.end() ? found->second : std::string();
}

void PageView::show_sheet(Sheet sheet) {
  if (editing()) {
    end_edit();  // nothing types under a sheet
  }
  sheet_ = std::move(sheet);
  sheet_open_ = true;
}

void PageView::close_sheet() noexcept {
  sheet_open_ = false;
  sheet_close_ = Rect{};
}

void PageView::cancel_confirmation() noexcept {
  confirming_.clear();
  scope_choice_.clear();  // the scope's current one again at the next open (D254)
  paged_.clear();         // and pages asked for afresh
  drawer_open_.clear();
  capture_ = Capture{};  // and an open capture: nothing changes (D209)
  if (editing()) {
    // As Escape, whatever closed the menu (§5.1, G38 (2)): a form's fields keep their text for the next open (kept at
    // each change), a setting's and a secret's edit is dropped, and search empties.
    if (edit_.kind == EditKind::search) {
      search_query_.clear();
      search_hits_.clear();
    }
    end_edit();
  }
}

// ---- The top bar's Live updates and Quick switches (G45, D252).

void PageView::set_live_mode(LiveMode mode) {
  live_mode_ = mode;
  applied_ms_ = now_ms_;
  if (mode != LiveMode::paused) {
    release_held();  // Live, or Every 5 s from Paused: what came meanwhile is drawn now
  }
}

bool PageView::tick(std::int64_t now_ms) {
  now_ms_ = now_ms;
  bool ended = false;
  for (auto at = copied_until_.begin(); at != copied_until_.end();) {
    if (now_ms_ >= at->second) {
      at = copied_until_.erase(at);  // "Copied" back to "Copy" (D263)
      ended = true;
    } else {
      ++at;
    }
  }
  if (ended) {
    return true;
  }
  if (live_mode_ == LiveMode::every_5s && !pending_.empty() && now_ms_ - applied_ms_ >= 5000) {
    release_held();
    return true;
  }
  return false;
}

bool PageView::hold(std::string_view topic) {
  if (releasing_ || live_mode_ == LiveMode::live || !current().live.value_or(false) || now_ms_ < release_until_ ||
      topics_.find(topic) == topics_.end()) {
    return false;  // not live, or Live; the player's own command answered; a topic's first body is always drawn
  }
  // (A quick switch's state may be held with its page: the Quick switches menu reads the newest body, held or not.)
  const std::vector<std::string> mine = component_topics();
  if (topic == "space.state" || std::find(mine.begin(), mine.end(), topic) == mine.end()) {
    return false;
  }
  if (live_mode_ == LiveMode::every_5s && pending_.empty() && now_ms_ - applied_ms_ >= 5000) {
    applied_ms_ = now_ms_;  // five seconds since the last: this one is drawn now, the next ones wait
    return false;
  }
  return true;
}

void PageView::release_held() {
  auto pending = std::move(pending_);
  pending_.clear();
  held_new_ = 0;
  releasing_ = true;
  for (const auto& [topic, body] : pending) {
    set_topic(topic, body);
  }
  releasing_ = false;
  applied_ms_ = now_ms_;
}

void PageView::note_sent(const Action& action) {
  if (action.kind == Action::Kind::command) {
    release_until_ = now_ms_ + 3000;  // its answer is never held, nor what else comes while it is answered
    release_held();
  }
}

const protocol::Json* PageView::latest(std::string_view topic) const {
  if (const auto held = pending_.find(topic); held != pending_.end()) {
    return &held->second;
  }
  const auto found = topics_.find(topic);
  return found != topics_.end() ? &found->second : nullptr;
}

std::vector<std::size_t> PageView::top_stops() const {
  std::vector<std::size_t> stops{kTopSearch};
  if (current().live.value_or(false)) {
    stops.push_back(kTopLive);
  }
  if (current().scoped.value_or(false) && !scope_choices().empty()) {
    stops.push_back(kTopScope);  // the scope's chooser (D254)
  }
  if (has_quick()) {
    stops.push_back(kTopQuick);
  }
  return stops;
}

bool PageView::is_stream(const protocol::PageBlock& block) {
  return block.kind == protocol::BlockKind::component &&
         (block.component == protocol::Component::live_feed || block.component == protocol::Component::log_tail);
}

bool PageView::has_drawer(const Json& table) {
  if (!table.is_object() || !table.contains("columns") || !table["columns"].is_array()) {
    return false;
  }
  return std::any_of(table["columns"].begin(), table["columns"].end(), [](const Json& c) {
    return c.is_object() && c.contains("drawer") && c["drawer"] == true;
  });
}

bool PageView::row_has_drawer(const Json& table, const Json& row) {
  // A row whose drawer columns are all empty has no drawer: no mark, no focus stop (D262).
  if (!row.is_object() || !has_drawer(table)) {
    return false;
  }
  return std::any_of(table["columns"].begin(), table["columns"].end(), [&](const Json& c) {
    return c.is_object() && c.contains("drawer") && c["drawer"] == true && c.contains("id") && c["id"].is_string() &&
           !cell_words(row, c["id"].get<std::string>()).empty();
  });
}

std::string PageView::drawer_key(const Json& row, std::size_t index) {
  // The row's id where it has one (a member's characters, no slash), else its place: the drawer follows its row.
  const std::string id = row.is_object() && row.contains("id") && row["id"].is_string() ? row["id"].get<std::string>()
                                                                                         : std::string();
  return !id.empty() && id.size() <= 64 && id.find('/') == std::string::npos ? id : "#" + std::to_string(index);
}

std::optional<bool> PageView::header_state(const protocol::PageAction& a) const {
  const Json* body = a.state ? latest(a.state->topic) : nullptr;
  if (body == nullptr || !body->is_object() || !body->contains(a.state->field) || !(*body)[a.state->field].is_boolean()) {
    return std::nullopt;  // not come, or not a boolean: "Not known yet", and nothing sent
  }
  return (*body)[a.state->field].get<bool>();
}

Action PageView::flip_quick(std::size_t index, int step) {
  const auto& quick = pages_.quick.value_or(std::vector<protocol::QuickSwitch>{});
  if (index >= quick.size()) {
    return {};
  }
  const protocol::QuickSwitch& q = quick[index];
  if (q.key) {
    const Setting* st = setting(*q.key);
    return st != nullptr ? change(*st, step, false) : Action{};  // as its row does: a switch turns, a choice steps
  }
  const protocol::Json* body = q.state ? latest(q.state->topic) : nullptr;
  if (body == nullptr || !body->is_object() || !body->contains(q.state->field) || !(*body)[q.state->field].is_boolean()) {
    return {};  // its state has not come: nothing is sent, nothing is claimed
  }
  Action action;
  action.kind = Action::Kind::command;
  action.command.space = pages_.space;
  action.command.name = *q.command;
  action.command.args = Json::object({{"on", !(*body)[q.state->field].get<bool>()}});
  return action;  // the space's next state shows it (invariant 1)
}

Action PageView::quick_input(const Input& in) {
  const std::size_t n = pages_.quick ? pages_.quick->size() : 0;
  switch (in.key) {
    case Key::up:
    case Key::previous:
      quick_focus_ = quick_focus_ > 0 ? quick_focus_ - 1 : 0;
      return {};
    case Key::down:
    case Key::next:
      quick_focus_ = n > 0 ? std::min(quick_focus_ + 1, n - 1) : 0;
      return {};
    case Key::activate:
    case Key::right:
      return flip_quick(quick_focus_, 1);
    case Key::left:
      return flip_quick(quick_focus_, -1);
    case Key::back:
    case Key::quick:
      quick_open_ = false;
      return {};
    default:
      return {};
  }
}

bool PageView::of_master_list(const protocol::PageMaster& m, const Focus& one) const {
  return one.kind == FocusKind::master_row ||
         (one.kind == FocusKind::paging && one.block == nullptr && one.id.rfind(master_name(m) + "/", 0) == 0);
}

PageView::Region PageView::region() const {
  const protocol::PageMaster* m = scope_ == Scope::page && page_focus_ < focus_.size() ? master() : nullptr;
  return Region{scope_, page_, tab_, m != nullptr && of_master_list(*m, focus_[page_focus_]), editing(), quick_open_,
                sheet_open_ || capture_open()};
}

bool PageView::repeat_acts(const Input& in) {
  const bool vertical = in.key == Key::up || in.key == Key::down;
  const bool sideways = in.key == Key::left || in.key == Key::right;
  if (!vertical && !sideways) {
    return false;  // Enter, Esc, a tab, a page, reset, search: once a press
  }
  if (sheet_open_ || capture_open()) {
    return false;  // a modal's keys: once a press
  }
  if (editing()) {
    return true;  // a field's caret, or search's pick
  }
  if (quick_open_) {
    return vertical;  // along the switches; ← → turn one
  }
  if (!confirming_.empty()) {
    return false;  // a destructive action's second press waits: only a fresh press lets it go
  }
  if (scope_ == Scope::rail) {
    return vertical;  // ← → follow the space switcher's links, → goes into the page
  }
  if (scope_ == Scope::top) {
    return sideways;  // along the top bar's stops; ↓ goes into the page
  }
  if (focus_.empty() || page_focus_ >= focus_.size()) {
    return false;  // ← goes back to the rail
  }
  const Focus& f = focus_[page_focus_];
  if (const protocol::PageMaster* m = master(); m != nullptr) {
    // Master and detail (D215): ↑ ↓ along the list, but not up from its first row to its filter bar, nor down into it
    // from the stop above, nor from the detail's first stop back to the chosen row; → from a row goes into the detail.
    const auto of_list = [&](const Focus& one) { return of_master_list(*m, one); };
    std::size_t first = focus_.size();
    std::size_t last = 0;
    for (std::size_t i = 0; i < focus_.size(); ++i) {
      if (of_list(focus_[i])) {
        first = std::min(first, i);
        last = i;
      }
    }
    if (first < focus_.size()) {
      if (of_list(f)) {
        return vertical && !(in.key == Key::up && page_focus_ == first);
      }
      if ((in.key == Key::down && page_focus_ + 1 == first) || (in.key == Key::up && page_focus_ == last + 1)) {
        return false;
      }
    }
  }
  if (vertical) {
    return true;  // along the page's stops (Shift+↑ ↓ moves an editor's list item, as far as it goes)
  }
  // ← →: a number's next step, and nothing else (a switch would turn back and forth, a choice go round, ← leave).
  if (f.kind == FocusKind::setting || f.kind == FocusKind::shortcut) {
    const SettingType type = settings_[f.index].type;
    return type == SettingType::integer || type == SettingType::number || type == SettingType::duration_s;
  }
  if (f.kind == FocusKind::editor && static_cast<EditorPart>(f.part) == EditorPart::field) {
    // A number field that takes its step: not one that is locked, nor on a greyed page, where ← goes to the rail.
    EditorAt at;
    return editor_of(f, at) && f.index < at.doc.fields.size() && at.doc.fields[f.index].kind == FieldKind::number &&
           at.doc.fields[f.index].locked.empty() && greyed_words(current().id).empty();
  }
  return false;
}

// ---- Rows read a page at a time (G45, D254).

const PageView::Paged* PageView::paged_of(std::string_view key) const {
  const auto found = paged_.find(key);
  return found != paged_.end() ? &found->second : nullptr;
}

std::vector<PageView::Source> PageView::page_sources() const {
  std::vector<Source> out;
  if (const protocol::PageMaster* m = master(); m != nullptr && m->page) {
    Source src;
    src.key = master_name(*m);
    src.source = &*m->page;
    src.bar = m->filter_bar ? &*m->filter_bar : nullptr;
    src.bar_key = "master:" + src.key;
    out.push_back(src);
  }
  for (const protocol::PageSection& section : sections()) {
    for (const auto* blocks : {&section.blocks, section.advanced ? &*section.advanced : nullptr}) {
      if (blocks == nullptr) {
        continue;
      }
      for (const protocol::PageBlock& block : *blocks) {
        if (block.page && block.kind == protocol::BlockKind::component && block.component == protocol::Component::table) {
          Source src;
          src.key = cell_base(block);
          src.source = &*block.page;
          src.bar = block.filter_bar ? &*block.filter_bar : nullptr;
          src.bar_key = src.key;
          out.push_back(src);
        }
      }
    }
  }
  return out;
}

Json PageView::page_args(const Source& src) const {
  // Its own args, then what the view chose: each filter's value by its column, the sort and its direction, the search
  // and the scope in view. The cursor is added for Show more.
  Json args = src.source->args && src.source->args->is_object() ? *src.source->args : Json::object();
  if (src.bar != nullptr) {
    const auto state = filters_.find(src.bar_key);
    const FilterState st = state != filters_.end() ? state->second : FilterState{};
    Json filters = Json::object();
    const auto& options = src.bar->filters.value_or(std::vector<protocol::FilterOption>{});
    for (std::size_t i = 0; i < options.size() && i < st.chosen.size(); ++i) {
      if (st.chosen[i]) {
        filters[options[i].column] = *st.chosen[i];
      }
    }
    if (!filters.empty()) {
      args["filters"] = std::move(filters);
    }
    if (src.bar->sort && !src.bar->sort->empty()) {
      const protocol::SortOption& by = (*src.bar->sort)[std::min(st.sort, src.bar->sort->size() - 1)];
      args["sort"] = Json::object({{"key", by.key}, {"direction", descending(*src.bar, st) ? "descending" : "ascending"}});
    }
    const std::size_t from = st.search.find_first_not_of(' ');
    if (from != std::string::npos) {
      args["search"] = st.search.substr(from, st.search.find_last_not_of(' ') - from + 1);  // as the browser sends it
    }
  }
  if (current().scoped.value_or(false)) {
    if (const std::string id = scope_id(); !id.empty()) {
      args["scope"] = id;
    }
  }
  return args;
}

std::vector<PageView::PageRequest> PageView::take_page_requests() {
  std::vector<PageRequest> out;
  for (const Source& src : page_sources()) {
    if (current().scoped.value_or(false) && pages_.scope && latest(pages_.scope->topic) == nullptr) {
      continue;  // its scope's topic has not come: nothing is asked until it has; with no choices, none is sent
    }
    if (src.bar != nullptr && edit_.kind == EditKind::filter_search && edit_.id == src.bar_key + "/search") {
      continue;  // its search goes when its field closes (proposal 0014 §4), not at each key
    }
    Paged& p = paged_[src.key];
    Json args = page_args(src);
    const std::string signature = args.dump();
    const bool first = signature != p.signature;
    if (!first && !(p.more && !p.next.empty() && !p.loading)) {
      p.more = p.more && p.loading;
      continue;
    }
    p.serial = ++page_serial_;  // never reused, so an answer from before a clear never matches (D254)
    p.loading = true;
    if (first) {
      if (!p.signature.empty()) {
        // Another query (a filter, the sort, the search or the scope): its rows are not these, so none are kept.
        p.body["rows"] = Json::array();
        p.answered = false;
        p.next.clear();
        p.total.reset();
        p.refused.clear();
        p.pages = 0;
      }
      p.signature = signature;
      p.appending = false;
      p.more = false;
      p.fresh = false;
    } else {
      p.appending = true;
      p.more = false;
      args["cursor"] = p.next;
    }
    PageRequest request;
    request.where = "page:" + src.key + "#" + std::to_string(p.serial);
    request.command.space = pages_.space;
    request.command.name = src.source->command;
    request.command.args = std::move(args);
    out.push_back(std::move(request));
  }
  return out;
}

void PageView::page_answer(std::string_view where, const protocol::Result& result) {
  if (where.rfind("page:", 0) != 0) {
    return;
  }
  const std::size_t hash = where.rfind('#');
  if (hash == std::string_view::npos || hash <= 5) {
    return;
  }
  const std::string key(where.substr(5, hash - 5));
  std::uint64_t serial = 0;
  for (const char ch : where.substr(hash + 1)) {
    if (ch < '0' || ch > '9') {
      return;
    }
    serial = serial * 10 + static_cast<std::uint64_t>(ch - '0');
  }
  const auto found = paged_.find(key);
  if (found == paged_.end() || found->second.serial != serial) {
    return;  // an answer to a page asked for before the last change: not drawn
  }
  Paged& p = found->second;
  const std::string was = focused();
  p.loading = false;
  if (result.error.has_value()) {
    p.refused = result.detail && !result.detail->empty() ? *result.detail : std::string("Not now.");
    if (!p.appending) {
      p.body["rows"] = Json::array();
      p.next.clear();
      p.total.reset();
      p.pages = 0;
    }
    p.answered = true;
  } else {
    const Json body = result.body && result.body->is_object() ? *result.body : Json::object();
    Json rows = Json::array();
    if (body.contains("rows") && body["rows"].is_array()) {
      for (const Json& row : body["rows"]) {
        if (row.is_object() && rows.size() < 200) {
          rows.push_back(row);
        }
      }
    }
    if (body.contains("columns") && body["columns"].is_array()) {
      p.body["columns"] = body["columns"];
    }
    if (p.appending) {
      for (Json& row : rows) {
        p.body["rows"].push_back(std::move(row));
      }
      p.pages += 1;
    } else {
      p.body["rows"] = std::move(rows);
      p.pages = 1;
    }
    p.next = body.contains("next") && body["next"].is_string() ? body["next"].get<std::string>() : std::string();
    // A count, whole and below 2^53 (as JSON's numbers keep exactly, and the browser takes it): else no count.
    const double total = body.contains("total") && body["total"].is_number() ? body["total"].get<double>() : -1.0;
    p.total = total >= 0 && total <= 9007199254740991.0 && std::floor(total) == total ? std::optional<double>(total)
                                                                                      : std::nullopt;
    p.refused.clear();
    p.answered = true;
  }
  rebuild();
  for (std::size_t i = 0; scope_ == Scope::page && i < focus_.size(); ++i) {
    if (name_of(Scope::page, i) == was) {
      page_focus_ = i;  // the same element, wherever it now is
      break;
    }
  }
  if (const protocol::PageMaster* m = master(); m != nullptr && m->page && master_name(*m) == key) {
    master_changed();
  }
}

// ---- Input (§5.3, §5.4, §5.7).

// The first control of the page's focus order: past the section labels where there is one (§5.7).
Action PageView::open_index(std::size_t index, Scope scope) {
  release_held();  // what the last page held is applied: the next page draws it as it is (D252)
  quick_open_ = false;
  paged_.clear();  // nothing a page answered is kept past leaving it (D254)
  drawer_open_.clear();  // nor an open row drawer (D256)
  page_ = index;
  tab_ = 0;
  page_focus_ = 0;
  scroll_ = 0;
  confirming_.clear();
  for (auto& [topic, st] : editors_) {
    st.draft.clear();  // a draft lives in the view that asked, while its page is open (proposal 0010 §3.3)
    st.notice.clear();
  }
  rebuild();
  focus_first_control();
  focus_master_on_open();
  settle_expected();  // a save's choice, if its row is listed now (D218)
  rail_focus_ = rail_index_of_page();
  scope_ = scope;
  Action action;
  action.kind = Action::Kind::opened_page;
  action.page = current().id;
  return action;
}

Action PageView::input_body(const Input& in) {
  if (sheet_open_) {
    // The sheet has every key until it closes (G9): Enter, Escape, A or B close it; nothing else does anything.
    if (in.key == Key::activate || in.key == Key::back) {
      close_sheet();
      Action closed;
      closed.kind = Action::Kind::sheet_closed;
      return closed;
    }
    return {};
  }
  if (capture_open()) {
    return capture_input(in);  // the modal has every key until it closes (D209)
  }
  if (editing()) {
    // A pad's buttons while a field edits (§5.4): A keeps, B leaves, the d-pad moves its caret or search's pick.
    switch (in.key) {
      case Key::activate: return edit(EditInput{EditKey::enter});
      case Key::back: return edit(EditInput{EditKey::escape});
      case Key::up: return edit(EditInput{EditKey::up});
      case Key::down: return edit(EditInput{EditKey::down});
      case Key::left: return edit(EditInput{EditKey::left});
      case Key::right: return edit(EditInput{EditKey::right});
      case Key::next: return edit(EditInput{EditKey::tab});
      case Key::previous: return edit(EditInput{EditKey::tab, true});
      default: return {};
    }
  }
  if (quick_open_) {
    return quick_input(in);  // the Quick switches menu has every key until it closes (D252)
  }
  if (in.key == Key::quick) {
    if (has_quick()) {
      confirming_.clear();
      quick_open_ = true;
      quick_focus_ = 0;
    }
    return {};
  }
  if (in.key == Key::search) {
    // `/` or Ctrl+F (§4.3): search's field, from anywhere in the menu.
    confirming_.clear();
    top_focus_ = kTopSearch;
    scope_ = Scope::top;
    start_edit(EditKind::search, "search", search_query_, kSearchMax, false);
    edited();
    return {};
  }
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
    const std::vector<std::size_t> stops = top_stops();
    auto at = std::find(stops.begin(), stops.end(), top_focus_);
    if (at == stops.end()) {
      top_focus_ = kTopSearch;  // a stop the page no longer has (another page, not live)
      at = stops.begin();
    }
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
      case Key::right:
        if (at + 1 != stops.end()) {
          top_focus_ = *(at + 1);  // the top bar's next stop (D252)
        }
        return {};
      case Key::left:
        if (at != stops.begin()) {
          top_focus_ = *(at - 1);
        }
        return {};
      case Key::activate:
        if (top_focus_ == kTopLive) {
          set_live_mode(live_mode_ == LiveMode::live ? LiveMode::every_5s
                        : live_mode_ == LiveMode::every_5s ? LiveMode::paused : LiveMode::live);
          return {};
        }
        if (top_focus_ == kTopQuick) {
          quick_open_ = true;
          quick_focus_ = 0;
          return {};
        }
        if (top_focus_ == kTopScope) {
          // The next of the space's choices, round again after the last (D254): the page's members follow it.
          const std::vector<ScopeChoice> choices = scope_choices();
          const std::string id = scope_id();
          std::size_t at = 0;
          for (std::size_t k = 0; k < choices.size(); ++k) {
            at = choices[k].id == id ? (k + 1) % choices.size() : at;
          }
          if (!choices.empty()) {
            scope_choice_ = choices[at].id;
            confirming_.clear();
            rebuild();
          }
          return {};
        }
        start_edit(EditKind::search, "search", search_query_, kSearchMax, false);
        edited();
        return {};
      default:
        return {};
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
  if (f.kind == FocusKind::editor) {
    bool handled = false;
    Action action = editor_input(f, in, handled);  // the editor's own keys (D217); the rest move focus as anywhere
    if (handled) {
      return action;
    }
  }
  if (const protocol::PageMaster* m = master(); m != nullptr) {
    // Master and detail (D215): ↑ ↓ along the list, → or Enter into the detail; ← or Esc in the detail back to the row.
    std::size_t first = focus_.size();
    std::size_t last = 0;
    std::size_t chosen_at = focus_.size();
    // The list's stops: its rows, and Show more and "New rows · Show" of a list read a page at a time (D254).
    const auto of_list = [&](const Focus& one) { return of_master_list(*m, one); };
    for (std::size_t i = 0; i < focus_.size(); ++i) {
      if (of_list(focus_[i])) {
        first = std::min(first, i);
        last = i;
        if (focus_[i].kind == FocusKind::master_row && focus_[i].id == master_name(*m) + "/" + chosen()) {
          chosen_at = i;
        }
      }
    }
    const bool on_row = f.kind == FocusKind::master_row;
    if (of_list(f) && (in.key == Key::up || in.key == Key::down)) {
      if (in.key == Key::up && page_focus_ > first) {
        --page_focus_;
      } else if (in.key == Key::up && page_focus_ == first && first > 0 &&
                 focus_[first - 1].kind == FocusKind::filter) {
        --page_focus_;  // up from the first row to the list's filter bar (D250)
      } else if (in.key == Key::down && page_focus_ < last) {
        ++page_focus_;
      }
      return {};
    }
    if (on_row && (in.key == Key::right || in.key == Key::activate)) {
      if (last + 1 < focus_.size()) {
        page_focus_ = last + 1;  // the detail's first stop
      }
      return {};
    }
    const bool takes_sideways = f.kind == FocusKind::cell || f.kind == FocusKind::filter ||
                                (s != nullptr && s->type != SettingType::key_name &&
                                 (s->type != SettingType::text || s->choices_from) && s->type != SettingType::secret);
    if (!on_row && chosen_at < focus_.size() &&
        (in.key == Key::back || (in.key == Key::left && !takes_sideways) ||
         ((in.key == Key::up || in.key == Key::previous) && page_focus_ == last + 1))) {
      page_focus_ = chosen_at;  // back to the chosen row, not the rail, nor the list's last row
      return {};
    }
  }
  // Esc or B on a row whose drawer is open closes the drawer first (§5.3's Esc: a drawer, then up a level; D256).
  if (f.kind == FocusKind::drawer && in.key == Key::back) {
    const std::size_t slash = f.id.rfind('/');
    if (const auto open = drawer_open_.find(f.id.substr(0, slash));
        open != drawer_open_.end() && open->second == f.id.substr(slash + 1)) {
      drawer_open_.erase(open);
      return {};
    }
  }
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
      if (f.kind == FocusKind::part) {
        return flip_part(f);  // a switch turns with ← and →, as a setting's does
      }
      if (f.kind == FocusKind::filter && !f.id.ends_with("/search") && !f.id.ends_with("/clear")) {
        return filter_input(f, in.key);  // a filter bar's select or direction (D250)
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
        case FocusKind::master_row:
        case FocusKind::editor:
          return {};  // handled above, with the master's keys (D215) or the editor's (D217)
        case FocusKind::row_action:
          return row_action(f);
        case FocusKind::filter:
          return filter_input(f, Key::activate);
        case FocusKind::link:
          return follow_link(f);
        case FocusKind::part:
          return flip_part(f);
        case FocusKind::share:
          return press_share(f);
        case FocusKind::drawer: {
          // A row's drawer (D256): opened under it, one a table; Enter again closes it.
          const std::size_t slash = f.id.rfind('/');
          const std::string base = f.id.substr(0, slash);
          const std::string key = f.id.substr(slash + 1);
          const auto open = drawer_open_.find(base);
          if (open != drawer_open_.end() && open->second == key) {
            drawer_open_.erase(open);
          } else {
            drawer_open_[base] = key;
          }
          return {};
        }
        case FocusKind::paging: {
          // Show more, or "New rows · Show": the host takes the request at its next look (D254).
          const std::size_t slash = f.id.rfind('/');
          Paged& p = paged_[f.id.substr(0, slash)];
          if (f.id.substr(slash + 1) == "more") {
            p.more = true;
          } else {
            p.signature.clear();
            p.fresh = false;
          }
          return {};
        }
        case FocusKind::section_action:
          return section_action(f);
        case FocusKind::action: {
          const protocol::PageAction& a = (*current().actions)[f.index];
          if (!greyed_words(current().id).empty()) {
            return {};  // a greyed page's actions are inert (D188)
          }
          if (a.state) {
            // A header switch (D256): its command with the state flipped, once the space has published it; nothing
            // changes here until the space publishes the new state (invariant 1).
            const std::optional<bool> on = header_state(a);
            if (!on) {
              return {};
            }
            Action action;
            action.kind = Action::Kind::command;
            action.command.space = pages_.space;
            action.command.name = a.command;
            action.command.args = Json::object({{"on", !*on}});
            return action;
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
          if (((s->type == SettingType::text && !s->choices_from) || s->type == SettingType::secret) && !s->locked &&
              !greyed(*s) && s->availability != "later") {
            // GraceUI's own field (G38 (a)): a text from its value; a secret empty, masked, never shown (invariant 5).
            const bool secret = s->type == SettingType::secret;
            const Json& v = value(s->key);
            start_edit(EditKind::setting, s->key, secret || !v.is_string() ? std::string() : v.get<std::string>(),
                       kSettingTextMax, secret);
            return {};
          }
          if (numeric(s->type) && !in.pad && !s->locked && !greyed(*s) && s->availability != "later") {
            // A slider's typed entry (§3.8, D266), from the keyboard: its value in GraceUI's own field, all of it
            // chosen, so that what is typed replaces it. A pad has its arrows (Steam's keyboard is not built yet).
            const Json& v = value(s->key);
            start_edit(EditKind::setting, s->key, plain_number(*s, v.is_number() ? v.get<double>() : 0.0),
                       kTypedNumberMax, false);
            edit_.field.select_all();
            return {};
          }
          if (s->type == SettingType::key_name && !s->locked && s->availability != "later") {
            capture_ = Capture{};  // a key's setting takes the next key press (D209)
            capture_.kind = CaptureKind::key;
            capture_.command = "settings.set";
            capture_.key_field = "key";
            capture_.key_value = s->key;
            capture_.title = s->label;
            return {};
          }
          return {};
        case FocusKind::cell:
          refusals_.erase(f.id);
          return toggle_cell(f);
        case FocusKind::form_field: {
          if (!greyed_words(current().id).empty() || f.block == nullptr || !f.block->fields) {
            return {};  // a greyed page's form is inert (D188)
          }
          const protocol::FormField& field = (*f.block->fields)[f.index];
          start_edit(EditKind::form_field, f.id, form_value(f.id), field.max_, false, f.block);
          edit_.prose = field.prose.value_or(false);  // several lines, wrapped; ↑ ↓ by line (D251)
          return {};
        }
        case FocusKind::form_submit: {
          if (!greyed_words(current().id).empty() || f.block == nullptr || !f.block->command) {
            return {};
          }
          Json args = Json::object();
          for (const protocol::FormField& field : f.block->fields.value_or(std::vector<protocol::FormField>{})) {
            args[field.id] = form_value(f.id + "/" + field.id);  // every field, an empty one as ""
          }
          refusals_.erase(f.id);
          Action action;
          action.kind = Action::Kind::command;
          action.command.space = pages_.space;
          action.command.name = *f.block->command;
          action.command.args = std::move(args);
          return action;
        }
        case FocusKind::row_secret: {
          if (!greyed_words(current().id).empty() || f.block == nullptr || !f.block->row_secret) {
            return {};
          }
          const Json* table = table_of(*f.block);
          Json key;
          if (table != nullptr && f.index < (*table)["rows"].size()) {
            const Json& row = (*table)["rows"][f.index];
            if (row.is_object() && row.contains(f.block->row_secret->key)) {
              key = row[f.block->row_secret->key];
            }
          }
          start_edit(EditKind::row_secret, f.id, std::string(), kSettingTextMax, true, f.block, std::move(key));
          return {};
        }
        case FocusKind::shortcut:
        case FocusKind::related: {
          const std::string target = f.kind == FocusKind::shortcut ? settings_[f.index].home : f.id;
          for (std::size_t p = 0; p < pages_.pages.size(); ++p) {
            if (pages_.pages[p].id == target && present(target)) {
              Action action = open_index(p, Scope::page);
              if (f.kind == FocusKind::shortcut) {  // the row focused on its home page, on its tab
                focus_setting(f.id);
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
    // Words wrapped to `w`, drawn from (x, y) a line under another; the height they took (D260, D261). A wrapped
    // line keeps the space it broke at; an empty text takes none.
    const auto draw_wrapped = [&c, s](std::string_view words, TypeRole role, float x, float y, float w, Colour ink,
                                      std::uint8_t alpha = 255) {
      const TextField wrapped(words, words.size() + 4);
      const float lh = std::round(line_height(role, s));
      float h = 0;
      for (const TextField::Line& line :
           wrapped.wrap(w, [&c, role](std::string_view run) { return c.width(run, role); })) {
        if (line.end > line.begin) {
          c.text(std::string_view(wrapped.text()).substr(line.begin, line.end - line.begin), role, x, y + h, w + 1, ink,
                 alpha);
        }
        h += lh;
      }
      return words.empty() ? 0.0f : h;
    };
    // ... and how many lines they take, measured alone.
    const auto wrapped_lines = [&c](std::string_view words, TypeRole role, float w) {
      if (words.empty()) {
        return std::size_t{0};
      }
      const TextField wrapped(words, words.size() + 4);
      return wrapped.wrap(w, [&c, role](std::string_view run) { return c.width(run, role); }).size();
    };
    const float W = viewport.width;
    const float H = viewport.height;
    out.clips.push_back(Rect{0, 0, W, H});
    // Where the pointer acts (§5.5): each focusable element, within the clip it is drawn in.
    hits_.clear();
    caret_rect_ = {};
    edit_box_ = {};
    edit_stops_.clear();
    prose_stops_.clear();
    const auto hit = [&](Rect rect, Scope scope, std::size_t index, Rect control = {}, Rect track = {},
                         std::size_t segments = 0, Rect value = {}) {
      const Rect& clip = out.clips[c.clip];
      const float left = std::max(rect.x, clip.x);
      const float top = std::max(rect.y, clip.y);
      const float right = std::min(rect.right(), clip.right());
      const float bottom = std::min(rect.bottom(), clip.bottom());
      if (right > left && bottom > top && index != kNone) {
        hits_.push_back(Hit{Rect{left, top, right - left, bottom - top}, control, track, scope, index, segments, value});
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
    // A text field (§5.6, D57): its well, its edge and its text. The one being edited shows its selection and its caret
    // (steady: nothing loops, invariant 14), its text scrolled so that the caret stays in view; a masked one is drawn
    // as discs, one a character, so that no glyph of a secret is laid out.
    const auto draw_field = [&](const Rect& box, std::string_view shown, Colour ink, bool editing_it, std::uint8_t alpha,
                                TypeRole role) {
      const float r = std::round(tokens::radius::control * s);
      c.fill(box, Colour::ground_well, r, alpha);
      c.stroke(box, editing_it ? Colour::gold : Colour::edge, std::round(tokens::kGrace.size.edge_width * s), r, alpha);
      const float pad = std::round(10 * s);
      const float inner = box.w - std::round(20 * s);  // as a field's text always had it, so that nothing moves
      const float lh = line_height(role, s);
      const float ty = box.y + std::round((box.h - lh) / 2);
      if (!editing_it) {
        c.text(shown, role, box.x + pad, ty, inner, ink, alpha);
        return;
      }
      const TextField& f = edit_.field;
      const std::string& text = f.text();
      const float disc = std::round(12 * s);  // a masked character's advance
      const auto after = [&](std::size_t at) {
        std::size_t next = at + 1;
        while (next < text.size() && !at_boundary(text, next)) {
          ++next;
        }
        return std::min(next, text.size());
      };
      const auto width = [&](std::size_t from, std::size_t to) {
        if (!edit_.masked) {
          return c.width(std::string_view(text).substr(from, to - from), role);
        }
        float w = 0;
        for (std::size_t at = from; at < to; at = after(at)) {
          w += disc;
        }
        return w;
      };
      if (f.caret() < edit_.offset) {
        edit_.offset = f.caret();
      }
      while (edit_.offset < f.caret() && width(edit_.offset, f.caret()) > inner) {
        edit_.offset = after(edit_.offset);
      }
      // The run drawn: from the offset, as far as it fits; each boundary's x kept for the pointer.
      std::size_t end = edit_.offset;
      edit_stops_.emplace_back(edit_.offset, box.x + pad);
      while (end < text.size()) {
        const std::size_t next = after(end);
        const float w = width(edit_.offset, next);
        if (w > inner) {
          break;
        }
        end = next;
        edit_stops_.emplace_back(end, box.x + pad + w);
      }
      const std::size_t a = std::clamp(f.selection_begin(), edit_.offset, end);
      const std::size_t b = std::clamp(f.selection_end(), edit_.offset, end);
      if (b > a) {
        const float xa = box.x + pad + width(edit_.offset, a);
        c.fill(Rect{xa, ty, box.x + pad + width(edit_.offset, b) - xa, lh}, Colour::tint_select);
      }
      if (edit_.masked) {
        const float d = std::round(7 * s);
        float x = box.x + pad;
        for (std::size_t at = edit_.offset; at < end; at = after(at)) {
          c.circle(Rect{x + std::round((disc - d) / 2), ty + std::round((lh - d) / 2), d, d}, Colour::ink_body);
          x += disc;
        }
      } else {
        c.text(std::string_view(text).substr(edit_.offset, end - edit_.offset), role, box.x + pad, ty, inner + 1, ink);
      }
      const float cx = box.x + pad + width(edit_.offset, std::clamp(f.caret(), edit_.offset, end));
      caret_rect_ = Rect{cx, ty, std::max(1.0f, std::round(2 * s)), lh};
      c.fill(caret_rect_, Colour::gold_hi);
      edit_box_ = box;
    };
    // The scrim over the game, then the menu: at most 1.9 × the height wide, centred (§3.6).
    c.fill(Rect{0, 0, W, H}, Colour::scrim);
    const float menu_w = std::min(W, std::round(1.9f * H));
    const float menu_x = std::round((W - menu_w) / 2);
    c.fill(Rect{menu_x, 0, menu_w, H}, Colour::ground_1);

    // ---- The rail.
    const float rail_w = std::round(size::rail * s);
    c.fill(Rect{menu_x, 0, rail_w, H}, Colour::ground_0);
    // A rail row's focus, inset so that its ring, drawn outside it by the ring's offset, falls whole within the rail: at
    // the display's left edge and short of the page (D245).
    const auto rail_ring = [&](Rect row) {
      const float inset = std::round(tokens::kGrace.size.focus_offset * s) + std::max(1.0f, std::round(s));
      return Rect{row.x + inset, row.y, row.w - 2 * inset, row.h};
    };
    const float pad = std::round(size::rail_padding * s);
    float y = pad;
    c.text(context_.title.empty() ? pages_.name : context_.title, TypeRole::display_md, menu_x + pad, y,
           rail_w - 2 * pad, Colour::gold);
    y += std::round(line_height(TypeRole::display_md, s) + 16 * s);
    // The rail's foot, pinned to its bottom under a rule (§4.2.2's foot): the space's own foot categories, with the open
    // one's pages (D248), then GraceUI's links (D79); below the rest where the rail is too short to keep them apart.
    const auto entry_h = [&](const RailEntry& e) {
      const RailLink* other = e.kind == RailEntry::Kind::head   ? &context_.head[e.index]
                              : e.kind == RailEntry::Kind::foot ? &context_.foot[e.index]
                                                                : nullptr;
      const bool is_page = e.own_page() || (other != nullptr && !other->category);
      return std::round((is_page ? size::rail_page : size::rail_category) * s);
    };
    std::size_t first_foot = rail_.size();
    for (std::size_t i = 0; i < rail_.size() && first_foot == rail_.size(); ++i) {
      if (rail_[i].kind == RailEntry::Kind::foot ||
          (rail_[i].category() && pages_.categories[rail_[i].index].foot.value_or(false))) {
        first_foot = i;
      }
    }
    float foot_y = H - pad;
    for (std::size_t i = first_foot; i < rail_.size(); ++i) {
      foot_y -= entry_h(rail_[i]);
    }
    for (std::size_t i = 0; i < rail_.size(); ++i) {
      const RailEntry& e = rail_[i];
      const bool link = e.kind == RailEntry::Kind::head || e.kind == RailEntry::Kind::foot;
      const RailLink* other = e.kind == RailEntry::Kind::head   ? &context_.head[e.index]
                              : e.kind == RailEntry::Kind::foot ? &context_.foot[e.index]
                                                                : nullptr;
      const bool is_page = e.own_page() || (other != nullptr && !other->category);
      const float h = entry_h(e);
      if (i == first_foot) {
        y = std::max(y, foot_y);
        c.line(menu_x + pad, y, rail_w - 2 * pad, Colour::rule);
      }
      const Rect row{menu_x, y, rail_w, h};
      hit(row, Scope::rail, i);
      if ((e.category() || (link && other->category && e.kind == RailEntry::Kind::head)) && i != 0 && i != first_foot &&
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
          c.focused(rail_ring(row));
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
        c.focused(rail_ring(row));
      }
      y += h;
    }

    // ---- The top bar: search at the left, the key that closes at the right.
    const float page_x = menu_x + rail_w;
    const float page_w = menu_w - rail_w;
    const float bar_h = std::round(size::topbar * s);
    const float gutter = std::round(size::gutter * s);
    const Rect search{page_x + gutter, std::round(8 * s), std::round(280 * s), bar_h - std::round(16 * s)};
    hit(search, Scope::top, 0);
    if (edit_.kind == EditKind::search) {
      draw_field(search, search_query_, Colour::ink_hi, true, 255, TypeRole::ui_sm);  // its field (§4.3, G38 (a))
    } else {
      c.fill(search, Colour::ground_well, std::round(tokens::radius::control * s));
      c.text(search_query_.empty() ? std::string_view("Search") : std::string_view(search_query_), TypeRole::ui_sm,
             search.x + std::round(10 * s), search.y + std::round((search.h - line_height(TypeRole::ui_sm, s)) / 2),
             search.w - std::round(20 * s), search_query_.empty() ? Colour::ink_faint : Colour::ink_body);
    }
    if (scope_ == Scope::top && top_focus_ == kTopSearch) {
      c.focused(search);
    }
    const char* close_hint = "F5  Close";
    const float close_w = c.width(close_hint, TypeRole::ui_sm);
    c.text(close_hint, TypeRole::ui_sm, page_x + page_w - gutter - close_w,
           std::round((bar_h - line_height(TypeRole::ui_sm, s)) / 2), close_w + 1, Colour::ink_faint);
    // Live updates, on a live page, and Quick switches, where the space has some (D252): pills at the right, before
    // the key that closes, in the top bar's order.
    Rect quick_pill{};
    {
      float right = page_x + page_w - gutter - close_w - std::round(24 * s);
      const auto pill = [&](std::string_view words, std::size_t stop, bool on) {
        const float w = c.width(words, TypeRole::ui_sm) + std::round(24 * s);
        const Rect box{right - w, search.y, w, search.h};
        c.fill(box, on ? Colour::tint_select : Colour::ground_well, std::round(tokens::radius::control * s));
        c.text(words, TypeRole::ui_sm, box.x + std::round(12 * s),
               box.y + std::round((box.h - line_height(TypeRole::ui_sm, s)) / 2), w, on ? Colour::gold_hi : Colour::ink_body);
        hit(box, Scope::top, stop);
        if (scope_ == Scope::top && top_focus_ == stop) {
          c.focused(box);
        }
        right = box.x - std::round(12 * s);
        return box;
      };
      if (has_quick()) {
        quick_pill = pill("Quick switches", kTopQuick, quick_open_);
      }
      if (current().scoped.value_or(false) && pages_.scope) {
        // The scope's chooser (D254): its label and the choice in view.
        const std::string id = scope_id();
        for (const ScopeChoice& choice : scope_choices()) {
          if (choice.id == id) {
            pill(pages_.scope->label + ": " + choice.label, kTopScope, false);
          }
        }
      }
      if (current().live.value_or(false)) {
        std::string words = live_mode_ == LiveMode::live ? "Live" : live_mode_ == LiveMode::every_5s ? "Every 5 s" : "Paused";
        if (live_mode_ != LiveMode::live && held_new_ > 0) {
          words += " \u00B7 " + std::to_string(held_new_) + " new";
        }
        pill("Updates: " + words, kTopLive, false);
      }
    }
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
    // A prose well from (bx, by) (D214, D251): its words wrapped, at least two lines tall; the one being edited shows its
    // selection and its caret, and keeps each boundary's place for ↑ ↓ and the pointer. The editor's prose fields and a
    // form's prose field share it. Its height.
    const auto prose_well = [&](float bx, float by, float bw, const std::string& text, bool editing_it,
                                const std::string& placeholder, std::uint8_t a, FocusKind kind, const std::string& id) {
      const float rr = std::round(tokens::radius::control * s);
      const float edge_w = std::round(tokens::kGrace.size.edge_width * s);
      const float body_lh = line_height(TypeRole::ui, s);
      const auto measure_in = [&c](TypeRole role) { return [&c, role](std::string_view run) { return c.width(run, role); }; };
      const float pad = std::round(10 * s);
      const float inner = bw - 2 * pad;
      const TextField shown_text = editing_it ? edit_.field : TextField(text, text.size() + 4);
      const std::vector<TextField::Line> lines = shown_text.wrap(inner, measure_in(TypeRole::ui));
      const float h = body_lh * static_cast<float>(std::max<std::size_t>(lines.size(), 2)) + 2 * pad;
      const Rect box{bx, by, bw, h};
      c.fill(box, Colour::ground_well, rr, a);
      c.stroke(box, editing_it ? Colour::gold : Colour::edge, edge_w, rr, a);
      const std::string& t = shown_text.text();
      const auto run_w = [&](std::size_t from, std::size_t to) {
        return c.width(std::string_view(t).substr(from, to - from), TypeRole::ui);
      };
      if (t.empty() && !editing_it) {
        c.text(placeholder, TypeRole::ui, bx + pad, by + pad, inner, Colour::ink_faint, a);
      }
      for (std::size_t i = 0; i < lines.size(); ++i) {
        const TextField::Line& line = lines[i];
        const float ly = by + pad + body_lh * static_cast<float>(i);
        if (editing_it) {
          const std::size_t sa = std::clamp(shown_text.selection_begin(), line.begin, line.end);
          const std::size_t sb = std::clamp(shown_text.selection_end(), line.begin, line.end);
          if (sb > sa) {
            const float xa = bx + pad + run_w(line.begin, sa);
            c.fill(Rect{xa, ly, bx + pad + run_w(line.begin, sb) - xa, body_lh}, Colour::tint_select);
          }
          for (std::size_t at = line.begin; at <= line.end; ++at) {
            if (at < t.size() && !at_boundary(t, at)) {
              continue;
            }
            if (at == line.end && i + 1 < lines.size()) {
              break;  // a line's end is the next line's start
            }
            prose_stops_.emplace_back(at, bx + pad + run_w(line.begin, at), ly);
          }
        }
        c.text(std::string_view(t).substr(line.begin, line.end - line.begin), TypeRole::ui, bx + pad, ly,
               inner + 1, Colour::ink_body, a);
      }
      if (editing_it) {
        const std::size_t li = TextField::line_of(lines, shown_text.caret());
        caret_rect_ = Rect{bx + pad + run_w(lines[li].begin, shown_text.caret()),
                           by + pad + body_lh * static_cast<float>(li), std::max(1.0f, std::round(2 * s)), body_lh};
        c.fill(caret_rect_, Colour::gold_hi);
        edit_box_ = box;
        prose_line_ = body_lh;
      }
      hit(box, Scope::page, focus_index(kind, id), box);
      if (page_focus(kind, id)) {
        c.focused(box);
      }
      return h;
    };
    // A filter bar (G43, D250): its controls left to right from (x, y) within w, wrapping where they do not fit; then,
    // while a filter or the search holds, "12 of 40 shown", or "Nothing matches these filters." and Clear filters.
    // Answers the y below it.
    const auto draw_bar = [&](const Bar& bar, float x, float y, float w) {
      const auto state = filters_.find(bar.key);
      const FilterState none;
      const FilterState& st = state != filters_.end() ? state->second : none;
      const float h = std::round(28 * s);
      const float gap = std::round(8 * s);
      const float r = std::round(tokens::radius::control * s);
      const float edge = std::round(tokens::kGrace.size.edge_width * s);
      float cx = x;
      float cy = y;
      const auto place = [&](float width) {
        if (cx > x && cx + width > x + w) {
          cx = x;
          cy += h + gap;
        }
        const Rect box{cx, cy, width, h};
        cx += width + std::round(16 * s);
        return box;
      };
      const auto mid = [&](const Rect& box, TypeRole role) {
        return box.y + std::round((box.h - line_height(role, s)) / 2);
      };
      // A select: its label, then its value in a well between ‹ and ›.
      const auto select = [&](const std::string& part, std::string_view label, std::string_view value) {
        const std::string id = bar.key + "/" + part;
        const float lw = c.width(label, TypeRole::ui_sm);
        const float vw = std::max(c.width(value, TypeRole::ui_sm) + std::round(44 * s), std::round(96 * s));
        const Rect all = place(lw + gap + vw);
        c.text(label, TypeRole::ui_sm, all.x, mid(all, TypeRole::ui_sm), lw + 1, Colour::ink_mute);
        const Rect box{all.x + lw + gap, all.y, vw, h};
        c.fill(box, Colour::ground_well, r);
        c.stroke(box, Colour::edge, edge, r);
        c.text("‹", TypeRole::ui_sm, box.x + std::round(8 * s), mid(box, TypeRole::ui_sm), box.w, Colour::ink_faint);
        c.text(value, TypeRole::ui_sm, box.x + std::round(22 * s), mid(box, TypeRole::ui_sm), box.w - std::round(44 * s),
               Colour::ink_hi);
        c.text("›", TypeRole::ui_sm, box.right() - std::round(16 * s), mid(box, TypeRole::ui_sm), box.w,
               Colour::ink_faint);
        hit(box, Scope::page, focus_index(FocusKind::filter, id));
        if (page_focus(FocusKind::filter, id)) {
          c.focused(box);
        }
      };
      const std::size_t filters = bar.bar->filters ? bar.bar->filters->size() : 0;
      for (std::size_t i = 0; i < filters; ++i) {
        const bool chosen_one = i < st.chosen.size() && st.chosen[i].has_value();
        std::string shown = chosen_one ? *st.chosen[i] : std::string("All");
        if (chosen_one) {
          for (const auto& [value, words] : bar_choices(bar, i)) {
            shown = value == *st.chosen[i] ? words : shown;  // a choice's own words (D254)
          }
        }
        select("f" + std::to_string(i), (*bar.bar->filters)[i].label, shown);
      }
      if (bar.bar->sort && !bar.bar->sort->empty()) {
        const std::size_t at = std::min(st.sort, bar.bar->sort->size() - 1);
        select("sort", "Sort", (*bar.bar->sort)[at].label);
        // Its direction: a square button with an arrow up (ascending) or down (descending).
        const std::string id = bar.key + "/dir";
        const Rect box = place(h);
        c.stroke(box, Colour::edge, edge, r);
        const bool down = descending(*bar.bar, st);
        Rect a = arrow_box(down ? Facing::down : Facing::up, s);
        a.x = box.x + std::round((box.w - a.w) / 2);
        a.y = box.y + std::round((box.h - a.h) / 2);
        arrow(c, a, down ? Facing::down : Facing::up, Colour::ink_hi);
        hit(box, Scope::page, focus_index(FocusKind::filter, id));
        if (page_focus(FocusKind::filter, id)) {
          c.focused(box);
        }
      }
      if (bar.bar->search) {
        const std::string id = bar.key + "/search";
        const Rect box = place(std::min(w, std::round(220 * s)));
        const bool editing_it = edit_.kind == EditKind::filter_search && edit_.id == id;
        if (editing_it) {
          draw_field(box, st.search, Colour::ink_hi, true, 255, TypeRole::ui_sm);
        } else {
          c.fill(box, Colour::ground_well, r);
          c.stroke(box, Colour::edge, edge, r);
          c.text(st.search.empty() ? std::string_view(bar.bar->search->label) : std::string_view(st.search),
                 TypeRole::ui_sm, box.x + std::round(10 * s), mid(box, TypeRole::ui_sm), box.w - std::round(20 * s),
                 st.search.empty() ? Colour::ink_faint : Colour::ink_body);
        }
        hit(box, Scope::page, focus_index(FocusKind::filter, id));
        if (page_focus(FocusKind::filter, id)) {
          c.focused(box);
        }
      }
      cy += h + gap;
      if (narrowed(*bar.bar, st)) {
        std::size_t total = 0;
        for (const Json& row : *bar.rows) {
          total += row.is_object() ? 1 : 0;
        }
        const std::size_t shown = visible_rows(*bar.rows, *bar.bar, st).size();
        if (shown > 0) {
          c.text(std::to_string(shown) + " of " + std::to_string(total) + " shown", TypeRole::ui_sm, x, cy, w,
                 Colour::ink_mute);
          cy += std::round(line_height(TypeRole::ui_sm, s) + 4 * s);
        } else {
          const std::string words = "Nothing matches these filters.";
          c.text(words, TypeRole::ui_sm, x, cy + std::round((h - line_height(TypeRole::ui_sm, s)) / 2), w,
                 Colour::ink_mute);
          const std::string id = bar.key + "/clear";
          const std::string label = "Clear filters";
          const float bw = c.width(label, TypeRole::ui_sm) + std::round(24 * s);
          const Rect button{x + c.width(words, TypeRole::ui_sm) + std::round(12 * s), cy, bw, h};
          c.stroke(button, Colour::edge, edge, r);
          c.text(label, TypeRole::ui_sm, button.x + std::round(12 * s), mid(button, TypeRole::ui_sm), bw,
                 Colour::ink_hi);
          hit(button, Scope::page, focus_index(FocusKind::filter, id));
          if (page_focus(FocusKind::filter, id)) {
            c.focused(button);
          }
          cy += h + gap;
        }
      }
      return cy;
    };
    // Rows read a page at a time (D254): at the top, "New rows · Show" when new rows came past the first page; at the end,
    // "Loading…" while a page is asked for, Show more while the space has a next page, and how many in all if it said.
    // Each answers the rows' own height; a focus stop is the stop the view keeps under the same name.
    const auto draw_paging = [&](const Paged& p, const std::string& key, bool top, float x, float y, float w) {
      const float lh = line_height(TypeRole::ui_sm, s);
      const float h = std::round(32 * s);
      const auto stop = [&](const std::string& words, const std::string& id) {
        const float bw = c.width(words, TypeRole::ui_sm) + std::round(28 * s);
        const Rect box{x, y + std::round(6 * s), bw, h - std::round(8 * s)};
        c.fill(box, hovered(box) ? Colour::tint_hover : Colour::ground_3, std::round(tokens::radius::control * s));
        c.text(words, TypeRole::ui_sm, box.x + std::round(14 * s), box.y + std::round((box.h - lh) / 2), bw,
               Colour::ink_hi);
        hit(box, Scope::page, focus_index(FocusKind::paging, id));
        if (page_focus(FocusKind::paging, id)) {
          c.focused(box);
        }
        return box.right();
      };
      if (top) {
        if (!p.fresh) {
          return 0.0f;
        }
        stop("New rows \u00B7 Show", key + "/fresh");
        return h + std::round(4 * s);
      }
      float right = x;
      if (p.loading && p.appending) {
        c.text("Loading\u2026", TypeRole::ui_sm, x, y + std::round((h - lh) / 2), w, Colour::ink_mute);
        right = x + c.width("Loading\u2026", TypeRole::ui_sm);
      } else if (!p.next.empty() && !p.loading) {
        right = stop("Show more", key + "/more");
      }
      if (!p.refused.empty() && !p.body["rows"].empty()) {
        // A refused Show more: the space's words beside it, the rows kept above.
        const float gap = right > x ? std::round(16 * s) : 0.0f;
        c.text(p.refused, TypeRole::ui_sm, right + gap, y + std::round((h - lh) / 2),
               std::max(0.0f, x + w - right - gap), Colour::ink_body);
        right += gap + c.width(p.refused, TypeRole::ui_sm);
      }
      if (p.total) {
        std::string digits = std::to_string(static_cast<long long>(*p.total));
        for (std::size_t at = digits.size(); at > 3; at -= 3) {
          digits.insert(at - 3, ",");  // "1,234 in all", as the browser writes it
        }
        const std::string all = digits + " in all";
        c.text(all, TypeRole::ui_sm, right + std::round(16 * s), y + std::round((h - lh) / 2),
               std::max(0.0f, x + w - right - std::round(16 * s)), Colour::ink_mute);
      }
      return right > x || p.total ? h : 0.0f;
    };
    // The header: title, description, actions, the lozenge rule.
    c.text(page.name, TypeRole::display_lg, x0, y, content_w / 2, Colour::ink_hi);
    float action_x = x0 + content_w;
    for (std::size_t a = page.actions ? page.actions->size() : 0; a-- > 0;) {
      const protocol::PageAction& act = (*page.actions)[a];
      if (act.state) {
        // A header switch (D256): its label and a switch showing the state the space publishes, or "Not known yet".
        const std::optional<bool> on = header_state(act);
        const std::string words = on ? act.label : act.label + "  \u00B7  Not known yet";
        const float knob_w = on ? std::round(40 * s) + std::round(12 * s) : 0.0f;
        const float w = c.width(words, TypeRole::ui) + std::round(32 * s) + knob_w;
        const Rect button{action_x - w, y + std::round(2 * s), w, std::round(32 * s)};
        const float r = std::round(tokens::radius::control * s);
        hit(button, Scope::page, focus_index(FocusKind::action, act.id));
        if (hovered(button)) {
          c.fill(button, Colour::tint_hover, r);
        }
        c.stroke(button, Colour::edge, std::round(tokens::kGrace.size.edge_width * s), r);
        c.text(words, TypeRole::ui, button.x + std::round(16 * s),
               button.y + std::round((button.h - line_height(TypeRole::ui, s)) / 2), w, on ? Colour::ink_hi : Colour::ink_mute);
        if (on) {
          const Rect control{button.right() - std::round(16 * s) - std::round(40 * s),
                             button.y + std::round((button.h - 22 * s) / 2), std::round(40 * s), std::round(22 * s)};
          if (*on) {
            c.fill(control, Colour::gold, control.h / 2);
          } else {
            c.fill(control, Colour::ground_well, control.h / 2);
            c.stroke(control, Colour::edge, std::round(tokens::kGrace.size.edge_width * s), control.h / 2);
          }
          const float knob = std::round(16 * s);
          c.circle(Rect{*on ? control.right() - knob - std::round(3 * s) : control.x + std::round(3 * s),
                        control.y + std::round((control.h - knob) / 2), knob, knob},
                   *on ? Colour::on_gold : Colour::ink_mute);
        }
        if (page_focus(FocusKind::action, act.id)) {
          c.focused(button);
        }
        action_x = button.x - std::round(12 * s);
        continue;
      }
      const bool asking = confirming_ == act.id;
      const std::string label = running("action:" + act.id) ? "Working…" : asking ? "Press again to confirm" : act.label;
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
    // Its description, wrapped to the page's width (D261).
    y += std::max(draw_wrapped(page.description, TypeRole::ui_sm, x0, y, content_w, Colour::ink_mute),
                  std::round(line_height(TypeRole::ui_sm, s)));
    y += std::round(12 * s);
    c.line(x0, y, content_w, Colour::rule_strong);
    c.diamond(Rect{x0, y - std::round(4 * s), std::round(9 * s), std::round(9 * s)}, Colour::gold_deep);
    y += std::round(20 * s);
    // Its ribbon (D258): the space's words in a band at the top, while the page is open.
    if (page.ribbon && !page.ribbon->empty()) {
      const Rect band{x0, y, content_w, std::round(32 * s)};
      c.fill(band, Colour::rot_fill, std::round(tokens::radius::control * s));
      c.text(*page.ribbon, TypeRole::ui_sm, band.x + std::round(16 * s),
             band.y + std::round((band.h - line_height(TypeRole::ui_sm, s)) / 2), band.w - std::round(32 * s), Colour::ink_hi);
      y += band.h + std::round(12 * s);
    }
    // A page its space has greyed (D188): the space's words over it, its controls inert and faint.
    if (const std::string words = greyed_words(page.id); !words.empty()) {
      const Rect band{x0, y, content_w, std::round(44 * s)};
      c.fill(band, Colour::ground_3, std::round(tokens::radius::control * s));
      c.fill(Rect{band.x, band.y, std::round(3 * s), band.h}, Colour::ember);
      c.text(words, TypeRole::ui, band.x + std::round(16 * s), band.y + std::round((band.h - line_height(TypeRole::ui, s)) / 2),
             band.w - std::round(32 * s), Colour::ink_hi);
      y += band.h + std::round(16 * s);
    }
    // Master and detail (D215, D36): the list at the left, in a card of its own; the page's tabs and sections at the
    // right, as the chosen row's detail.
    const protocol::PageMaster* m = master();
    const bool show_detail = m == nullptr || !chosen().empty();
    const float detail_top = y;
    float detail_x = x0;
    float detail_w = content_w;
    float master_bottom = y;
    if (m != nullptr) {
      const float mw = std::round(std::min(content_w * 0.34f, 440 * s));
      const float gap = std::round(24 * s);
      const float mpad = std::round(20 * s);
      detail_x = x0 + mw + gap;
      detail_w = content_w - mw - gap;
      const std::size_t card_at = out.items.size();
      float my = y + std::round(16 * s);
      c.text(m->label, TypeRole::eyebrow, x0 + mpad, my, mw - 2 * mpad, Colour::gold);
      my += std::round(line_height(TypeRole::eyebrow, s) + 8 * s);
      const Bar master_bar = bar_of_master();  // its filter bar (D250), above its rows
      if (master_bar) {
        my = draw_bar(master_bar, x0 + mpad, my, mw - 2 * mpad);
      }
      const Paged* paged = m->page ? paged_of(master_name(*m)) : nullptr;  // rows read a page at a time (D254)
      if (paged != nullptr) {
        my += draw_paging(*paged, master_name(*m), true, x0 + mpad, my, mw - 2 * mpad);
      }
      const auto rows = master_rows();
      std::string first_column;
      std::string second_column;
      if (const Json* body = master_body(); body != nullptr && body->is_object() && body->contains("columns") &&
                                            (*body)["columns"].is_array()) {
        for (const Json& column : (*body)["columns"]) {
          if (column.is_object() && column.contains("id") && column["id"].is_string()) {
            (first_column.empty() ? first_column : second_column) = column["id"].get<std::string>();
            if (!second_column.empty()) {
              break;
            }
          }
        }
      }
      const float row_hm = std::round(14 * s + line_height(TypeRole::ui, s) + line_height(TypeRole::ui_sm, s));
      for (const auto& [key, row] : rows) {
        const std::string id = master_name(*m) + "/" + key;
        const Rect r{x0 + std::round(8 * s), my, mw - std::round(16 * s), row_hm};
        const bool chosen_row = key == chosen();
        if (chosen_row) {
          c.fill(r, Colour::tint_select);
          c.fill(Rect{r.x, r.y, std::round(2 * s), r.h}, Colour::gold);
        } else if (hovered(r)) {
          c.fill(r, Colour::tint_hover);
        }
        const std::string label = first_column.empty() ? key : cell_words(*row, first_column);
        c.text(label, TypeRole::ui, r.x + std::round(12 * s), r.y + std::round(7 * s), r.w - std::round(24 * s),
               chosen_row ? Colour::gold_hi : Colour::ink_hi);
        if (!second_column.empty()) {
          c.text(cell_words(*row, second_column), TypeRole::ui_sm, r.x + std::round(12 * s),
                 r.y + std::round(7 * s) + line_height(TypeRole::ui, s), r.w - std::round(24 * s), Colour::ink_mute);
        }
        hit(r, Scope::page, focus_index(FocusKind::master_row, id));
        if (page_focus(FocusKind::master_row, id)) {
          c.focused(r);
        }
        my += row_hm;
      }
      if (paged != nullptr && !rows.empty()) {
        my += draw_paging(*paged, master_name(*m), false, x0 + mpad, my, mw - 2 * mpad);  // Show more (D254)
      }
      if (rows.empty() && (!master_bar || m->page)) {
        // Its own words once its topic has spoken (D186), nothing else until then (invariant 2); a list read a page at
        // a time says "Loading…" while it asks, or the space's words for a refusal (D254).
        const bool published = master_body() != nullptr;
        const bool refused = paged != nullptr && !paged->refused.empty();
        const std::string words = refused ? paged->refused
                                  : m->page && (paged == nullptr || paged->loading || !paged->answered)
                                      ? std::string("Loading\u2026")
                                  : published && m->empty ? *m->empty
                                                          : std::string("Nothing to show.");
        c.text(words, TypeRole::ui_sm, x0 + mpad, my + std::round(4 * s), mw - 2 * mpad,
               refused ? Colour::ink_body : Colour::ink_mute);
        my += std::round(line_height(TypeRole::ui_sm, s) + 12 * s);
      }
      my += std::round(12 * s);
      Item ground;
      ground.shape = Shape::fill;
      ground.colour = Colour::ground_2;
      ground.clip = 1;
      ground.rect = Rect{x0, y, mw, my - y};
      ground.radius = std::round(tokens::radius::card * s);
      Item border = ground;
      border.shape = Shape::stroke;
      border.colour = Colour::rule;
      border.width = std::max(1.0f, std::round(s));
      out.items.insert(out.items.begin() + static_cast<std::ptrdiff_t>(card_at), {ground, border});
      master_bottom = my + std::round(16 * s);
    }
    {
    // The detail's column (D215): on a page with a master list, every block below is laid out in it.
    const float x0 = detail_x;
    const float content_w = detail_w;
    y = detail_top;
    if (!show_detail) {
      // No row chosen yet: the list's own words for it, in a card where the detail goes.
      const Rect card{x0, y, content_w, std::round(72 * s)};
      c.fill(card, Colour::ground_2, std::round(tokens::radius::card * s));
      c.stroke(card, Colour::rule, std::max(1.0f, std::round(s)), std::round(tokens::radius::card * s));
      c.text(m->none ? *m->none : std::string("Choose one in the list."), TypeRole::ui, x0 + std::round(20 * s),
             y + std::round((card.h - line_height(TypeRole::ui, s)) / 2), content_w - std::round(40 * s),
             Colour::ink_mute);
      y += card.h + std::round(16 * s);
    }
    // Tabs.
    if (show_detail && page.tabs && !page.tabs->empty()) {
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
    for (const protocol::PageSection& section : show_detail ? sections() : no_sections_) {
      const float card_top = y;
      const std::size_t card_at = out.items.size();  // the card's ground goes under its rows, inserted below
      y += std::round(16 * s);
      const bool collapsed = collapsed_.count(page.id + "/" + section.id) != 0;
      // Its head: the title and its chip at the left; its own actions, its live status and the collapse mark at the
      // right (D202).
      const std::size_t own_actions = section.actions ? section.actions->size() : 0;
      const float eyebrow_h = line_height(TypeRole::eyebrow, s);
      const float head_h = own_actions > 0 ? std::max(eyebrow_h, std::round(30 * s)) : eyebrow_h;
      const float title_y = y + std::round((head_h - eyebrow_h) / 2);
      const float small_y = y + std::round((head_h - line_height(TypeRole::ui_sm, s)) / 2);
      const float title_w = c.text(section.title, TypeRole::eyebrow, x0 + card_pad, title_y,
                                   content_w - 2 * card_pad - std::round(20 * s), Colour::gold);
      if (section.chip) {
        c.text(*section.chip, TypeRole::ui_sm, x0 + card_pad + title_w + std::round(12 * s), small_y,
               content_w / 3, Colour::ink_faint);
      }
      // A head with buttons centres its words on them, and rings its title alone; any other is drawn as before.
      c.text(collapsed ? "+" : "-", TypeRole::ui_sm, x0 + content_w - card_pad - std::round(10 * s),
             own_actions > 0 ? small_y : y, std::round(12 * s), Colour::ink_faint);
      hit(Rect{x0 + card_pad, y, content_w - 2 * card_pad, head_h}, Scope::page, focus_index(FocusKind::section, section.id));
      if (page_focus(FocusKind::section, section.id)) {
        c.focused(own_actions > 0 ? Rect{x0 + card_pad, title_y, title_w + std::round(8 * s), eyebrow_h}
                                  : Rect{x0 + card_pad, y, content_w - 2 * card_pad, eyebrow_h});
      }
      float right = x0 + content_w - card_pad - std::round(28 * s);
      if (const auto [word, tone] = section_status(section); !word.empty()) {
        const float ww = c.width(word, TypeRole::ui_sm);
        c.text(word, TypeRole::ui_sm, right - ww, small_y, ww + 1, Colour::ink_body);
        const float dot = std::round(8 * s);
        const Colour ink = tone == "moss" ? Colour::moss : tone == "ember" ? Colour::ember : tone == "rot" ? Colour::rot
                                                                                                            : Colour::glint;
        c.circle(Rect{right - ww - std::round(8 * s) - dot, y + std::round((head_h - dot) / 2), dot, dot}, ink);
        right -= ww + dot + std::round(28 * s);
      }
      std::string said;
      for (std::size_t a = own_actions; a-- > 0;) {
        const protocol::PageAction& act = (*section.actions)[a];
        const std::string id = section.id + "/" + act.id;
        const bool asking = confirming_ == "section:" + id;
        const std::string words = running("section-action:" + id) ? "Working…"
                                  : asking                        ? "Press again to confirm"
                                                                  : act.label;
        const float w = c.width(words, TypeRole::ui_sm) + std::round(24 * s);
        const Rect button{right - w, y + std::round((head_h - 28 * s) / 2), w, std::round(28 * s)};
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
        hit(button, Scope::page, focus_index(FocusKind::section_action, id));
        if (page_focus(FocusKind::section_action, id)) {
          c.focused(button);
        }
        if (const auto refusal = refusals_.find(id); refusal != refusals_.end()) {
          said = refusal->second;
        }
        right = button.x - std::round(12 * s);
      }
      y += std::round(head_h + 4 * s);
      if (!said.empty()) {  // the space's reason, under the head (§5.7)
        c.text(said, TypeRole::ui_sm, x0 + card_pad, y, content_w - 2 * card_pad, Colour::rot);
        y += std::round(line_height(TypeRole::ui_sm, s));
      }
      if (section.description) {
        // Wrapped to the card (D260), as D251's preview fields are.
        y += draw_wrapped(*section.description, TypeRole::ui_sm, x0 + card_pad, y, content_w - 2 * card_pad,
                          Colour::ink_mute);
      }
      y += std::round(8 * s);
      const auto draw_blocks = [&](const std::vector<protocol::PageBlock>& blocks, bool advanced) {
        for (std::size_t b = 0; b < blocks.size(); ++b) {
          const protocol::PageBlock& block = blocks[b];
          if (block.kind == protocol::BlockKind::editor && block.editor) {
            // An editor (D217): its state and buttons, the last answer's words for the whole file, the draft's notes and
            // button, then each field: its label, count and help, its control, its reference, its messages, and a
            // draft's proposal with Use this and Keep mine.
            const std::string eid = form_id(section, b, advanced);
            const float ix = x0 + card_pad;
            const float iw = content_w - 2 * card_pad;
            const std::uint8_t alpha = greyed_words(page.id).empty() ? 255 : 128;
            const std::string topic = topic_of(block);
            const auto body = topics_.find(topic);
            EditorDocument doc;
            if (topic.empty() || body == topics_.end() || !EditorDocument::parse(body->second, doc)) {
              c.text(empty_words(block, body != topics_.end()), TypeRole::ui_sm, ix, y + std::round(12 * s), iw,
                     Colour::ink_mute);
              y += row_h;
              continue;
            }
            const EditorState fresh;
            const EditorState& st = editor_state(topic) != nullptr ? *editor_state(topic) : fresh;
            const protocol::EditorBlock& ed = *block.editor;
            const float small = line_height(TypeRole::ui_sm, s);
            const float body_lh = line_height(TypeRole::ui, s);
            const float gap = std::round(10 * s);
            const float button_h = std::round(30 * s);
            const float rr = std::round(tokens::radius::control * s);
            const float edge_w = std::round(tokens::kGrace.size.edge_width * s);
            const auto measure_in = [&c](TypeRole role) { return [&c, role](std::string_view run) { return c.width(run, role); }; };
            // Words wrapped to a width from y, drawn; the height they took.
            const auto para = [&](std::string_view words, TypeRole role, float x, float w, Colour ink) {
              const TextField wrapped(words, words.size() + 4);
              const float lh = line_height(role, s);
              float h = 0;
              for (const TextField::Line& line : wrapped.wrap(w, measure_in(role))) {
                c.text(std::string_view(wrapped.text()).substr(line.begin, line.end - line.begin), role, x, y + h, w + 1,
                       ink, alpha);
                h += lh;
              }
              return h;
            };
            const auto name_for = [&](const std::string& rest) { return "editor:" + eid + "/" + rest; };
            const auto words_for = [&](const std::string& rest, const std::string& words) {
              const std::string name = name_for(rest);
              return running(name) ? std::string("Working…") : confirming_ == name ? std::string("Press again to confirm")
                                                                                   : words;
            };
            // A button at (bx, by): plain, primary (1) or destructive (2), greyed if off; its width.
            const auto button = [&](float bx, float by, const std::string& words, int tone, const std::string& rest,
                                    bool off) {
              const std::string id = eid + "/" + rest;
              const float w = c.width(words, TypeRole::ui_sm) + std::round(24 * s);
              const Rect r{bx, by, w, button_h};
              const std::uint8_t a = off ? 128 : alpha;
              Colour ink = Colour::ink_hi;
              if (tone == 1) {
                c.fill(r, Colour::gold, rr, a);
                ink = Colour::on_gold;
              } else if (tone == 2) {
                c.fill(r, Colour::rot_fill, rr, a);
              } else {
                c.stroke(r, Colour::edge, edge_w, rr, a);
              }
              c.text(words, TypeRole::ui_sm, r.x + std::round(12 * s), r.y + std::round((r.h - small) / 2), w, ink, a);
              hit(r, Scope::page, focus_index(FocusKind::editor, id));
              if (page_focus(FocusKind::editor, id)) {
                c.focused(r);
              }
              return w;
            };
            const auto choice_label = [&](const EditorField& field, const std::string& value) {
              const std::vector<protocol::Choice> list =
                  !field.choices.empty() ? field.choices : choices_in(topics_, field.choices_from);
              for (const protocol::Choice& choice : list) {
                if (choice.value == value) {
                  return choice.label.empty() ? choice.value : choice.label;
                }
              }
              return value;
            };
            const auto value_words = [&](const EditorField& field, const Json& v) -> std::string {
              if (v.is_null()) {
                return "Not set here";
              }
              if (v.is_string()) {
                return field.kind == FieldKind::choice || field.kind == FieldKind::pick
                           ? choice_label(field, v.get<std::string>())
                           : v.get<std::string>();
              }
              if (v.is_boolean()) {
                return v.get<bool>() ? "On" : "Off";
              }
              if (v.is_number()) {
                return std::to_string(std::llround(v.get<double>()));
              }
              std::string out;
              for (const Json& item : v.is_array() ? v : Json::array()) {
                const ListItem li = list_item(item);
                out += (out.empty() ? "" : "; ") + (li.choice.empty() ? li.text : choice_label(field, li.choice) + ": " + li.text);
              }
              return out;
            };
            // A prose well from y: its words wrapped, at least two lines tall; the one being edited shows its selection
            // and its caret, and keeps each boundary's place for ↑ ↓ and the pointer. Its height.
            const auto prose_box = [&](float bx, float bw, const std::string& text, bool editing_it,
                                       const std::string& placeholder, std::uint8_t a, const std::string& id) {
              return prose_well(bx, y, bw, text, editing_it, placeholder, a, FocusKind::editor, id);
            };
            const auto message_lines = [&](const std::string& field, std::optional<std::size_t> item, float x, float w) {
              for (const EditorMessage& m : st.messages) {
                if (m.field == field && m.item == item) {
                  y += para((m.error ? "Error: " : "Warning: ") + m.words, TypeRole::ui_sm, x, w,
                            m.error ? Colour::rot : Colour::ember);
                }
              }
            };
            // A conflict: the band and its Reload.
            if (st.conflict) {
              const std::string words = words_for("reload", "Reload");
              const float bw = c.width(words, TypeRole::ui_sm) + std::round(24 * s);
              const Rect band{ix, y, iw, button_h + std::round(14 * s)};
              c.fill(band, Colour::rot_fill, rr);
              c.text(st.top.empty() ? std::string("Changed elsewhere since you opened it.") : st.top, TypeRole::ui,
                     band.x + std::round(12 * s), band.y + std::round((band.h - body_lh) / 2),
                     band.w - bw - std::round(36 * s), Colour::ink_hi);
              button(band.right() - bw - std::round(7 * s), band.y + std::round(7 * s), words, 0, "reload", false);
              y += band.h + gap;
            }
            // Its state, and whether it holds unsaved changes.
            {
              float sx = ix;
              if (!doc.state_word.empty()) {
                const float dot = std::round(8 * s);
                const Colour ink = doc.state_tone == "moss"    ? Colour::moss
                                   : doc.state_tone == "ember" ? Colour::ember
                                   : doc.state_tone == "rot"   ? Colour::rot
                                                               : Colour::glint;
                c.circle(Rect{sx, y + std::round((small - dot) / 2), dot, dot}, ink, alpha);
                sx += dot + std::round(8 * s);
                sx += c.text(doc.state_word, TypeRole::ui_sm, sx, y, iw / 3, Colour::ink_body, alpha) + std::round(12 * s);
              }
              const char* unsaved = "Unsaved changes";
              const float uw = st.dirty() ? c.width(unsaved, TypeRole::ui_sm) : 0;
              if (!doc.state_line.empty()) {
                c.text(doc.state_line, TypeRole::ui_sm, sx, y, std::max(0.0f, ix + iw - sx - uw - std::round(16 * s)),
                       Colour::ink_mute, alpha);
              }
              if (st.dirty()) {
                c.text(unsaved, TypeRole::ui_sm, ix + iw - uw, y, uw + 1, Colour::ember, alpha);
              }
              y += small + gap;
            }
            // Its buttons.
            {
              float bx = ix;
              bx += button(bx, y, words_for("save", "Save"), 1, "save", false) + gap;
              if (ed.check) {
                bx += button(bx, y, words_for("check", "Check"), 0, "check", false) + gap;
              }
              if (ed.restore) {
                bx += button(bx, y, words_for("restore", "Restore the previous version"), 0, "restore", false) + gap;
              }
              if (ed.reset) {
                button(bx, y, words_for("reset", "Reset all"), 2, "reset", false);
              }
              y += button_h + gap;
            }
            // The last answer's words, and its messages about the whole file or about a field the document lacks.
            const bool errors = std::any_of(st.messages.begin(), st.messages.end(), [](const EditorMessage& m) { return m.error; });
            if (!st.conflict && !st.top.empty()) {
              y += para(st.top, TypeRole::ui_sm, ix, iw, errors ? Colour::rot : Colour::ink_body);
            }
            for (const EditorMessage& m : st.messages) {
              if (m.field.empty() || doc.field(m.field) == nullptr) {
                y += para((m.error ? "Error: " : "Warning: ") + m.words, TypeRole::ui_sm, ix, iw,
                          m.error ? Colour::rot : Colour::ember);
              }
            }
            // The draft: its notes, its button and why it is not available now, Use all, and a notice.
            if (ed.draft) {
              y += std::round(4 * s);
              c.line(ix, y, iw, Colour::rule);
              y += gap;
              const std::size_t cap = ed.notes_max.value_or(400);
              const bool editing_notes = edit_.kind == EditKind::editor_notes && edit_.id == eid + "/notes";
              const std::string& notes = editing_notes ? edit_.field.text() : st.notes;
              c.text(ed.notes_label.value_or("Notes for the draft"), TypeRole::ui, ix, y, iw * 2 / 3, Colour::ink_hi, alpha);
              const std::string count = std::to_string(count_chars(notes)) + " / " + std::to_string(cap);
              const float cw = c.width(count, TypeRole::ui_sm);
              c.text(count, TypeRole::ui_sm, ix + iw - cw, y, cw + 1, Colour::ink_faint, alpha);
              y += body_lh + std::round(6 * s);
              y += prose_box(ix, iw, notes, editing_notes, "", alpha, eid + "/notes") + gap;
              const bool blocked = !doc.draft_blocked.empty();
              const float dw = button(ix, y, words_for("draft", "Draft"), 0, "draft", blocked);
              const std::string& why = blocked ? doc.draft_blocked : doc.draft_help;
              float right = ix + iw;
              if (!st.draft.empty()) {
                const std::string words = "Use all";
                const float uw = c.width(words, TypeRole::ui_sm) + std::round(24 * s);
                right -= uw;
                button(right, y, words, 0, "use_all", false);
                right -= gap;
              }
              if (!why.empty()) {
                c.text(why, TypeRole::ui_sm, ix + dw + gap, y + std::round((button_h - small) / 2),
                       std::max(0.0f, right - ix - dw - 2 * gap), blocked ? Colour::ink_body : Colour::ink_mute, alpha);
              }
              y += button_h + gap;
              if (!st.notice.empty()) {
                y += para(st.notice, TypeRole::ui_sm, ix, iw, Colour::ember);
              }
            }
            // The fields.
            const float well_h = std::round(32 * s);
            for (const EditorField& field : doc.fields) {
              y += std::round(4 * s);
              c.line(ix, y, iw, Colour::rule);
              y += gap;
              const Json& v = st.value(field);
              const bool locked = !field.locked.empty();
              const std::uint8_t a = locked ? 128 : alpha;
              const std::string fid = eid + "/f/" + field.id;
              const bool editing_it = edit_.kind == EditKind::editor_text && edit_.id == fid;
              // Its label, "Unsaved" if the player changed it, and its count at the right.
              const float lw = c.text(field.label, TypeRole::ui, ix, y, iw * 2 / 3, Colour::ink_hi, alpha);
              if (st.edits.count(field.id) != 0) {
                c.text("Unsaved", TypeRole::ui_sm, ix + lw + std::round(10 * s), y + std::round((body_lh - small) / 2),
                       iw / 4, Colour::ember, alpha);
              }
              std::string count;
              bool over = false;
              if (field.kind == FieldKind::line || field.kind == FieldKind::prose) {
                const std::string text = editing_it ? edit_.field.text() : v.is_string() ? v.get<std::string>() : "";
                if (field.max_words != 0) {
                  const std::size_t n = count_words(text);
                  count = std::to_string(n) + " / " + std::to_string(field.max_words) + " words";
                  over = n > field.max_words;
                } else if (field.max_chars != 0) {
                  count = std::to_string(count_chars(text)) + " / " + std::to_string(field.max_chars);
                }
              } else if (field.kind == FieldKind::list && field.max_items != 0) {
                count = std::to_string(v.is_array() ? v.size() : 0) + " / " + std::to_string(field.max_items);
              }
              if (!count.empty()) {
                const float cw = c.width(count, TypeRole::ui_sm);
                c.text(count, TypeRole::ui_sm, ix + iw - cw, y + std::round((body_lh - small) / 2), cw + 1,
                       over ? Colour::ember : Colour::ink_faint, alpha);
              }
              y += body_lh + std::round(4 * s);
              if (!field.help.empty()) {
                y += para(field.help, TypeRole::ui_sm, ix, iw, Colour::ink_mute);
              }
              y += std::round(4 * s);
              // Its control.
              if (field.kind == FieldKind::shown) {
                y += para(value_words(field, v), TypeRole::ui, ix, iw, Colour::ink_body);
              } else if (field.kind == FieldKind::prose) {
                y += prose_box(ix, iw, v.is_string() ? v.get<std::string>() : std::string(), editing_it,
                               v.is_null() ? "Not set here" : "", a, eid + "/f/" + field.id);
              } else if (field.kind == FieldKind::line) {
                const Rect box{ix, y, iw, well_h};
                if (v.is_null() && !editing_it) {
                  draw_field(box, "Not set here", Colour::ink_faint, false, a, TypeRole::ui_sm);
                } else {
                  draw_field(box, v.is_string() ? v.get<std::string>() : std::string(), Colour::ink_body, editing_it, a,
                             TypeRole::ui_sm);
                }
                hit(box, Scope::page, focus_index(FocusKind::editor, fid), box);
                if (page_focus(FocusKind::editor, fid)) {
                  c.focused(box);
                }
                y += well_h;
              } else if (field.kind == FieldKind::list) {
                const bool with_choice = !field.choices.empty() || !field.choices_from.empty();
                const float choice_w = with_choice ? std::round(std::min(iw * 0.3f, 220 * s)) : 0;
                const std::size_t n = v.is_array() ? v.size() : 0;
                for (std::size_t i = 0; i < n; ++i) {
                  const ListItem item = list_item(v[i]);
                  const std::string id = eid + "/i/" + field.id + "/" + std::to_string(i);
                  const bool item_locked = locked || !item.locked.empty();
                  const std::uint8_t ia = item_locked ? 128 : alpha;
                  const bool editing_item = edit_.kind == EditKind::editor_text && edit_.id == id;
                  const Rect row{ix, y, iw, well_h};
                  if (with_choice) {
                    const Rect cbox{ix, y, choice_w, well_h};
                    c.fill(cbox, Colour::ground_3, rr, ia);
                    c.text(choice_label(field, item.choice), TypeRole::ui_sm, cbox.x + std::round(10 * s),
                           y + std::round((well_h - small) / 2), choice_w - std::round(20 * s), Colour::gold, ia);
                  }
                  const Rect box{ix + choice_w + (with_choice ? gap : 0), y, iw - choice_w - (with_choice ? gap : 0), well_h};
                  draw_field(box, item.text, Colour::ink_body, editing_item, ia, TypeRole::ui_sm);
                  hit(row, Scope::page, focus_index(FocusKind::editor, id), box);
                  if (page_focus(FocusKind::editor, id)) {
                    c.focused(row);
                  }
                  y += well_h + std::round(4 * s);
                  if (!item.locked.empty()) {
                    y += para("Locked: " + item.locked, TypeRole::ui_sm, ix, iw, Colour::ink_mute);
                  }
                  message_lines(field.id, i, ix, iw);
                }
                if (!locked && (field.max_items == 0 || n < field.max_items)) {
                  const std::string id = eid + "/add/" + field.id;
                  const Rect add{ix, y, std::round(120 * s), button_h};
                  c.text("+ Add", TypeRole::ui_sm, add.x, add.y + std::round((button_h - small) / 2), add.w, Colour::gold,
                         alpha);
                  hit(add, Scope::page, focus_index(FocusKind::editor, id));
                  if (page_focus(FocusKind::editor, id)) {
                    c.focused(add);
                  }
                  y += button_h;
                } else if (n == 0) {
                  y += para("No items.", TypeRole::ui_sm, ix, iw, Colour::ink_mute);
                }
              } else {
                // A choice, a pick, a number or a switch: its value in a well, ‹ › for ← →.
                const Rect box{ix, y, std::round(std::min(iw, 360 * s)), well_h};
                c.fill(box, Colour::ground_well, rr, a);
                c.stroke(box, Colour::edge, edge_w, rr, a);
                c.text(value_words(field, v), TypeRole::ui_sm, box.x + std::round(10 * s),
                       y + std::round((well_h - small) / 2), box.w - std::round(50 * s),
                       v.is_null() ? Colour::ink_faint : Colour::ink_body, a);
                c.text("‹ ›", TypeRole::ui_sm, box.right() - std::round(30 * s),
                       y + std::round((well_h - small) / 2), std::round(24 * s), Colour::ink_faint, a);
                hit(box, Scope::page, focus_index(FocusKind::editor, fid), box);
                if (page_focus(FocusKind::editor, fid)) {
                  c.focused(box);
                }
                y += well_h;
              }
              y += std::round(6 * s);
              if (locked) {
                y += para("Locked: " + field.locked, TypeRole::ui_sm, ix, iw, Colour::ink_mute);
              }
              // Its reference, read-only and never sent.
              if (field.reference.is_string() || field.reference.is_array()) {
                std::string ref;
                for (const Json& r : field.reference.is_array() ? field.reference : Json::array({field.reference})) {
                  ref += (ref.empty() ? "" : "; ") + r.get<std::string>();
                }
                if (!ref.empty()) {
                  y += para((field.reference_label.empty() ? std::string("For reference") : field.reference_label) + ": " +
                                ref,
                            TypeRole::ui_sm, ix, iw, Colour::ink_mute);
                }
              }
              message_lines(field.id, std::nullopt, ix, iw);
              // A draft's proposal for it: its words, then Use this and Keep mine.
              if (const auto proposal = st.draft.find(field.id); proposal != st.draft.end()) {
                y += std::round(4 * s);
                const float top = y;
                const std::size_t ground_at = out.items.size();
                y += gap;
                c.text("Draft", TypeRole::eyebrow, ix + gap, y, iw / 3, Colour::gold);
                y += line_height(TypeRole::eyebrow, s) + std::round(4 * s);
                y += para(value_words(field, proposal->second), TypeRole::ui, ix + gap, iw - 2 * gap, Colour::ink_body);
                y += std::round(6 * s);
                const float uw = button(ix + gap, y, "Use this", 1, "use/" + field.id, false);
                button(ix + gap + uw + gap, y, "Keep mine", 0, "keep/" + field.id, false);
                y += button_h + gap;
                Item ground;
                ground.shape = Shape::fill;
                ground.colour = Colour::tint_select;
                ground.clip = c.clip;
                ground.rect = Rect{ix, top, iw, y - top};
                ground.radius = rr;
                out.items.insert(out.items.begin() + static_cast<std::ptrdiff_t>(ground_at), ground);
              }
            }
            y += std::round(8 * s);
            continue;
          }
          if (block.kind == protocol::BlockKind::upload) {
            // An upload (D226): a file is chosen and sent from the browser view on this PC alone (§7.2 item 18); the
            // game has no file picker, so the menu says where, by the block's own label.
            const std::string label = block.submit.value_or("Add a file");
            c.text(label + ": in the browser view on this PC.", TypeRole::ui_sm, x0 + card_pad, y + std::round(12 * s),
                   content_w - 2 * card_pad, Colour::ink_mute);
            y += row_h;
            continue;
          }
          if (block.kind == protocol::BlockKind::document) {
            // A document (G44, D251): the chosen row's member, as document.h shapes it. Spans flow word by word and
            // wrap; a link to a document the list holds is a focus stop (Enter follows it), one it does not hold plain
            // words (invariant 2). Never a URL, never markup.
            const float bx = x0 + card_pad;
            const float bw = std::min(content_w - 2 * card_pad, std::round(760 * s));
            const std::string topic = topic_of(block);
            const auto found = topics_.find(topic);
            const DocumentView doc = found != topics_.end() ? parse_document(found->second) : DocumentView{};
            if (found == topics_.end() || doc.error != DocumentView::Error::none) {
              c.text(doc.error == DocumentView::Error::too_large ? "This document is too large to show."
                                                                  : "Nothing to show.",
                     TypeRole::ui_sm, bx, y + std::round(12 * s), bw, Colour::ink_mute);
              y += row_h;
              continue;
            }
            std::size_t link_n = 0;
            // Spans from (x, y) within w; same-line words of a span drawn as one run. Answers the height.
            const auto flow = [&](const std::vector<DocSpan>& spans, float x, float w, TypeRole base) {
              const float lh = line_height(base, s);
              float cx = x;
              float cy = y;
              for (const DocSpan& sp : spans) {
                const bool is_link = sp.style == DocSpan::Style::link && listed(sp.doc);
                const std::string id = is_link ? topic + "/" + std::to_string(link_n++) : std::string();
                const TypeRole role = sp.style == DocSpan::Style::code ? TypeRole::num : base;
                const Colour ink = is_link ? Colour::gold_hi
                                   : sp.style == DocSpan::Style::strong ? Colour::ink_hi : Colour::ink_body;
                std::string run;
                float run_x = cx;
                bool ringed = false;
                const auto flush = [&] {
                  if (run.empty()) {
                    return;
                  }
                  const float rw = c.width(run, role);
                  c.text(run, role, run_x, cy + std::round((lh - line_height(role, s)) / 2), rw + 1, ink);
                  if (is_link) {
                    const Rect r{run_x, cy, rw, lh};
                    hit(r, Scope::page, focus_index(FocusKind::link, id));
                    if (!ringed && page_focus(FocusKind::link, id)) {
                      c.focused(r);
                      ringed = true;
                    }
                  }
                  run.clear();
                };
                std::size_t at = 0;
                while (at < sp.text.size()) {
                  std::size_t end = sp.text.find(' ', at);
                  end = end == std::string::npos ? sp.text.size() : end + 1;
                  const std::string_view word = std::string_view(sp.text).substr(at, end - at);
                  const float ww = c.width(word, role);
                  if (cx > x && cx + c.width(word.substr(0, word.find_last_not_of(' ') + 1), role) > x + w) {
                    flush();
                    cx = x;
                    cy += lh;
                    run_x = cx;
                  }
                  run.append(word);
                  cx += ww;
                  at = end;
                }
                flush();
              }
              return cy + lh - y;
            };
            if (!doc.title.empty()) {
              c.text(doc.title, TypeRole::display_md, bx, y + std::round(8 * s), bw, Colour::ink_hi);
              y += std::round(8 * s + line_height(TypeRole::display_md, s) + 12 * s);
            }
            const float gap = std::round(10 * s);
            for (const DocBlock& b : doc.blocks) {
              switch (b.kind) {
                case DocBlock::Kind::heading: {
                  const TypeRole role = b.level == 2 ? TypeRole::ui : TypeRole::ui_sm;
                  y += std::round(8 * s);
                  if (!doc_heading_.empty() && b.text == doc_heading_) {
                    // A followed link's heading: brought to the page's top at the next layout (D251).
                    scroll_ = std::max(0.0f, scroll_ + y - (page_area.y + std::round(24 * s)));
                    doc_heading_.clear();
                  }
                  c.text(b.text, role, bx, y, bw, Colour::gold);
                  y += line_height(role, s) + std::round(6 * s);
                  break;
                }
                case DocBlock::Kind::paragraph:
                  y += flow(b.spans, bx, bw, TypeRole::ui) + gap;
                  break;
                case DocBlock::Kind::note: {
                  const float top = y;
                  y += std::round(6 * s);
                  const float h = flow(b.spans, bx + std::round(16 * s), bw - std::round(16 * s), TypeRole::ui);
                  c.fill(Rect{bx, top, std::max(2.0f, std::round(2 * s)), h + std::round(12 * s)}, Colour::gold);
                  y += h + std::round(6 * s) + gap;
                  break;
                }
                case DocBlock::Kind::list:
                  for (std::size_t i = 0; i < b.items.size(); ++i) {
                    const std::string mark = b.ordered ? std::to_string(i + 1) + "." : "•";
                    c.text(mark, TypeRole::ui, bx, y, std::round(24 * s), Colour::ink_mute);
                    y += flow(b.items[i], bx + std::round(28 * s), bw - std::round(28 * s), TypeRole::ui) +
                         std::round(4 * s);
                  }
                  y += gap;
                  break;
                case DocBlock::Kind::code: {
                  const float pad = std::round(10 * s);
                  const float lh = line_height(TypeRole::num, s);
                  std::vector<std::string_view> lines;
                  std::string_view rest(b.text);
                  while (true) {
                    const std::size_t nl = rest.find('\n');
                    lines.push_back(rest.substr(0, nl));
                    if (nl == std::string_view::npos) {
                      break;
                    }
                    rest.remove_prefix(nl + 1);
                  }
                  const float h = lh * static_cast<float>(lines.size()) + 2 * pad;
                  c.fill(Rect{bx, y, bw, h}, Colour::ground_well, std::round(tokens::radius::control * s));
                  for (std::size_t i = 0; i < lines.size(); ++i) {
                    c.text(lines[i], TypeRole::num, bx + pad, y + pad + lh * static_cast<float>(i), bw - 2 * pad,
                           Colour::ink_body);
                  }
                  y += h + gap;
                  break;
                }
                case DocBlock::Kind::table: {
                  const float cw = std::floor(bw / static_cast<float>(b.column_labels.size()));
                  const float lh = line_height(TypeRole::ui_sm, s) + std::round(10 * s);
                  for (std::size_t k = 0; k < b.column_labels.size(); ++k) {
                    c.text(b.column_labels[k], TypeRole::eyebrow, bx + cw * static_cast<float>(k), y + std::round(4 * s),
                           cw - std::round(8 * s), Colour::ink_faint);
                  }
                  y += lh;
                  for (const auto& row : b.rows) {
                    c.line(bx, y, bw, Colour::rule);
                    for (std::size_t k = 0; k < row.size(); ++k) {
                      c.text(row[k], TypeRole::ui_sm, bx + cw * static_cast<float>(k), y + std::round(5 * s),
                             cw - std::round(8 * s), Colour::ink_body);
                    }
                    y += lh;
                  }
                  y += gap;
                  break;
                }
              }
            }
            // Drawn: a followed link's heading that this document lacks leaves it at its top, and is not kept for a later
            // document that has one of that name (invariant 2).
            doc_heading_.clear();
            continue;
          }
          if (block.kind == protocol::BlockKind::form) {
            // A form (D195): a row a field, its label at the left and GraceUI's own field at the right (G38 (a)), then
            // its button at the right; the space's refusal under it.
            const std::string form = form_id(section, b, advanced);
            const std::uint8_t alpha = greyed_words(page.id).empty() ? 255 : 128;
            const float field_w = std::round(std::min((content_w - 2 * card_pad) * 0.6f, 560 * s));
            for (const protocol::FormField& field : block.fields.value_or(std::vector<protocol::FormField>{})) {
              const std::string id = form + "/" + field.id;
              if (field.prose.value_or(false)) {
                // A prose field (D251): its label above, the editor's prose well under it, several lines wrapped.
                const float rx = x0 + card_pad;
                const float rw = content_w - 2 * card_pad;
                c.line(rx, y, rw, Colour::rule);
                c.text(field.label, TypeRole::ui, rx, y + std::round(10 * s), rw, Colour::ink_hi, alpha);
                const float wy = y + std::round(10 * s + line_height(TypeRole::ui, s) + 6 * s);
                const float h = prose_well(rx, wy, rw, form_value(id),
                                           edit_.kind == EditKind::form_field && edit_.id == id, "", alpha,
                                           FocusKind::form_field, id);
                y = wy + h + std::round(12 * s);
                continue;
              }
              const Rect row{x0 + card_pad, y, content_w - 2 * card_pad, row_h};
              if (hovered(row)) {
                c.fill(row, Colour::tint_hover);
              }
              c.line(row.x, row.y, row.w, Colour::rule);
              c.text(field.label, TypeRole::ui, row.x, y + std::round((row_h - line_height(TypeRole::ui, s)) / 2),
                     row.w - field_w - std::round(24 * s), Colour::ink_hi, alpha);
              const Rect box{row.right() - field_w, y + std::round((row_h - 28 * s) / 2), field_w, std::round(28 * s)};
              draw_field(box, form_value(id), Colour::ink_body, edit_.kind == EditKind::form_field && edit_.id == id,
                         alpha, TypeRole::ui_sm);
              hit(row, Scope::page, focus_index(FocusKind::form_field, id), box);
              if (page_focus(FocusKind::form_field, id)) {
                c.focused(box);
              }
              y += row_h;
            }
            const std::string words = running("submit:" + form) ? "Working…" : block.submit.value_or("Send");
            const float w = c.width(words, TypeRole::ui) + std::round(32 * s);
            const Rect button{x0 + content_w - card_pad - w, y + std::round(10 * s), w, std::round(32 * s)};
            c.fill(button, Colour::gold, std::round(tokens::radius::control * s), alpha);
            c.text(words, TypeRole::ui, button.x + std::round(16 * s),
                   button.y + std::round((button.h - line_height(TypeRole::ui, s)) / 2), w, Colour::on_gold, alpha);
            hit(button, Scope::page, focus_index(FocusKind::form_submit, form));
            if (page_focus(FocusKind::form_submit, form)) {
              c.focused(button);
            }
            y += std::round(52 * s);
            if (const auto refusal = refusals_.find(form); refusal != refusals_.end()) {
              c.text(refusal->second, TypeRole::ui_sm, x0 + card_pad, y - std::round(4 * s), content_w - 2 * card_pad,
                     Colour::rot);
              y += std::round(line_height(TypeRole::ui_sm, s));
            }
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
                // Wrapped to the card (D261): a row's height for its first line, a line's for each after it.
                const float h = draw_wrapped((*props)["text"].get<std::string>(), TypeRole::ui, inner_x,
                                             y + std::round(10 * s), inner_w, Colour::ink_body);
                y += row_h + std::max(0.0f, h - std::round(line_height(TypeRole::ui, s)));
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
            if (is_stream(block)) {
              // A stream (D257): the newest rows the menu holds, each its time and its line, the line wrapped; a live
              // feed newest first, a log tail so if it says so (D187); filtered by its bar, if it has one.
              const Json* rows = body != nullptr && body->contains("rows") && (*body)["rows"].is_array()
                                     ? &(*body)["rows"]
                                     : nullptr;
              const Bar stream_bar = bar_of_table(block);
              if (stream_bar) {
                y = draw_bar(stream_bar, inner_x, y + std::round(4 * s), inner_w);
              }
              std::vector<std::size_t> order;
              if (rows != nullptr) {
                for (const std::size_t r : shown_rows(block, *rows)) {
                  if ((*rows)[r].is_object() && !cell_words((*rows)[r], "line").empty()) {
                    order.push_back(r);
                  }
                }
              }
              const bool newest_first = block.component == protocol::Component::live_feed ||
                                        (block.props && block.props->is_object() && block.props->contains("newest_first") &&
                                         (*block.props)["newest_first"] == true);
              if (newest_first) {
                std::reverse(order.begin(), order.end());
              }
              if (order.empty()) {
                const bool narrowed = stream_bar && rows != nullptr && !rows->empty();
                one_line(narrowed ? std::string("Nothing matches these filters.") : none, Colour::ink_mute);
                continue;
              }
              const float lh = std::round(line_height(TypeRole::ui_sm, s));
              const float time_w = std::round(96 * s);
              y += std::round(6 * s);
              for (const std::size_t r : order) {
                const Json& row = (*rows)[r];
                c.text(cell_words(row, "time"), TypeRole::ui_sm, inner_x, y, time_w - std::round(8 * s), Colour::ink_faint);
                const float lx = inner_x + time_w;
                const float lw = inner_w - time_w;
                const std::string line = cell_words(row, "line");
                std::string run;
                std::size_t at = 0;
                while (at <= line.size()) {
                  const std::size_t end = std::min(line.find(' ', at), line.size());
                  const std::string_view word = std::string_view(line).substr(at, end - at);
                  std::string tried = run.empty() ? std::string(word) : run + " " + std::string(word);
                  if (!run.empty() && c.width(tried, TypeRole::ui_sm) > lw) {
                    c.text(run, TypeRole::ui_sm, lx, y, lw, Colour::ink_body);
                    y += lh;
                    run = std::string(word);
                  } else {
                    run = std::move(tried);
                  }
                  at = end + 1;
                }
                c.text(run, TypeRole::ui_sm, lx, y, lw, Colour::ink_body);
                y += lh + std::round(4 * s);
              }
              y += std::round(6 * s);
              continue;
            }
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
              if (block.component == protocol::Component::report_preview) {
                // A report's preview (G44, D251): a part a card with its label, an optional one's switch at its right;
                // each field of a part that is in: its label, then its value, a code field's lines in the mono face.
                const auto parts = objects("parts");
                if (parts.empty()) {
                  one_line(none, Colour::ink_mute);
                  continue;
                }
                const float edge_w = std::round(tokens::kGrace.size.edge_width * s);
                for (const Json* part : parts) {
                  const std::string id = words(*part, "id");
                  const std::string label = words(*part, "label");
                  if (id.empty() || label.empty()) {
                    continue;
                  }
                  const bool optional = part->contains("optional") && (*part)["optional"] == true;
                  const bool included = !optional || !(part->contains("included") && (*part)["included"] == false);
                  const Rect head{inner_x, y, inner_w, row_h};
                  c.line(head.x, head.y, head.w, Colour::rule);
                  c.text(label, TypeRole::ui, head.x, y + std::round((row_h - line_height(TypeRole::ui, s)) / 2),
                         head.w / 2, included ? Colour::ink_hi : Colour::ink_mute);
                  if (optional && block.command) {
                    const std::string fid = *block.topic + "/" + id;
                    const Rect control{head.right() - std::round(40 * s), y + std::round((row_h - 22 * s) / 2),
                                       std::round(40 * s), std::round(22 * s)};
                    const std::string state = included ? "Included" : "Left out";
                    const float word_w = c.width(state, TypeRole::ui_sm);
                    c.text(state, TypeRole::ui_sm, control.x - std::round(12 * s) - word_w,
                           y + std::round((row_h - line_height(TypeRole::ui_sm, s)) / 2), word_w + 1, Colour::ink_body);
                    if (included) {
                      c.fill(control, Colour::gold, control.h / 2);
                    } else {
                      c.fill(control, Colour::ground_well, control.h / 2);
                      c.stroke(control, Colour::edge, edge_w, control.h / 2);
                    }
                    const float knob = std::round(16 * s);
                    c.circle(Rect{included ? control.right() - knob - std::round(3 * s) : control.x + std::round(3 * s),
                                  control.y + std::round((control.h - knob) / 2), knob, knob},
                             included ? Colour::on_gold : Colour::ink_mute);
                    hit(head, Scope::page, focus_index(FocusKind::part, fid), control);
                    if (page_focus(FocusKind::part, fid)) {
                      c.focused(control);
                    }
                  }
                  y += row_h;
                  if (!included) {
                    continue;  // a part left out: its fields hidden
                  }
                  const float label_w = std::round(inner_w * 0.3f);
                  if (part->contains("fields") && (*part)["fields"].is_array()) {
                    for (const Json& field : (*part)["fields"]) {
                      if (!field.is_object() || words(field, "label").empty()) {
                        continue;
                      }
                      const bool code = field.contains("code") && field["code"] == true;
                      const std::string value = words(field, "value");
                      c.text(words(field, "label"), TypeRole::ui_sm, inner_x, y + std::round(4 * s), label_w,
                             Colour::ink_mute);
                      const TypeRole role = code ? TypeRole::num : TypeRole::ui_sm;
                      const float lh = line_height(role, s);
                      const float vw = inner_w - label_w;
                      std::string_view rest(value);
                      float fy = y + std::round(4 * s);
                      const auto put = [&](std::string_view line) {
                        c.text(line, role, inner_x + label_w, fy, vw, Colour::ink_body);
                        fy += lh;
                      };
                      while (true) {
                        // Its line breaks kept; a code field's lines as they are, any other's words wrapped to its
                        // column, so that every word of it shows (EldenNet's "What happened", up to 2000 characters).
                        const std::size_t nl = rest.find('\n');
                        const std::string_view line = rest.substr(0, nl);
                        if (code) {
                          put(line);
                        } else {
                          std::string run;
                          std::size_t at = 0;
                          while (at <= line.size()) {
                            const std::size_t end = std::min(line.find(' ', at), line.size());
                            const std::string_view word = line.substr(at, end - at);
                            std::string tried = run.empty() ? std::string(word) : run + " " + std::string(word);
                            if (!run.empty() && c.width(tried, role) > vw) {
                              put(run);
                              run = std::string(word);
                            } else {
                              run = std::move(tried);
                            }
                            at = end + 1;
                          }
                          put(run);
                        }
                        if (nl == std::string_view::npos) {
                          break;
                        }
                        rest.remove_prefix(nl + 1);
                      }
                      y = fy + std::round(6 * s);
                    }
                  }
                  y += std::round(6 * s);
                }
                if (block.share && block.topic) {
                  // Its Copy and Save (D263): greyed, with the space's blocked words under them if any, until it has
                  // published a text to share; then the space's words after a Save, or its refusal's.
                  std::string blocked;
                  const Json* share = shared(block, blocked);
                  const bool inert = share == nullptr || !greyed_words(current().id).empty();
                  const float bh = std::round(28 * s);
                  const float rr = std::round(tokens::radius::control * s);
                  float bx = inner_x;
                  const auto share_button = [&](const std::string& id, const std::string& label) {
                    const float w = c.width(label, TypeRole::ui_sm) + std::round(24 * s);
                    const Rect button{bx, y, w, bh};
                    c.stroke(button, inert ? Colour::rule : Colour::edge, edge_w, rr);
                    c.text(label, TypeRole::ui_sm, button.x + std::round(12 * s),
                           button.y + std::round((bh - line_height(TypeRole::ui_sm, s)) / 2), w,
                           inert ? Colour::ink_mute : Colour::ink_hi);
                    if (!inert) {
                      hit(button, Scope::page, focus_index(FocusKind::share, id));
                      if (page_focus(FocusKind::share, id)) {
                        c.focused(button);
                      }
                    }
                    bx += w + std::round(12 * s);
                  };
                  std::string under = blocked;
                  Colour under_ink = Colour::ink_mute;
                  if (block.share->copy.value_or(false)) {
                    const std::string id = *block.topic + "/copy";
                    const auto until = copied_until_.find(id);
                    share_button(id, until != copied_until_.end() && now_ms_ < until->second ? "Copied" : "Copy");
                  }
                  if (block.share->save) {
                    const std::string id = *block.topic + "/save";
                    share_button(id, share_busy_.contains(id) ? "Saving…" : "Save");
                    if (const auto refusal = share_refused_.find(id); refusal != share_refused_.end() && share != nullptr) {
                      under = refusal->second;
                      under_ink = Colour::rot;
                    } else if (const auto said = share_said_.find(id); said != share_said_.end() && share != nullptr) {
                      under = said->second;
                    }
                  }
                  y += bh + std::round(8 * s);
                  if (!under.empty()) {
                    y += draw_wrapped(under, TypeRole::ui_sm, inner_x, y, inner_w, under_ink) + std::round(6 * s);
                  }
                }
                continue;
              }
              if (block.component == protocol::Component::computed_sentence) {
                const std::string sentence = body != nullptr ? cell_words(*body, "sentence") : std::string();
                if (sentence.empty()) {
                  one_line(none, Colour::ink_mute);
                  continue;
                }
                // Wrapped to the card (D260), as D251's preview fields are: a row's height for its first line.
                const float lh = std::round(line_height(TypeRole::ui_sm, s));
                const float h = draw_wrapped(sentence, TypeRole::ui_sm, inner_x, y + std::round(12 * s), inner_w,
                                             Colour::ink_body);
                y += row_h + std::max(0.0f, h - lh);
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
                  const std::string value = words(*tiles[i], "value");
                  const float value_y = tile.y + std::round(8 * s) + line_height(TypeRole::ui_sm, s);
                  c.text(value, TypeRole::display_md, tile.x + pad, value_y, tile.w - 2 * pad, Colour::ink_hi);
                  // Its unit after it in the small face, as the browser's tile__unit (D260), on the value's foot.
                  if (const std::string unit = words(*tiles[i], "unit"); !unit.empty()) {
                    const float unit_x = tile.x + pad + c.width(value, TypeRole::display_md) + std::round(6 * s);
                    const float unit_y = value_y + line_height(TypeRole::display_md, s) -
                                         line_height(TypeRole::ui_sm, s) - std::round(2 * s);
                    if (unit_x < tile.right() - pad) {
                      c.text(unit, TypeRole::ui_sm, unit_x, unit_y, tile.right() - pad - unit_x, Colour::ink_mute);
                    }
                  }
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
                  // Its value wrapped to its two thirds (D261), the row growing by a line for each after the first.
                  const float h = draw_wrapped(words(*item, "value"), TypeRole::ui_sm, inner_x + inner_w / 3, small_y,
                                               inner_w * 2 / 3, Colour::ink_body);
                  y += std::max(0.0f, h - std::round(line_height(TypeRole::ui_sm, s)));
                }
                y += row_h;
              }
              continue;
            }
            const Json* table = table_of(block);
            const Paged* paged = block.page ? paged_of(cell_base(block)) : nullptr;
            if (table == nullptr) {
              // A table read a page at a time asks for its first page (D254); any other, its words until it has rows.
              one_line(block.page && (paged == nullptr || paged->loading) ? std::string("Loading\u2026") : none,
                       Colour::ink_mute);
              continue;
            }
            // A table in game (D181): a row a row, its first column's words, the others' after them, and a switch
            // column's switch at the right, as a setting's.
            std::vector<std::string> text_columns;
            std::vector<std::string> text_labels;  // each text column's label, for the header line (D260)
            std::vector<bool> text_numeric;        // ... and whether it is numeric: right-aligned in its slot
            std::vector<std::pair<std::string, std::string>> drawer_columns;  // shown in an open row's drawer (D256)
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
              } else if (column.contains("drawer") && column["drawer"] == true) {
                drawer_columns.emplace_back(column["id"].get<std::string>(), cell_words(column, "label"));
              } else {
                text_columns.push_back(column["id"].get<std::string>());
                text_labels.push_back(cell_words(column, "label"));
                text_numeric.push_back(column.contains("numeric") && column["numeric"] == true);
                if (block.row_secret && column["id"] == block.row_secret->column) {
                  secret_label = cell_words(column, "label");
                }
              }
            }
            const std::vector<protocol::RowAction>& row_actions =
                block.row_actions ? *block.row_actions : std::vector<protocol::RowAction>{};
            const Json& rows = (*table)["rows"];
            if (paged != nullptr) {
              // Read a page at a time (D254): its bar with or without rows (the space filters), then New rows.
              if (const Bar table_bar = bar_of_table(block); table_bar) {
                y = draw_bar(table_bar, x0 + card_pad, y + std::round(4 * s), content_w - 2 * card_pad);
              }
              y += draw_paging(*paged, cell_base(block), true, x0 + card_pad, y, content_w - 2 * card_pad);
            }
            if (rows.empty()) {
              c.line(x0 + card_pad, y, content_w - 2 * card_pad, Colour::rule);
              const bool refused = paged != nullptr && !paged->refused.empty();
              c.text(refused ? paged->refused : paged != nullptr && paged->loading ? std::string("Loading\u2026") : none,
                     TypeRole::ui_sm, x0 + card_pad, y + std::round(12 * s), content_w - 2 * card_pad,
                     refused ? Colour::ink_body : Colour::ink_mute);
              y += row_h;
              continue;
            }
            if (const Bar table_bar = bar_of_table(block); table_bar && paged == nullptr) {
              y = draw_bar(table_bar, x0 + card_pad, y + std::round(4 * s), content_w - 2 * card_pad);  // D250
            }
            // Its columns (D260): the first at the row's left third, as before; each other text column in a slot of its
            // own between that third and the widest row's controls (a numeric one right-aligned), under one header
            // line of their labels, so that a missing cell leaves its slot empty and shifts nothing. With slots
            // narrower than a readable width, each value is shown after its column's label instead, joined as before.
            const std::vector<std::size_t> shown_lines = shown_rows(block, rows);
            const float table_x = x0 + card_pad;
            const float table_w = content_w - 2 * card_pad;
            const float gap12 = std::round(12 * s);
            const float lead_w = drawer_columns.empty() ? 0.0f : std::round(20 * s);
            // A row's controls at its right, by their own words (a pressed button's "Working…" may narrow its cells).
            const auto controls_of = [&](const Json& line) {
              float w = 0;
              for (const protocol::RowAction& act : row_actions) {
                if (line.is_object() && !cell_words(line, act.key).empty()) {
                  w += c.width(act.label, TypeRole::ui_sm) + std::round(24 * s) + gap12;
                }
              }
              if (block.row_secret && line.is_object() && cell_words(line, block.row_secret->column) != "none" &&
                  !cell_words(line, block.row_secret->key).empty()) {
                w += std::max(c.width("Set", TypeRole::ui_sm) + std::round(24 * s), std::round(240 * s)) + gap12;
              }
              return w + (sw != nullptr ? control_w : 0.0f);
            };
            float widest_controls = 0;
            for (const std::size_t r : shown_lines) {
              widest_controls = std::max(widest_controls, controls_of(rows[r]));
            }
            const std::size_t slot_count = text_columns.size() > 1 ? text_columns.size() - 1 : 0;
            const float slots_x = table_x + table_w / 3;
            const float slots_w = table_x + table_w - widest_controls - slots_x;
            const float even_w = slot_count > 0 ? std::floor(slots_w / static_cast<float>(slot_count)) : 0.0f;
            const bool slotted = slot_count > 0 && even_w >= std::round(64 * s);
            // A cell's words in its slot: a row secret's status in words alone, its label in the header (D194, D260).
            const auto slot_words = [&](const Json& line, std::size_t k) {
              std::string words = line.is_object() ? cell_words(line, text_columns[k]) : std::string();
              if (block.row_secret && text_columns[k] == block.row_secret->column) {
                words = secret_status_words(words);
              }
              return words;
            };
            // Each slot as wide as its column's widest words, label or cell, where the room allows (D262): a narrow
            // column takes what it needs, the wider ones share what is left, and room to spare goes to all alike. The
            // slots follow the rows held, so a live table's may widen as a wider value comes.
            std::vector<float> slot_xs(slot_count, slots_x);
            std::vector<float> slot_ws(slot_count, even_w);
            if (slotted) {
              std::vector<float> need(slot_count, 0.0f);
              for (std::size_t k = 1; k < text_columns.size(); ++k) {
                need[k - 1] = c.width(text_labels[k], TypeRole::ui_sm) + gap12;
                for (const std::size_t r : shown_lines) {
                  need[k - 1] = std::max(need[k - 1], c.width(slot_words(rows[r], k), TypeRole::ui_sm) + gap12);
                }
              }
              std::vector<bool> settled(slot_count, false);
              float room = slots_w;
              std::size_t open = slot_count;
              for (bool changed = true; changed && open > 0;) {
                changed = false;
                const float share = room / static_cast<float>(open);
                for (std::size_t k = 0; k < slot_count; ++k) {
                  if (!settled[k] && need[k] <= share) {
                    slot_ws[k] = need[k];
                    room -= need[k];
                    settled[k] = true;
                    --open;
                    changed = true;
                  }
                }
              }
              const float rest = open > 0 ? room / static_cast<float>(open) : room / static_cast<float>(slot_count);
              float x = slots_x;
              for (std::size_t k = 0; k < slot_count; ++k) {
                slot_ws[k] = std::floor(open > 0 ? (settled[k] ? slot_ws[k] : rest) : slot_ws[k] + rest);
                slot_xs[k] = x;
                x += slot_ws[k];
              }
            }
            // Its words in its slot, a numeric column's right-aligned; too long, cut with an ellipsis (place_text's).
            const auto slot_text = [&](const std::string& words, std::size_t k, float sx, float ty, float w, Colour ink) {
              const float tw = c.width(words, TypeRole::ui_sm);
              const float tx = k < text_numeric.size() && text_numeric[k] && tw < w ? sx + w - tw : sx;
              c.text(words, TypeRole::ui_sm, tx, ty, w, ink);
            };
            if (slotted && std::any_of(text_labels.begin(), text_labels.end(), [](const std::string& l) { return !l.empty(); })) {
              const float hy = y + std::round(6 * s);
              c.text(text_labels[0], TypeRole::ui_sm, table_x + lead_w, hy, table_w / 3 - gap12 - lead_w, Colour::ink_mute);
              for (std::size_t k = 1; k < text_columns.size(); ++k) {
                slot_text(text_labels[k], k, slot_xs[k - 1], hy, slot_ws[k - 1] - gap12, Colour::ink_mute);
              }
              y += std::round(line_height(TypeRole::ui_sm, s) + 10 * s);
            }
            for (const std::size_t r : shown_lines) {
              const Json& line = rows[r];
              if (!line.is_object()) {
                continue;
              }
              const std::string row_key = sw != nullptr ? cell_words(line, (*sw)["key"].get<std::string>()) : std::string();
              const std::string cell_id = cell_base(block) + "/" + (row_key.empty() ? std::to_string(r) : row_key);
              const auto refusal = sw != nullptr ? refusals_.find(cell_id) : refusals_.end();
              const Rect row{x0 + card_pad, y, content_w - 2 * card_pad, row_h};
              // A row with a drawer (D256): a mark at its start, pointing down while its drawer is open.
              const std::string drawer_id = drawer_columns.empty() || !row_has_drawer(*table, line)
                                                ? std::string()
                                                : cell_base(block) + "/" + drawer_key(line, r);
              const auto open_at = drawer_id.empty() ? drawer_open_.end() : drawer_open_.find(cell_base(block));
              const bool drawer_shown = open_at != drawer_open_.end() && cell_base(block) + "/" + open_at->second == drawer_id;
              if ((sw != nullptr || !drawer_id.empty()) && hovered(row)) {
                c.fill(row, Colour::tint_hover);
              }
              c.line(row.x, row.y, row.w, Colour::rule);
              const float mark_w = lead_w;  // the mark's room on every row, so that the columns line up (D262)
              if (!drawer_id.empty()) {
                const float m = std::round(10 * s);
                c.triangle(Rect{row.x, y + std::round((row_h - m) / 2), m, m}, drawer_shown ? Facing::down : Facing::right,
                           Colour::ink_mute);
                hit(row, Scope::page, focus_index(FocusKind::drawer, drawer_id));
                if (page_focus(FocusKind::drawer, drawer_id)) {
                  c.focused(row);
                }
              }
              const std::string label = text_columns.empty() ? std::string() : cell_words(line, text_columns[0]);
              std::vector<std::string> cells(text_columns.size());
              std::string rest;  // without slots: each value after its column's label, joined (D260)
              for (std::size_t k = 1; k < text_columns.size(); ++k) {
                std::string words = cell_words(line, text_columns[k]);
                if (block.row_secret && text_columns[k] == block.row_secret->column) {
                  words = (secret_label.empty() || slotted ? "" : secret_label + ": ") + secret_status_words(words);
                } else if (!slotted && !words.empty() && !text_labels[k].empty()) {
                  words = text_labels[k] + " " + words;
                }
                if (!words.empty()) {
                  rest += (rest.empty() ? "" : "  ·  ") + words;
                }
                cells[k] = std::move(words);
              }
              const float label_y = y + std::round((row_h - line_height(TypeRole::ui, s)) / 2);
              c.text(label, TypeRole::ui, row.x + mark_w, label_y, row.w / 3 - std::round(12 * s) - mark_w, Colour::ink_hi);
              // Its row actions (D192), right-aligned, the last at the row's end; a switch, if any, before them.
              float right = row.right();
              std::string row_refusal_id;
              for (std::size_t a = row_actions.size(); a-- > 0;) {
                const protocol::RowAction& act = row_actions[a];
                if (!line.is_object() || cell_words(line, act.key).empty()) {
                  continue;  // a row with no key takes no actions (D209)
                }
                const std::string id = row_action_id(block, line, r, act);
                const bool asking = confirming_ == "row:" + id;
                const std::string words = running("row:" + id) ? "Working…" : asking ? "Press again to confirm" : act.label;
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
              // Its row secret (D194): a Set button, or while it is edited GraceUI's own field, masked (G38 (a)).
              if (block.row_secret && cell_words(line, block.row_secret->column) != "none" &&
                  !cell_words(line, block.row_secret->key).empty()) {
                const std::string id = secret_id(block, line, r);
                if (edit_.kind == EditKind::row_secret && edit_.id == id) {
                  const float fw = std::round(240 * s);
                  const Rect box{right - fw, y + std::round((row_h - 28 * s) / 2), fw, std::round(28 * s)};
                  draw_field(box, {}, Colour::ink_body, true, 255, TypeRole::ui_sm);
                  hit(box, Scope::page, focus_index(FocusKind::row_secret, id));
                  if (page_focus(FocusKind::row_secret, id)) {
                    c.focused(box);
                  }
                  right = box.x - std::round(12 * s);
                } else {
                  const char* set_words = "Set";
                  const float w = c.width(set_words, TypeRole::ui_sm) + std::round(24 * s);
                  const Rect button{right - w, y + std::round((row_h - 28 * s) / 2), w, std::round(28 * s)};
                  c.stroke(button, Colour::edge, std::round(tokens::kGrace.size.edge_width * s),
                           std::round(tokens::radius::control * s));
                  c.text(set_words, TypeRole::ui_sm, button.x + std::round(12 * s),
                         button.y + std::round((button.h - line_height(TypeRole::ui_sm, s)) / 2), w, Colour::ink_hi);
                  hit(button, Scope::page, focus_index(FocusKind::row_secret, id));
                  if (page_focus(FocusKind::row_secret, id)) {
                    c.focused(button);
                  }
                  right = button.x - std::round(12 * s);
                }
                if (refusals_.count(id) != 0) {
                  row_refusal_id = id;
                }
              }
              const float actions_w = row.right() - right;
              const float rest_w = (sw != nullptr ? row.w - control_w : row.w) - actions_w - row.w / 3 - std::round(12 * s);
              const float cell_y = y + std::round((row_h - line_height(TypeRole::ui_sm, s)) / 2);
              if (slotted) {
                // Each cell in its column's slot, within this row's own controls; an empty one leaves its slot empty.
                const float limit = row.right() - actions_w - (sw != nullptr ? control_w : 0.0f) - std::round(12 * s);
                for (std::size_t k = 1; k < cells.size(); ++k) {
                  const float sx = slot_xs[k - 1];
                  const float w = std::min(slot_ws[k - 1] - std::round(12 * s), limit - sx);
                  if (!cells[k].empty() && w > 0) {
                    slot_text(cells[k], k, sx, cell_y, w, Colour::ink_body);
                  }
                }
              } else {
                c.text(rest, TypeRole::ui_sm, row.x + row.w / 3, cell_y, rest_w, Colour::ink_mute);
              }
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
              if (drawer_shown) {
                // Its drawer (D256): each drawer column's label, then its words wrapped to the row, line breaks kept.
                const float dx = row.x + mark_w;
                const float dw = row.w - mark_w;
                const float lh = std::round(line_height(TypeRole::ui_sm, s));
                y += std::round(4 * s);
                for (const auto& [column_id, column_label] : drawer_columns) {
                  const std::string value = cell_words(line, column_id);
                  if (value.empty()) {
                    continue;
                  }
                  c.text(column_label, TypeRole::ui_sm, dx, y, dw, Colour::ink_faint);
                  y += lh;
                  std::string_view rest = value;
                  while (true) {
                    const std::size_t nl = rest.find('\n');
                    const std::string_view one = rest.substr(0, nl);
                    std::string run;
                    std::size_t at = 0;
                    while (at <= one.size()) {
                      const std::size_t end = std::min(one.find(' ', at), one.size());
                      const std::string_view word = one.substr(at, end - at);
                      std::string tried = run.empty() ? std::string(word) : run + " " + std::string(word);
                      if (!run.empty() && c.width(tried, TypeRole::ui_sm) > dw) {
                        c.text(run, TypeRole::ui_sm, dx, y, dw, Colour::ink_body);
                        y += lh;
                        run = std::string(word);
                      } else {
                        run = std::move(tried);
                      }
                      at = end + 1;
                    }
                    c.text(run, TypeRole::ui_sm, dx, y, dw, Colour::ink_body);
                    y += lh;
                    if (nl == std::string_view::npos) {
                      break;
                    }
                    rest = rest.substr(nl + 1);
                  }
                  y += std::round(8 * s);
                }
              }
            }
            if (paged != nullptr) {
              y += draw_paging(*paged, cell_base(block), false, x0 + card_pad, y, content_w - 2 * card_pad);  // D254
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
            // The help line opens on focus and ends with the default (§4.7), unless it formats to nothing: then the
            // space's help says it, if it must (D198). Wrapped to the row (D261), which grows to hold it.
            std::string help;
            if (focused_row) {
              const std::string fallback = format(key, st.default_);
              help = fallback.empty() ? st.help : st.help + " Default: " + fallback + ".";
            }
            const std::size_t help_lines =
                focused_row ? std::max<std::size_t>(1, wrapped_lines(help, TypeRole::ui_sm, content_w - 2 * card_pad)) : 0;
            const float extra = line_height(TypeRole::ui_sm, s) * static_cast<float>(help_lines) +
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
            Rect value_rect{};  // a slider's value, where a press types one (D266)
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
            if (focused_row) {
              draw_wrapped(help, TypeRole::ui_sm, row.x, line_y, row.w, Colour::ink_mute);
              line_y += line_height(TypeRole::ui_sm, s) * static_cast<float>(help_lines);
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
              const bool typing = edit_.kind == EditKind::setting && edit_.id == key;
              // Its value, or while typed into (D266) GraceUI's field in its place, wider, the track shorter for it.
              const float value_w = typing ? std::round(96 * s) : std::round(64 * s);
              if (typing) {
                const Rect box{col.right() - inset - value_w, y + std::round((row_h - 28 * s) / 2), value_w,
                               std::round(28 * s)};
                draw_field(box, "", Colour::ink_body, true, alpha, TypeRole::num);
                value_rect = box;
              } else {
                const std::string shown = format(key, v);
                const float vw = c.width(shown, TypeRole::num);
                c.text(shown, TypeRole::num, col.right() - inset - vw,
                       y + std::round((row_h - line_height(TypeRole::num, s)) / 2), vw + 1, Colour::ink_body, alpha);
                // A press here types a value (D266): from past the knob at the track's end to the column's.
                value_rect = Rect{col.right() - inset - value_w + std::round(8 * s), y, value_w + inset - std::round(8 * s),
                                  row_h};
              }
              const float track_w = control_w - value_w - 2 * inset;
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
            } else {  // a key, a text, a secret, in a field: a text and a secret edited in GraceUI's own (G38 (a))
              const std::string shown = format(key, v);
              const TypeRole role = st.type == SettingType::key_name ? TypeRole::num : TypeRole::ui_sm;
              const float w = st.type == SettingType::key_name ? c.width(shown, role) + std::round(20 * s) : control_w;
              control = Rect{col.right() - w, y + std::round((row_h - 28 * s) / 2), w, std::round(28 * s)};
              const Colour ink = st.type == SettingType::secret && shown == "Set" ? Colour::moss : Colour::ink_body;
              draw_field(control, shown, ink, edit_.kind == EditKind::setting && edit_.id == key, alpha, role);
            }
            hit(row, Scope::page, focus_index(shortcut ? FocusKind::shortcut : FocusKind::setting, key), control, track_rect,
                segment_count, shortcut ? Rect{} : value_rect);
            if (focused_row) {
              c.focused(control);
            }
            y += row.h;
          }
        }
      };
      if (!collapsed) {
        draw_blocks(section.blocks, false);
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
            draw_blocks(*section.advanced, true);
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
        // The words clear of the focus ring, which goes round the row, and centred in it (D245).
        const float inner = std::round(12 * s);
        const float label_y = y + std::round((row.h - line_height(TypeRole::ui, s)) / 2);
        c.text(target != nullptr ? target->name : link, TypeRole::ui, row.x + inner, label_y, row.w / 3 - inner,
               Colour::ink_hi);
        if (target != nullptr) {
          c.text(target->description, TypeRole::ui_sm, row.x + row.w / 3,
                 y + std::round((row.h - line_height(TypeRole::ui_sm, s)) / 2),
                 row.w * 2 / 3 - std::round(30 * s) - inner, Colour::ink_mute);
        }
        c.text("→", TypeRole::ui, row.right() - inner - std::round(16 * s), label_y, std::round(16 * s),
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
    }
    y = std::max(y, master_bottom);
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
      if (f.kind == FocusKind::paging) {
        hints = f.id.ends_with("/more") ? "Enter  Show more    Esc  Back" : "Enter  Show the new rows    Esc  Back";
      } else if (f.kind == FocusKind::drawer) {
        const std::size_t slash = f.id.rfind('/');
        const auto open = drawer_open_.find(f.id.substr(0, slash));
        hints = open != drawer_open_.end() && open->second == f.id.substr(slash + 1) ? "Enter  Close    Esc  Close"
                                                                                       : "Enter  Open    Esc  Back";
      } else if (f.kind == FocusKind::shortcut) {
        // Its home's control, and its link (D189).
        const Setting& st = settings_[f.index];
        const bool changes = !st.locked && st.availability != "later" && st.type != SettingType::key_name &&
                             (st.type != SettingType::text || st.choices_from) && st.type != SettingType::secret;
        hints = changes ? "← →  Change    Enter  Go to its page    Esc  Back" : "Enter  Go to its page    Esc  Back";
      } else if (f.kind == FocusKind::setting) {
        const Setting& st = settings_[f.index];
        // Enter types a value from the keyboard (D266); a pad has no keyboard yet, so its footer leaves it out.
        hints = numeric(st.type) && !st.locked && !greyed(st) && st.availability != "later" && !pad_mode_
                    ? "← →  Change    Shift+← →  By ten    Enter  Type    R  Reset    Esc  Back"
                : numeric(st.type) ? "← →  Change    Shift+← →  By ten    R  Reset    Esc  Back"
                : st.type == SettingType::bool_ ? "Enter  Toggle    R  Reset    Esc  Back"
                : (st.type == SettingType::choice || st.type == SettingType::tri_state ||
                   (st.type == SettingType::text && st.choices_from))
                    ? "← →  Choose    R  Reset    Esc  Back"
                : st.type == SettingType::key_name && st.availability != "later"
                    ? "Enter  Press a new key    R  Reset    Esc  Back"  // captured in game since D209
                : st.type == SettingType::secret && st.availability != "later"
                    ? "Enter  Set a new one    R  Reset    Esc  Back"  // GraceUI's own field since G38 (a)
                : st.type == SettingType::text && st.availability != "later"
                    ? "Enter  Edit    R  Reset    Esc  Back"
                    : "Later version    Esc  Back";
        if (st.locked) {
          hints = "Locked in this version    Esc  Back";
        }
      } else if (f.kind == FocusKind::cell || f.kind == FocusKind::part) {
        hints = "Enter  Toggle    Esc  Back";  // a table's switch, as a setting's but with no default to reset to
      } else if (f.kind == FocusKind::master_row) {
        hints = "↑ ↓  Choose    →  Open    Esc  Back";
      } else if (f.kind == FocusKind::editor) {
        hints = editor_hints(f);
      } else if (f.kind == FocusKind::form_field) {
        hints = "Enter  Type    Esc  Back";
      } else if (f.kind == FocusKind::form_submit) {
        hints = "Enter  Send    Esc  Back";
      } else if (f.kind == FocusKind::row_secret) {
        hints = "Enter  Set    Esc  Back";
      } else if (f.kind == FocusKind::section_action) {
        hints = confirming_ == "section:" + f.id ? "Enter  Confirm    Esc  Back" : "Enter  Press    Esc  Back";
      } else if (f.kind == FocusKind::share) {
        hints = f.index == 0 ? "Enter  Copy    Esc  Back" : "Enter  Save    Esc  Back";
      } else if (f.kind == FocusKind::row_action) {
        hints = confirming_ == "row:" + f.id ? "Enter  Confirm    Esc  Back" : "Enter  Press    Esc  Back";
      } else if (f.kind == FocusKind::filter) {
        // A filter bar's control (D250).
        hints = f.id.ends_with("/search") ? "Enter  Type    Esc  Back"
                : f.id.ends_with("/clear") ? "Enter  Clear filters    Esc  Back"
                : f.id.ends_with("/dir")   ? "Enter  Reverse    Esc  Back"
                                           : "← →  Change    Esc  Back";
      } else {
        hints = "Enter  Open    Esc  Back";
      }
    } else if (scope_ == Scope::rail) {
      hints = "↑ ↓  Move    Enter  Open    →  Page    Esc  Close";
    } else if (scope_ == Scope::top) {
      hints = top_focus_ == kTopLive || top_focus_ == kTopScope
                                        ? "Enter  Change    ← →  Move    ↓  Page    Esc  Back"
              : top_focus_ == kTopQuick ? "Enter  Open    ← →  Move    ↓  Page    Esc  Back"
              : top_stops().size() > 1  ? "Enter  Search    →  Next    ↓  Page    Esc  Back"
                                        : "Enter  Search    ↓  Page    Esc  Back";
    }
    if (editing()) {
      // A field's keys (§5.3): every other key types into it.
      hints = edit_.kind == EditKind::search       ? "↑ ↓  Choose    Enter  Open    Esc  Close"
              : edit_.kind == EditKind::filter_search ? "Enter  Done    Esc  Clear    Ctrl+V  Paste"
              : edit_.kind == EditKind::form_field && edit_.prose ? "↑ ↓  Line    Enter  Next    Esc  Done    Ctrl+V  Paste"
              : edit_.kind == EditKind::form_field ? "Enter  Next    Esc  Done    Ctrl+V  Paste"
              : edit_.kind == EditKind::editor_notes || (edit_.kind == EditKind::editor_text && edit_.prose)
                  ? "↑ ↓  Line    Enter  Done    Esc  Done    Ctrl+V  Paste"
              : edit_.kind == EditKind::editor_text ? "Enter  Done    Esc  Done    Ctrl+V  Paste"
              : edit_.masked                       ? "Enter  Set    Esc  Cancel    Ctrl+V  Paste"
              : edit_.kind == EditKind::setting && setting(edit_.id) != nullptr && numeric(setting(edit_.id)->type)
                  ? "Enter  Set    Esc  Cancel"
                                                   : "Enter  Save    Esc  Cancel    Ctrl+V  Paste";
    }
    if (capture_open()) {
      hints = capture_.stage == CaptureStage::asking ? "Enter  Use it here    Esc  Cancel"
              : capture_.stage == CaptureStage::sent  ? "Esc  Cancel"
              : capture_.kind == CaptureKind::chord   ? "↑ ↓  Move    ← →  Change    Enter  Use    R  No pad chord    Esc  Cancel"
              : capture_.row                          ? "Press a key    Backspace  No key    Esc  Cancel"
                                                      : "Press a key    Esc  Cancel";
    }
    if (quick_open_ && has_quick()) {
      // The Quick switches menu's keys (D252): a choice steps, a switch flips.
      const protocol::QuickSwitch& q = (*pages_.quick)[std::min(quick_focus_, pages_.quick->size() - 1)];
      const Setting* st = q.key ? setting(*q.key) : nullptr;
      hints = st != nullptr && st->type == SettingType::choice ? "↑ ↓  Choose    ← →  Change    Esc  Close"
                                                               : "↑ ↓  Choose    Enter  Flip    Esc  Close";
    }
    footer_ = !hints_shown_ ? std::string() : pad_mode_ ? pad_words(hints) : hints;
    if (hints_shown_) {
      hints_line(c, hints, pad_mode_, page_x + gutter, H - footer_h, footer_h, page_w - 2 * gutter);  // D242, D246
    }
    if (c.focus_found && focus_shown_) {
      const float off = std::round(tokens::kGrace.size.focus_offset * s);
      const Rect ring{c.focus.x - off, c.focus.y - off, c.focus.w + 2 * off, c.focus.h + 2 * off};
      c.clip = scope_ == Scope::page ? 1 : 0;
      c.stroke(ring, Colour::gold_hi, std::round(tokens::kGrace.size.focus_width * s),
               std::round((tokens::radius::control + tokens::kGrace.size.focus_offset) * s));
    }
    // Search's results (§4.3), under its box and over the page: this space's settings, each by its label and its page.
    if (edit_.kind == EditKind::search && !search_query_.empty()) {
      c.clip = 0;
      const float rr = std::round(tokens::radius::control * s);
      const float rh = std::round(40 * s);
      const float list_w = std::round(std::min(page_w - 2 * gutter, 560 * s));
      const std::size_t shown_rows = std::max<std::size_t>(search_hits_.size(), 1);
      const Rect list{search.x, search.bottom() + std::round(6 * s), list_w,
                      rh * static_cast<float>(shown_rows) + std::round(12 * s)};
      c.fill(list, Colour::ground_3, rr);
      c.stroke(list, Colour::edge, std::round(tokens::kGrace.size.edge_width * s), rr);
      const float pad = std::round(14 * s);
      if (search_hits_.empty()) {
        c.text("No setting matches.", TypeRole::ui_sm, list.x + pad,
               list.y + std::round(6 * s) + std::round((rh - line_height(TypeRole::ui_sm, s)) / 2), list.w - 2 * pad,
               Colour::ink_mute);
      }
      for (std::size_t i = 0; i < search_hits_.size(); ++i) {
        const SearchHit& found = search_hits_[i];
        const Rect row{list.x, list.y + std::round(6 * s) + rh * static_cast<float>(i), list.w, rh};
        if (i == search_pick_) {
          c.fill(row, Colour::tint_select);
          c.fill(Rect{row.x, row.y, std::round(2 * s), row.h}, Colour::gold);
        } else if (hovered(row)) {
          c.fill(row, Colour::tint_hover);
        }
        const float label_w = c.text(found.label, TypeRole::ui, row.x + pad,
                                     row.y + std::round((rh - line_height(TypeRole::ui, s)) / 2), row.w / 2, Colour::ink_hi);
        // Its home page, and another space's name before it (D219).
        const std::string where = found.space.empty() ? found.where : found.space_name + " \u203A " + found.where;
        c.text(where, TypeRole::ui_sm, row.x + pad + label_w + std::round(12 * s),
               row.y + std::round((rh - line_height(TypeRole::ui_sm, s)) / 2),
               std::max(0.0f, row.w - 2 * pad - label_w - std::round(12 * s)), Colour::ink_mute);
        hit(row, Scope::top, i + 1);
      }
    }
    // The Quick switches menu (D252), over the page under its pill: a row a switch, its words, the page it lives in and
    // its control; a session switch's state as the space last published it, and none claimed until it has come.
    if (quick_open_ && has_quick()) {
      c.clip = 0;
      const auto& quick = *pages_.quick;
      const float card_w = std::min(std::round(480 * s), page_w - 2 * gutter);
      const float pad = std::round(16 * s);
      const float rh = std::round(60 * s);
      const float radius = std::round(tokens::radius::card * s);
      const Rect card{std::max(page_x + gutter, quick_pill.right() - card_w), bar_h + std::round(6 * s), card_w,
                      rh * static_cast<float>(quick.size()) + 2 * pad};
      c.fill(card, Colour::ground_2, radius);
      c.stroke(card, Colour::rule, std::max(1.0f, std::round(s)), radius);
      hit(card, Scope::top, kQuickRow - 1);  // its ground: a press there keeps it open
      const auto page_name = [&](const std::string& id) {
        for (const protocol::Page& p : pages_.pages) {
          if (p.id == id) {
            return p.name;
          }
        }
        return id;
      };
      for (std::size_t i = 0; i < quick.size(); ++i) {
        const protocol::QuickSwitch& q = quick[i];
        const Rect row{card.x, card.y + pad + rh * static_cast<float>(i), card.w, rh};
        std::string label;
        std::string home;
        std::string words;  // a choice's
        bool is_switch = true;
        bool known = true;
        bool on = false;
        if (q.key) {
          const Setting* st = setting(*q.key);
          if (st == nullptr) {
            continue;
          }
          label = st->label;
          home = st->home;
          const Json& v = value(*q.key);
          if (st->type == SettingType::bool_) {
            on = v.is_boolean() && v.get<bool>();
          } else {
            is_switch = false;
            for (const protocol::Choice& choice : st->choices.value_or(std::vector<protocol::Choice>{})) {
              if (v.is_string() && choice.value == v.get_ref<const std::string&>()) {
                words = choice.label;
              }
            }
          }
        } else {
          label = *q.label;
          home = *q.home;
          const Json* body = latest(q.state->topic);
          known = body != nullptr && body->is_object() && body->contains(q.state->field) &&
                  (*body)[q.state->field].is_boolean();
          on = known && (*body)[q.state->field].get<bool>();
        }
        if (i == quick_focus_) {
          c.fill(row, Colour::tint_select);
        } else if (hovered(row)) {
          c.fill(row, Colour::tint_hover);
        }
        const float text_w = row.w - 2 * pad - std::round(140 * s);
        const float ty = row.y + std::round((rh - line_height(TypeRole::ui, s) - line_height(TypeRole::ui_sm, s)) / 2);
        c.text(label, TypeRole::ui, row.x + pad, ty, text_w, Colour::ink_hi);
        c.text("Lives in " + page_name(home), TypeRole::ui_sm, row.x + pad, ty + line_height(TypeRole::ui, s), text_w,
               Colour::ink_faint);
        const float mid = row.y + rh / 2;
        if (is_switch && known) {
          const float edge_w = std::round(tokens::kGrace.size.edge_width * s);
          const Rect control{row.right() - pad - std::round(40 * s), mid - std::round(11 * s), std::round(40 * s),
                             std::round(22 * s)};
          if (on) {
            c.fill(control, Colour::gold, control.h / 2);
          } else {
            c.fill(control, Colour::ground_well, control.h / 2);
            c.stroke(control, Colour::edge, edge_w, control.h / 2);
          }
          const float knob = std::round(16 * s);
          c.circle(Rect{on ? control.right() - knob - std::round(3 * s) : control.x + std::round(3 * s),
                        control.y + std::round((control.h - knob) / 2), knob, knob},
                   on ? Colour::on_gold : Colour::ink_mute);
        } else {
          const std::string shown = is_switch ? std::string("Not known yet") : words;
          const float w = c.width(shown, TypeRole::ui_sm);
          c.text(shown, TypeRole::ui_sm, row.right() - pad - w, mid - std::round(line_height(TypeRole::ui_sm, s) / 2),
                 w + 1, is_switch ? Colour::ink_mute : Colour::ink_body);
        }
        hit(row, Scope::top, kQuickRow + i);
        if (i == quick_focus_) {
          c.focused(row);
        }
      }
    }
    // The capture's modal (D209), over everything: the page under it takes no pointer.
    if (capture_open()) {
      hits_.clear();
      c.clip = 0;
      c.fill(Rect{0, 0, W, H}, Colour::scrim, 0, 160);
      const float card_w = std::min(std::round(680 * s), page_w - 2 * gutter);
      const float pad = std::round(28 * s);
      const float inner = card_w - 2 * pad;
      // Its words, wrapped to the card.
      const auto wrap = [&](std::string_view text, TypeRole role) {
        std::vector<std::string> lines;
        std::string line;
        std::size_t at = 0;
        while (at < text.size()) {
          const std::size_t end = std::min(text.find(' ', at), text.size());
          const std::string word(text.substr(at, end - at));
          const std::string tried = line.empty() ? word : line + " " + word;
          if (!line.empty() && c.width(tried, role) > inner) {
            lines.push_back(line);
            line = word;
          } else {
            line = tried;
          }
          at = end + 1;
        }
        if (!line.empty()) {
          lines.push_back(line);
        }
        return lines;
      };
      std::string body = capture_.words;
      if (body.empty()) {
        body = capture_.stage == CaptureStage::sent ? "One moment…"
               : capture_.kind == CaptureKind::chord
                   ? "Choose the button held first, then the one pressed while it is held."
                   : capture_.row ? "Press the key to use, with Ctrl, Shift or Alt if you like."
                                  : "Press the key to use, on its own.";
      }
      const std::vector<std::string> title_lines = wrap(capture_.title, TypeRole::display_md);
      const std::vector<std::string> body_lines = wrap(body, TypeRole::ui);
      const float title_h = line_height(TypeRole::display_md, s);
      const float body_h = line_height(TypeRole::ui, s);
      const float picker_h = std::round(44 * s);
      const float hints_h = std::round(24 * s);  // a key cap's line (D246)
      float card_h = pad * 2 + title_h * static_cast<float>(title_lines.size()) + std::round(12 * s) +
                     body_h * static_cast<float>(body_lines.size()) + std::round(20 * s) + hints_h;
      if (capture_.kind == CaptureKind::chord && capture_.stage == CaptureStage::waiting) {
        card_h += std::round(16 * s) + 2 * picker_h;
      }
      const Rect card{std::round(page_x + (page_w - card_w) / 2), std::round((H - card_h) / 2), card_w, card_h};
      const float rr = std::round(tokens::radius::control * 2 * s);
      c.fill(card, Colour::ground_2, rr);
      c.stroke(card, Colour::edge, std::round(tokens::kGrace.size.edge_width * s), rr);
      float cy = card.y + pad;
      for (const std::string& line : title_lines) {
        c.text(line, TypeRole::display_md, card.x + pad, cy, inner, Colour::ink_hi);
        cy += title_h;
      }
      cy += std::round(12 * s);
      const Colour body_ink = capture_.words.empty() ? Colour::ink_mute : Colour::ink_hi;
      for (const std::string& line : body_lines) {
        c.text(line, TypeRole::ui, card.x + pad, cy, inner, body_ink);
        cy += body_h;
      }
      if (capture_.kind == CaptureKind::chord && capture_.stage == CaptureStage::waiting) {
        cy += std::round(16 * s);
        for (std::size_t row = 0; row < 2; ++row) {
          const Rect line_rect{card.x + pad, cy, inner, picker_h - std::round(8 * s)};
          const std::string label = row == 0 ? "Held first" : "Then pressed";
          const std::string value =
              "←  " + std::string(kPadButtons[row == 0 ? capture_.anchor : capture_.key].name) + "  →";
          const float label_y = line_rect.y + std::round((line_rect.h - body_h) / 2);
          c.text(label, TypeRole::ui, line_rect.x + std::round(12 * s), label_y, inner / 2, Colour::ink_mute);
          const float value_w = c.width(value, TypeRole::ui);
          c.text(value, TypeRole::ui, line_rect.right() - value_w - std::round(12 * s), label_y, value_w + 1,
                 Colour::ink_hi);
          if (capture_.picker == row) {
            c.stroke(line_rect, Colour::gold_hi, std::round(tokens::kGrace.size.focus_width * s),
                     std::round(tokens::radius::control * s));
          }
          cy += picker_h;
        }
      }
      // Its keys, in the card where they can be read: the footer's are under its scrim. Key caps, or the pad's glyphs.
      hints_line(c, hints, pad_mode_, card.x + pad, cy + std::round(20 * s), hints_h, inner);
    }
    // The sheet (G9), over everything, as the capture's modal is: its title, its figure, its lines and its Close.
    if (sheet_open_) {
      hits_.clear();
      c.clip = 0;
      c.fill(Rect{0, 0, W, H}, Colour::scrim, 0, 160);
      const float card_w = std::min(std::round(680 * s), page_w - 2 * gutter);
      const float pad = std::round(28 * s);
      const float inner = card_w - 2 * pad;
      const auto wrap = [&](std::string_view text, TypeRole role) {
        std::vector<std::string> lines;
        std::string line;
        std::size_t at = 0;
        while (at < text.size()) {
          const std::size_t end = std::min(text.find(' ', at), text.size());
          const std::string word(text.substr(at, end - at));
          const std::string tried = line.empty() ? word : line + " " + word;
          if (!line.empty() && c.width(tried, role) > inner) {
            lines.push_back(line);
            line = word;
          } else {
            line = tried;
          }
          at = end + 1;
        }
        if (!line.empty()) {
          lines.push_back(line);
        }
        return lines;
      };
      const std::vector<std::string> title_lines = wrap(sheet_.title, TypeRole::display_md);
      std::vector<std::vector<std::string>> body;
      std::size_t body_count = 0;
      for (const std::string& line : sheet_.lines) {
        body.push_back(wrap(line, TypeRole::ui));
        body_count += body.back().size();
      }
      const float title_h = line_height(TypeRole::display_md, s);
      const float figure_h = sheet_.figure.empty() ? 0.0f : line_height(TypeRole::display_xl, s) + std::round(24 * s);
      const float body_h = line_height(TypeRole::ui, s);
      const float gap = std::round(8 * s);
      const float button_h = std::round(44 * s);
      const float hints_h = line_height(TypeRole::ui_sm, s);
      const float card_h = pad * 2 + title_h * static_cast<float>(title_lines.size()) + std::round(12 * s) + figure_h +
                           body_h * static_cast<float>(body_count) +
                           gap * static_cast<float>(body.empty() ? 0 : body.size() - 1) + std::round(24 * s) +
                           button_h + std::round(14 * s) + hints_h;
      const Rect card{std::round(page_x + (page_w - card_w) / 2), std::round(std::max(0.0f, (H - card_h) / 2)), card_w,
                      card_h};
      const float rr = std::round(tokens::radius::control * 2 * s);
      c.fill(card, Colour::ground_2, rr);
      c.stroke(card, Colour::edge, std::round(tokens::kGrace.size.edge_width * s), rr);
      float cy = card.y + pad;
      for (const std::string& line : title_lines) {
        c.text(line, TypeRole::display_md, card.x + pad, cy, inner, Colour::ink_hi);
        cy += title_h;
      }
      cy += std::round(12 * s);
      if (!sheet_.figure.empty()) {
        const float figure_w = c.width(sheet_.figure, TypeRole::display_xl);
        c.text(sheet_.figure, TypeRole::display_xl, std::round(card.x + (card.w - figure_w) / 2), cy, figure_w + 1,
               Colour::gold_hi);
        cy += figure_h;
      }
      for (std::size_t i = 0; i < body.size(); ++i) {
        for (const std::string& line : body[i]) {
          c.text(line, TypeRole::ui, card.x + pad, cy, inner, Colour::ink_hi);
          cy += body_h;
        }
        cy += i + 1 < body.size() ? gap : 0.0f;
      }
      cy += std::round(24 * s);
      const std::string close_label = "Close";
      const float button_w = c.width(close_label, TypeRole::ui) + std::round(48 * s);
      const Rect button{card.x + pad, cy, button_w, button_h};
      const float br = std::round(tokens::radius::control * s);
      c.fill(button, hovered(button) ? Colour::tint_hover : Colour::ground_3, br);
      c.stroke(button, Colour::gold_hi, std::round(tokens::kGrace.size.focus_width * s), br);
      c.text(close_label, TypeRole::ui, button.x + std::round(24 * s),
             button.y + std::round((button.h - line_height(TypeRole::ui, s)) / 2), button_w, Colour::ink_hi);
      sheet_close_ = button;
      c.text("Enter or Esc  Close", TypeRole::ui_sm, card.x + pad, button.bottom() + std::round(14 * s), inner,
             Colour::ink_faint);
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
