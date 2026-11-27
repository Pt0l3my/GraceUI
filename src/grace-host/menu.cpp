// menu.cpp: the in-game menu (menu.h; §5.1, D79, D115, D167).
//
// Three threads touch it, and none waits for another: the window procedure pushes keys and the present thread the pad's
// new presses, each into a ring of its own (one producer each); GraceUI's own thread, woken by either, pops them, runs
// the page view and GraceUI's settings store, lays the page out and tessellates it into the back list of a draw list's
// triple buffer, and publishes it; the present thread takes the newest list for the holder of its generation. The
// holder itself is grace-input's (D77): the menu asks for input as any holder does, and the one close routine ends it,
// whoever ends it; the menu's own thread learns of a close at its next wake.
#include "menu.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <set>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "draw_atlas.h"
#include "grace/abi/drawlist.h"
#include "grace/abi/registry.h"
#include "grace/draw/tessellate.h"
#include "grace/input/devices.h"
#include "grace/input/queue.h"
#include "grace/pages/settings_store.h"
#include "grace/spaces/own_space.h"
#include "grace/spaces/registry.h"
#include "grace/tokens.h"
#include "grace/view/page_view.h"
#include "hooks.h"
#include "input.h"
#include "log.h"
#include "session.h"
#include "spaces.h"

