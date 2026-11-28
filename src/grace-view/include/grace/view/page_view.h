// SPDX-License-Identifier: MIT
// page_view.h: one space's menu in game (§2.5, §3.16, §4.2, §4.7, §5.3, §5.4, §5.7; D162): its page data, settings
// description and values loaded into a view of the frame (rail, top bar, page, key hints) and one page, laid out with the
// Grace tokens into a display list, and the keyboard's and pad's navigation turned into the protocol's commands. Pure: no
// ImGui, no Win32; the host maps keys and buttons to Input (§5.3, §5.4) and sends the commands to the space.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "grace/protocol/messages.h"
#include "grace/view/display.h"
#include "grace/view/document.h"
#include "grace/view/editor.h"
#include "grace/view/filter.h"
#include "grace/view/text.h"
#include "grace/view/text_field.h"

namespace grace::view {

struct Viewport {
  float width = 1920;
  float height = 1080;
  float scale = 1;  // ui.scale (§3.13)
};

// What the host makes of a key or a button (§5.3, §5.4): ↑ ↓ and the d-pad; ← →; Tab and Shift+Tab; Enter, Space and A;
// Esc and B; Q, E, LB and RB; Page Up, Page Down, LT and RT; R and X; `/`, Ctrl+F and Y, search (§4.3); the right stick,
// a step of the page's scroll; View, the key hints shown or hidden (D242). `shift` is Shift held (a slider's ten steps);
// `pad`, the input came from a pad, so that the key hints follow the last device used (§3.8); `repeat`, it is a held
// direction's repeat (a held arrow key's, the d-pad's or the left stick's, D270), which moves focus along the list it is
// in and steps a number, and does nothing else (repeat_acts).
enum class Key : std::uint8_t {
  up, down, left, right, next, previous, activate, back, previous_tab, next_tab, previous_page, next_page, reset, search,
  scroll_up, scroll_down, toggle_hints,
  quick,  // the pad's Start (§5.4): the space's Quick switches, from anywhere in the menu (D252)
};

struct Input {
  Key key = Key::down;
  bool shift = false;
  bool pad = false;
  bool repeat = false;
};

enum class Scope : std::uint8_t { rail, top, page };

// The mouse (§5.5), in the display's pixels as the last layout drew them.
struct Pointer {
  enum class Kind : std::uint8_t { move, press, release, wheel, leave };
  Kind kind = Kind::move;
  float x = 0;
  float y = 0;
  float wheel = 0;  // with wheel: notches, positive away from the player (the page moves down)
};

struct Action {
  enum class Kind : std::uint8_t {
    none,
    command,      // `command` is ready to send to the space (its id 0: the host numbers it)
    opened_page,  // the view moved to `page`
    close,        // back from the rail: the menu closes (the host runs its close routine)
    switch_space, // a link on the rail to another space's page: the host opens `page` of `space` (D79, D171)
    sheet_closed, // the sheet's Close (G9): the host ends what it showed (a pairing code)
  };
  Kind kind = Kind::none;
  protocol::Cmd command{};
  std::string page;
  std::string space;  // with switch_space
  std::string key;    // with switch_space from a search hit: the setting whose row the host then reveals (D219)
};

// A link on the rail to a page of another space (D79, D171): drawn as a category row (or a page row, indented), and
// activated, it answers switch_space.
struct RailLink {
  std::string label;
  std::string space;
  std::string page;
  bool category = true;
  bool foot = false;  // a category of the space's own foot (D248): GraceUI's rail keeps it at its foot too
  std::optional<protocol::Icon> icon{};  // a category's line icon, as its space gave it (D277)
};

// The other spaces around this one on the rail (D79, D171): the content space's wordmark, its categories before
// GraceUI's own when GraceUI's page is open (those of its foot at the foot, D248), GraceUI's at the rail's foot when a
// content space's is, after the space's own foot, and from two
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
  std::string where;       // its home page's name
  std::string space;       // another space's id (D219); empty for the view's own space
  std::string space_name;  // that space's name
};

class PageView {
 public:
  // The space's pages and settings, which the page lint has checked; false with `why` if they disagree all the same.
  bool load(const protocol::SpacePages& pages, const protocol::SettingsDescription& settings, std::string& why);
  // The settings topic's values (an object of key to value); a key not in it shows its default.
  void set_values(const protocol::Json& values);
  // The keys the space says are changed from their defaults (the settings body's `changed`, D198): the lozenge follows
  // them. Until a space has said, a row is changed when its value differs from its default.
  void set_changed(const std::vector<std::string>& keys);
  // What each inherit tri-state on inherit resolves to (the settings body's `inherited`, D163), shown faint beside it.
  void set_inherited(const std::vector<protocol::InheritedValue>& inherited);
  // The increment the space runs ("0.2", as its registration names it): a page of a later increment is absent, from the
  // rail, Page Up and Page Down, search, related links and shortcuts, until that increment (§4.2.2, D173). Empty, the
  // default: every page is present.
  void set_increment(std::string_view increment);
  void leave_absent_page();  // the open page gone (a later increment, its setting off): the first present one
  // Whether a page is present: in the space's increment, not the browser's alone, and its shown_with switch on (D173,
  // D258). The rail, Page Up and Page Down, search, Related and shortcuts all ask this one rule.
  bool present(std::string_view page_id) const;
  // Whether the space's page data has a page of that id, present or not; and every page's id, in its order.
  bool has_page(std::string_view page_id) const;
  std::vector<std::string> page_ids() const;
  // The space's page data as loaded.
  const protocol::SpacePages& space_pages() const noexcept { return pages_; }
  // Opens a page (its first tab; a flow's, the step its space publishes), focus on its first control; false for no such
  // page, or one absent.
  bool open(std::string_view page_id);
  // ---- Flows (D278). A page with a flow is outside the rail: its tabs are its steps, drawn as a strip of numbered
  // names that takes no focus, and the step shown is the one its state topic names (`step`; the first while none or an
  // unknown one is published), each in `done` checked, `blocked` the space's words beside Next, which waits for them.
  // Under the step's sections: Back (not on the first step), Skip (on an optional step), and Next, or Done on the last,
  // named "flow:back", "flow:skip", "flow:next" and "flow:done"; each sends the flow's command with {step, go} and
  // takes no press until its answer (accepted() or refused(), by that name) or a new step. Done, answered, opens the
  // flow's `then`, or the space's first page. Every present flow's state topic is watched with every page of its space.
  // The flow whose state says `start` (a present one, the first in the page data), or empty: the host lands on it once
  // a session, at an opening the player has not yet moved in.
  std::string flow_to_land() const;

