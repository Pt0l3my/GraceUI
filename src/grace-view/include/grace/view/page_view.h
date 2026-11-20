// SPDX-License-Identifier: MIT
// page_view.h: one space's menu in game (§2.5, §3.16, §4.2, §4.7, §5.3, §5.4, §5.7; D162): its page data, settings
// description and values loaded into a view of the frame (rail, top bar, page, key hints) and one page, laid out with the
// Grace tokens into a display list, and the keyboard's and pad's navigation turned into the protocol's commands. Pure: no
// ImGui, no Win32; the host maps keys and buttons to Input (§5.3, §5.4) and sends the commands to the space.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "grace/protocol/messages.h"
#include "grace/view/display.h"
#include "grace/view/text.h"

namespace grace::view {

struct Viewport {
  float width = 1920;
  float height = 1080;
  float scale = 1;  // ui.scale (§3.13)
};

// What the host makes of a key or a button (§5.3, §5.4): ↑ ↓ and the d-pad; ← →; Tab and Shift+Tab; Enter, Space and A;
// Esc and B; Q, E, LB and RB; Page Up, Page Down, LT and RT; R and X. `shift` is Shift held (a slider's ten steps).
enum class Key : std::uint8_t {
  up, down, left, right, next, previous, activate, back, previous_tab, next_tab, previous_page, next_page, reset,
};

struct Input {
  Key key = Key::down;
  bool shift = false;
};

enum class Scope : std::uint8_t { rail, top, page };

struct Action {
  enum class Kind : std::uint8_t {
    none,
    command,      // `command` is ready to send to the space (its id 0: the host numbers it)
    opened_page,  // the view moved to `page`
    close,        // back from the rail: the menu closes (the host runs its close routine)
    switch_space, // a link on the rail to another space's page: the host opens `page` of `space` (D79, D171)
  };
  Kind kind = Kind::none;
  protocol::Cmd command{};
  std::string page;
  std::string space;  // with switch_space
};

// A link on the rail to a page of another space (D79, D171): drawn as a category row (or a page row, indented), and
// activated, it answers switch_space.
struct RailLink {
  std::string label;
  std::string space;
  std::string page;
  bool category = true;
};

// The other spaces around this one on the rail (D79, D171): the content space's wordmark, its categories before
// GraceUI's own when GraceUI's page is open, GraceUI's at the rail's foot when a content space's is, and from two
// content spaces a switcher under the wordmark, whose ← and → open the previous and next one. Empty: this space alone.
struct RailContext {
  std::string title;     // the wordmark; empty, this space's own name
  std::vector<RailLink> head;
  std::vector<RailLink> foot;  // pinned to the rail's bottom, under a rule
  std::string switcher;  // its label ("Lanterns, 1 of 2"); empty, no switcher
  RailLink previous;     // what ← on the switcher opens
  RailLink next;         // what → (or Enter) on the switcher opens
};

struct SearchHit {
  std::string key;
  std::string page;  // the setting's home
  std::string label;
};

class PageView {
 public:
  // The space's pages and settings, which the page lint has checked; false with `why` if they disagree all the same.
  bool load(const protocol::SpacePages& pages, const protocol::SettingsDescription& settings, std::string& why);
  // The settings topic's values (an object of key to value); a key not in it shows its default.
  void set_values(const protocol::Json& values);
  // What each inherit tri-state on inherit resolves to (the settings body's `inherited`, D163), shown faint beside it.
  void set_inherited(const std::vector<protocol::InheritedValue>& inherited);
  // Opens a page (its first tab), focus on its first control; false for no such page.
  bool open(std::string_view page_id);

  // The frame and the open page at `viewport`, drawn with `fonts`' faces, into `out` (cleared first). It keeps the
  // focused control in view, scrolling the page as it must.
  void layout(const Viewport& viewport, const Fonts& fonts, DisplayList& out);

  // A key or a button: what it did (§5.3, §5.4, §5.7).
  Action input(const Input& input);
  // A command the space refused: the reason, in words, under its control, focus back on it (§5.7).
  void refused(std::string_view key, std::string_view reason);
  // A setting's value as the space last gave it, after a refusal: `value`, or its default if null (D171).
  void restore(std::string_view key, const protocol::Json& value);
  // The rail's other spaces (D79, D171): the host sets them whenever the spaces or the open one change.
  void set_rail(RailContext context);
  // Focus on the rail, as a link from another space's rail leaves it: on the switcher, or on the open page's entry, as
  // opening a category leaves it (D171).
  void focus_rail(bool switcher);

  // The menu's close routine (§5.1): a destructive action waiting for its second press asks again at the next open; the
  // page, tab and focused row stay for the next open in the session.
  void cancel_confirmation() noexcept { confirming_.clear(); }

  // Settings whose label, synonyms or key hold `query`, without case (§4.3's settings part).
  std::vector<SearchHit> search(std::string_view query) const;

  std::string_view page() const noexcept;
  Scope scope() const noexcept { return scope_; }
  // The focused element, as "rail:<page or category>", "search", "action:<id>", "tab:<id>", "section:<id>",
  // "advanced:<section>", "setting:<key>", "shortcut:<key>" or "related:<page>".
  std::string focused() const;
  // A setting's value as the view shows it: its value, or its default.
  const protocol::Json& value(std::string_view key) const;
  // A value as the view writes it ("2.5 s", "1.00×", "On", a choice's label).
  std::string format(std::string_view key, const protocol::Json& value) const;

 private:
  enum class FocusKind : std::uint8_t { action, tab, section, advanced, setting, shortcut, related };
  struct Focus {
    FocusKind kind;
    std::string id;     // an action's, tab's or section's id, a setting's key, a related page
    std::size_t index;  // into page().actions, tabs, sections; a setting's index
  };
  struct RailEntry {
    enum class Kind : std::uint8_t { category, page, head, foot, switcher };
    Kind kind;
    std::size_t index;  // into categories, pages, or the context's head or foot links
    bool category() const noexcept { return kind == Kind::category; }
    bool own_page() const noexcept { return kind == Kind::page; }
  };
  Action follow(const RailLink& link) const;

  const protocol::Page& current() const { return pages_.pages[page_]; }
  const std::vector<protocol::PageSection>& sections() const;
  const protocol::Setting* setting(std::string_view key) const;
  void rebuild();  // the rail's entries and the page's focus order, after a page, tab or disclosure changes
  Action change(const protocol::Setting& setting, int direction, bool shift);
  Action set(const protocol::Setting& setting, protocol::Json value);
  Action open_index(std::size_t index, Scope scope);
  std::size_t rail_index_of_page() const;
  void focus_first_control();

  protocol::SpacePages pages_;
  std::vector<protocol::Setting> settings_;
  std::map<std::string, std::size_t, std::less<>> by_key_;
  protocol::Json values_ = protocol::Json::object();
  std::map<std::string, std::string, std::less<>> refusals_;
  std::map<std::string, protocol::InheritedValue, std::less<>> inherited_;
  std::set<std::string, std::less<>> collapsed_;
  std::set<std::string, std::less<>> advanced_open_;
  std::size_t page_ = 0;
  std::size_t tab_ = 0;
  std::vector<RailEntry> rail_;
  RailContext context_;
  std::vector<Focus> focus_;
  Scope scope_ = Scope::page;
  std::size_t rail_focus_ = 0;
  std::size_t page_focus_ = 0;
  std::string confirming_;  // a destructive action's id, waiting for its second press
  float scroll_ = 0;
  protocol::Json null_;                               // a key the space does not have
  std::vector<protocol::PageSection> no_sections_;    // a page with neither tabs nor sections
};

}  // namespace grace::view