namespace grace::host::menu {
namespace {

using grace::input::Kind;
using grace::input::Phase;
using grace::view::Key;

// The menu is the holder of kind menu that GraceUI itself asked for (owner 0); a test's holder of that kind is not.
constexpr bool ours(const grace::input::Holder& holder) noexcept {
  return holder.kind == Kind::menu && holder.owner == 0 && holder.phase == Phase::active;
}

struct MenuInput {
  Key key = Key::down;
  std::uint8_t shift = 0;
  std::uint32_t generation = 0;  // the holder it is for
  grace::input::Key captured = grace::input::kNoKey;  // while a capture waits (D209): the key pressed, whole
  std::uint8_t modifiers = 0;                         // and its Ctrl, Shift and Alt (GRACEUI_MOD_*)
  // While a field edits (G38 (a)): one of its keys (an EditKey, plus 1), a character typed, or the clipboard's text,
  // which waits in g_paste.
  std::uint8_t edit = 0;
  char32_t character = 0;
  bool paste = false;
  bool pad = false;  // the present thread's, from the pad: the key hints follow it (D242)
  bool repeat = false;  // a held key's or direction's repeat (D270)
};

// Whether the menu's view waits for a key to capture (D209): set on GraceUI's thread after each input, read by the
// window procedure's key_down.
constinit std::atomic<bool> g_awaiting_key{false};
// Whether one of its fields edits (G38 (a)), likewise: its keys then go to the field, and characters to the menu.
constinit std::atomic<bool> g_editing{false};
// The window procedure's thread alone (key_down, then character, in message order): whether the key last pressed went
// down while a field edited, and whether it was Space taken as the menu's own press (D270).
bool g_pressed_in_field = false;
bool g_pressed_space = false;
constinit std::atomic<bool> g_timer{false};  // Live updates holds bodies Every 5 s (D252): wake once a second
constinit std::atomic<std::int64_t> g_due{0};  // and the open page's next deadline, sooner (D274): 0, none
// The clipboard's text for a paste, read on the window procedure's thread at Ctrl+V and taken on GraceUI's.
constinit grace::abi::SpinLock g_paste_lock;
constinit std::string* g_paste = nullptr;  // made at the first paste, never freed

constinit grace::input::SpscRing<MenuInput, 64> g_keys;  // the window procedure's
constinit grace::input::SpscRing<MenuInput, 64> g_pad;   // the present thread's

// The mouse's events for the menu (§5.5), the present thread's: a move a frame at most, in the display's pixels.
struct MenuPointer {
  grace::view::Pointer pointer{};
  std::uint32_t generation = 0;  // the holder it is for
};
constinit grace::input::SpscRing<MenuPointer, 64> g_pointer;
constinit std::atomic<grace::abi::DrawListBuffers*> g_lists{nullptr};  // made by GraceUI's thread, never freed
constinit std::atomic<bool> g_off{false};
constinit std::atomic<std::uint32_t> g_width{0};
constinit std::atomic<std::uint32_t> g_height{0};
constinit std::atomic<bool> g_clear_theme{false};  // ui.menu.theme is Grace Clear: the pointer's colours (G31)
constinit std::atomic<float> g_ui_scale{0.0f};  // ui.scale as the store holds it, for the next bake's scale; 0 unread
constinit std::atomic<int> g_plain_text{-1};     // ui.menu.plain_text as the store holds it (D267): 1, 0, or -1 unread
constinit std::atomic<bool> g_reduce_motion{false};  // ui.menu.reduce_motion as the store holds it: no fades (D271)
// The present thread's fades, for the test build (D271): frames drawn fading in, and fading out after a close; the
// last frame's alpha, in thousandths.
constinit std::atomic<std::uint32_t> g_fade_in_frames{0};
constinit std::atomic<std::uint32_t> g_fade_out_frames{0};
constinit std::atomic<std::uint32_t> g_alpha_milli{0};
constinit std::atomic<std::uint32_t> g_body_fade_frames{0};  // frames drawn while a page's body faded in (D272)

// The test exports' view (and nothing else reads it).
constinit std::atomic<std::uint64_t> g_published{0};
constinit std::atomic<std::uint64_t> g_frames{0};
constinit std::atomic<std::uint32_t> g_inputs{0};
constinit std::atomic<std::uint32_t> g_refusals{0};
constinit std::atomic<std::uint32_t> g_layout_us{0};
constinit std::atomic<std::uint32_t> g_vertices{0};
constinit std::atomic<std::uint32_t> g_indices{0};
constinit std::atomic<std::uint32_t> g_commands{0};
constinit std::atomic<float> g_scale{0.0f};

// The present thread's: the pad's buttons as last seen for the holder, so that only a new press counts, and the press
// that was held when the menu opened does nothing until it is released (§5.2).
struct PadSeen {
  std::uint32_t generation;
  std::uint16_t buttons;
  bool left_trigger;
  bool right_trigger;
  std::uint8_t directions;        // up 1, down 2, left 4, right 8: the d-pad's, or the left stick past half its travel
  std::int8_t scroll = 0;         // the right stick held up (1) or down (-1) past half its travel (D242)
  std::int64_t scroll_next = 0;   // when its next step is due (GetTickCount64)
  std::uint8_t repeat = 0;        // the direction that repeats while held, the last pressed (D270); 0, none
  std::int64_t repeat_next = 0;   // when its next repeat is due
};
constinit PadSeen g_pad_seen{};

constexpr float kStickPress = 16384.0f;  // half the left stick's travel: a press of the d-pad (EST)
constexpr float kStickFull = 29490.0f;   // nine tenths of a stick's travel: a stick's faster steps (EST)
// A held direction's repeat (D270): the first after 400 ms, then one every 80 ms, 40 with the left stick past nine
// tenths of its travel (EST: near a held arrow key's, under Windows' default delay; slower, as a pad's is).
constexpr std::int64_t kRepeatDelayMs = 400;
constexpr std::int64_t kRepeatEveryMs = 80;
constexpr std::int64_t kRepeatFastMs = 40;

// A space the menu shows (D79, D171): GraceUI's own, or a mod's that asked for the game's view.
struct SpaceView {
  std::string id;
  std::string name;
  bool own = false;
  grace::view::PageView view;
  std::vector<grace::view::RailLink> categories;  // its categories, as links to their first pages
  std::uint64_t settings_rev = 0;                 // its settings topic's, as the menu last took it
  grace::protocol::Json values = grace::protocol::Json::object();  // its values, as its settings state last gave them
  bool watching = false;                          // the menu watches its settings topic (while it is open on it)
  // Its open page's component topics the menu draws (D184), watched while the page is open, and their last rev.
  std::map<std::string, std::uint64_t, std::less<>> topics;
};

// GraceUI's thread's: the spaces' page views, GraceUI's settings and what was laid out last.
struct State {
  bool loaded = false;
  bool open = false;
  std::uint32_t generation = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t serial = 0;
  std::string shown_page;          // the space and page the last list showed ("space\npage")
  std::uint64_t page_serial = 0;   // counted up at each change of them: the present thread fades the body in (D272)
  double ui_scale = 1.0;
  bool plain = false;  // ui.menu.plain_text: the serif and display roles in Noto Sans (§3.4, D267)
  bool clear = false;  // ui.menu.theme is Grace Clear
  std::vector<std::unique_ptr<SpaceView>> spaces;  // the mods' first, in the order they registered, GraceUI's last
  std::size_t active = 0;                          // the space whose page is open
  std::string last_content;                        // the mod's space last open: its wordmark heads GraceUI's rail
  std::uint64_t spaces_rev = 0;                    // the registry's, as the menu last took it
  bool ever_opened = false;
  std::uint32_t next_command = 1;
  std::map<std::uint64_t, std::pair<std::string, std::string>> commands;  // a ticket: its space and its key
  std::map<std::uint64_t, std::string> command_names;  // a ticket: its command's name, for a slow one's sender (D216)
  std::set<std::uint64_t> captures;  // the tickets the capture's modal sent: their answers go back to it (D209)
  bool pairing = false;                 // Pair a device's sheet is open (G9)
  bool away = false;                    // let go of input at an Alt+Tab, kept for the focus's return (G40)
  bool focus_shown = false;             // the focus ring drawn: after a key or the pad, until the mouse (G42)
  bool focus_drawn = false;             // the last published layout's, under g_view_lock (the tests')
  bool pad_mode = false;                // the key hints as the pad's: the last device used (D242)
  bool hints_shown = true;              // the key hints drawn: View on a pad turns them off and on (D242)
  bool pad_drawn = false;               // the last published layout's, under g_view_lock (the tests')
  bool hints_drawn = true;
  std::string footer;                   // its footer's words
  grace::view::PageView::Sheet sheet;   // as last shown
  std::uint64_t revision = 0;  // GraceUI's own space's, as the menu last took it
  std::uint64_t followed = ~std::uint64_t{0};  // and as the present thread's settings last followed it (D273)
  std::uint64_t states = 0;    // its other topics', as the menu last took them (D181)
  grace::view::DisplayList list;
  std::string page;  // as the test exports read it, under its lock
  std::string focused;
#if defined(GRACEUI_TEST_EXPORTS)
  std::vector<std::pair<std::string, grace::view::PageView::Bounds>> bounds;  // the last layout's, under the same lock
  std::string shown;  // the last layout's words, one text after another, under the same lock
  // Each space's pages at the last layout, "<space>\n<page>", as present (true) or absent (D269), under the same lock.
  std::map<std::string, bool, std::less<>> pages_present;
#endif
};
constinit State* g_state = nullptr;
constinit grace::abi::SpinLock g_view_lock;  // the test exports' copy of page and focused

std::int64_t qpc() noexcept {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return now.QuadPart;
}

std::uint32_t micros_since(std::int64_t start) noexcept {
  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  return frequency.QuadPart > 0 ? static_cast<std::uint32_t>((qpc() - start) * 1000000 / frequency.QuadPart) : 0;
}

// GraceUI's own space (D115), shared with the browser's views through one settings store (D169).
grace::spaces::OwnSpace& own_space() {
  grace::spaces::OwnSpace* own = spaces::own();
  if (own == nullptr) {
    throw std::runtime_error("GraceUI's own space does not load");
  }
  return *own;
}

grace::view::PageView& active_view(State& m) { return m.spaces[m.active]->view; }
SpaceView& own_view(State& m) { return *m.spaces.back(); }

// A space's categories as links to their first pages, in the increment the space runs (D173, D245): a category with
// no page present is left out, as the space's own rail leaves it out, and each links to its first page present.
std::vector<grace::view::RailLink> category_links(const grace::protocol::SpacePages& pages,
                                                  const grace::view::PageView& view) {
  std::vector<grace::view::RailLink> links;
  for (const grace::protocol::Category& category : pages.categories) {
    for (const std::string& page : category.pages) {
      if (view.present(page)) {
        links.push_back(grace::view::RailLink{category.name, pages.space, page, true, category.foot.value_or(false)});
        break;
      }
    }
  }
  return links;
}

void link_search(State& m);

void load(State& m) {
  grace::spaces::OwnSpace& own = own_space();
  auto view = std::make_unique<SpaceView>();
  view->id = own.pages().space;
  view->name = own.pages().name;
  view->own = true;
  std::string why;
  if (!view->view.load(own.pages(), own.settings(), why)) {
    throw std::runtime_error("GraceUI's own space does not load");
  }
  view->view.set_increment(spaces::increment_text());  // its later pages absent (§4.2.2, D173)
  view->categories = category_links(own.pages(), view->view);
  if (!view->view.open("graceui.menu")) {
    throw std::runtime_error("GraceUI's own space has no Menu page");
  }
  m.spaces.push_back(std::move(view));
  m.active = 0;
  m.loaded = true;
  link_search(m);
}

// The rails around each space (D79): a mod's space with GraceUI's at its foot, and from two mods' spaces a switcher;
// GraceUI's own under the wordmark and the categories of the mod's space last open, those of its foot at the foot
// (D248).
void update_rails(State& m) {
  std::vector<SpaceView*> content;
  for (auto& space : m.spaces) {
    if (!space->own) {
      content.push_back(space.get());
    }
  }
  const std::size_t n = content.size();
  for (std::size_t i = 0; i < n; ++i) {
    grace::view::RailContext context;
    context.foot = own_view(m).categories;
    if (n >= 2) {
      const SpaceView& previous = *content[(i + n - 1) % n];
      const SpaceView& next = *content[(i + 1) % n];
      context.switcher = content[i]->name + ", " + std::to_string(i + 1) + " of " + std::to_string(n);
      context.previous = grace::view::RailLink{previous.name, previous.id, previous.categories.empty()
                                                                              ? std::string()
                                                                              : previous.categories[0].page, true};
      context.next = grace::view::RailLink{next.name, next.id, next.categories.empty() ? std::string()
                                                                                       : next.categories[0].page, true};
    }
    content[i]->view.set_rail(std::move(context));
  }
  grace::view::RailContext own_context;
  if (n != 0) {
    const SpaceView* last = content[0];
    for (SpaceView* space : content) {
      if (space->id == m.last_content) {
        last = space;
      }
    }
    own_context.title = last->name;
    for (const grace::view::RailLink& link : last->categories) {  // its foot's at GraceUI's foot too (D248)
      (link.foot ? own_context.foot : own_context.head).push_back(link);
    }
  }
  own_view(m).view.set_rail(std::move(own_context));
}

// The open page's component topics (D184): watched while the page is open and the menu watches its space, one watch a
// topic, given up when the page or the space changes or the menu closes. True if any watch changed.
bool watch_topics(State& m) {
  SpaceView& space = *m.spaces[m.active];
  if (space.own) {
    return false;
  }
  std::vector<std::string> wanted;
  if (space.watching) {
    wanted = space.view.component_topics();
  }
  bool changed = false;
  for (auto it = space.topics.begin(); it != space.topics.end();) {
    if (std::find(wanted.begin(), wanted.end(), it->first) == wanted.end()) {
      spaces::registry().game_watch(space.id, it->first, false);
      it = space.topics.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  for (const std::string& topic : wanted) {
    if (space.topics.emplace(topic, 0).second) {
      spaces::registry().game_watch(space.id, topic, true);
      changed = true;
    }
  }
  return changed;
}

void watch(State& m, bool on) {
  SpaceView& space = *m.spaces[m.active];
  if (space.own || space.watching == on) {
    return;
  }
  spaces::registry().game_watch(space.id, "settings", on);
  space.watching = on;
  if (on) {
    space.settings_rev = 0;  // the newest state again, whatever came before
  }
  watch_topics(m);  // its open page's components with it
}

// Search across spaces (D219): each space's view finds every other space's settings too, linked anew whenever the
// spaces change, so that no view keeps a link to one that has gone.
void link_search(State& m) {
  for (std::size_t i = 0; i < m.spaces.size(); ++i) {
    std::vector<grace::view::PageView::SearchOther> others;
    for (std::size_t j = 0; j < m.spaces.size(); ++j) {
      if (j != i) {
        others.push_back({m.spaces[j]->id, m.spaces[j]->name, &m.spaces[j]->view});
      }
    }
    m.spaces[i]->view.set_search_others(std::move(others));
  }
}

// The spaces that asked for the game's view, as the registry has them now: a new one gets a page view, open on its
// first page; one that has gone is dropped, and if it was open, GraceUI's own is (D171).
bool sync_spaces(State& m) {
  const std::uint64_t revision = spaces::registry().spaces_revision();
  if (revision == m.spaces_rev) {
    return false;
  }
  m.spaces_rev = revision;
  const std::string active_id = m.spaces[m.active]->id;
  std::vector<std::unique_ptr<SpaceView>> next;
  for (const auto& listed : spaces::registry().game_spaces()) {
    std::unique_ptr<SpaceView> kept;
    for (auto& space : m.spaces) {
      if (space != nullptr && !space->own && space->id == listed.id) {
        kept = std::move(space);
      }
    }
    if (kept == nullptr) {
      kept = std::make_unique<SpaceView>();
      kept->id = listed.id;
      kept->name = listed.name;
      std::string why;
      if (kept->view.load(*listed.pages, *listed.settings, why)) {
        kept->view.set_increment(listed.increment);  // its later pages absent (§4.2.2, D173)
      }
      kept->categories = category_links(*listed.pages, kept->view);
      const std::string page = kept->categories.empty() ? std::string() : kept->categories[0].page;
      if (!why.empty() || !kept->view.open(page)) {
        log_line("the menu cannot show %s's space: %s", listed.id.c_str(), why.c_str());
        continue;
      }
    }
    next.push_back(std::move(kept));
  }
  next.push_back(std::move(m.spaces.back()));  // GraceUI's own, at the foot
  m.spaces = std::move(next);
  m.active = m.spaces.size() - 1;
  for (std::size_t i = 0; i < m.spaces.size(); ++i) {
    if (m.spaces[i]->id == active_id) {
      m.active = i;
    }
  }
  link_search(m);
  update_rails(m);
  return true;
}

// Opens `page` of the space `id`. From a link on another space's rail, focus stays on the rail: on the switcher if it
// was the switcher, else on the page's entry, as opening a category leaves it.
enum class From : std::uint8_t { open, link, switcher };
void activate(State& m, const std::string& id, const std::string& page, From from) {
  for (std::size_t i = 0; i < m.spaces.size(); ++i) {
    if (m.spaces[i]->id != id) {
      continue;
    }
    watch(m, false);
    m.active = i;
    m.spaces[i]->view.stop_repeats();  // a key held before the switch does nothing in the space it lands in (D270)
    if (!page.empty()) {
      m.spaces[i]->view.open(page);
    }
    if (!m.spaces[i]->own) {
      m.last_content = id;
    }
    update_rails(m);
    if (from != From::open) {
      m.spaces[i]->view.focus_rail(from == From::switcher);
    }
    if (m.open) {
      watch(m, true);
    }
    return;
  }
}

// A refusal in words, as §6.7's table gives them and the browser view writes them (refusalWords), when the space gave
// none of its own.
const char* refusal_words(grace::protocol::ErrorCode code) noexcept {
  using grace::protocol::ErrorCode;
  switch (code) {
    case ErrorCode::unknown_topic:
    case ErrorCode::unknown_command:
    case ErrorCode::unknown_key: return "Not available in this version.";
    case ErrorCode::invalid_value: return "That value is not allowed here.";
    case ErrorCode::locked: return "Locked in this version.";
    case ErrorCode::not_now: return "Not possible right now.";
    case ErrorCode::busy: return "Already running.";
    case ErrorCode::confirm_required: return "This needs a confirmation first.";
    case ErrorCode::rate_limited: return "Too many changes at once: try again in a moment.";
    case ErrorCode::conflict: return "Changed elsewhere since you opened it.";
    case ErrorCode::unauthorised:
    case ErrorCode::version:
    case ErrorCode::bad_message:
    case ErrorCode::internal: break;
  }
  return "Something went wrong; the log has the details.";
}

// What the menu watches came back: the open mod's space's settings (its values, what its inherit tri-states resolve
// to), and the results of the menu's commands to mods' spaces (a refusal under its row, as GraceUI's own are).
bool take_from_spaces(State& m) {
  bool changed = false;
  SpaceView& space = *m.spaces[m.active];
  grace::protocol::Json body;
  std::uint64_t rev = 0;
  if (!space.own && space.watching &&
      spaces::registry().game_state(space.id, "settings", space.settings_rev, body, rev)) {
    space.settings_rev = rev;
    if (body.is_object() && body.contains("values") && body["values"].is_object()) {
      space.values = body["values"];
      space.view.set_values(space.values);
      // A page shown with a switch comes and goes with it (D258): GraceUI's rail's links to the space's categories,
      // and the switcher's, are worked out again with the values (D269's review).
      space.categories = category_links(space.view.space_pages(), space.view);
      update_rails(m);
    }
    if (body.is_object() && body.contains("changed") && body["changed"].is_array()) {
      std::vector<std::string> keys;  // the rows the space says are changed (D198)
      for (const grace::protocol::Json& key : body["changed"]) {
        if (key.is_string()) {
          keys.push_back(key.get<std::string>());
        }
      }
      space.view.set_changed(keys);
    }
    if (body.is_object() && body.contains("inherited") && body["inherited"].is_array()) {
      std::vector<grace::protocol::InheritedValue> inherited;
      for (const grace::protocol::Json& item : body["inherited"]) {
        grace::protocol::InheritedValue value;
        grace::protocol::DecodeError error;
        if (decode(item, value, "", error)) {
          inherited.push_back(std::move(value));
        }
      }
      space.view.set_inherited(inherited);
    }
    changed = true;
  }
  for (auto& [topic, seen] : space.topics) {  // its open page's components (D184)
    if (!space.own && spaces::registry().game_state(space.id, topic, seen, body, rev)) {
      seen = rev;
      space.view.set_topic(topic, body);
      changed = true;
    }
  }
  for (const auto& result : spaces::registry().take_game_results()) {
    const auto it = m.commands.find(result.ticket);
    if (it == m.commands.end()) {
      continue;
    }
    if (const auto named = m.command_names.find(result.ticket); named != m.command_names.end()) {
      for (auto& view : m.spaces) {
        if (view->id == it->second.first) {
          view->view.finished(named->second);  // a slow command's control takes presses again (D216)
        }
      }
      m.command_names.erase(named);
    }
    if (m.captures.erase(result.ticket) != 0) {
      for (auto& view : m.spaces) {
        if (view->id == it->second.first) {
          const bool refused = result.result.error.has_value();
          std::string words = refused ? refusal_words(*result.result.error) : std::string();
          if (refused && result.result.detail.has_value() && !result.result.detail->empty()) {
            words = *result.result.detail;
          }
          view->view.capture_answered(!refused,
                                      refused && *result.result.error == grace::protocol::ErrorCode::confirm_required,
                                      words);
        }
      }
      m.commands.erase(it);
      changed = true;
      continue;
    }
    if (it->second.second.rfind("page:", 0) == 0) {
      // A page of rows (D254): its answer, rows and all, goes back to the list or table that asked, which drops it if
      // it has asked again since.
      for (auto& view : m.spaces) {
        if (view->id == it->second.first) {
          grace::protocol::Result answer = result.result;
          if (answer.error.has_value() && (!answer.detail.has_value() || answer.detail->empty())) {
            answer.detail = refusal_words(*answer.error);  // the view's own words where the space gave none
          }
          view->view.page_answer(it->second.second, answer);
        }
      }
      m.commands.erase(it);
      changed = true;
      continue;
    }
    if (it->second.second.rfind("editor:", 0) == 0) {
      // An editor's command (D217): its answer, body and all, goes to the element that sent it, whatever is open.
      if (result.result.error.has_value()) {
        g_refusals.fetch_add(1, std::memory_order_relaxed);
      }
      for (auto& view : m.spaces) {
        if (view->id == it->second.first) {
          grace::protocol::Result answer = result.result;
          const bool messages = answer.body.has_value() && answer.body->is_object() && answer.body->contains("messages");
          if (answer.error.has_value() && (!answer.detail.has_value() || answer.detail->empty()) && !messages) {
            answer.detail = refusal_words(*answer.error);  // the view's own words where the space gave none
          }
          view->view.editor_answer(it->second.second, answer);
        }
      }
      m.commands.erase(it);
      changed = true;
      continue;
    }
    if (result.result.error.has_value()) {
      g_refusals.fetch_add(1, std::memory_order_relaxed);
      std::string detail = refusal_words(*result.result.error);
      if (result.result.detail.has_value() && !result.result.detail->empty()) {
        detail = *result.result.detail;  // the space's own words (protocol 1.1)
      }
      for (auto& view : m.spaces) {
        if (view->id == it->second.first && !it->second.second.empty()) {
          const std::string& key = it->second.second;
          view->view.refused(key, detail);
          view->view.restore(key, view->values.contains(key) ? view->values[key] : grace::protocol::Json());
        }
      }
    } else {
      for (auto& view : m.spaces) {
        if (view->id == it->second.first && !it->second.second.empty()) {
          // A form the space took: its fields empty (D195); a report's Save: the space's words under it (D263).
          view->view.accepted(it->second.second, result.result.detail.value_or(std::string()));
        }
      }
    }
    m.commands.erase(it);
    changed = true;
  }
  return changed;
}

// GraceUI's own settings that the present thread and the test build read (the scale, the theme, Plain text, Reduce
// motion), as the store holds them now, whether the menu is open or not (D273): read at GraceUI's thread's first wake
// and after every change, a browser's while the menu is closed among them, as the mods hear of them.
void follow_settings(State& m) {
  try {
    const std::uint64_t revision = own_space().revision();
    if (revision == m.followed) {
      return;
    }
    const grace::pages::SettingsStore& store = own_space().store();
    const grace::protocol::Json& scale = store.held("ui.scale");
    g_ui_scale.store(scale.is_number() ? static_cast<float>(scale.get<double>()) : 1.0f, std::memory_order_relaxed);
    g_clear_theme.store(store.held("ui.menu.theme") == "clear", std::memory_order_relaxed);
    g_plain_text.store(store.held("ui.menu.plain_text") == true ? 1 : 0, std::memory_order_relaxed);
    g_reduce_motion.store(store.held("ui.menu.reduce_motion") == true, std::memory_order_relaxed);
    m.followed = revision;
  } catch (...) {
    // GraceUI's own space not loaded yet: followed at a later wake
  }
}

// What a changed setting does at once (its effect "now"): the scale, the theme. The rest are kept for their users.
void take_effects(State& m) {
  const grace::pages::SettingsStore& store = own_space().store();
  const grace::protocol::Json& scale = store.held("ui.scale");
  m.ui_scale = scale.is_number() ? scale.get<double>() : 1.0;
  g_ui_scale.store(static_cast<float>(m.ui_scale), std::memory_order_relaxed);
  m.clear = store.held("ui.menu.theme") == "clear";
  g_clear_theme.store(m.clear, std::memory_order_relaxed);
  m.plain = store.held("ui.menu.plain_text") == true;
  g_plain_text.store(m.plain ? 1 : 0, std::memory_order_relaxed);
  g_reduce_motion.store(store.held("ui.menu.reduce_motion") == true, std::memory_order_relaxed);
  own_view(m).view.set_values(store.values());
  m.revision = own_space().revision();
  spaces::publish_ui_scale(own_space());  // the mods hear of a new ui.scale (D180)
  spaces::publish_menu_key(own_space());  // a new menu key works at once (D208)
  spaces::keep_own_settings();  // for the next session (D179): only after a change
}

// GraceUI's own topics other than its settings (the Mods page's, D181), as its space last published them, to the menu's
// view of it. True if any changed.
bool take_topics(State& m) {
  const grace::spaces::OwnSpace& own = own_space();
  if (m.states == own.states_revision()) {
    return false;
  }
  m.states = own.states_revision();
  for (const grace::protocol::TopicInfo& topic : own.pages().topics) {
    if (const grace::protocol::Json* body = own.state_of(topic.id); body != nullptr) {
      own_view(m).view.set_topic(topic.id, *body);
    }
  }
  return true;
}

std::string key_of(const grace::protocol::Cmd& command) {
  const grace::protocol::Json& args = command.args;
  for (const char* field : {"key", "target"}) {
    if (args.is_object() && args.contains(field) && args[field].is_string()) {
      return args[field].get<std::string>();
    }
  }
  return {};
}

// Pair a device's sheet (G9, §7.2 item 16): the code and where to enter it, then the device paired, or why it ended.
// The code and the address are shown here alone, in game: never on a page, in a topic or in the log (D66, PV3).
grace::view::PageView::Sheet pairing_sheet(const spaces::PairingView& view) {
  grace::view::PageView::Sheet sheet;
  switch (view.state) {
    case spaces::PairingView::State::showing: {
      sheet.title = "Pair a device";
      sheet.figure = view.code.size() == 6 ? view.code.substr(0, 3) + " " + view.code.substr(3) : view.code;
      sheet.lines.push_back(view.addresses.size() == 1
                                ? "On the phone or computer, open this address in its browser:"
                                : "On the phone or computer, open one of these addresses in its browser:");
      for (const std::string& address : view.addresses) {
        sheet.lines.push_back(address);
      }
      char left[16] = {};
      std::snprintf(left, sizeof left, "%u:%02u", view.seconds / 60, view.seconds % 60);
      sheet.lines.push_back("Enter this code there, with a name for the device. It works once, for " + std::string(left) +
                            " more, while this stays open.");
      break;
    }
    case spaces::PairingView::State::paired:
      sheet.title = "Paired";
      sheet.lines.push_back(view.paired_name + " is paired, and opens the browser view from now on.");
      sheet.lines.push_back("It is listed under Other devices, where Forget revokes it.");
      break;
    case spaces::PairingView::State::ended:
      sheet.title = "The code has ended";
      sheet.lines.push_back(view.why);
      sheet.lines.push_back("Close this, and choose Pair a device again for a new code.");
      break;
    case spaces::PairingView::State::none:
      break;
  }
  return sheet;
}

// The sheet as it should be now: shown again when its words changed (each second, while the code shows). True if so.
bool refresh_pairing(State& m) {
  if (!m.pairing) {
    return false;
  }
  if (!own_view(m).view.sheet_open()) {
    m.pairing = false;  // closed by the view itself: the code goes with it
    spaces::cancel_pairing();
    return true;
  }
  grace::view::PageView::Sheet sheet = pairing_sheet(spaces::pairing_view());
  if (sheet == m.sheet) {
    return false;
  }
  m.sheet = sheet;
  own_view(m).view.show_sheet(std::move(sheet));
  return true;
}

void end_pairing(State& m) {
  if (m.pairing) {
    spaces::cancel_pairing();  // void at once (D113)
    own_view(m).view.close_sheet();
    m.pairing = false;
    m.sheet = {};
  }
}

// A command from the page view: GraceUI's own space answers it here, at its one entry point (invariant 3); a mod's
// space's goes to the mod, as a view's command does, and its result comes back to the menu (D171).
void run(State& m, const grace::protocol::Cmd& command) {
  if (command.name == "menu.close") {
    input::close_holder(grace::input::Closed::released, m.generation);
    return;
  }
  SpaceView& space = *m.spaces[m.active];
  if (!space.own) {
    grace::protocol::Cmd sent = command;
    sent.space = space.id;
    sent.id = m.next_command++;
    // Where its answer's words go: the row of a table's switch or row action that sent it (D181, D192), else the
    // setting it names.
    const std::string focused = space.view.focused();
    const std::string where = focused.rfind("cell:", 0) == 0             ? focused.substr(5)
                              : focused.rfind("row:", 0) == 0            ? focused.substr(4)
                              : focused.rfind("section-action:", 0) == 0 ? focused.substr(15)
                              : focused.rfind("submit:", 0) == 0         ? focused.substr(7)  // a form's (D195)
                              : focused.rfind("secret:", 0) == 0         ? focused.substr(7)  // a row secret's (D194)
                              : focused.rfind("editor:", 0) == 0         ? focused  // an editor's, whole (D217)
                              : focused.rfind("share:", 0) == 0          ? focused.substr(6)  // a report's Save (D263)
                                                                         : key_of(command);
    std::uint64_t ticket = 0;
    const bool capture = space.view.capture_open();
    const std::string name = sent.name;
    if (spaces::registry().game_command(space.id, std::move(sent), ticket)) {
      m.commands[ticket] = {space.id, where};
      m.command_names[ticket] = name;
      if (capture) {
        m.captures.insert(ticket);
      }
    } else {
      g_refusals.fetch_add(1, std::memory_order_relaxed);
      space.view.finished(name);  // never sent: nothing to wait for
      if (capture) {
        space.view.capture_answered(false, false, "Not now: the space has too many commands waiting.");
      } else if (where.rfind("editor:", 0) == 0) {
        grace::protocol::Result busy;
        busy.error = grace::protocol::ErrorCode::busy;
        busy.detail = "Not now: the space has too many commands waiting.";
        space.view.editor_answer(where, busy);
      } else {
        space.view.refused(where, "Not now: the space has too many commands waiting.");
      }
    }
    return;
  }
  if (own_view(m).view.capture_open()) {
    // The capture's command (D209): its answer goes back to the modal, which asks before a move.
    const grace::pages::Outcome outcome = own_space().apply(command);
    if (!outcome.ok) {
      g_refusals.fetch_add(1, std::memory_order_relaxed);
    }
    own_view(m).view.capture_answered(
        outcome.ok, !outcome.ok && outcome.error == grace::protocol::ErrorCode::confirm_required,
        outcome.ok ? std::string_view() : std::string_view(outcome.detail));
    if (command.name == "settings.set") {
      take_effects(m);
    }
    take_topics(m);
    return;
  }
  if (command.name == "settings.set" || command.name == "settings.reset") {
    const grace::pages::Outcome outcome = own_space().apply(command);  // the browser's views see it too
    if (!outcome.ok) {
      g_refusals.fetch_add(1, std::memory_order_relaxed);
      own_view(m).view.refused(key_of(command), outcome.detail);
    }
    take_effects(m);
    return;
  }
  if (command.name == "dashboard.disconnect") {
    spaces::disconnect_browsers();  // a new session cookie; every browser must load the page again (§7.2 item 17)
    log_line("the menu's Disconnect: every browser's session ended");
    return;
  }
  if (command.name == "dashboard.pair") {
    // Pair a device (G9): its code on a sheet over the menu, valid while the sheet stays open (D113).
    const spaces::PairingStart start = spaces::start_pairing();
    if (!start.ok) {
      g_refusals.fetch_add(1, std::memory_order_relaxed);
      const std::string focused = own_view(m).view.focused();
      own_view(m).view.refused(focused.rfind("section-action:", 0) == 0 ? focused.substr(15) : focused, start.words);
      return;
    }
    m.pairing = true;
    m.sheet = pairing_sheet(spaces::pairing_view());
    own_view(m).view.show_sheet(m.sheet);  // open before refresh_pairing looks, which takes a closed sheet as ended
    return;
  }
  // Its other commands (a Mods page switch, D181) go to the one entry point the browser's views use too.
  const grace::pages::Outcome outcome = own_space().apply(command);
  if (!outcome.ok) {
    g_refusals.fetch_add(1, std::memory_order_relaxed);
    const std::string focused = own_view(m).view.focused();
    own_view(m).view.refused(focused.rfind("cell:", 0) == 0 ? focused.substr(5) : key_of(command),
                             outcome.detail.empty() ? "Not possible right now." : outcome.detail);
  }
  take_topics(m);
}

// What a key, a button or the mouse did in the page view.
void act(State& m, const grace::view::Action& action) {
  const bool on_switcher = active_view(m).focused() == "rail:switcher";
  switch (action.kind) {
    case grace::view::Action::Kind::command:
      run(m, action.command);
      break;
    case grace::view::Action::Kind::close:
      input::close_holder(grace::input::Closed::released, m.generation);  // back from the rail (§5.3)
      break;
    case grace::view::Action::Kind::sheet_closed:
      end_pairing(m);  // its Close: the code is void (G9)
      break;
    case grace::view::Action::Kind::switch_space:
      activate(m, action.space, action.page, on_switcher ? From::switcher : From::link);  // another space (D79, D171)
      if (!action.key.empty() && m.spaces[m.active]->id == action.space) {
        m.spaces[m.active]->view.reveal(action.key);  // a search hit's row, on its home page (D219)
      }
      break;
    case grace::view::Action::Kind::opened_page:
    case grace::view::Action::Kind::none:
      break;
  }
}

// Ctrl+C or Ctrl+X in a field (G38 (a)): its text on the clipboard, as Unicode, from GraceUI's own thread. The game's
// window is the clipboard's owner, so that the clipboard takes the text (a null owner would refuse it).
void put_on_clipboard(const std::string& utf8) noexcept {
  const int units = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (units <= 0 || !OpenClipboard(input::window())) {
    return;
  }
  EmptyClipboard();
  if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (static_cast<SIZE_T>(units) + 1) * sizeof(wchar_t)); memory != nullptr) {
    if (auto* text = static_cast<wchar_t*>(GlobalLock(memory)); text != nullptr) {
      MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), text, units);
      text[units] = L'\0';
      GlobalUnlock(memory);
      if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);  // the clipboard did not take it
      }
    } else {
      GlobalFree(memory);
    }
  }
  CloseClipboard();
}