  // The frame and the open page at `viewport`, drawn with `fonts`' faces, into `out` (cleared first). It keeps the
  // focused control in view, scrolling the page as it must.
  void layout(const Viewport& viewport, const Fonts& fonts, DisplayList& out);

  // A key or a button: what it did (§5.3, §5.4, §5.7).
  Action input(const Input& input);
  // The held key's repeats do nothing from here until a new press (D270): the host calls it when another space's view
  // starts taking the keys.
  void stop_repeats() noexcept { repeat_stays_ = false; }
  // The mouse (§5.5): a press focuses what it is on and does what Enter does there, or on a control what a click on its
  // part does (a switch turns, a choice's segment is chosen, a slider takes the value under the pointer); a slider's
  // moves while its button is held set it, and the release ends that; the wheel scrolls the page, which then stays
  // where the wheel left it until a key moves focus; a move changes only what is hovered. Hover tints are drawn only
  // while the pointer is over the menu.
  Action pointer(const Pointer& pointer);
  // A command the space refused: the reason, in words, under its control, focus back on it (§5.7).
  void refused(std::string_view key, std::string_view reason);

  // ---- Capture (G37, D209). A row action that captures (protocol 1.4), or a key's setting (hotkeys.menu), opens a
  // small modal over the page. A key capture waits for the next key press, which the host gives by its name with its
  // modifiers ("Ctrl+K"), never as a key of the menu's own; Escape (Key::back) cancels, and on a row's capture
  // Backspace leaves it with no key. A chord capture has two pickers, the button held first and the one pressed then,
  // moved with ↑ ↓ and changed with ← →; Enter uses them, R leaves no chord. The command it makes is answered through
  // capture_answered: done, it closes; `confirm_required` shows the space's words and Enter sends it again with
  // "confirm"; any other refusal shows its words and waits again.
  bool capture_open() const noexcept { return capture_.kind != CaptureKind::none; }