void take(State& m, const MenuInput& in, bool& dirty) {
  if (in.generation != m.generation) {
    return;  // pressed for an earlier holder
  }
  grace::view::PageView& view = active_view(m);
  if (in.character != 0 && !view.editing() && in.character != U'/') {
    return;  // a menu key's own character (Q, E, Enter): nothing to type it into, nothing to lay out again
  }
  if (!m.focus_shown) {
    m.focus_shown = true;  // a key or the pad: the ring shows where they act (G42)
    dirty = true;
  }
  grace::view::Action action;
  if (in.captured != grace::input::kNoKey) {
    action = view.captured(grace::input::key_text(in.captured, in.modifiers));
  } else if (in.character != 0) {
    action = view.typed(in.character);
  } else if (in.paste) {
    std::string text;
    g_paste_lock.lock();
    if (g_paste != nullptr) {
      text.swap(*g_paste);
    }
    g_paste_lock.unlock();
    action = view.pasted(text);
  } else if (in.edit != 0) {
    action = view.edit(grace::view::PageView::EditInput{static_cast<grace::view::PageView::EditKey>(in.edit - 1),
                                                         (in.modifiers & 2u) != 0, (in.modifiers & 1u) != 0});
  } else {
    action = view.input(grace::view::Input{in.key, in.shift != 0, in.pad, in.repeat});
  }
  // The last device used and the key hints' View toggle are the menu's, whichever space's view is drawn (D242).
  m.pad_mode = in.pad && in.captured == grace::input::kNoKey && in.character == 0 && !in.paste && in.edit == 0;
  m.hints_shown = view.hints_shown();
  g_inputs.fetch_add(1, std::memory_order_relaxed);
  dirty = true;
  act(m, action);
  if (std::string copied; active_view(m).take_clipboard(copied)) {
    put_on_clipboard(copied);
  }
  g_awaiting_key.store(active_view(m).awaiting_key(), std::memory_order_release);
  g_editing.store(active_view(m).editing(), std::memory_order_release);
}