  // ---- A sheet (G9): a modal of the host's own words over the page, such as Pair a device's code. The host sets it,
  // and sets it again whenever its words change (a countdown). Enter, Escape, A, B or a press on its Close closes it and
  // answers sheet_closed, which the host takes as the end of what it showed; close_sheet, the host's own close (the
  // menu's close routine), answers nothing. While it is open the page under it takes no key, character or pointer.
  struct Sheet {
    std::string title;
    std::string figure;              // drawn large and centred, such as a code; empty for none
    std::vector<std::string> lines;  // each wrapped to the card
    bool operator==(const Sheet&) const = default;
  };
  void show_sheet(Sheet sheet);
  void close_sheet() noexcept;
  bool sheet_open() const noexcept { return sheet_open_; }
  bool awaiting_key() const noexcept { return capture_.kind == CaptureKind::key && capture_.stage == CaptureStage::waiting; }
  Action captured(std::string_view key);
  void capture_answered(bool ok, bool confirm_required, std::string_view words);
  // A setting's value as the space last gave it, after a refusal: `value`, or its default if null (D171).
  void restore(std::string_view key, const protocol::Json& value);
  // The rail's other spaces (D79, D171): the host sets them whenever the spaces or the open one change.
  void set_rail(RailContext context);
  // A component's topic body, as the host has it (D181). A table whose topic body the view has is drawn in game: one row
  // a row, its first column's words, the others' after them, and a switch column's switch, which ↵, A, ← and → turn
  // (the column's command, with the row's key and "on"). Any other component, or a table without its body, is shown in
  // the browser view. Focus stays on the row it was on.
  void set_topic(std::string_view topic, const protocol::Json& body);
  // ---- The top bar (G45, D252). After search come Live updates, on a page that says it is live, and Quick switches,
  // where the space has some: ← → move between them, Enter acts, ↓ goes to the page. Live updates is the view's state
  // for the session: Live; Every 5 s, which applies the page's topics at most once every five seconds; or Paused, which
  // applies none until Live comes back, its words counting what came meanwhile ("Paused · 3 new"). A settings value,
  // a topic's first body and whatever comes in the 3 s after the player sends a command are never held, and leaving the
  // page applies what it held; the Quick switches menu shows a session switch's newest state, held or not. Quick switches (Enter on it, or Start from anywhere) opens a menu of the space's switches: ↑ ↓
  // choose, Enter or → flips a switch or steps a choice forward, ← back, Esc or Start closes. A setting's switch sends
  // settings.set as its row does; a session switch sends its command with {"on": ...} once its state has come, and shows
  // what the space publishes (invariant 1).
  enum class LiveMode : std::uint8_t { live, every_5s, paused };
  LiveMode live_mode() const noexcept { return live_mode_; }
  void set_live_mode(LiveMode mode);
  // The time, in milliseconds: the host gives it before each wake's topics; true if it applied held bodies.
  bool tick(std::int64_t now_ms);
  // Whether bodies are held (the host keeps a timer while Every 5 s has some), and how many came while held.
  bool holding() const noexcept { return !pending_.empty(); }
  // Something drawn for a while that tick() ends (a report's "Copied", D263), or a confirmation that waits before its
  // second press counts (D274), drawn live once it does: the host wakes at next_due() for it.
  bool timed() const noexcept { return !copied_until_.empty() || (!confirming_.empty() && confirm_waiting_); }
  // When the first of them ends, on tick()'s clock: so that the host wakes then (D274); 0 with none.
  std::int64_t next_due() const noexcept;
  std::size_t held_new() const noexcept { return held_new_; }
  bool quick_open() const noexcept { return quick_open_; }
  // ---- A space's scope, and rows read a page at a time (G45, D254). A scoped page follows the scope in view (the
  // top bar's chooser, after Live updates; Enter steps through the space's choices, the current one again at each
  // open): a block or master list bound to a family draws <family>/<scope> (a master page's detail family still the
  // chosen row's), and a choice marked read_only greys the page's writing controls. A table or master list with a page
  // source asks its space for its rows: take_page_requests() gives the host each command to send (the first page when
  // the page opens, or its filters, sort, search or scope change; the next one on Show more; the first again when its
  // changes topic moves), and page_answer() takes each answer by where it was sent from. While a page is asked for,
  // "Loading…"; a refusal's words stand in for its rows; nothing a page answered is kept past leaving the page.
  struct PageRequest {
    std::string where;  // "page:<key>#<serial>", for page_answer
    protocol::Cmd command;
  };
  std::vector<PageRequest> take_page_requests();
  void page_answer(std::string_view where, const protocol::Result& result);
  std::string scope_id() const;  // the scope in view: the chosen one if the space still lists it, else its current
  // The state topics of the open page's components that the view draws in game (a table, a key-value list, a check
  // list, stat tiles, a computed sentence; D184): what the host watches while the page is open.
  std::vector<std::string> component_topics() const;
  // Focus on the rail, as a link from another space's rail leaves it: on the switcher, or on the open page's entry, as
  // opening a category leaves it (D171).
  void focus_rail(bool switcher);

  // The menu's close routine (§5.1): a destructive action waiting for its second press asks again at the next open; the
  // page, tab and focused row stay for the next open in the session.
  // An edit ends as Escape ends it (G38 (2)).
  void cancel_confirmation() noexcept;

  // Settings whose label, synonyms or key hold `query`, without case (§4.3's settings part).
  std::vector<SearchHit> search(std::string_view query) const;
  // Search across spaces (D219): the other spaces the menu shows, whose settings the search field finds too, each hit
  // named by its space; opening one answers switch_space with the hit's page and key. The host keeps them current, and
  // each view outlives its use here (it is relinked whenever the spaces change).
  struct SearchOther {
    std::string id;
    std::string name;
    const PageView* view = nullptr;
  };
  void set_search_others(std::vector<SearchOther> others);
  // This space's hits, then every other space's, sorted by label and then by space; at most `limit`.
  std::vector<SearchHit> search_all(std::string_view query, std::size_t limit) const;
  // A setting's row focused on its home page, its tab chosen and its section and Advanced opened (a search hit from
  // another space, once the host has switched to it).
  void reveal(std::string_view key);

  // ---- Text entry (§5.6, D57, G38 (a)): GraceUI's own field, for a text setting with no choices, a secret setting
  // (masked, write-only: it starts empty, and is sent and forgotten), a form's fields (D195), a table's row secret
  // (D194, masked likewise) and search (§4.3, this space's settings). Enter, A or a click starts editing the focused
  // one; while it edits, the host gives it characters (typed()), its editing keys (edit()) and the clipboard's text
  // (pasted()), and every other key is the field's. Enter keeps the text (a setting's is sent, a form field's moves on
  // to the next field, a row secret's is sent, search opens the chosen setting's row); Tab keeps it and moves on;
  // Escape leaves (a setting's and a secret's edit is dropped, a form field keeps its text, search empties). The
  // menu's close routine ends an edit as Escape does, a form's fields staying for the next open (G38 (2)).
  enum class EditKey : std::uint8_t {
    left, right, up, down, home, end, backspace, erase, enter, escape, tab, select_all, copy, cut,
  };
  struct EditInput {
    EditKey key = EditKey::enter;
    bool shift = false;
    bool ctrl = false;  // by word (← → Backspace Delete)
  };
  bool editing() const noexcept { return edit_.kind != EditKind::none; }
  Action edit(const EditInput& in);
  Action typed(char32_t code_point);
  Action pasted(std::string_view utf8);
  // The text Ctrl+C or Ctrl+X put aside for the clipboard, taken once; never a masked field's.
  bool take_clipboard(std::string& out);
  // The edited field's caret as the last layout drew it (the IME's place, §5.6); empty while nothing is edited.
  Rect caret() const noexcept { return caret_rect_; }
  // The edited field's box as the last layout drew it: where Steam's floating keyboard opens on a pad (§5.6, D275);
  // empty while nothing is edited. And each edit's own number, a new one at each start, so that the host tells one
  // field's edit from the next even when one press ends the first and begins the second.
  Rect edit_box() const noexcept { return edit_box_; }
  std::uint64_t edit_serial() const noexcept { return edit_serial_; }
  // A command the space took, by where its refusal's words would have gone: a form's fields empty again (D195), a
  // report's Save its words under it (D263); a flow's button takes presses again, Done opening the flow's next page,
  // and an action that opens a page opens it if the page that sent it is still open (D278).
  void accepted(std::string_view where, std::string_view words = {});
  // A form field's text as the view holds it (tests).
  std::string form_value(std::string_view field) const;