void take_pointer(State& m, const MenuPointer& in, bool& dirty) {
  if (in.generation != m.generation) {
    return;  // for an earlier holder
  }
  if (in.pointer.kind != grace::view::Pointer::Kind::leave) {
    m.focus_shown = false;  // the mouse: the ring goes, the focus stays (G42)
    m.pad_mode = false;     // and the key hints are the keyboard's (D242)
  }
  const grace::view::Action action = active_view(m).pointer(in.pointer);
  if (in.pointer.kind != grace::view::Pointer::Kind::move) {
    g_inputs.fetch_add(1, std::memory_order_relaxed);
  }
  dirty = true;  // a move changes what is hovered
  act(m, action);
  if (std::string copied; active_view(m).take_clipboard(copied)) {
    put_on_clipboard(copied);  // a click on a report's Copy (D263), on the click itself, as a key's press is
  }
}

void publish(State& m) {
  const grace::view::Fonts* fonts = atlas::menu_fonts(m.plain);  // Plain text's table while it is on (D267)
  grace::abi::DrawListBuffers* lists = g_lists.load(std::memory_order_acquire);
  if (fonts == nullptr || lists == nullptr || m.width == 0 || m.height == 0) {
    return;
  }
  const std::int64_t started = qpc();
  const float scale = static_cast<float>(m.height) / 1080.0f * static_cast<float>(m.ui_scale);  // §3.13
  active_view(m).set_focus_shown(m.focus_shown);  // the menu's, whichever space's view is drawn (G42)
  active_view(m).set_pad_mode(m.pad_mode);        // likewise the last device used, and View's toggle (D242)
  active_view(m).set_hints_shown(m.hints_shown);
  active_view(m).layout(grace::view::Viewport{static_cast<float>(m.width), static_cast<float>(m.height), scale},
                        *fonts, m.list);
  grace::draw::Atlas atlas{grace::abi::kAtlasTexture, 0.0f, 0.0f};
  atlas::white_pixel(atlas.white_u, atlas.white_v);
  const grace::tokens::Palette& palette = m.clear ? grace::tokens::kClear.colour : grace::tokens::kGrace.colour;
  lists->writer_lock().lock();
  grace::abi::DrawListData& out = lists->back();
  const grace::draw::Stats stats = grace::draw::tessellate(m.list, palette, atlas, out);
  out.serial = ++m.serial;
  out.panel_generation = m.generation;  // drawn only for this holder
  if (std::string page = m.spaces[m.active]->id + "\n" + std::string(active_view(m).page()); page != m.shown_page) {
    m.shown_page = std::move(page);
    ++m.page_serial;  // another page, or another space's: its body fades in (§3.10, D272)
  }
  out.page_serial = m.page_serial;
  out.atlas_generation = atlas::generation();
  out.placed_x = 0.0f;
  out.placed_y = 0.0f;
  lists->publish();
  lists->writer_lock().unlock();
  g_published.fetch_add(1, std::memory_order_relaxed);
  g_layout_us.store(micros_since(started), std::memory_order_relaxed);
  g_vertices.store(stats.vertices, std::memory_order_relaxed);
  g_indices.store(stats.indices, std::memory_order_relaxed);
  g_commands.store(stats.commands, std::memory_order_relaxed);
  g_scale.store(scale, std::memory_order_relaxed);
#if defined(GRACEUI_TEST_EXPORTS)
  std::vector<std::pair<std::string, grace::view::PageView::Bounds>> bounds = active_view(m).all_bounds();
  std::string shown = m.list.text;
  std::map<std::string, bool, std::less<>> pages_present;
  for (const auto& space : m.spaces) {
    for (const std::string& id : space->view.page_ids()) {
      pages_present[space->id + "\n" + id] = space->view.present(id);
    }
  }
#endif
  // The IME's composition window at the edited field's caret, in the back buffer's pixels (§5.6, D57); none otherwise.
  if (const grace::view::Rect caret = active_view(m).caret(); caret.w > 0) {
    input::set_caret(static_cast<std::int32_t>(caret.x), static_cast<std::int32_t>(caret.y),
                     static_cast<std::int32_t>(caret.h));
  } else {
    input::set_caret(0, 0, 0);
  }
  g_view_lock.lock();
  m.page.assign(active_view(m).page());
  m.focused = active_view(m).focused();
  m.focus_drawn = m.focus_shown;
  m.pad_drawn = m.pad_mode;
  m.hints_drawn = m.hints_shown;
  m.footer.assign(active_view(m).footer());
#if defined(GRACEUI_TEST_EXPORTS)
  m.bounds.swap(bounds);
  m.shown.swap(shown);
  m.pages_present.swap(pages_present);
#endif
  g_view_lock.unlock();
}

void drain() noexcept {
  MenuInput in;
  while (g_keys.pop(in)) {
  }
  while (g_pad.pop(in)) {
  }
  MenuPointer pointer;
  while (g_pointer.pop(pointer)) {
  }
}

// The menu's part of the close routine (§5.1), whatever closed it: changes stay and the page is kept; a confirmation
// is cancelled and a field's edit ends (its text kept, G38 (2)); Pair a device's code is void (G9, D113).
void close_view(State& m) {
  end_pairing(m);
  active_view(m).cancel_confirmation();
}

// Rows read a page at a time (D254): what the open page asks of its space, sent as the view's own commands are, each
// answer going back by where it was asked from. True if anything was asked.
bool send_page_requests(State& m) {
  SpaceView& space = *m.spaces[m.active];
  if (space.own || !m.open) {
    return false;
  }
  bool any = false;
  for (grace::view::PageView::PageRequest& request : space.view.take_page_requests()) {
    any = true;
    grace::protocol::Cmd sent = std::move(request.command);
    sent.space = space.id;
    sent.id = m.next_command++;
    std::uint64_t ticket = 0;
    if (spaces::registry().game_command(space.id, std::move(sent), ticket)) {
      m.commands[ticket] = {space.id, request.where};
    } else {
      g_refusals.fetch_add(1, std::memory_order_relaxed);
      grace::protocol::Result busy;
      busy.error = grace::protocol::ErrorCode::busy;
      busy.detail = "Not now: the space has too many commands waiting.";
      space.view.page_answer(request.where, busy);
    }
  }
  return any;
}

void serve_menu() {
  if (g_lists.load(std::memory_order_acquire) == nullptr) {
    g_lists.store(new grace::abi::DrawListBuffers(), std::memory_order_release);
  }
  if (g_state == nullptr) {
    g_state = new State();
  }
  State& m = *g_state;
  follow_settings(m);  // open or not: the test build's and the present thread's reads follow a browser's change (D273)
  const grace::input::Holder holder = input::capture().holder();
  if (m.loaded) {
    sync_spaces(m);  // a mod's space came or went, open or not
  }
  if (!ours(holder)) {
    const bool away = input::menu_away();  // G40: let go at an Alt+Tab, kept for the focus's return
    if (m.open) {
      m.open = false;
      m.away = away;
      g_editing.store(false, std::memory_order_release);  // no key reaches a field while nothing holds input
      g_timer.store(false, std::memory_order_release);    // and nothing is applied on time while it is closed
      g_due.store(0, std::memory_order_release);
      input::set_caret(0, 0, 0);
      active_view(m).pointer(grace::view::Pointer{grace::view::Pointer::Kind::leave, 0, 0, 0});  // no hover, no drag
      watch(m, false);                       // a mod builds nothing the menu no longer shows
      if (!away) {
        close_view(m);
      }
    } else if (m.away && !away) {
      m.away = false;  // the return was refused, or danger or loading came while away: the close it was spared
      close_view(m);
    }
    drain();
    return;
  }
  if (!m.loaded) {
    load(m);
    take_effects(m);
    sync_spaces(m);
  }
  bool dirty = false;
  // The time, for Live updates' Every 5 s (D252): given before this wake's topics, which a view may then hold.
  const std::int64_t now = input::now_ms();
  for (const auto& space : m.spaces) {
    if (space->view.tick(now) && space.get() == m.spaces[m.active].get()) {
      dirty = true;
    }
  }
  if (m.revision != own_space().revision()) {
    take_effects(m);  // a browser changed a setting: the menu shows it, and its scale and theme follow (D169)
    dirty = true;
  }
  if (take_topics(m)) {
    dirty = true;  // a mod came or went, or a switch turned (D181)
  }
  if (!m.open || m.generation != holder.generation) {
    m.open = true;
    m.generation = holder.generation;
    if (!m.ever_opened && m.spaces.size() > 1) {
      activate(m, m.spaces[0]->id, std::string(), From::open);  // the first open: the first mod's space, its first page
    }
    m.ever_opened = true;
    if (!m.away) {
      active_view(m).cancel_confirmation();
      m.focus_shown = false;  // a fresh open: no ring until a key or the pad is used (G42)
    }
    m.away = false;  // back from an Alt+Tab: the page, row, confirmation, field and sheet as they were (G40)
    watch(m, true);
    dirty = true;
  }
  if (take_from_spaces(m)) {
    dirty = true;
  }
  if (refresh_pairing(m)) {
    dirty = true;  // the code's time, or a device paired (G9)
  }
  MenuInput in;
  for (int i = 0; i < 64 && g_keys.pop(in); ++i) {
    take(m, in, dirty);
  }
  for (int i = 0; i < 64 && g_pad.pop(in); ++i) {
    take(m, in, dirty);
  }
  MenuPointer pointer;
  for (int i = 0; i < 64 && g_pointer.pop(pointer); ++i) {
    take_pointer(m, pointer, dirty);
  }
  if (watch_topics(m)) {  // the page or the space changed: what its components need (D184)
    take_from_spaces(m);
    dirty = true;
  }
  if (send_page_requests(m)) {  // rows read a page at a time: "Loading…" until they come (D254)
    dirty = true;
  }
  const std::uint32_t width = g_width.load(std::memory_order_relaxed);
  const std::uint32_t height = g_height.load(std::memory_order_relaxed);
  if (width != m.width || height != m.height) {
    m.width = width;
    m.height = height;
    dirty = true;
  }
  // A mod's answer may have closed the capture, or the menu closed with it open (D209).
  g_awaiting_key.store(m.open && active_view(m).awaiting_key(), std::memory_order_release);
  g_editing.store(m.open && active_view(m).editing(), std::memory_order_release);
  // Every 5 s holding bodies: GraceUI's thread wakes once a second to apply them on time (D252).
  g_timer.store(m.open && ((active_view(m).holding() &&
                             active_view(m).live_mode() == grace::view::PageView::LiveMode::every_5s) ||
                            active_view(m).timed()),  // a report's "Copied", ended on time (D263)
                std::memory_order_release);
  g_due.store(m.open ? active_view(m).next_due() : 0, std::memory_order_release);  // a confirmation's wait (D274)
  if (dirty && m.open) {
    publish(m);
  }
}