  // ---- Master and detail (D215, D36). A page with a master list draws its rows at the left, each by its first
  // column's words and its second's, and its tabs and sections at the right as their detail: a block bound to the
  // master's family draws the chosen row's member (<family>/<key>), which the host watches as one of the page's
  // component topics. Focus on a row chooses it; ↑ ↓ move along the list, → or Enter go into the detail, and ← or Esc
  // in the detail come back to the chosen row. The first row is chosen as soon as the list has one, and a page keeps its
  // chosen row for the session. Nothing of the row's key goes into a log or an address (§3.6 of proposal 0010).
  std::string chosen() const;  // the open page's chosen row's key, or empty

  // ---- Slow commands (D216). A command the space names slow (a model's draft) may take up to 90 s: the control that
  // sent it reads "Working…" and takes no press until the host says its answer came (finished(), with the command's
  // name, whatever the answer was), the oldest of its name first.
  void finished(std::string_view command);
  bool running(std::string_view element) const;  // an element, named as focused() names it, waits for its answer

  // ---- The editor block (D217, proposal 0010 §3.3 and §7). Its document is its topic's body; the player's edits over
  // it are kept in the view for the session (G38 (2)), whatever page is open and whether the menu is open or not, by the
  // document's topic. Its focus stops are named "editor:<section>/<n>/<part>": save, check, reset, restore, reload,
  // notes, draft, use_all; f/<field> a field; i/<field>/<n> a list's item; add/<field>; use/<field> and keep/<field> a
  // draft's proposal. Enter on a text field or an item edits it in GraceUI's own field (one paragraph; a prose field
  // wraps, ↑ ↓ moving by line), ← → change a choice, a pick, a number or a switch, and an item's choice; Shift+↑ ↓ move an
  // item; R undoes a field's edit, or leaves a field that may be unset to the space ("not set here"), or removes an item.
  // A command's answer comes back through editor_answer(), by the element that sent it: a check's messages, a save's
  // (or its conflict), a draft's proposals. A save's answer may name the row the page's master list should choose
  // (`chosen`, D218), taken once the list has it.
  void editor_answer(std::string_view where, const protocol::Result& result);
  // An editor's state, by its document's topic (tests).
  const EditorState* editor_state(std::string_view topic) const;

  std::string_view page() const noexcept;
  Scope scope() const noexcept { return scope_; }
  // The focused element, as "rail:<page or category>", "search", "action:<id>", "tab:<id>", "section:<id>",
  // "advanced:<section>", "setting:<key>", "shortcut:<key>", "related:<page>" or "cell:<topic>/<row's key>".
  std::string focused() const;
  // Whether the focus ring is drawn (G42): set by a key or the pad (input), cleared by the mouse (pointer), and given
  // by the host, whose menu opens with it cleared. A view starts with it set, as a keyboard player sees it (grace-dev's
  // dumps); the browser view's own rule is the browser's :focus-visible.
  void set_focus_shown(bool shown) noexcept { focus_shown_ = shown; }
  bool focus_shown() const noexcept { return focus_shown_; }
  // Pad mode (§5.4, D242): the key hints drawn as the pad's buttons, in our own glyphs. Set by an input from a pad,
  // cleared by a key or the mouse, and given by the host, which keeps the last device for the whole menu. A view starts
  // without it, as a keyboard player sees it (grace-dev's dumps).
  void set_pad_mode(bool on) noexcept { pad_mode_ = on; }
  bool pad_mode() const noexcept { return pad_mode_; }
  // Whether the key hints are drawn (View on a pad turns them off and on, D242), and the footer's words as the last
  // layout drew them: the keys' names and labels, or in pad mode the buttons' ("A  Toggle    B  Back").
  void set_hints_shown(bool shown) noexcept { hints_shown_ = shown; }
  bool hints_shown() const noexcept { return hints_shown_; }
  const std::string& footer() const noexcept { return footer_; }
  // Where the last layout drew an element, named as focused() names it: its row (within the page's clip) and its control
  // (a setting's or a cell's; else the row). Both empty if it was not drawn. For tests and the pointer's host.
  struct Bounds {
    Rect row;
    Rect control;
  };
  Bounds bounds_of(std::string_view name) const;
  // Every element the last layout drew, by name, in drawing order.
  std::vector<std::pair<std::string, Bounds>> all_bounds() const;
  // A setting's value as the view shows it: its value, or its default.
  const protocol::Json& value(std::string_view key) const;
  // A value as the view writes it ("2.5 s", "1.00×", "On", a choice's label).
  std::string format(std::string_view key, const protocol::Json& value) const;
  // A row's availability badge in §4.7's words, or empty: none for live, and "later" replaces the control instead.
  std::string availability_badge(const protocol::Setting& setting) const;