void push_pad(Key key, std::uint32_t generation, bool& any, bool repeat = false) noexcept {
  MenuInput in;
  in.key = key;
  in.generation = generation;
  in.pad = true;
  in.repeat = repeat;
  any = g_pad.push(in) || any;
}

// The present thread: the holder's pad, its new presses as the menu's inputs (§5.4).
void pad_inputs(std::uint32_t generation) noexcept {
  grace::input::HolderPad view;
  if (!input::pads().holder_view(generation, view)) {
    return;
  }
  const grace::input::Pad& pad = view.pad;
  // The four directions: the d-pad's, and the left stick's past half its travel, as the d-pad (§5.4).
  const auto directions = static_cast<std::uint8_t>((pad.buttons & 0x000F) | (pad.left_y > kStickPress ? 1 : 0) |
                                                    (pad.left_y < -kStickPress ? 2 : 0) |
                                                    (pad.left_x < -kStickPress ? 4 : 0) |
                                                    (pad.left_x > kStickPress ? 8 : 0));
  PadSeen now{generation, pad.buttons, pad.left_trigger > grace::input::kTriggerThreshold,
              pad.right_trigger > grace::input::kTriggerThreshold, directions};
  // The right stick scrolls the page (§5.4, D242): a step when it is pushed, then, held, one every 90 ms after 250 (45 ms
  // past nine tenths of its travel), as a key repeats.
  now.scroll = static_cast<std::int8_t>(pad.right_y > kStickPress ? 1 : pad.right_y < -kStickPress ? -1 : 0);
  const PadSeen before = g_pad_seen;
  const bool first = before.generation != generation;
  const std::int64_t at = input::now_ms();
  bool scroll = false;
  if (!first && now.scroll != 0) {
    const bool fast = pad.right_y > kStickFull || pad.right_y < -kStickFull;
    if (now.scroll != before.scroll) {
      scroll = true;
      now.scroll_next = at + 250;
    } else if (at >= before.scroll_next) {
      scroll = true;
      now.scroll_next = at + (fast ? 45 : 90);
    } else {
      now.scroll_next = before.scroll_next;
    }
  }
  // A held direction (D270): a step when it is pressed, then, held, its repeats, as a held arrow key's: the first after
  // 400 ms, then one every 80 ms (40 with the left stick past nine tenths of its travel). Only the last one pressed
  // repeats, and only while it is held; the menu's view lets a repeat only move along or step a number.
  const auto pressed_directions = static_cast<std::uint8_t>(now.directions & ~before.directions);
  std::uint8_t repeat = 0;
  if (!first && pressed_directions != 0) {
    for (std::uint8_t bit = 8; bit != 0; bit = static_cast<std::uint8_t>(bit >> 1)) {
      if ((pressed_directions & bit) != 0) {
        now.repeat = bit;  // of several pressed at once, the first in up, down, left, right's order
      }
    }
    now.repeat_next = at + kRepeatDelayMs;
  } else if (!first && before.repeat != 0 && (now.directions & before.repeat) != 0) {
    now.repeat = before.repeat;
    now.repeat_next = before.repeat_next;
    if (at >= before.repeat_next) {
      const bool fast = (before.repeat == 1 && pad.left_y > kStickFull) ||
                        (before.repeat == 2 && pad.left_y < -kStickFull) ||
                        (before.repeat == 4 && pad.left_x < -kStickFull) ||
                        (before.repeat == 8 && pad.left_x > kStickFull);
      repeat = before.repeat;
      now.repeat_next = at + (fast ? kRepeatFastMs : kRepeatEveryMs);
    }
  }
  g_pad_seen = now;
  if (first) {
    return;  // the first look for this holder: what is held now does nothing until it is released
  }
  const std::uint16_t pressed = static_cast<std::uint16_t>(now.buttons & ~before.buttons);
  struct Map {
    std::uint16_t mask;
    Key key;
  };
  static constexpr Key kDirections[] = {Key::up, Key::down, Key::left, Key::right};
  static constexpr Map kButtons[] = {
      {0x1000, Key::activate},     {0x2000, Key::back},    {0x0100, Key::previous_tab},
      {0x0200, Key::next_tab},     {0x4000, Key::reset},   {0x8000, Key::search},   // Y (D242)
      {0x0020, Key::toggle_hints},  // View: the key hints off and on (D242)
      {0x0010, Key::quick},         // Start: the space's Quick switches (D252); like B, it never reaches the game
  };
  bool any = false;
  for (std::size_t i = 0; i < 4; ++i) {
    if ((pressed_directions & (1u << i)) != 0) {
      push_pad(kDirections[i], generation, any);
    }
  }
  for (const Map& map : kButtons) {
    if ((pressed & map.mask) != 0) {
      push_pad(map.key, generation, any);
    }
  }
  if (now.left_trigger && !before.left_trigger) {
    push_pad(Key::previous_page, generation, any);
  }
  if (now.right_trigger && !before.right_trigger) {
    push_pad(Key::next_page, generation, any);
  }
  if (repeat != 0) {
    push_pad(kDirections[repeat == 1 ? 0 : repeat == 2 ? 1 : repeat == 4 ? 2 : 3], generation, any, true);
  }
  if (scroll) {
    push_pad(now.scroll > 0 ? Key::scroll_up : Key::scroll_down, generation, any);
  }
  if (any) {
    hooks::wake();
  }
}

}  // namespace

namespace {

// ui.scale as GraceUI's own store holds it, the last session's if it kept one (D179), else its default: the scale of a
// bake before the menu has read its settings (GraceUI's thread, which reads them too). 1 if the space does not load.
float stored_ui_scale() noexcept {
  try {
    const grace::protocol::Json& scale = own_space().store().held("ui.scale");
    if (scale.is_number()) {
      return static_cast<float>(scale.get<double>());
    }
  } catch (...) {
  }
  return 1.0f;
}

}  // namespace

bool clear_theme() noexcept { return g_clear_theme.load(std::memory_order_relaxed); }

bool bake_plain() noexcept {
  const int read = g_plain_text.load(std::memory_order_relaxed);
  if (read >= 0) {
    return read == 1;
  }
  try {
    return own_space().store().held("ui.menu.plain_text") == true;
  } catch (...) {
    return false;
  }
}

float bake_scale(std::uint32_t height) noexcept {
  float ui_scale = g_ui_scale.load(std::memory_order_relaxed);
  if (ui_scale <= 0.0f) {
    ui_scale = stored_ui_scale();
  }
  const float scale = (height > 0 ? static_cast<float>(height) : 1080.0f) / 1080.0f * ui_scale;
  return scale < 0.5f ? 0.5f : scale > 4.0f ? 4.0f : scale;
}

void open_requested() noexcept {
  if (g_off.load(std::memory_order_acquire)) {
    session::own_notice_text(session::kOwnKeyMenu, GRACEUI_TONE_WARNING, session::kOwnNoticeMs,
                             "GraceUI's menu is off for this session after a fault.");
    return;
  }
  if (atlas::menu_fonts() == nullptr) {
    session::own_notice_text(session::kOwnKeyMenu, GRACEUI_TONE_NORMAL, session::kOwnNoticeMs,
                             "Not now: GraceUI's menu is not ready yet.");
    return;
  }
  const grace::input::Request request = input::request(0, Kind::menu, grace::input::want_all);
  if (request.answer == grace::input::Answer::granted) {
    hooks::wake();
    return;
  }
  if (request.answer == grace::input::Answer::not_now) {
    using grace::input::NotNow;
    const char* why = request.why == NotNow::danger      ? "Not now: you are in danger."
                      : request.why == NotNow::loading   ? "Not now: the game is loading."
                      : request.why == NotNow::game_menu ? "Not now: a game menu is open."
                                                         : "Not now: GraceUI is not drawing yet.";
    session::own_notice_text(session::kOwnKeyMenu, GRACEUI_TONE_NORMAL, session::kOwnNoticeMs, why);
  }
}

void pointer(std::uint8_t kind, float x, float y, float wheel, std::uint32_t generation) noexcept {
  MenuPointer in;
  in.pointer = grace::view::Pointer{static_cast<grace::view::Pointer::Kind>(kind), x, y, wheel};
  in.generation = generation;
  if (g_pointer.push(in)) {
    hooks::wake();
  }
}

namespace {

// Ctrl+V in a field (G38 (a)): the clipboard's Unicode text, as UTF-8, at most 16 KiB, read on the window procedure's
// thread and handed to GraceUI's. False if it holds none.
bool read_clipboard() noexcept {
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(input::window())) {
    return false;
  }
  bool any = false;
  if (HANDLE data = GetClipboardData(CF_UNICODETEXT); data != nullptr) {
    if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data)); text != nullptr) {
      const SIZE_T units = GlobalSize(data) / sizeof(wchar_t);
      int length = 0;
      while (static_cast<SIZE_T>(length) < units && length < 16384 && text[length] != L'\0') {
        ++length;
      }
      const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
      if (bytes > 0) {
        try {
          std::string utf8(static_cast<std::size_t>(bytes), '\0');
          WideCharToMultiByte(CP_UTF8, 0, text, length, utf8.data(), bytes, nullptr, nullptr);
          g_paste_lock.lock();
          if (g_paste == nullptr) {
            g_paste = new std::string();
          }
          g_paste->swap(utf8);
          g_paste_lock.unlock();
          any = true;
        } catch (...) {
          // no room for it: no paste
        }
      }
      GlobalUnlock(data);
    }
  }
  CloseClipboard();
  return any;
}