 private:
  enum class FocusKind : std::uint8_t { action, tab, section, advanced, setting, shortcut, related, cell, row_action,
                                        section_action, form_field, form_submit, row_secret, master_row, editor,
                                        filter,  // a filter bar's control (G43, D250)
                                        link,    // a document's link (G44, D251)
                                        part,    // a report preview's optional part's switch (D251)
                                        paging,  // Show more, or "New rows · Show", of rows read a page at a time (D254)
                                        drawer,    // a table's row, its drawer opened with Enter (D256)
                                        share,     // a report preview's Copy or Save (D263)
                                        flow };    // a flow's Back, Skip, Next or Done (D278)
  struct Focus {
    FocusKind kind;
    std::string id;     // an action's, tab's or section's id, a setting's key, a related page, a cell's topic/key, a
                        // row action's topic/key/action
    std::size_t index;  // into page().actions, tabs, sections; a setting's index; a cell's or a row action's row
    const protocol::PageBlock* block = nullptr;  // a row action's table (D192), an editor's block (D217)
    std::size_t sub = 0;                         // a row action's index in its table's row_actions; a list's item
    std::uint8_t part = 0;                       // an editor's element (EditorPart)
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
  // A table component's body, if the view has its topic's and it has columns and rows; and its switch column, if any.
  const protocol::Json* table_of(const protocol::PageBlock& block) const;
  // A filter bar (G43, D250): its key (the table's topic, or "master:" and the master's), its description, and the
  // rows it filters; none where the block or the page has no bar or no rows.
  struct Bar {
    std::string key;
    const protocol::FilterBar* bar = nullptr;
    const protocol::Json* rows = nullptr;
    explicit operator bool() const noexcept { return bar != nullptr && rows != nullptr; }
  };
  Bar bar_of_table(const protocol::PageBlock& block) const;
  Bar bar_of_master() const;
  Bar bar_of(const Focus& focus) const;
  // A filter's choices, each its value and its words: the values its rows hold, or on rows read a page at a time its
  // own list or a state topic's (D254).
  std::vector<std::pair<std::string, std::string>> bar_choices(const Bar& bar, std::size_t filter) const;
  // A table's rows that show, by index into its rows, in the order they show: every row where it has no bar.
  std::vector<std::size_t> shown_rows(const protocol::PageBlock& block, const protocol::Json& rows) const;
  // A bar's controls, in the focus order: "f0" to "f3", "sort", "dir", "search", and "clear" while it hides every row.
  std::vector<std::string> bar_parts(const Bar& bar) const;
  void push_bar(const Bar& bar, const protocol::PageBlock* block);
  Action filter_input(const Focus& focus, Key key);
  void refocus(const std::string& id);
  // A document's links (D251): each to a document the page's list holds; and a report preview's switches.
  bool listed(const std::string& key) const;
  void push_doc_links(const protocol::PageBlock& block);
  void push_parts(const protocol::PageBlock& block);
  Action follow_link(const Focus& focus);
  Action flip_part(const Focus& focus) const;
  // A report preview's shareable text (D263): its body's share while it holds a text of at most 256 KiB and a file
  // name GraceUI takes, else null, with the space's blocked words if it gave some.
  const protocol::Json* shared(const protocol::PageBlock& block, std::string& blocked) const;
  Action press_share(const Focus& focus);
  static bool drawn_in_game(protocol::Component component);
  // A component's body (D185: its topic's body, or the member props.field names), and whether its topic has
  // published anything yet (D186: the block's own empty words only then).
  struct Drawn {
    const protocol::Json* body = nullptr;
    bool published = false;
  };
  Drawn drawn(const protocol::PageBlock& block) const;
  std::string empty_words(const protocol::PageBlock& block, bool published) const;
  // A table's cells' ids: its topic, and its field after '#' (D185).
  static std::string cell_base(const protocol::PageBlock& block);
  // A row action's focus id: its table's cell base, the row's key (or its index) and the action's id (D192).
  static std::string row_action_id(const protocol::PageBlock& block, const protocol::Json& row, std::size_t r,
                                   const protocol::RowAction& action);
  Action row_action(const Focus& f);
  Action section_action(const Focus& f);
  // A section's live status (D202): its word and tone (moss, ember, glint or rot), or both empty while unpublished.
  std::pair<std::string, std::string> section_status(const protocol::PageSection& section) const;
  // A text setting's choices from its topic (D193): each {value, label} it publishes, at most 256; `published` once
  // the topic has a state.
  std::vector<protocol::Choice> live_choices(const protocol::Setting& s, bool& published) const;
  // A page greyed by its space (D188): the space's words for it, or empty if it is usable.
  std::string greyed_words(std::string_view page_id) const;
  bool greyed(const protocol::Setting& setting) const;
  Action toggle_cell(const Focus& focus) const;
  // Where the pointer acts (§5.5): each focusable element as the last layout drew it, the page's within its clip.
  struct Hit {
    Rect rect;            // where it takes the pointer
    Rect control;         // a setting's or a cell's control within it; empty, the whole rect acts
    Rect track;           // a slider's track
    Scope scope = Scope::page;
    std::size_t index = 0;     // into rail_ or focus_ (top: 0)
    std::size_t segments = 0;  // a choice's
    Rect value;                // a slider's value: a press there types one (D266)
  };
  const Hit* hit_at(float x, float y) const;
  std::string name_of(Scope scope, std::size_t index) const;
  Action slide(float x);
  std::size_t rail_index_of_page() const;
  void focus_first_control();

  // A form block's id (D195): its section's and its place among the section's blocks ("hire/0", "hire/a0" in Advanced).
  static std::string form_id(const protocol::PageSection& section, std::size_t index, bool advanced);
  // A table's row secret's id (D194): its cell base and the row's key, or the row's index.
  static std::string secret_id(const protocol::PageBlock& block, const protocol::Json& row, std::size_t r);

  enum class EditKind : std::uint8_t { none, setting, form_field, row_secret, search, editor_text, editor_notes,
                                       filter_search };
  struct Edit {
    EditKind kind = EditKind::none;
    std::string id;                              // a setting's key, a form field's or a row secret's focus id
    TextField field;
    bool masked = false;                         // a secret: drawn as discs, never copied, never kept
    std::size_t offset = 0;                      // the first byte drawn, so that the caret stays in view
    const protocol::PageBlock* block = nullptr;  // a form's or a row secret's
    protocol::Json row_key;                      // a row secret's row
    std::string topic;                           // an editor's document's topic
    std::string editor_field;                    // an editor's field
    std::size_t item = static_cast<std::size_t>(-1);  // a list's item, or none
    bool prose = false;                          // wraps; ↑ ↓ move by line
  };
  Edit edit_;
  void start_edit(EditKind kind, std::string id, std::string_view text, std::size_t max, bool masked,
                  const protocol::PageBlock* block = nullptr, protocol::Json row_key = {});
  Action finish_edit(bool keep, int move_on);  // move_on: +1 Enter (a form's next field), 0 Tab (G41) or neither
  void end_edit();  // the field gone, a masked one's text overwritten first
  void edited();    // after a change: a form field's or an editor's text kept, search's hits found again
  void editor_store();  // the edited editor text into its document's edits (or the draft's notes)
  std::map<std::string, std::string, std::less<>> form_values_;  // a form field's text, by its focus id
  std::map<std::string, FilterState, std::less<>> filters_;      // each filter bar's choices, by its key (D250)
  struct DocLink {
    std::string doc;
    std::string heading;
  };
  std::vector<DocLink> doc_links_;  // the document's links, by their focus stops' index (D251)
  std::string doc_heading_;         // a followed link's heading, scrolled to once its document is laid out
  std::string search_query_;
  std::vector<SearchHit> search_hits_;
  std::vector<SearchOther> search_others_;
  std::size_t search_pick_ = 0;
  Action open_hit(const SearchHit& hit);
  // A setting's row focused on the open page: its tab chosen, its section opened, its Advanced disclosure open.
  void focus_setting(std::string_view key);

  // Master and detail (D215).
  const protocol::PageMaster* master() const;
  // The master's rows that name a member: each row's key and the row, in the list's order.
  std::vector<std::pair<std::string, const protocol::Json*>> master_rows() const;
  // The topic a block draws: its own, or for a block bound to the master's family the chosen row's member (empty if none).
  std::string topic_of(const protocol::PageBlock& block) const;
  // After a key or the pointer: focus on a row chooses it; a list with rows and none chosen chooses its first.
  void follow_master();
  void choose(const std::string& key);
  void focus_master_on_open();  // after an open: focus on the chosen row, or the first, or once the list has one
  Action input_body(const Input& in);
  Action pointer_body(const Pointer& in);
  std::map<std::string, std::string, std::less<>> chosen_;  // a page's chosen row's key, by the page's id
  bool master_focus_pending_ = false;  // a master page opened before its list had rows: focus goes to the first one
  // A row a save's answer named (D218), by the page's id: chosen once the page's list has it, unless the player chooses
  // a row by hand first.
  std::map<std::string, std::string, std::less<>> expected_;
  // The element focus was on when a new row's choice took it away, until the new member's state draws it again (and no
  // key or press has moved focus since): so that Save, say, keeps focus across a save's choice (D218).
  std::string refocus_name_;
  void settle_expected();
  bool on_master_row() const;
  std::map<std::string, std::vector<std::string>, std::less<>> running_;  // a slow command's senders, oldest first
  void note_running(const Action& action, const std::string& sender);

  // The editor block (D217).
  enum class EditorPart : std::uint8_t { save, check, reset, restore, reload, notes, draft, use_all, field, item, add,
                                         use, keep };
  std::map<std::string, EditorState, std::less<>> editors_;      // by the document's topic
  struct EditorSent {
    std::string topic;  // the document's
    std::string page;   // the page its editor is on
  };
  std::map<std::string, EditorSent, std::less<>> editor_sent_;  // a command's sender: what it was for
  struct EditorAt {
    const protocol::PageBlock* block = nullptr;
    std::string topic;
    EditorDocument doc;
    EditorState* state = nullptr;
    std::string eid;
  };
  // The editor a focus stop belongs to: its block, its document's topic, its document and its state; false if its topic
  // has published no document.
  bool editor_of(const Focus& f, EditorAt& out);
  Action editor_input(const Focus& f, const Input& in, bool& handled);
  Action editor_command(EditorAt& at, EditorPart part, const std::string& sender);
  void editor_set(EditorAt& at, const EditorField& field, protocol::Json value);
  static std::string editor_part_name(EditorPart part);
  std::string editor_hints(const Focus& f);  // the focused editor element's keys
  // An edit of an editor's text: a field, a list's item (item) or the draft's notes (an empty field), capped in
  // characters (0: none) as well as bytes.
  void start_editor_edit(EditKind kind, std::string id, std::string_view text, std::size_t chars,
                         const EditorAt& at, std::string field, std::size_t item, bool prose);
  std::vector<std::tuple<std::size_t, float, float>> prose_stops_;  // the edited prose's boundaries: offset, x, line top
  float prose_line_ = 0;                                            // its lines' height
  Rect caret_rect_{};
  Rect edit_box_{};                                         // the edited field, as the last layout drew it
  std::uint64_t edit_serial_ = 0;                           // the edits begun so far (D275)
  std::vector<std::pair<std::size_t, float>> edit_stops_;   // its drawn boundaries: a byte offset and its x
  std::string clipboard_;
  bool has_clipboard_ = false;
  // A report preview's Copy and Save (D263): "Copied" until its time, a Save awaiting its answer, and the words the
  // space answered a Save with, by "<topic>/copy" or "<topic>/save".
  std::map<std::string, std::int64_t, std::less<>> copied_until_;
  std::set<std::string, std::less<>> share_busy_;
  std::map<std::string, std::string, std::less<>> share_said_;
  std::map<std::string, std::string, std::less<>> share_refused_;  // a Save's refusal, kept across a republish