// A key while a field edits (G38 (a)): its editing keys go to GraceUI's thread; every other key is the field's, and
// types through the characters the window procedure takes.
bool edit_key(std::uint32_t vk, bool shift, bool ctrl) noexcept {
  using EditKey = grace::view::PageView::EditKey;
  EditKey key{};
  switch (vk) {
    case VK_LEFT: key = EditKey::left; break;
    case VK_RIGHT: key = EditKey::right; break;
    case VK_UP: key = EditKey::up; break;
    case VK_DOWN: key = EditKey::down; break;
    case VK_HOME: key = EditKey::home; break;
    case VK_END: key = EditKey::end; break;
    case VK_BACK: key = EditKey::backspace; break;
    case VK_DELETE: key = EditKey::erase; break;
    case VK_RETURN: key = EditKey::enter; break;
    case VK_ESCAPE: key = EditKey::escape; break;
    case VK_TAB: key = EditKey::tab; break;
    case 'A':
      if (!ctrl) {
        return true;
      }
      key = EditKey::select_all;
      break;
    case 'C':
      if (!ctrl) {
        return true;
      }
      key = EditKey::copy;
      break;
    case 'X':
      if (!ctrl) {
        return true;
      }
      key = EditKey::cut;
      break;
    case 'V': {
      if (!ctrl) {
        return true;
      }
      MenuInput in;
      in.generation = input::capture().holder().generation;
      in.paste = true;
      if (read_clipboard() && g_keys.push(in)) {
        hooks::wake();
      }
      return true;
    }
    default:
      return true;  // the field's: its character comes as WM_CHAR
  }
  MenuInput in;
  in.generation = input::capture().holder().generation;
  in.edit = static_cast<std::uint8_t>(static_cast<std::uint8_t>(key) + 1);
  in.modifiers = static_cast<std::uint8_t>((ctrl ? 1u : 0u) | (shift ? 2u : 0u));
  if (g_keys.push(in)) {
    hooks::wake();
  }
  return true;
}

}  // namespace

void character(char32_t code_point, bool repeat) noexcept {
  // While a capture waits, a character is none of the menu's. Otherwise it goes in order with the keys, even before
  // GraceUI's thread has seen the Enter that starts an edit, and that thread drops what no field wants (only `/` opens
  // search, §4.3).
  if (g_awaiting_key.load(std::memory_order_acquire)) {
    return;
  }
  // A held key (D270): Space taken as the menu's own press types no space into the field it opens, nor do its
  // repeats; and a key pressed outside a field types none of its repeats into one (a held `/` opens search once).
  if ((g_pressed_space && code_point == U' ') || (repeat && !g_pressed_in_field)) {
    return;
  }
  MenuInput in;
  in.generation = input::capture().holder().generation;
  in.character = code_point;
  if (g_keys.push(in)) {
    hooks::wake();
  }
}

bool key_down(std::uint32_t vk, grace::input::Key code, bool shift, bool ctrl, bool alt, bool repeat) noexcept {
  if (!repeat) {
    g_pressed_in_field = g_editing.load(std::memory_order_acquire) && !g_awaiting_key.load(std::memory_order_acquire);
    g_pressed_space = false;
  }
  if (g_editing.load(std::memory_order_acquire) && !g_awaiting_key.load(std::memory_order_acquire)) {
    if (repeat && (vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB)) {
      return true;  // a field is kept, left or finished by a fresh press: the held Enter that opened it does not (D270)
    }
    return edit_key(vk, shift, ctrl);  // the caret's keys, Backspace and Delete repeat as in any field
  }
  if (g_awaiting_key.load(std::memory_order_acquire) && vk != VK_ESCAPE) {
    if (repeat) {
      return true;  // a capture takes a fresh press, never the held Enter's repeat that opened it (D270)
    }
    // A capture waits (D209): the key itself, with its modifiers; a modifier alone waits on, and Escape cancels below.
    if (code == grace::input::kNoKey || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LSHIFT ||
        vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU ||
        vk == VK_LWIN || vk == VK_RWIN) {
      return true;
    }
    MenuInput in;
    in.generation = input::capture().holder().generation;
    in.captured = code;
    in.modifiers = static_cast<std::uint8_t>((ctrl ? 1u : 0u) | (shift ? 2u : 0u) | (alt ? 4u : 0u));
    if (g_keys.push(in)) {
      hooks::wake();
    }
    return true;
  }
  Key key{};
  switch (vk) {
    case VK_UP: key = Key::up; break;
    case VK_DOWN: key = Key::down; break;
    case VK_LEFT: key = Key::left; break;
    case VK_RIGHT: key = Key::right; break;
    // Tab moves no focus in the menu (G41): the arrows, Enter and Esc do. Inside a field it finishes the entry (its
    // edit keys, above); otherwise it does nothing here, and never reaches the game while the menu holds the keyboard.
    case VK_RETURN: key = Key::activate; break;
    case VK_SPACE:
      key = Key::activate;
      g_pressed_space = !ctrl;  // its character is the press's own, never typed (D270)
      break;
    case VK_ESCAPE: key = Key::back; break;
    case 'Q': key = Key::previous_tab; break;
    case 'E': key = Key::next_tab; break;
    case VK_PRIOR: key = Key::previous_page; break;
    case VK_NEXT: key = Key::next_page; break;
    case 'R': key = Key::reset; break;
    case 'F':
      if (!ctrl) {
        return false;
      }
      key = Key::search;  // Ctrl+F (§4.3); `/` comes as its character
      break;
    default: return false;
  }
  if (ctrl && key != Key::search) {
    return false;
  }
  const std::uint32_t generation = input::capture().holder().generation;
  MenuInput in{key, static_cast<std::uint8_t>(shift ? 1 : 0), generation};
  in.repeat = repeat;  // the view lets a repeat only move along or step a number (D270)
  if (g_keys.push(in)) {
    hooks::wake();
  }
  return true;
}

namespace {

// The present thread's: the menu's fade (§3.10, D271), for the holder whose list it draws. Its alpha goes from `from`
// at `start_us` to `to` over `length_us`, in a straight line; 0 long, it is at `to`.
struct Fade {
  std::uint32_t generation = 0;
  bool drawn = false;  // a list of this holder's is drawn, or fading out
  float from = 0.0f;
  float to = 0.0f;
  std::int64_t start_us = 0;
  std::int64_t length_us = 0;
};
constinit Fade g_fade{};
constexpr std::int64_t kFadeInUs = 120'000;   // open (§3.10)
// The present thread's: the page body's fade in at a change of page (§3.10, D272), for the holder whose list it draws.
struct BodyFade {
  std::uint32_t generation = 0;
  std::uint64_t page_serial = 0;  // the page whose body is drawn
  std::int64_t start_us = 0;
  bool running = false;
};
constinit BodyFade g_body{};
constexpr std::int64_t kBodyFadeUs = 120'000;  // the page change's (§3.10)
constexpr std::int64_t kFadeOutUs = 200'000;  // close by the player: F5, Esc or B on the top level, the close button

constinit std::int64_t g_qpc_frequency = 0;  // the present thread's, read at its first fade (no static's guard)

std::int64_t now_us() noexcept {
  if (g_qpc_frequency == 0) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    g_qpc_frequency = frequency.QuadPart > 0 ? static_cast<std::int64_t>(frequency.QuadPart) : 1;
  }
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<std::int64_t>(static_cast<long double>(now.QuadPart) * 1'000'000.0L /
                                   static_cast<long double>(g_qpc_frequency));
}

float alpha_of(const Fade& fade, std::int64_t now) noexcept {
  if (fade.length_us <= 0 || now >= fade.start_us + fade.length_us) {
    return fade.to;
  }
  const float t = static_cast<float>(now - fade.start_us) / static_cast<float>(fade.length_us);
  return fade.from + (fade.to - fade.from) * std::clamp(t, 0.0f, 1.0f);
}

// The page body's alpha now (D272): below 1 for 120 ms after a change of page, else 1.
float body_alpha(std::int64_t now) noexcept {
  if (!g_body.running) {
    return 1.0f;
  }
  const std::int64_t since = now - g_body.start_us;
  if (since >= kBodyFadeUs) {
    g_body.running = false;
    return 1.0f;
  }
  return static_cast<float>(std::max<std::int64_t>(since, 0)) / static_cast<float>(kBodyFadeUs);
}

// The last drawn frame's alpha for the test build, in thousandths: at least 1 while anything is drawn, 0 only once
// nothing is.
void store_alpha(float alpha) noexcept {
  g_alpha_milli.store(std::max<std::uint32_t>(static_cast<std::uint32_t>(alpha * 1000.0f), 1u),
                      std::memory_order_relaxed);
}

// The list of the holder g_fade is for, at the fade's alpha now; nothing, and the fade over, once it has faded out or
// its list is gone. `out`: drawn fading out, counted as such.
Frame faded(grace::abi::DrawListBuffers* lists, std::int64_t now, bool out) noexcept {
  const float alpha = alpha_of(g_fade, now);
  if (lists != nullptr && alpha > 0.0f) {
    grace::abi::DrawListData& data = lists->read();
    if (data.serial != 0 && data.panel_generation == g_fade.generation && !data.commands.empty()) {
      if (out) {
        g_fade_out_frames.fetch_add(1, std::memory_order_relaxed);
      }
      // A page's body still fading in goes on from where it is (D272), under the close's fade.
      const float body = g_body.generation == g_fade.generation ? body_alpha(now) : 1.0f;
      store_alpha(alpha * body);
      return {&data, alpha, true, body};
    }
  }
  g_fade.drawn = false;
  g_alpha_milli.store(0, std::memory_order_relaxed);
  return {};
}

}  // namespace