  protocol::SpacePages pages_;
  std::vector<protocol::Setting> settings_;
  std::map<std::string, std::size_t, std::less<>> by_key_;
  protocol::Json values_ = protocol::Json::object();
  std::map<std::string, std::string, std::less<>> refusals_;
  std::set<std::string, std::less<>> changed_;  // the space's own changed keys (D198)
  bool changed_known_ = false;
  std::map<std::string, protocol::InheritedValue, std::less<>> inherited_;
  std::map<std::string, protocol::Json, std::less<>> topics_;  // components' topic bodies (D181)
  std::map<std::string, std::string, std::less<>> greyed_;    // space.state's greyed pages and their words (D188)
  std::string inactive_;                                        // space.state's words for every page not named
  std::set<std::string, std::less<>> collapsed_;
  std::set<std::string, std::less<>> advanced_open_;
  std::size_t page_ = 0;
  std::size_t tab_ = 0;
  // A flow (D278): a button's press waiting for its answer, which one; an action that opens a page, by where its answer
  // goes ("action:<id>", or a section action's "<section>/<id>"): the page that sent it and the page it opens.
  // A flow's presses not yet answered, oldest first (D278): the page and step each was sent from, and its go.
  struct FlowPress {
    std::string page;
    std::string step;
    std::string go;
  };
  std::deque<FlowPress> flow_sent_;
  bool flow_waiting() const;                    // the newest press was on the step shown, and waits for its answer
  std::optional<FlowPress> flow_answered(std::string_view where);  // the press an answer is for, by its button
  void arrive();  // a page just opened: its flow's step, and what stayed with the last visit gone
  struct Opening {
    std::string from;
    std::string to;
  };
  std::map<std::string, Opening, std::less<>> opens_;
  std::size_t flow_step() const;                 // the open flow's step the space publishes, or its first
  bool flow_optional(std::size_t step) const;    // a step the flow may skip (never its last)
  std::string flow_blocked() const;              // the space's words for what Next waits for, or empty
  bool flow_follow(std::string_view topic);      // a flow's state came: its step shown; whether it moved
  Action flow_press(const Focus& f);
  void flow_done();                              // Done answered: the flow's `then`, or the space's first page
  std::vector<RailEntry> rail_;
  RailContext context_;
  std::vector<Focus> focus_;
  Scope scope_ = Scope::page;
  std::size_t rail_focus_ = 0;
  std::size_t page_focus_ = 0;
  std::string confirming_;  // a destructive action's id, waiting for its second press
  // Its second press counts from then (D274: confirm_after_ms, up to 3000): 0, at once.
  std::int64_t confirm_armed_at_ = 0;
  bool confirm_waiting_ = false;  // armed later than now as last ticked: tick() redraws it once it is
  // Asks for `name`'s second press, counting only after `after_ms` (D274).
  void ask(std::string name, std::optional<std::uint32_t> after_ms);
  // Whether the confirmation asked for counts a press now.
  bool armed() const noexcept { return confirm_armed_at_ == 0 || now_ms_ >= confirm_armed_at_; }

  enum class CaptureKind : std::uint8_t { none, key, chord };
  enum class CaptureStage : std::uint8_t { waiting, sent, asking };
  struct Capture {
    CaptureKind kind = CaptureKind::none;
    CaptureStage stage = CaptureStage::waiting;
    bool row = false;               // a row action's (Backspace: no key), or a setting's
    std::string command;            // the row action's command, or settings.set
    std::string key_field;          // the row action's key column, or "key"
    protocol::Json key_value;       // the row's key, or the setting's
    std::string title;              // "Change key: Tide Charts, Show the tide table"
    std::string words;              // a refusal's, or the question before a move
    protocol::Json value;           // what it sends: a key's name, or a chord
    std::size_t anchor = 0;         // the chord's pickers: indexes into the fourteen buttons
    std::size_t key = 12;
    std::size_t picker = 0;         // which of the two is focused
  };
  Capture capture_;
  Sheet sheet_;
  bool sheet_open_ = false;
  Rect sheet_close_{};  // its Close, as the last layout drew it
  Action capture_command(bool confirm) const;
  Action capture_input(const Input& in);
  float scroll_ = 0;
  bool follow_ = true;          // keep focus in view; false once the wheel has scrolled, until a key moves focus
  float scale_ = 1;             // the last layout's effective scale
  std::vector<Hit> hits_;       // the last layout's
  bool pointer_in_ = false;     // the pointer is over the menu: hover tints
  bool focus_shown_ = true;     // the focus ring is drawn (G42)
  bool pad_mode_ = false;       // the key hints as the pad's buttons (D242)
  bool hints_shown_ = true;     // the key hints drawn (View, D242)
  std::string footer_;          // the footer's words as the last layout drew them
  float pointer_x_ = 0;
  float pointer_y_ = 0;
  std::string drag_;            // a slider's key while its button is held
  Rect drag_track_{};
  std::set<std::string, std::less<>> absent_;         // pages of a later increment (set_increment)
  protocol::Json null_;                               // a key the space does not have
  std::vector<protocol::PageSection> no_sections_;    // a page with neither tabs nor sections

  // The top bar's stops and the Quick switches menu (D252): the focused stop's number (kTopSearch, kTopLive or
  // kTopQuick, also its hit's index; a quick row's hit is kQuickRow and on), the menu's focused row.
  static constexpr std::size_t kTopSearch = 0;
  static constexpr std::size_t kTopLive = 1000;
  static constexpr std::size_t kTopQuick = 1001;
  static constexpr std::size_t kQuickRow = 2000;
  std::vector<std::size_t> top_stops() const;
  // A page's master list (D215): its name in focus ids (its topic, or for a list read a page at a time its page's id
  // and ".list"), its topic as watched (none for such a list), and its rows' body, wherever they come from (D254).
  std::string master_name(const protocol::PageMaster& m) const;
  struct Paged {  // a table's or a master list's rows read a page at a time (D254), by its key
    protocol::Json body = protocol::Json::object({{"columns", protocol::Json::array()}, {"rows", protocol::Json::array()}});
    bool answered = false;   // a page has come
    std::string signature;   // the args it was last asked with, the cursor aside: a change asks for the first page
    std::uint64_t serial = 0;
    bool loading = false;
    bool appending = false;  // the request in flight is Show more's
    bool more = false;       // Show more pressed
    std::string next;        // the space's cursor for the next page
    std::optional<double> total;
    std::string refused;     // the space's words, in place of rows
    bool fresh = false;      // new rows came past the first page: "New rows · Show"
    int pages = 0;
  };
  std::optional<bool> header_state(const protocol::PageAction& a) const;  // a header switch's state (D256)
  static bool has_drawer(const protocol::Json& table);  // a table with drawer columns (D256)
  static bool row_has_drawer(const protocol::Json& table, const protocol::Json& row);  // ... one it fills (D262)
  static bool is_stream(const protocol::PageBlock& block);  // a live feed or a log tail (D257)
  static std::string drawer_key(const protocol::Json& row, std::size_t index);
  std::map<std::string, std::string, std::less<>> drawer_open_;  // a table's open row drawer, by its key (D256)
  std::map<std::string, Paged, std::less<>> paged_;
  std::uint64_t page_serial_ = 0;  // every page asked for, across pages and the menu's closes: never reset
  struct ScopeChoice {
    std::string id;
    std::string label;
    bool read_only = false;
  };
  std::vector<ScopeChoice> scope_choices() const;
  std::string scope_choice_;  // the player's choice; empty: the space's current
  bool is_family(std::string_view topic) const;
  // The open page's page sources (D254), each with its key: a table's cell_base, a master list's master_name.
  struct Source {
    std::string key;
    const protocol::PageSource* source = nullptr;
    const protocol::FilterBar* bar = nullptr;
    std::string bar_key;  // its filter bar's state's key (a master list's "master:<name>")
  };
  std::vector<Source> page_sources() const;
  protocol::Json page_args(const Source& source) const;
  const Paged* paged_of(std::string_view key) const;
  void master_changed();  // after its list's rows change: a chosen row it no longer has let go, the first chosen
  static constexpr std::size_t kTopScope = 1002;
  std::string master_topic() const;
  const protocol::Json* master_body() const;
  bool has_quick() const noexcept { return pages_.quick && !pages_.quick->empty(); }
  Action quick_input(const Input& in);
  // Whether a held direction's repeat acts where focus is (D270): along the list focus is in (the rail, the top bar's
  // stops, a page's stops, a master's list, the Quick switches, a field's caret), and a slider's or a number field's
  // next step. Never across into another part of the menu, nor a link followed, a switch turned, a choice's next, a tab
  // or a page turned, a modal's key or a confirmation let go: those take a fresh press. Any other key's repeat: false.
  bool repeat_acts(const Input& in);
  // Where focus is, as far as a held direction's repeats go (D270): the rail, the top bar or the page; the page and its
  // tab; a master's list or the rest of the page; a field editing; the Quick switches or a modal open.
  struct Region {
    Scope scope;
    std::size_t page;
    std::size_t tab;
    bool list;
    bool editing;
    bool quick;
    bool modal;
    bool operator==(const Region&) const = default;
  };
  Region region() const;
  bool of_master_list(const protocol::PageMaster& m, const Focus& one) const;  // a list's rows and paging stops
  bool repeat_stays_ = false;  // the held press's repeats may act: the last fresh press left focus where it was
  Action flip_quick(std::size_t index, int step);
  const protocol::Json* latest(std::string_view topic) const;  // a held body first
  bool hold(std::string_view topic);
  void release_held();
  void note_sent(const Action& action);
  std::size_t top_focus_ = kTopSearch;
  bool quick_open_ = false;
  std::size_t quick_focus_ = 0;
  LiveMode live_mode_ = LiveMode::live;
  std::map<std::string, protocol::Json, std::less<>> pending_;  // held bodies, the newest of each topic
  std::size_t held_new_ = 0;
  bool releasing_ = false;
  std::int64_t now_ms_ = 0;
  std::int64_t applied_ms_ = 0;    // when Every 5 s last applied
  std::int64_t release_until_ = 0; // the player's command: nothing held until then
};

}  // namespace grace::view