Frame frame(std::uint32_t width, std::uint32_t height) noexcept {
  if (g_width.load(std::memory_order_relaxed) != width || g_height.load(std::memory_order_relaxed) != height) {
    g_width.store(width, std::memory_order_relaxed);
    g_height.store(height, std::memory_order_relaxed);
    hooks::wake();  // the menu lays itself out again for the new size, if it is open
  }
  const std::int64_t now = now_us();
  const grace::input::Holder holder = input::capture().holder();
  grace::abi::DrawListBuffers* lists = g_lists.load(std::memory_order_acquire);
  if (!ours(holder)) {
    g_pad_seen.generation = 0;
    if (g_fade.drawn && g_fade.to > 0.0f) {
      if (holder.phase == grace::input::Phase::closing && holder.generation == g_fade.generation) {
        return faded(lists, now, false);  // its close is under way, its record not yet written: decided next frame
      }
      // Just closed. Input went back to the game with the close (invariant 7); what is drawn fades over 200 ms if the
      // player closed it, from where it is, and goes at once on danger, a load, a fault, focus lost or the player's exit.
      const grace::input::LastClose last = input::capture().last_close();
      const float alpha = alpha_of(g_fade, now);
      const bool still = g_reduce_motion.load(std::memory_order_relaxed);  // Reduce motion: no fades (§3.10)
      if (!still && alpha > 0.0f && last.holder.generation == g_fade.generation &&
          (last.why == grace::input::Closed::released || last.why == grace::input::Closed::menu_key)) {
        g_fade.from = alpha;
        g_fade.to = 0.0f;
        g_fade.start_us = now;
        g_fade.length_us = static_cast<std::int64_t>(static_cast<float>(kFadeOutUs) * alpha);
      } else {
        g_fade.drawn = false;
        g_alpha_milli.store(0, std::memory_order_relaxed);
      }
    }
    if (g_fade.drawn && input::danger_or_loading()) {
      g_fade.drawn = false;  // danger or a load cuts a fade out short (invariant 9, §5.1)
      g_alpha_milli.store(0, std::memory_order_relaxed);
    }
    return g_fade.drawn ? faded(lists, now, true) : Frame{};
  }
  pad_inputs(holder.generation);
  if (lists == nullptr) {
    return {};
  }
  grace::abi::DrawListData& data = lists->read();
  if (data.serial == 0 || data.panel_generation != holder.generation || data.commands.empty()) {
    // The first list for this holder is still being laid out; a list still fading out goes on meanwhile, so that
    // reopening it during its fade does not blink.
    return g_fade.drawn && g_fade.to == 0.0f && g_fade.generation != holder.generation ? faded(lists, now, true)
                                                                                         : Frame{};
  }
  if (!g_fade.drawn || g_fade.generation != holder.generation || g_fade.to < 1.0f) {
    // Its first list drawn: a fade in over 120 ms, from what still shows of a list fading out, or at once.
    const bool still = g_reduce_motion.load(std::memory_order_relaxed);  // read after its list: Reduce motion's own
    const float from = g_fade.drawn ? alpha_of(g_fade, now) : 0.0f;
    g_fade.generation = holder.generation;
    g_fade.drawn = true;
    g_fade.from = still ? 1.0f : from;
    g_fade.to = 1.0f;
    g_fade.start_us = now;
    g_fade.length_us = still ? 0 : static_cast<std::int64_t>(static_cast<float>(kFadeInUs) * (1.0f - from));
  }
  const float alpha = alpha_of(g_fade, now);
  // The page's body (D272): another page than the last list's fades in over 120 ms, the rail, the top bar and the key
  // hints staying still; the open's own fade covers its first page.
  if (g_body.generation != holder.generation) {
    g_body = BodyFade{holder.generation, data.page_serial, now, false};
  } else if (data.page_serial != g_body.page_serial) {
    g_body.page_serial = data.page_serial;
    g_body.start_us = now;
    g_body.running = !g_reduce_motion.load(std::memory_order_relaxed);
  }
  const float body = body_alpha(now);
  g_frames.fetch_add(1, std::memory_order_relaxed);
  if (alpha < 1.0f) {
    g_fade_in_frames.fetch_add(1, std::memory_order_relaxed);
  }
  store_alpha(alpha * body);
  return {&data, alpha, false, body};
}

bool drawing() noexcept { return g_fade.drawn; }

void body_faded() noexcept { g_body_fade_frames.fetch_add(1, std::memory_order_relaxed); }

bool wants_timer() noexcept { return g_timer.load(std::memory_order_acquire); }

std::uint32_t timer_wait_ms() noexcept {
  const std::int64_t due = g_due.load(std::memory_order_acquire);
  if (due == 0) {
    return 1000;
  }
  return static_cast<std::uint32_t>(std::clamp<std::int64_t>(due - input::now_ms(), 1, 1000));
}

bool open_now() noexcept { return ours(input::capture().holder()); }

void serve() noexcept {
  if (g_off.load(std::memory_order_acquire)) {
    drain();
    return;
  }
  try {
    serve_menu();
  } catch (...) {
    g_off.store(true, std::memory_order_release);
    log_line("the menu is off for this session: an exception in its layout or its commands");
    const grace::input::Holder holder = input::capture().holder();
    if (ours(holder)) {
      input::close_holder(grace::input::Closed::fault, holder.generation);  // §5.1: a fault in the menu
    }
  }
}

#if defined(GRACEUI_TEST_EXPORTS)
void test_view(TestView& out) noexcept {
  out = TestView{};
  const grace::input::Holder holder = input::capture().holder();
  out.open = ours(holder) ? 1 : 0;
  out.generation = out.open != 0 ? holder.generation : 0;
  out.lists = g_published.load(std::memory_order_relaxed);
  out.frames = g_frames.load(std::memory_order_relaxed);
  out.inputs = g_inputs.load(std::memory_order_relaxed);
  out.refusals = g_refusals.load(std::memory_order_relaxed);
  out.layout_us = g_layout_us.load(std::memory_order_relaxed);
  out.vertices = g_vertices.load(std::memory_order_relaxed);
  out.indices = g_indices.load(std::memory_order_relaxed);
  out.commands = g_commands.load(std::memory_order_relaxed);
  out.off = g_off.load(std::memory_order_relaxed) ? 1 : 0;
  out.scale = g_scale.load(std::memory_order_relaxed);
  out.faces = atlas::menu_fonts() != nullptr ? static_cast<std::uint32_t>(grace::tokens::kTypeCount) : 0;
  out.baked_scale = atlas::menu_scale();
  out.plain_text = g_plain_text.load(std::memory_order_relaxed) == 1 ? 1 : 0;
  out.plain_baked = atlas::plain_baked() ? 1 : 0;
  out.fade_in_frames = g_fade_in_frames.load(std::memory_order_relaxed);
  out.fade_out_frames = g_fade_out_frames.load(std::memory_order_relaxed);
  out.alpha_milli = g_alpha_milli.load(std::memory_order_relaxed);
  out.body_fade_frames = g_body_fade_frames.load(std::memory_order_relaxed);
  g_view_lock.lock();
  if (g_state != nullptr) {
    std::snprintf(out.page, sizeof out.page, "%s", g_state->page.c_str());
    std::snprintf(out.focused, sizeof out.focused, "%s", g_state->focused.c_str());
    out.focus_shown = g_state->focus_drawn ? 1 : 0;
    out.pad_mode = g_state->pad_drawn ? 1 : 0;
    out.hints_shown = g_state->hints_drawn ? 1 : 0;
    std::snprintf(out.footer, sizeof out.footer, "%s", g_state->footer.c_str());
  }
  g_view_lock.unlock();
}

bool test_shows(const char* text) noexcept {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  g_view_lock.lock();
  const bool found = g_state != nullptr && g_state->shown.find(text) != std::string::npos;
  g_view_lock.unlock();
  return found;
}

std::uint32_t test_page_present(const char* space, const char* page) noexcept {
  if (space == nullptr || page == nullptr) {
    return 2;
  }
  try {
    const std::string key = std::string(space) + "\n" + page;
    g_view_lock.lock();
    std::uint32_t out = 2;
    if (g_state != nullptr) {
      if (const auto found = g_state->pages_present.find(key); found != g_state->pages_present.end()) {
        out = found->second ? 1u : 0u;
      }
    }
    g_view_lock.unlock();
    return out;
  } catch (...) {
    return 2;
  }
}

bool test_bounds(const char* name, float (&out)[8]) noexcept {
  if (name == nullptr) {
    return false;
  }
  bool found = false;
  g_view_lock.lock();
  if (g_state != nullptr) {
    for (const auto& [named, b] : g_state->bounds) {
      if (!found && named == name) {
        const float values[8] = {b.row.x,     b.row.y,     b.row.w,     b.row.h,
                                 b.control.x, b.control.y, b.control.w, b.control.h};
        std::copy(std::begin(values), std::end(values), std::begin(out));
        found = true;
      }
    }
  }
  g_view_lock.unlock();
  return found;
}

std::uint32_t test_setting(const char* key, char* out, std::uint32_t capacity) noexcept {
  // Read on the caller's thread while GraceUI's thread may be writing: a test reads it only once GraceUI is idle.
  if (key == nullptr || out == nullptr || capacity == 0) {
    return 0;
  }
  try {
    grace::spaces::OwnSpace* own = spaces::loaded_own();
    if (own == nullptr) {
      return 0;
    }
    const grace::protocol::Json values = own->store().values();
    if (!values.contains(key)) {
      return 0;
    }
    const std::string text = values[key].dump();
    const int written = std::snprintf(out, capacity, "%s", text.c_str());
    return written < 0 ? 0 : static_cast<std::uint32_t>(written);
  } catch (...) {
    return 0;
  }
}
#endif

}  // namespace grace::host::menu
