// grace-view (D162): the keyboard's and pad's navigation and its commands on GraceUI's own space and the demo space,
// search, and the layout's structure dumps against golden files (§9.3); and two goldens the web view's tests read, every
// page's rows in their focus order and every setting's formats and commands (D166). Set GRACE_UPDATE_GOLDEN=1 to write
// them anew.
#include <algorithm>
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <tuple>

#include "fake_face.h"
#include "grace/view/page_view.h"

namespace {

using namespace grace::view;
using grace::protocol::Json;

PageView loaded(std::string_view id, std::string_view page) {
  PageView view;
  std::string why;
  const auto& s = test::space(id);
  REQUIRE(view.load(test::decoded<grace::protocol::SpacePages>(s.pages),
                    test::decoded<grace::protocol::SettingsDescription>(s.settings), why));
  REQUIRE(view.open(page));
  return view;
}

Action press(PageView& view, Key key, bool shift = false) { return view.input(Input{key, shift}); }

void golden(const std::string& name, const std::string& text) {
  const std::string path = std::string(GRACE_VIEW_GOLDEN) + "/" + name;
  if (const char* update = std::getenv("GRACE_UPDATE_GOLDEN"); update != nullptr && update[0] == '1') {
    std::ofstream(path) << text;
  }
  std::ifstream file(path);
  std::stringstream want;
  want << file.rdbuf();
  CHECK_MESSAGE(want.str() == text, "the structure dump differs from ", path,
                " (run with GRACE_UPDATE_GOLDEN=1 to accept it)");
}

}  // namespace

TEST_CASE("the Menu page: focus on its first control, and each key's command") {
  PageView view = loaded("graceui", "graceui.menu");
  CHECK(view.scope() == Scope::page);
  CHECK(view.focused() == "setting:ui.scale");
  Action a = press(view, Key::right);
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.space == "graceui");
  CHECK(a.command.name == "settings.set");
  CHECK(a.command.args == Json::object({{"key", "ui.scale"}, {"value", 1.3}}));
  CHECK(view.format("ui.scale", view.value("ui.scale")) == "1.30×");
  a = press(view, Key::right, true);  // ten steps, held to the maximum
  CHECK(a.command.args["value"].get<double>() == doctest::Approx(1.5));
  a = press(view, Key::reset);
  CHECK(a.command.name == "settings.reset");
  CHECK(a.command.args == Json::object({{"scope", "key"}, {"target", "ui.scale"}}));
  CHECK(view.value("ui.scale").get<double>() == 1.25);
  press(view, Key::down);
  CHECK(view.focused() == "setting:ui.menu.theme");
  a = press(view, Key::right);
  CHECK(a.command.args["value"] == "clear");
  CHECK(view.format("ui.menu.theme", view.value("ui.menu.theme")) == "Grace Clear (high contrast)");
  press(view, Key::down);  // the next section's label
  CHECK(view.focused() == "section:comfort");
  press(view, Key::down);
  a = press(view, Key::activate);
  CHECK(a.command.args == Json::object({{"key", "ui.menu.reduce_motion"}, {"value", true}}));
}

TEST_CASE("the focus ring: drawn after a key or the pad, gone with the mouse, the focus kept (G42)") {
  PageView view = loaded("graceui", "graceui.menu");
  const auto rings = [&] {
    DisplayList list;
    view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
    const std::string dump = list.dump();
    int count = 0;
    for (std::size_t at = dump.find("stroke  gold.hi"); at != std::string::npos; at = dump.find("stroke  gold.hi", at + 1)) {
      ++count;
    }
    return count;
  };
  CHECK(rings() == 1);  // a view starts as a keyboard player sees it (grace-dev's dumps)
  const std::string focus = view.focused();
  view.pointer(Pointer{Pointer::Kind::move, 5, 5});
  CHECK(rings() == 0);
  CHECK(view.focused() == focus);  // the ring goes, the focus stays
  press(view, Key::down);
  CHECK(rings() == 1);
  CHECK(view.focused() != focus);
  view.pointer(Pointer{Pointer::Kind::press, 5, 5});
  CHECK(rings() == 0);
  view.set_focus_shown(true);
  view.pointer(Pointer{Pointer::Kind::leave, 0, 0});
  CHECK(rings() == 1);  // the pointer leaving the menu is no use of the mouse
  view.set_focus_shown(false);  // the host's word (its menu opens with no ring)
  CHECK(rings() == 0);
}

TEST_CASE("pad mode: the key hints as the pad's buttons after the pad, the keyboard's after a key or the mouse (D242)") {
  PageView view = loaded("graceui", "graceui.menu");
  const auto laid = [&] {
    DisplayList list;
    view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
    return list.dump();
  };
  const auto count = [](const std::string& dump, const std::string& what) {
    int n = 0;
    for (std::size_t at = dump.find(what); at != std::string::npos; at = dump.find(what, at + 1)) {
      ++n;
    }
    return n;
  };
  const std::string keyboard = laid();
  CHECK_FALSE(view.pad_mode());  // a view starts as a keyboard player sees it (grace-dev's dumps)
  CHECK(view.footer() == "← →  Change    Shift+← →  By ten    R  Reset    Esc  Back");
  const std::string focus = view.focused();
  // The pad: the same hints with its buttons (the D-pad's cross, X, B), Shift's ten steps left out, focus where it was.
  view.input(Input{Key::down, false, true});
  view.input(Input{Key::up, false, true});
  CHECK(view.focused() == focus);
  const std::string pad = laid();
  CHECK(view.pad_mode());
  CHECK(view.footer() == "D-pad ← →  Change    X  Reset    B  Back");
  CHECK(pad.find("\"← →  Change") == std::string::npos);
  CHECK(pad.find("\"Change\"") != std::string::npos);
  CHECK(pad.find("\"X\"") != std::string::npos);
  CHECK(pad.find("\"B\"") != std::string::npos);
  // X's and B's diamonds: one dot filled and three rings each; the cross's two bars dim and its two arms marked.
  CHECK(count(pad, "circle  ink.hi") == count(keyboard, "circle  ink.hi") + 2);
  CHECK(count(pad, "stroke  ink.faint") == count(keyboard, "stroke  ink.faint") + 6);
  golden("menu-pad-1920x1080.txt", pad);
  // A key: the keyboard's hints again; the pad, then the mouse, likewise; the host's word sets it.
  view.input(Input{Key::down});
  view.input(Input{Key::up});
  laid();
  CHECK_FALSE(view.pad_mode());
  CHECK(view.footer() == "← →  Change    Shift+← →  By ten    R  Reset    Esc  Back");
  view.input(Input{Key::down, false, true});
  view.input(Input{Key::up, false, true});
  view.pointer(Pointer{Pointer::Kind::move, 5, 5});
  laid();
  CHECK_FALSE(view.pad_mode());
  CHECK(view.footer() == "← →  Change    Shift+← →  By ten    R  Reset    Esc  Back");
  view.set_pad_mode(true);
  laid();
  CHECK(view.footer() == "D-pad ← →  Change    X  Reset    B  Back");
  // The rail's hints, and a toggle's: A for Enter, the D-pad's ↑ ↓ and →, B for Esc.
  view.input(Input{Key::back, false, true});
  laid();
  CHECK(view.footer() == "D-pad ↑ ↓  Move    A  Open    D-pad →  Page    B  Close");
}

TEST_CASE("View hides and shows the key hints, and the right stick scrolls the page with focus kept (D242)") {
  PageView view = loaded("graceui", "graceui.menu");
  const auto laid = [&](float height) {
    DisplayList list;
    view.layout(Viewport{1280, height, 1.0f}, test::fonts(), list);
    return list.dump();
  };
  const std::string shown = laid(1080);
  CHECK(view.hints_shown());
  CHECK(view.footer().find("Esc  Back") != std::string::npos);
  CHECK(shown.find("\"Back\"") != std::string::npos);  // drawn: its label beside Esc's key cap (D246)
  view.input(Input{Key::toggle_hints, false, true});
  const std::string hidden = laid(1080);
  CHECK_FALSE(view.hints_shown());
  CHECK(view.footer().empty());
  CHECK(hidden.find("Back") == std::string::npos);
  view.input(Input{Key::toggle_hints, false, true});
  laid(1080);
  CHECK(view.footer() == "D-pad ← →  Change    X  Reset    B  Back");
  // On a page taller than its display, each step of the stick moves the page by 40 px at scale 1; focus stays, and the
  // page no longer follows it until a key moves focus. At the top a step up does nothing.
  const std::string focus = view.focused();
  laid(400);
  const float before = view.bounds_of(focus).row.y;
  view.input(Input{Key::scroll_down, false, true});
  laid(400);
  CHECK(view.focused() == focus);
  CHECK(view.bounds_of(focus).row.y == doctest::Approx(before - 40));
  view.input(Input{Key::scroll_up, false, true});
  view.input(Input{Key::scroll_up, false, true});
  laid(400);
  CHECK(view.bounds_of(focus).row.y == doctest::Approx(before));
  // Search on the pad's Y, as on `/`.
  view.input(Input{Key::search, false, true});
  CHECK(view.editing());
}

// The first item in a dump whose line starts with `prefix` (e.g. "stroke  gold.hi"), and holds `text` if given: its
// rectangle, read from the dump's "x, y  wxh" columns; found false if there is none.
struct DumpRect {
  bool found = false;
  float x = 0, y = 0, w = 0, h = 0;
};
DumpRect dump_rect(const std::string& dump, const std::string& prefix, const std::string& text = {}) {
  std::istringstream lines(dump);
  for (std::string line; std::getline(lines, line);) {
    if (line.rfind(prefix, 0) != 0 || (!text.empty() && line.find(text) == std::string::npos)) {
      continue;
    }
    DumpRect r;
    std::istringstream at(line.substr(prefix.size()));
    char comma = 0, by = 0;
    if (at >> r.x >> comma >> r.y >> r.w >> by >> r.h && comma == ',' && by == 'x') {
      r.found = true;
      return r;
    }
  }
  return {};
}

TEST_CASE("the focus ring on the rail falls whole within it, and a related row's clears its words, centred (D245)") {
  PageView view = loaded("graceui", "graceui.menu");
  const float scale = 1.25f;
  const auto laid = [&] {
    DisplayList list;
    view.layout(Viewport{1920, 1080, scale}, test::fonts(), list);
    return list.dump();
  };
  // The rail: from x = 0 to its width, at the display's left edge; the ring's outer edge within it.
  press(view, Key::back);
  REQUIRE(view.scope() == Scope::rail);
  const std::string rail = laid();
  const DumpRect ring = dump_rect(rail, "stroke  gold.hi");
  const DumpRect ground = dump_rect(rail, "fill    ground.0");
  REQUIRE(ring.found);
  REQUIRE(ground.found);
  CHECK(ground.x == 0);
  CHECK(ring.x >= ground.x);
  CHECK(ring.x + ring.w <= ground.x + ground.w);
  // A related row: the ring round the row, its label at least 10 px (at scale) inside the ring's inner edge (it was 1
  // px), and the label's middle at the ring's.
  press(view, Key::activate);
  for (int i = 0; i < 30 && view.focused().rfind("related:", 0) != 0; ++i) {
    press(view, Key::down);
  }
  REQUIRE(view.focused() == "related:graceui.hotkeys");
  const std::string page = laid();
  const DumpRect related = dump_rect(page, "stroke  gold.hi");
  const DumpRect label = dump_rect(page, "text    ink.hi", "\"Hotkeys\"");
  REQUIRE(related.found);
  REQUIRE(label.found);
  const float ring_width = std::round(3 * scale);
  CHECK(label.x - (related.x + ring_width) >= 10 * scale);
  CHECK(std::fabs((label.y + label.h / 2) - (related.y + related.h / 2)) <= 1.5f);
}

TEST_CASE("the keyboard's hints as key caps: each key in a cap on ground.well, its label beside it (§3.8, D246)") {
  PageView view = loaded("graceui", "graceui.menu");
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  const std::string dump = list.dump();
  CHECK(view.footer() == "← →  Change    Shift+← →  By ten    R  Reset    Esc  Back");
  // Four caps, each a fill on ground.well under an edge border, with its keys in the mono face; arrows drawn (D247).
  for (const char* keys : {"\"Shift+\"", "\"R\"", "\"Esc\""}) {
    CHECK(dump.find(keys) != std::string::npos);
  }
  for (const char* label : {"\"Change\"", "\"By ten\"", "\"Reset\"", "\"Back\""}) {
    CHECK(dump.find(label) != std::string::npos);
  }
  const DumpRect cap = dump_rect(dump, "fill    ground.well", "");
  const DumpRect esc = dump_rect(dump, "text    ink.body", "\"Esc\"");
  REQUIRE(esc.found);
  CHECK(cap.found);
  CHECK(dump.find("\"← →  Change") == std::string::npos);  // no longer one run of words
}

TEST_CASE("a key cap's arrows are drawn, each a shaft and a head centred in its cap (D247)") {
  PageView view = loaded("graceui", "graceui.menu");
  const float scale = 1.25f;
  DisplayList list;
  view.layout(Viewport{1920, 1080, scale}, test::fonts(), list);
  const std::string dump = list.dump();
  // No arrow is left to the mono face: "← →  Change" and "Shift+← →  By ten" have two each, drawn. (The Related card's
  // arrow, in gold, is a word of its own and stays one.)
  std::istringstream lines(dump);
  int left = 0, right = 0;
  std::vector<DumpRect> wells;
  for (std::string line; std::getline(lines, line);) {
    CHECK_FALSE((line.rfind("text    ink.body", 0) == 0 && (line.find("←") != std::string::npos ||
                                                             line.find("→") != std::string::npos)));
    left += line.rfind("tri     ink.body", 0) == 0 && line.find(" <") != std::string::npos;
    right += line.rfind("tri     ink.body", 0) == 0 && line.find(" >") != std::string::npos;
    if (line.rfind("fill    ground.well", 0) == 0) {
      wells.push_back(dump_rect(line, "fill    ground.well"));
    }
  }
  CHECK(left == 2);
  CHECK(right == 2);
  // "Shift+" and its arrow stand apart, as the keys' names' own space parts "← →".
  const DumpRect shift = dump_rect(dump, "text    ink.body", "\"Shift+\"");
  REQUIRE(shift.found);
  std::istringstream again(dump);
  float after_shift = -1;
  for (std::string line; std::getline(again, line);) {
    const DumpRect r = dump_rect(line, "tri     ink.body", " <");
    if (r.found && r.x > shift.x) {
      after_shift = r.x;
      break;
    }
  }
  CHECK(after_shift >= shift.x + shift.w + std::round(2 * scale));
  // The first cap's first arrow: its head within the cap, its box's middle at the cap's middle, to a pixel.
  const DumpRect head = dump_rect(dump, "tri     ink.body", " <");
  REQUIRE(head.found);
  DumpRect cap;
  for (const DumpRect& well : wells) {
    if (well.x <= head.x && head.x < well.x + well.w && well.y <= head.y && head.y < well.y + well.h) {
      cap = well;
    }
  }
  REQUIRE(cap.found);
  CHECK(head.x >= cap.x);
  CHECK(head.y >= cap.y);
  CHECK(head.y + head.h <= cap.y + cap.h);
  CHECK(std::abs((head.y + head.h / 2) - (cap.y + cap.h / 2)) <= 1.0f);
  CHECK(head.h >= std::round(10 * scale));  // larger than the mono face's arrow, which stood at about half that
  // Pad mode draws the D-pad for them instead: no drawn arrow.
  Input pad;
  pad.key = Key::down;
  pad.pad = true;
  view.input(pad);
  DisplayList padded;
  view.layout(Viewport{1920, 1080, scale}, test::fonts(), padded);
  CHECK(padded.dump().find("tri ") == std::string::npos);
}

TEST_CASE("a triangle's dump names where it points (D247)") {
  for (const Facing facing : {Facing::right, Facing::down, Facing::left, Facing::up}) {
    DisplayList list;
    list.clips.push_back(Rect{0, 0, 100, 100});
    Item item;
    item.shape = Shape::triangle;
    item.colour = grace::tokens::Colour::ink_body;
    item.rect = Rect{10, 20, 12, 10};
    item.facing = facing;
    list.items.push_back(item);
    CHECK(list.dump().find(facing == Facing::right ? " >" : facing == Facing::down ? " v" : facing == Facing::left ? " <" : " ^") !=
          std::string::npos);
  }
}

TEST_CASE("back goes to the rail, and back again closes; the rail opens pages") {
  PageView view = loaded("graceui", "graceui.menu");
  press(view, Key::back);
  CHECK(view.scope() == Scope::rail);
  CHECK(view.focused() == "rail:graceui.menu");
  press(view, Key::down);
  press(view, Key::down);
  CHECK(view.focused() == "rail:graceui.browser");
  Action a = press(view, Key::activate);
  CHECK(a.kind == Action::Kind::opened_page);
  CHECK(a.page == "graceui.browser");
  CHECK(view.scope() == Scope::page);
  press(view, Key::back);
  a = press(view, Key::back);
  CHECK(a.kind == Action::Kind::close);
}

TEST_CASE("Tab and Shift+Tab: rail, top bar, page; ← at the page's edge back to the rail") {
  PageView view = loaded("graceui", "graceui.menu");
  press(view, Key::back);
  press(view, Key::next);
  CHECK(view.scope() == Scope::top);
  CHECK(view.focused() == "search");
  press(view, Key::next);
  CHECK(view.scope() == Scope::page);
  PageView about = loaded("graceui", "graceui.about");
  press(about, Key::left);  // not a value: back to the rail
  CHECK(about.scope() == Scope::rail);
}

TEST_CASE("a destructive action asks for its second press; any other key lets it go") {
  PageView view = loaded("graceui", "graceui.browser");
  CHECK(view.focused() == "action:disconnect");
  CHECK(press(view, Key::activate).kind == Action::Kind::none);
  press(view, Key::down);
  press(view, Key::up);
  CHECK(press(view, Key::activate).kind == Action::Kind::none);  // asked again
  const Action a = press(view, Key::activate);
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.name == "dashboard.disconnect");
  CHECK(a.command.args == Json::object({{"confirm", true}}));  // its confirmation, which the server requires (D173)
}

TEST_CASE("a locked setting does not change; a refusal returns focus with its reason") {
  PageView view = loaded("demo", "demo.festival");
  for (int i = 0; i < 40 && view.focused() != "setting:lantern.festival"; ++i) {
    press(view, Key::down);
  }
  REQUIRE(view.focused() == "setting:lantern.festival");
  const Json before = view.value("lantern.festival");
  CHECK(press(view, Key::right).kind == Action::Kind::none);
  CHECK(press(view, Key::activate).kind == Action::Kind::none);
  CHECK(view.value("lantern.festival") == before);
  press(view, Key::up);
  view.refused("lantern.festival", "Only in a later version.");
  CHECK(view.focused() == "setting:lantern.festival");
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  CHECK(list.dump().find("rot          ") != std::string::npos);
}

TEST_CASE("Other devices: Pair a device is a section action, and each device's Forget asks first (G9)") {
  PageView view = loaded("graceui", "graceui.browser");
  for (int i = 0; i < 40 && view.focused() != "section-action:other-devices/pair"; ++i) {
    press(view, Key::down);
  }
  REQUIRE(view.focused() == "section-action:other-devices/pair");
  Action a = press(view, Key::activate);
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.name == "dashboard.pair");
  CHECK(a.command.args == Json::object());
  press(view, Key::down);
  REQUIRE(view.focused() == "setting:dashboard.lan");
  a = press(view, Key::activate);
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.args == Json::object({{"key", "dashboard.lan"}, {"value", true}}));
  // The device list, as the host publishes it: each row's Forget is destructive, so it asks first.
  view.set_topic("graceui.devices",
                 Json::parse(R"({"columns": [{"id": "name", "label": "Device"}, {"id": "paired", "label": "Paired"}],
                                 "rows": [{"id": "3f9a0c51d27e8b46", "name": "Hallway tablet", "paired": "2026-11-20"}]})"));
  for (int i = 0; i < 10 && view.focused().rfind("row:", 0) != 0; ++i) {
    press(view, Key::down);
  }
  REQUIRE(view.focused().rfind("row:", 0) == 0);
  CHECK(press(view, Key::activate).kind == Action::Kind::none);
  a = press(view, Key::activate);
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.name == "dashboard.forget_device");
  CHECK(a.command.args["id"] == "3f9a0c51d27e8b46");
}

TEST_CASE("a sheet holds every key until Enter, Escape or its Close ends it, and answers sheet_closed (G9)") {
  PageView view = loaded("graceui", "graceui.browser");
  const std::string before = view.focused();
  view.show_sheet(PageView::Sheet{"Pair a device", "482 913", {"On the phone, open this address:", "Then the code."}});
  CHECK(view.sheet_open());
  // Nothing under it moves or sends.
  for (const Key key : {Key::down, Key::up, Key::left, Key::right, Key::next, Key::reset, Key::search}) {
    CHECK(press(view, key).kind == Action::Kind::none);
  }
  CHECK(view.typed(U'a').kind == Action::Kind::none);
  CHECK(view.focused() == before);
  CHECK(view.sheet_open());
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  const std::string dump = list.dump();
  CHECK(dump.find("\"482 913\"") != std::string::npos);
  CHECK(dump.find("\"Close\"") != std::string::npos);
  // A press away from its Close keeps it; Escape closes it.
  CHECK(view.pointer(Pointer{Pointer::Kind::press, 5, 5}).kind == Action::Kind::none);
  CHECK(view.sheet_open());
  CHECK(press(view, Key::back).kind == Action::Kind::sheet_closed);
  CHECK_FALSE(view.sheet_open());
  CHECK(view.focused() == before);
  // Enter closes it too, and so does a press on its Close (found by its words in the dump).
  view.show_sheet(PageView::Sheet{"Paired", "", {"Hallway tablet is paired."}});
  CHECK(press(view, Key::activate).kind == Action::Kind::sheet_closed);
  view.show_sheet(PageView::Sheet{"Pair a device", "123 456", {"Words."}});
  DisplayList again;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), again);
  float x = 0;
  float y = 0;
  std::istringstream lines(again.dump());
  for (std::string line; std::getline(lines, line);) {
    if (line.find("\"Close\"") != std::string::npos) {
      std::istringstream fields(line.substr(line.find_first_of("0123456789")));
      char comma = 0;
      fields >> x >> comma >> y;
    }
  }
  REQUIRE(x > 0);
  CHECK(view.pointer(Pointer{Pointer::Kind::press, x + 2, y + 2}).kind == Action::Kind::sheet_closed);
  CHECK_FALSE(view.sheet_open());
  // The host's own close answers nothing.
  view.show_sheet(PageView::Sheet{"Pair a device", "123 456", {}});
  view.close_sheet();
  CHECK_FALSE(view.sheet_open());
  CHECK(press(view, Key::down).kind != Action::Kind::sheet_closed);
}

TEST_CASE("Page Up and Page Down cross categories in rail order") {
  PageView view = loaded("demo", "demo.behaviour");
  Action a = press(view, Key::next_page);
  CHECK(a.page == "demo.keepers");
  a = press(view, Key::next_page);
  CHECK(a.page == "demo.log");
  a = press(view, Key::previous_page);
  CHECK(a.page == "demo.keepers");
  press(view, Key::previous_page);
  press(view, Key::previous_page);
  CHECK(view.page() == "demo.overview");
  CHECK(press(view, Key::previous_page).kind == Action::Kind::none);
}

TEST_CASE("the demo space: tabs, the Advanced disclosure, every control") {
  PageView view = loaded("demo", "demo.behaviour");
  CHECK(view.focused() == "setting:lantern.enabled");
  press(view, Key::next_tab);
  CHECK(view.focused() == "tab:timing");
  press(view, Key::down);  // the tabs come first in the focus order, then the section's label
  CHECK(view.focused() == "tab:keeper");
  press(view, Key::down);
  CHECK(view.focused() == "section:rhythm");
  press(view, Key::down);
  CHECK(view.focused() == "setting:lantern.gap_s");
  Action a = press(view, Key::left);
  CHECK(a.command.args["value"].get<double>() == doctest::Approx(2.0));
  CHECK(view.format("lantern.gap_s", view.value("lantern.gap_s")) == "2.0 s");
  press(view, Key::down);
  a = press(view, Key::right, true);
  CHECK(a.command.args["value"] == 8);  // whole, and held to its maximum
  CHECK(a.command.args["value"].is_number_integer());
  press(view, Key::down);
  CHECK(view.focused() == "advanced:rhythm");
  press(view, Key::activate);
  press(view, Key::down);
  CHECK(view.focused() == "setting:lantern.night_only");
  press(view, Key::next_tab);
  press(view, Key::down);
  press(view, Key::down);
  press(view, Key::down);
  CHECK(view.focused() == "setting:lantern.trim_key");
  CHECK(press(view, Key::right).kind == Action::Kind::none);  // a key is edited in the browser until GraceUI's field
  PageView light = loaded("demo", "demo.behaviour");
  press(light, Key::down);
  press(light, Key::down);
  a = press(light, Key::left);  // a choice, backwards, wrapping
  CHECK(a.command.args["value"] == "blue");
  press(light, Key::down);
  a = press(light, Key::right);  // the tri-state: auto, then on
  CHECK(a.command.args["value"] == "on");
}

TEST_CASE("an inherit tri-state cycles on, off, inherit, and shows what it inherits (D163)") {
  PageView view = loaded("demo", "demo.behaviour");
  while (view.focused() != "setting:lantern.shield") {
    press(view, Key::down);
  }
  CHECK(view.format("lantern.shield", view.value("lantern.shield")) == "Inherit");
  view.set_inherited({{"lantern.shield", true, "The lighthouse", std::string("demo.overview")}});
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  CHECK(list.dump().find("\"On, from The lighthouse\"") != std::string::npos);
  Action a = press(view, Key::right);
  CHECK(a.command.args["value"] == "on");
  a = press(view, Key::right);
  CHECK(a.command.args["value"] == "off");
  a = press(view, Key::right);
  CHECK(a.command.args["value"] == "inherit");
  a = press(view, Key::reset);
  CHECK(a.command.name == "settings.reset");
}

TEST_CASE("a section that starts collapsed opens with Enter") {
  PageView view = loaded("demo", "demo.log");
  while (view.focused() != "section:journal") {
    press(view, Key::down);
  }
  press(view, Key::activate);
  press(view, Key::activate);
  CHECK(view.focused() == "section:journal");
}

TEST_CASE("a shortcut opens its setting's home with the row focused") {
  PageView view = loaded("demo", "demo.overview");
  while (view.focused() != "shortcut:lantern.brightness") {
    press(view, Key::down);
  }
  const Action a = press(view, Key::activate);
  CHECK(a.page == "demo.behaviour");
  CHECK(view.focused() == "setting:lantern.brightness");
}

TEST_CASE("search finds settings by label, synonym and key") {
  PageView view = loaded("graceui", "graceui.menu");
  auto hits = view.search("contrast");
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].key == "ui.menu.theme");
  CHECK(hits[0].page == "graceui.menu");
  CHECK(view.search("PORT").size() == 1);
  CHECK(view.search("dashboard.").size() == 3);
  CHECK(view.search("").empty());
}

TEST_CASE("every page's rows in their focus order, the golden the web view's parity test reads (§9.3)") {
  // For each page of both built-in spaces, and each of its tabs: the settings and shortcut rows in the order grace-view
  // focuses them, every section open and every Advanced disclosure open, walked with the keys a player presses. The
  // web view's tests (web/tests) compare their own order, and their DOM's, with this file.
  std::string out = "# Generated by grace_view_tests (view_tests.cpp) from grace-view's focus order: do not edit.\n";
  for (const char* id : {"graceui", "demo"}) {
    const auto pages = test::decoded<grace::protocol::SpacePages>(test::space(id).pages);
    for (const grace::protocol::Page& page : pages.pages) {
      const std::size_t tabs = page.tabs ? page.tabs->size() : 1;
      for (std::size_t t = 0; t < tabs; ++t) {
        PageView view = loaded(id, page.id);
        for (std::size_t i = 0; i < t; ++i) {
          press(view, Key::next_tab);
        }
        const auto& sections = page.tabs ? (*page.tabs)[t].sections : page.sections.value();
        for (int i = 0; i < 400; ++i) {
          press(view, Key::up);  // to the first element of the page's focus order
        }
        out += std::string(id) + " " + page.id + (page.tabs ? "#" + (*page.tabs)[t].id : std::string()) + ":";
        std::string last;
        for (int guard = 0; guard < 400; ++guard) {
          const std::string f = view.focused();
          if (f == last) {
            break;  // ↓ at the end of the order stays
          }
          last = f;
          if (f.rfind("section:", 0) == 0) {
            for (const auto& section : sections) {
              if ("section:" + section.id == f && section.collapsed.value_or(false)) {
                press(view, Key::activate);  // opens a section that starts collapsed
              }
            }
          } else if (f.rfind("advanced:", 0) == 0) {
            press(view, Key::activate);  // opens the disclosure
          } else if (f.rfind("setting:", 0) == 0 || f.rfind("shortcut:", 0) == 0) {
            out += " " + f;
          }
          press(view, Key::down);
        }
        out += "\n";
      }
    }
  }
  golden("rows-order.txt", out);
}

TEST_CASE("every setting's value as grace-view writes it, and each press's command, for the web view's parity (§9.3)") {
  // One line a setting of both built-in spaces, tab-separated: its default as formatted, then for → ← Shift+→ Shift+←
  // from the default the command's args and the new value as formatted ("none" when the press sends nothing), and
  // for a secret how it writes set and not set. The web view's tests reproduce every field (web/tests).
  std::string out = "# Generated by grace_view_tests (view_tests.cpp) from grace-view's format and change: do not edit.\n";
  for (const char* id : {"graceui", "demo"}) {
    const auto pages = test::decoded<grace::protocol::SpacePages>(test::space(id).pages);
    const auto settings = test::decoded<grace::protocol::SettingsDescription>(test::space(id).settings);
    for (const grace::protocol::Setting& s : settings.settings) {
      const grace::protocol::Page* home = nullptr;
      for (const auto& page : pages.pages) {
        if (page.id == s.home) {
          home = &page;
        }
      }
      REQUIRE(home != nullptr);
      // The tab that holds the key, and whether it sits behind Advanced.
      std::size_t tab = 0;
      const auto holds = [&](const std::vector<grace::protocol::PageSection>& sections) {
        for (const auto& section : sections) {
          for (const auto* blocks : {&section.blocks, section.advanced ? &*section.advanced : nullptr}) {
            for (const auto& block : blocks ? *blocks : std::vector<grace::protocol::PageBlock>{}) {
              for (const std::string& key : block.keys.value_or(std::vector<std::string>{})) {
                if (key == s.key && block.kind == grace::protocol::BlockKind::settings) {
                  return true;
                }
              }
            }
          }
        }
        return false;
      };
      for (std::size_t t = 0; home->tabs && t < home->tabs->size(); ++t) {
        if (holds((*home->tabs)[t].sections)) {
          tab = t;
        }
      }
      const auto& sections = home->tabs ? (*home->tabs)[tab].sections : home->sections.value();
      const auto focus = [&](PageView& view) {
        for (std::size_t i = 0; i < tab; ++i) {
          press(view, Key::next_tab);
        }
        for (int i = 0; i < 400; ++i) {
          press(view, Key::up);
        }
        for (int guard = 0; guard < 400 && view.focused() != "setting:" + s.key; ++guard) {
          const std::string f = view.focused();
          bool open = f.rfind("advanced:", 0) == 0;
          for (const auto& section : sections) {
            open = open || (f == "section:" + section.id && section.collapsed.value_or(false));
          }
          if (open) {
            press(view, Key::activate);
          }
          press(view, Key::down);
        }
        REQUIRE(view.focused() == "setting:" + s.key);
      };
      const PageView view = loaded(id, home->id);
      out += std::string(id) + "\t" + s.key + "\tdefault=" + view.format(s.key, s.default_);
      for (const auto& [name, key, shift] : {std::tuple{"right", Key::right, false}, std::tuple{"left", Key::left, false},
                                             std::tuple{"shift_right", Key::right, true},
                                             std::tuple{"shift_left", Key::left, true}}) {
        PageView fresh = loaded(id, home->id);
        focus(fresh);
        const Action a = press(fresh, key, shift);
        if (a.kind != Action::Kind::command) {
          out += std::string("\t") + name + "=none";
          continue;
        }
        CHECK(a.command.name == "settings.set");
        out += std::string("\t") + name + "=" + a.command.args.dump() + "|" + fresh.format(s.key, fresh.value(s.key));
      }
      if (s.type == grace::protocol::SettingType::secret) {
        out += "\tset=" + view.format(s.key, Json("set")) + "\tunset=" + view.format(s.key, Json(""));
      }
      out += "\n";
    }
  }
  golden("formats.txt", out);
}

TEST_CASE("the Menu page's layout, as its structure dump says (§9.3)") {
  PageView view = loaded("graceui", "graceui.menu");
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);  // at 1080 lines, ui.scale's default
  golden("menu-1920x1080.txt", list.dump());
  DisplayList large;
  view.layout(Viewport{1280, 800, 1.5f}, test::fonts(), large);
  golden("menu-1280x800-150.txt", large.dump());
  // A value changed from its default gains the lozenge, and nothing else moves (the theme itself is the palette the
  // host draws with: the items name their colours by role, grace-draw's tests).
  view.set_values(Json::object({{"ui.menu.theme", "clear"}}));
  DisplayList again;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), again);
  CHECK(again.items.size() == list.items.size() + 1);
  // Every item lies within the display, and the page's within its clip.
  for (const Item& item : list.items) {
    CHECK(item.rect.x >= 0);
    CHECK(item.rect.right() <= 1920.5f);
    CHECK(item.clip < list.clips.size());
  }
}

TEST_CASE("the demo's Behaviour page, every control drawn; focus scrolled into view") {
  PageView view = loaded("demo", "demo.behaviour");
  view.set_values(Json::object({{"lantern.oil_token", "set"}}));
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  golden("demo-behaviour-1920x1080.txt", list.dump());
  // A short display: the focused row stays within the page's clip.
  PageView log = loaded("demo", "demo.log");
  for (int i = 0; i < 6; ++i) {
    press(log, Key::down);
  }
  DisplayList shortened;
  log.layout(Viewport{1280, 420, 1.0f}, test::fonts(), shortened);
  const Rect& page = shortened.clips[1];
  bool ring_in_view = false;
  for (const Item& item : shortened.items) {
    if (item.shape == Shape::stroke && item.colour == grace::tokens::Colour::gold_hi) {
      ring_in_view = item.rect.y >= page.y - 3 && item.rect.bottom() <= page.bottom() + 3;
    }
  }
  CHECK(ring_in_view);
}

TEST_CASE("a content space's rail with GraceUI's at its foot: the link answers switch_space (D79, D171)") {
  PageView view = loaded("demo", "demo.overview");
  RailContext context;
  context.foot.push_back(RailLink{"GraceUI", "graceui", "graceui.menu", true});
  view.set_rail(context);
  press(view, Key::back);
  CHECK(view.scope() == Scope::rail);
  while (view.focused() != "rail:graceui/graceui.menu") {
    const std::string before = view.focused();
    press(view, Key::down);
    REQUIRE(view.focused() != before);
  }
  const Action a = press(view, Key::activate);
  CHECK(a.kind == Action::Kind::switch_space);
  CHECK(a.space == "graceui");
  CHECK(a.page == "graceui.menu");
  // Pinned to the rail's bottom, under a rule.
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  const std::string dump = list.dump();
  CHECK(dump.find("\"Demo space\"") != std::string::npos);
  bool foot_low = false;
  for (const Item& item : list.items) {
    foot_low = foot_low || (item.shape == Shape::text && item.rect.y > 900 && item.rect.x < 240);
  }
  CHECK(foot_low);
}

TEST_CASE("a space's foot: its categories pinned to the rail's bottom under a rule, last in the rail's order (D248)") {
  // The demo's Records is at its foot; Lanterns is not.
  const auto text_at = [](const DisplayList& l, std::string_view words) -> const Item* {
    for (const Item& item : l.items) {
      if (item.shape == Shape::text && item.rect.x < 300 &&
          std::string_view(l.text).substr(item.text_first, item.text_size) == words) {
        return &item;
      }
    }
    return nullptr;
  };
  PageView view = loaded("demo", "demo.overview");
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  const Item* lanterns = text_at(list, "Lanterns");
  const Item* records = text_at(list, "Records");
  REQUIRE(lanterns != nullptr);
  REQUIRE(records != nullptr);
  CHECK(lanterns->rect.y < 200);
  CHECK(records->rect.y > 1000);  // at the bottom of a 1080 rail, its padding below
  // A rule just above it.
  bool rule = false;
  for (const Item& item : list.items) {
    rule = rule || (item.shape == Shape::line && item.rect.x < 300 && item.rect.y <= records->rect.y &&
                    records->rect.y - item.rect.y < 40);
  }
  CHECK(rule);
  // In the rail's order it comes after Lanterns' pages.
  press(view, Key::back);
  std::vector<std::string> order;
  for (int i = 0; i < 12 && (order.empty() || order.back() != view.focused()); ++i) {
    order.push_back(view.focused());
    press(view, Key::down);
  }
  const auto at = [&](const std::string& id) { return std::find(order.begin(), order.end(), id) - order.begin(); };
  CHECK(at("rail:demo.keepers") < at("rail:records"));
  CHECK(at("rail:records") < static_cast<std::ptrdiff_t>(order.size()));
  // Open, its pages listed under it, the whole foot still pinned to the bottom.
  REQUIRE(view.open("demo.log"));
  DisplayList open;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), open);
  const Item* records_open = text_at(open, "Records");
  const Item* lamps = text_at(open, "Lamps");
  const Item* handbook = text_at(open, "Handbook");
  REQUIRE(records_open != nullptr);
  REQUIRE(lamps != nullptr);
  REQUIRE(handbook != nullptr);
  CHECK(records_open->rect.y < records->rect.y);
  CHECK(lamps->rect.y < handbook->rect.y);
  CHECK(handbook->rect.y > 1000);  // its last page at the bottom
  // Registration refuses a foot before a category that is not of it.
  auto pages = test::decoded<grace::protocol::SpacePages>(test::space("demo").pages);
  std::reverse(pages.categories.begin(), pages.categories.end());
  PageView refused;
  std::string why;
  CHECK_FALSE(refused.load(pages, test::decoded<grace::protocol::SettingsDescription>(test::space("demo").settings), why));
  CHECK(why.find("follows records, at the rail's foot") != std::string::npos);
}

TEST_CASE("GraceUI's space open under a content space: the content space's foot at GraceUI's foot (D248)") {
  PageView view = loaded("graceui", "graceui.menu");
  RailContext context;
  context.title = "Demo space";
  context.head.push_back(RailLink{"Lanterns", "demo", "demo.overview", true});
  context.foot.push_back(RailLink{"Records", "demo", "demo.log", true, true});
  view.set_rail(context);
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  float lanterns = -1, records = -1;
  for (const Item& item : list.items) {
    const std::string_view words = std::string_view(list.text).substr(item.text_first, item.text_size);
    if (item.shape == Shape::text && item.rect.x < 300) {
      lanterns = words == "Lanterns" ? item.rect.y : lanterns;
      records = words == "Records" ? item.rect.y : records;
    }
  }
  CHECK(lanterns > 0);
  CHECK(lanterns < 300);
  CHECK(records > 1000);
}

TEST_CASE("GraceUI's space open under a content space: its categories first, the content space's wordmark") {
  PageView view = loaded("graceui", "graceui.menu");
  RailContext context;
  context.title = "Demo space";
  context.head.push_back(RailLink{"Lanterns", "demo", "demo.overview", true});
  context.head.push_back(RailLink{"Records", "demo", "demo.log", true});
  view.set_rail(context);
  press(view, Key::back);
  CHECK(view.focused() == "rail:graceui.menu");  // focus where the open page is
  press(view, Key::up);
  press(view, Key::up);
  CHECK(view.focused() == "rail:demo/demo.log");
  const Action a = press(view, Key::activate);
  CHECK(a.kind == Action::Kind::switch_space);
  CHECK(a.space == "demo");
  CHECK(a.page == "demo.log");
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  CHECK(list.dump().find("\"Demo space\"") != std::string::npos);
  CHECK(list.dump().find("\"Records\"") != std::string::npos);
}

TEST_CASE("from two content spaces, a switcher heads the rail: ← and → open the previous and the next") {
  PageView view = loaded("demo", "demo.overview");
  RailContext context;
  context.switcher = "Demo space, 1 of 2";
  context.previous = RailLink{"Other", "other", "other.home", true};
  context.next = RailLink{"Other", "other", "other.home", true};
  view.set_rail(context);
  press(view, Key::back);
  while (view.focused() != "rail:switcher") {
    press(view, Key::up);
  }
  Action a = press(view, Key::left);
  CHECK(a.kind == Action::Kind::switch_space);
  CHECK(a.space == "other");
  a = press(view, Key::right);
  CHECK(a.kind == Action::Kind::switch_space);
  CHECK(view.scope() == Scope::rail);  // → on the switcher opens the next space, rather than entering the page
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  CHECK(list.dump().find("Demo space, 1 of 2") != std::string::npos);
}

TEST_CASE("a space opened from another's rail keeps focus on the rail: the switcher, or the page's entry (D171)") {
  PageView view = loaded("demo", "demo.overview");
  RailContext context;
  context.switcher = "Demo space, 2 of 2";
  context.previous = RailLink{"Other", "other", "other.home", true};
  context.next = RailLink{"Other", "other", "other.home", true};
  view.set_rail(context);
  CHECK(view.scope() == Scope::page);
  view.focus_rail(true);
  CHECK(view.scope() == Scope::rail);
  CHECK(view.focused() == "rail:switcher");
  view.focus_rail(false);
  CHECK(view.focused() == "rail:demo.overview");
  press(view, Key::up);
  press(view, Key::up);
  CHECK(view.focused() == "rail:switcher");
}

TEST_CASE("a refused change shows the space's own value again, or the default (D171)") {
  PageView view = loaded("demo", "demo.behaviour");
  view.set_values(Json{{"lantern.brightness", 0.5}});
  view.restore("lantern.brightness", Json(0.5));
  CHECK(view.value("lantern.brightness") == Json(0.5));
  view.set_values(Json{{"lantern.brightness", 0.55}});
  view.refused("lantern.brightness", "Not now: the keeper is trimming the wick.");
  view.restore("lantern.brightness", Json());
  CHECK(view.value("lantern.brightness") == Json(0.8));
  CHECK(view.focused() == "setting:lantern.brightness");
}

TEST_CASE("a page of a later increment is absent: rail, Page Down, search, related links and open (§4.2.2, D173)") {
  PageView view = loaded("graceui", "graceui.menu");
  view.set_increment("0.2");
  CHECK(view.present("graceui.hotkeys"));  // 0.2 since D206 (every hotkey listed)
  CHECK(view.present("graceui.browser"));  // 0.2
  CHECK(view.present("graceui.mods"));     // 0.2 since D181 (G32's switches)
  CHECK_FALSE(view.present("graceui.compatibility"));  // 0.6
  CHECK_FALSE(view.open("graceui.compatibility"));
  CHECK_FALSE(view.search("menu key").empty());  // hotkeys.menu lives on Hotkeys
  // Page Down from Menu: Hotkeys, Browser view, Installed mods, then About past Compatibility.
  press(view, Key::next_page);
  CHECK(view.page() == "graceui.hotkeys");
  press(view, Key::next_page);
  CHECK(view.page() == "graceui.browser");
  press(view, Key::next_page);
  CHECK(view.page() == "graceui.mods");
  press(view, Key::next_page);
  CHECK(view.page() == "graceui.about");
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  const std::string dump = list.dump();
  CHECK(dump.find("Compatibility") == std::string::npos);  // neither on the rail nor among About's related pages
  view.set_increment("0.6");
  CHECK(view.open("graceui.compatibility"));
  view.set_increment("");
  CHECK(view.present("graceui.compatibility"));
}

TEST_CASE("a table in game (D181): its rows, a switch a row by its key, turned to the column's command") {
  PageView view = loaded("graceui", "graceui.mods");
  view.set_increment("0.2");
  const Json switch_column = {{"id", "notices"},
                              {"label", "Notices"},
                              {"switch", {{"command", "mods.notices"}, {"key", "id"}, {"on", "Shown"}, {"off", "Quiet"}}}};
  const Json mods = {{"columns", Json::array({{{"id", "mod"}, {"label", "Mod"}},
                                              {{"id", "version"}, {"label", "Version"}, {"numeric", true}},
                                              {{"id", "uses"}, {"label", "Uses"}},
                                              switch_column})},
                     {"rows", Json::array({{{"id", "demo"}, {"mod", "Demo space"}, {"version", "1.0.0"},
                                            {"uses", "Pages, notices"}, {"notices", "Shown"}},
                                           {{"id", "quiet-camp"}, {"mod", "Quiet Camp"}, {"version", "2.1.0"},
                                            {"uses", "Notices"}, {"notices", "Quiet"}}})}};
  // Without its body the table holds nothing yet (invariant 2), and no row takes focus.
  DisplayList before;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), before);
  CHECK(before.dump().find("Nothing to show.") != std::string::npos);
  CHECK(view.focused().rfind("cell:", 0) != 0);

  view.set_topic("graceui.mods", mods);
  view.set_topic("graceui.notices",
                 Json{{"columns", Json::array({{{"id", "mod"}, {"label", "Mod"}}, {{"id", "text"}, {"label", "Notice"}}})},
                      {"rows", Json::array({{{"mod", "Quiet Camp"}, {"text", "Firewood is running low."}}})}});
  view.open("graceui.mods");
  CHECK(view.focused() == "cell:graceui.mods/demo");
  Action a = press(view, Key::activate);
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.name == "mods.notices");
  CHECK(a.command.args == Json::object({{"id", "demo"}, {"on", false}}));
  press(view, Key::down);
  CHECK(view.focused() == "cell:graceui.mods/quiet-camp");
  a = press(view, Key::right);
  CHECK(a.command.args == Json::object({{"id", "quiet-camp"}, {"on", true}}));
  // A newer body with the rows in another order: focus stays on the same mod's switch; a read-only table takes none.
  Json reordered = mods;
  std::swap(reordered["rows"][0], reordered["rows"][1]);
  view.set_topic("graceui.mods", reordered);
  CHECK(view.focused() == "cell:graceui.mods/quiet-camp");
  press(view, Key::down);
  CHECK(view.focused() == "cell:graceui.mods/demo");
  press(view, Key::down);
  CHECK(view.focused().rfind("cell:graceui.notices", 0) != 0);

  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  golden("mods-1920x1080.txt", list.dump());
  const std::string dump = list.dump();
  CHECK(dump.find("Quiet Camp") != std::string::npos);
  CHECK(dump.find("2.1.0  ·  Notices") != std::string::npos);
  CHECK(dump.find("Firewood is running low.") != std::string::npos);
  CHECK(dump.find("Shown in the browser view.") == std::string::npos);
  // An empty table says so.
  view.set_topic("graceui.notices", Json{{"columns", Json::array()}, {"rows", Json::array()}});
  DisplayList empty;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), empty);
  CHECK(empty.dump().find("Nothing to show.") != std::string::npos);
  // A refused switch: the space's reason under its row, focus on it; a new body of the table takes the line away.
  view.refused("graceui.mods/quiet-camp", "That mod is not connected now.");
  CHECK(view.focused() == "cell:graceui.mods/quiet-camp");
  DisplayList refused;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), refused);
  CHECK(refused.dump().find("That mod is not connected now.") != std::string::npos);
  view.set_topic("graceui.mods", mods);
  DisplayList answered;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), answered);
  CHECK(answered.dump().find("That mod is not connected now.") == std::string::npos);
}

namespace {

// The middle of a rectangle, and a point a fraction of the way across it.
Pointer at(Pointer::Kind kind, const Rect& r, float across = 0.5f) {
  return Pointer{kind, r.x + r.w * across, r.y + r.h / 2, 0};
}

}  // namespace

TEST_CASE("the mouse (§5.5): a press focuses and acts on what it is on, a slider follows a held button") {
  PageView view = loaded("demo", "demo.behaviour");
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  const std::string before = list.dump();

  // A switch: a press on its control turns it; a press on its label only focuses the row.
  const PageView::Bounds lit = view.bounds_of("setting:lantern.enabled");
  REQUIRE(lit.control.w > 0);
  Action a = view.pointer(at(Pointer::Kind::press, lit.row, 0.05f));
  CHECK(a.kind == Action::Kind::none);
  CHECK(view.focused() == "setting:lantern.enabled");
  a = view.pointer(at(Pointer::Kind::press, lit.control));
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.args == Json::object({{"key", "lantern.enabled"}, {"value", false}}));

  // A slider: the value under the pointer, snapped to its step; moves while held follow, and the release ends that.
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  const PageView::Bounds bright = view.bounds_of("setting:lantern.brightness");
  REQUIRE(bright.control.w > 0);
  a = view.pointer(at(Pointer::Kind::press, bright.control, 0.02f));
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(view.focused() == "setting:lantern.brightness");
  CHECK(a.command.args["value"].get<double>() == doctest::Approx(0.0));
  a = view.pointer(at(Pointer::Kind::move, bright.control, 0.9f));
  REQUIRE(a.kind == Action::Kind::command);
  const double high = a.command.args["value"].get<double>();
  CHECK(high > 0.8);
  CHECK(std::fmod(std::round(high * 100), 5.0) == doctest::Approx(0.0));  // on its step of 0.05
  CHECK(view.pointer(at(Pointer::Kind::move, bright.control, 0.9f)).kind == Action::Kind::none);  // the same value
  view.pointer(at(Pointer::Kind::release, bright.control, 0.9f));
  CHECK(view.pointer(at(Pointer::Kind::move, bright.control, 0.1f)).kind == Action::Kind::none);  // let go: a hover

  // A choice: the segment under the pointer.
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  const PageView::Bounds colour = view.bounds_of("setting:lantern.colour");
  REQUIRE(colour.control.w > 0);
  a = view.pointer(at(Pointer::Kind::press, colour.control, 0.9f));
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.args == Json::object({{"key", "lantern.colour"}, {"value", "blue"}}));

  // The rail: a press opens what Enter would (a category, its first page).
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  const PageView::Bounds records = view.bounds_of("rail:records");
  REQUIRE(records.row.w > 0);
  view.pointer(at(Pointer::Kind::press, records.row));
  CHECK(view.page() == "demo.log");

  // Outside every element: nothing.
  CHECK(view.pointer(Pointer{Pointer::Kind::press, 1, 1, 0}).kind == Action::Kind::none);
  CHECK(view.page() == "demo.log");

  // Hover: drawn only while the pointer is over the menu; a layout without a pointer is as before.
  PageView fresh = loaded("demo", "demo.behaviour");
  DisplayList plain;
  fresh.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), plain);
  CHECK(plain.dump() == before);
  CHECK(plain.dump().find("tint.hover") == std::string::npos);
  fresh.pointer(at(Pointer::Kind::move, fresh.bounds_of("setting:lantern.colour").row, 0.2f));
  DisplayList hovering;
  fresh.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), hovering);
  CHECK(hovering.dump().find("tint.hover") != std::string::npos);
  fresh.pointer(Pointer{Pointer::Kind::leave, 0, 0, 0});
  DisplayList left;
  fresh.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), left);
  // As before but for the focus ring, which the mouse's use put away (G42).
  PageView unringed = loaded("demo", "demo.behaviour");
  unringed.set_focus_shown(false);
  DisplayList quiet;
  unringed.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), quiet);
  CHECK(quiet.dump() != before);
  CHECK(left.dump() == quiet.dump());
}

TEST_CASE("the wheel scrolls the page, which stays until a key moves focus; a table's switch takes a click (§5.5)") {
  PageView view = loaded("demo", "demo.behaviour");
  DisplayList list;
  view.layout(Viewport{1280, 720, 1.0f}, test::fonts(), list);
  const float top = view.bounds_of("setting:lantern.enabled").row.y;
  view.pointer(Pointer{Pointer::Kind::wheel, 900, 400, -1});  // towards the player: the page moves up
  view.layout(Viewport{1280, 720, 1.0f}, test::fonts(), list);
  const PageView::Bounds after = view.bounds_of("setting:lantern.enabled");
  CHECK((after.row.w == 0 || after.row.y < top));  // scrolled, though focus stayed on the first row
  CHECK(view.focused() == "setting:lantern.enabled");
  view.pointer(Pointer{Pointer::Kind::wheel, 900, 400, 50});  // far past the top: held at it
  view.layout(Viewport{1280, 720, 1.0f}, test::fonts(), list);
  CHECK(view.bounds_of("setting:lantern.enabled").row.y == doctest::Approx(top));
  view.pointer(Pointer{Pointer::Kind::wheel, 900, 400, -50});  // far past the end: held at it
  view.layout(Viewport{1280, 720, 1.0f}, test::fonts(), list);
  press(view, Key::down);  // a key: focus moves, and the page follows it again
  view.layout(Viewport{1280, 720, 1.0f}, test::fonts(), list);
  CHECK(view.bounds_of(view.focused()).row.w > 0);

  PageView mods = loaded("graceui", "graceui.mods");
  mods.set_increment("0.2");
  mods.set_topic("graceui.mods",
                 Json{{"columns", Json::array({{{"id", "mod"}, {"label", "Mod"}},
                                               {{"id", "notices"},
                                                {"label", "Notices"},
                                                {"switch", {{"command", "mods.notices"}, {"key", "id"}, {"on", "Shown"},
                                                            {"off", "Quiet"}}}}})},
                      {"rows", Json::array({{{"id", "tide-charts"}, {"mod", "Tide Charts"}, {"notices", "Shown"}}})}});
  mods.open("graceui.mods");
  mods.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  const PageView::Bounds cell = mods.bounds_of("cell:graceui.mods/tide-charts");
  REQUIRE(cell.control.w > 0);
  CHECK(mods.pointer(at(Pointer::Kind::press, cell.row, 0.05f)).kind == Action::Kind::none);
  const Action a = mods.pointer(at(Pointer::Kind::press, cell.control));
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.args == Json::object({{"id", "tide-charts"}, {"on", false}}));
}

TEST_CASE("a destructive action asks for its second press even without its confirm text (§7.3, D173)") {
  const auto& s = test::space("demo");
  std::string pages(s.pages);
  const std::string confirm = ",\"confirm\":\"The log's lines are removed. The journal is kept.\"";
  REQUIRE(pages.find(confirm) != std::string::npos);
  pages.erase(pages.find(confirm), confirm.size());
  PageView view;
  std::string why;
  REQUIRE(view.load(test::decoded<grace::protocol::SpacePages>(pages),
                    test::decoded<grace::protocol::SettingsDescription>(s.settings), why));
  REQUIRE(view.open("demo.log"));
  CHECK(view.focused() == "action:clear");
  CHECK(press(view, Key::activate).kind == Action::Kind::none);  // asks first
  const Action a = press(view, Key::activate);
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.args == Json::object({{"confirm", true}}));
}

TEST_CASE("a space's state components in game (D184): a key-value list, a check list, tiles and a sentence") {
  PageView about = loaded("graceui", "graceui.about");
  CHECK(about.component_topics() == std::vector<std::string>{"graceui.about"});
  DisplayList list;
  about.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  CHECK(list.dump().find("Nothing to show.") != std::string::npos);  // nothing published yet (invariant 2)
  CHECK(list.dump().find("yet") == std::string::npos);  // and no promise of more to come (D249)
  about.set_topic("graceui.about", Json{{"items", Json::array({{{"label", "Version"}, {"value", "GraceUI 9.9.9"}},
                                                               {{"label", "Licence"}, {"value", "MIT"}},
                                                               {{"label", "No value"}}})}});
  about.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  std::string dump = list.dump();
  CHECK(dump.find("\"Version\"") != std::string::npos);
  CHECK(dump.find("\"GraceUI 9.9.9\"") != std::string::npos);
  CHECK(dump.find("\"No value\"") == std::string::npos);  // an item without its value is left out, as the browser does
  CHECK(dump.find("Nothing to show.") == std::string::npos);

  PageView compatibility = loaded("graceui", "graceui.compatibility");
  compatibility.set_topic("graceui.compatibility",
                          Json{{"checks", Json::array({{{"state", "pass"}, {"label", "One copy"}},
                                                       {{"state", "fail"}, {"label", "Old mod"}, {"detail", "Built for ABI 2."}},
                                                       {{"state", "odd"}, {"label", "Unknown"}}})}});
  compatibility.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  dump = list.dump();
  CHECK(dump.find("moss") != std::string::npos);
  CHECK(dump.find("\"Passed\"") != std::string::npos);
  CHECK(dump.find("\"Failed\"") != std::string::npos);
  CHECK(dump.find("\"Built for ABI 2.\"") != std::string::npos);
  CHECK(dump.find("\"Waiting\"") != std::string::npos);  // a state it does not know reads as waiting, as the browser's

  // The demo's overview: tiles and a sentence from one topic; its streams' page shows them in the browser alone.
  PageView overview = loaded("demo", "demo.overview");
  CHECK(overview.component_topics() == std::vector<std::string>{"lantern.status"});
  overview.set_topic("lantern.status",
                     Json{{"tiles", Json::array({{{"label", "Oil"}, {"value", 62}, {"unit", "%"}}})},
                          {"sentence", "The lantern burns steady."}});
  overview.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  dump = list.dump();
  CHECK(dump.find("\"62 %\"") != std::string::npos);
  CHECK(dump.find("\"The lantern burns steady.\"") != std::string::npos);
  PageView log = loaded("demo", "demo.log");
  // A live feed and a log tail: streams, the browser's for now; the lamp report's preview is drawn in game (D251).
  // (And lantern.status on every page: the Quick switches' bell, D252.)
  CHECK(log.component_topics() == std::vector<std::string>{"lantern.status", "lantern.report"});
  log.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  CHECK(list.dump().find("Shown in the browser view.") != std::string::npos);
}

namespace {

// The demo space's page data, changed by `edit`, loaded and open on `page`.
PageView demo_edited(const std::function<void(Json&)>& edit, std::string_view page) {
  const auto& s = test::space("demo");
  Json pages = Json::parse(s.pages);
  edit(pages);
  PageView view;
  std::string why;
  REQUIRE(view.load(test::decoded<grace::protocol::SpacePages>(pages.dump()),
                    test::decoded<grace::protocol::SettingsDescription>(s.settings), why));
  REQUIRE(view.open(page));
  return view;
}

Json& overview_blocks(Json& pages) { return pages["pages"][0]["sections"][0]["blocks"]; }

}  // namespace

TEST_CASE("props.field and props.empty (D185, D186): a topic's member, and the space's words once it has spoken") {
  PageView view = demo_edited(
      [](Json& pages) { overview_blocks(pages)[0]["props"] = Json{{"field", "now"}, {"empty", "All quiet."}}; },
      "demo.overview");
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  CHECK(list.dump().find("Nothing to show.") != std::string::npos);  // nothing published: never "All quiet."
  CHECK(list.dump().find("All quiet.") == std::string::npos);
  view.set_topic("lantern.status", Json{{"now", {{"tiles", Json::array({{{"label", "Oil"}, {"value", "40%"}}})}}},
                                        {"sentence", "Low on oil."}});
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  CHECK(list.dump().find("\"40%\"") != std::string::npos);         // the tiles from the member
  CHECK(list.dump().find("\"Low on oil.\"") != std::string::npos);  // the sentence block, with no field, the whole body
  view.set_topic("lantern.status", Json{{"now", {{"tiles", Json::array()}}}});
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  CHECK(list.dump().find("\"All quiet.\"") != std::string::npos);
}

TEST_CASE("space.state greys pages (D188): their words, inert controls; a page not named stays usable") {
  const auto with_state = [](Json& pages) {
    pages["topics"].push_back(Json{{"id", "space.state"}, {"shape", "state"}});
  };
  PageView view = demo_edited(with_state, "demo.behaviour");
  CHECK(view.component_topics() == std::vector<std::string>{"space.state", "lantern.status"});  // the bell's (D252)
  view.set_topic("space.state", Json{{"greyed", {{"demo.behaviour", "Load a lantern to see this."}}}});
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  CHECK(list.dump().find("\"Load a lantern to see this.\"") != std::string::npos);
  CHECK(view.focused() == "setting:lantern.enabled");
  CHECK(press(view, Key::activate).kind == Action::Kind::none);  // inert
  CHECK(press(view, Key::right).kind == Action::Kind::none);
  CHECK(press(view, Key::reset).kind == Action::Kind::none);
  // The overview is not named: usable, but its shortcut to Behaviour's setting is inert, its home being greyed.
  view.open("demo.overview");
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  CHECK(list.dump().find("Load a lantern") == std::string::npos);
  // The whole space, every page not named, with the shorthand; and lifted again.
  view.set_topic("space.state", Json{{"inactive", "The lantern is out."}});
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  CHECK(list.dump().find("\"The lantern is out.\"") != std::string::npos);
  CHECK(press(view, Key::activate).kind == Action::Kind::none);  // the overview's Trim action, inert
  view.set_topic("space.state", Json::object());
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  CHECK(list.dump().find("The lantern is out.") == std::string::npos);
}

TEST_CASE("a shortcut row's control in game (D189): ← → change its home's setting, Enter follows its link") {
  PageView view = loaded("demo", "demo.overview");
  for (int i = 0; i < 6 && view.focused() != "shortcut:lantern.brightness"; ++i) {
    press(view, Key::down);
  }
  REQUIRE(view.focused() == "shortcut:lantern.brightness");
  const Action a = press(view, Key::right);
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.args["key"] == "lantern.brightness");
  CHECK(a.command.args["value"].get<double>() == doctest::Approx(0.85));
  press(view, Key::activate);
  CHECK(view.page() == "demo.behaviour");
  CHECK(view.focused() == "setting:lantern.brightness");
  // Greyed at its home: the shortcut changes nothing, and its link still goes there.
  PageView greyed = demo_edited(
      [](Json& pages) { pages["topics"].push_back(Json{{"id", "space.state"}, {"shape", "state"}}); }, "demo.overview");
  greyed.set_topic("space.state", Json{{"greyed", {{"demo.behaviour", "Not now."}}}});
  for (int i = 0; i < 6 && greyed.focused() != "shortcut:lantern.brightness"; ++i) {
    press(greyed, Key::down);
  }
  CHECK(press(greyed, Key::right).kind == Action::Kind::none);
  press(greyed, Key::activate);
  CHECK(greyed.page() == "demo.behaviour");
}

// ---- G34 (D192 to D195): a table's row actions and row secret, a form, a picker whose choices come from a topic.

namespace {

const Json kKeepers = Json::parse(R"({"columns": [{"id": "name", "label": "Keeper"}, {"id": "post", "label": "Post"},
  {"id": "ward", "label": "Ward word"}], "rows": [{"id": "k1", "name": "Orla Venn", "post": "North light", "ward": "set"},
  {"id": "k2", "name": "Tamsin Reed", "post": "Harbour wall", "ward": ""},
  {"id": "k3", "name": "Ada Marsh", "post": "Store room", "ward": "refused"},
  {"id": "k4", "name": "Bran Teel", "post": "Lamp loft", "ward": "none"}]})");
const Json kOils = Json::parse(R"({"choices": [{"value": "rapeseed", "label": "Rapeseed oil"},
  {"value": "olive", "label": "Olive oil"}, {"value": "tallow", "label": "Tallow"}]})");

}  // namespace

TEST_CASE("a table's row actions: a focus stop each, a destructive one sent after its second press (D192)") {
  PageView view = loaded("demo", "demo.keepers");
  const auto topics = view.component_topics();
  CHECK(std::find(topics.begin(), topics.end(), "lantern.keepers") != topics.end());
  CHECK(std::find(topics.begin(), topics.end(), "lantern.oils") != topics.end());  // the picker's choices (D193)
  view.set_topic("lantern.keepers", kKeepers);
  for (int i = 0; i < 12 && view.focused() != "row:lantern.keepers/k1/dismiss"; ++i) {
    press(view, Key::down);  // from the roster's own action, at the page's top (D202)
  }
  CHECK(view.focused() == "row:lantern.keepers/k1/dismiss");
  CHECK(press(view, Key::activate).kind == Action::Kind::none);  // it asks first (§7.3, D173)
  DisplayList asking;
  view.layout(Viewport{}, test::fonts(), asking);
  CHECK(asking.dump().find("Press again to confirm") != std::string::npos);
  Action sent = press(view, Key::activate);
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.name == "keepers.dismiss");
  CHECK(sent.command.args == Json::object({{"id", "k1"}, {"confirm", true}}));
  // Another key lets the confirmation go; the next row's secret, then its action, are the next focus stops (G38 (a)).
  press(view, Key::activate);
  press(view, Key::down);
  CHECK(view.focused() == "secret:lantern.keepers/k2");
  press(view, Key::down);
  CHECK(view.focused() == "row:lantern.keepers/k2/dismiss");
  CHECK(press(view, Key::activate).kind == Action::Kind::none);
  // The space's refusal shows under its row.
  view.refused("lantern.keepers/k2/dismiss", "Tamsin is on watch.");
  DisplayList refused;
  view.layout(Viewport{}, test::fonts(), refused);
  CHECK(refused.dump().find("rot          ") != std::string::npos);
  // A row secret's status in words (D194, D197), and a Set on each row that takes one; a form's fields in game, its
  // labels and its button (G38 (a)): nothing is left to the browser view but the roster file's upload, which the game has
  // no picker for (D226).
  const std::string dump = refused.dump();
  CHECK(dump.find("Ward word: Set") != std::string::npos);
  CHECK(dump.find("Ward word: Missing") != std::string::npos);
  CHECK(dump.find("Ward word: Refused") != std::string::npos);  // D197
  CHECK(dump.find("Ward word: Not needed") != std::string::npos);
  const std::size_t upload = dump.find("Add a roster file: in the browser view on this PC.");
  CHECK(upload != std::string::npos);
  CHECK(dump.find("Shown in the browser view") == std::string::npos);
  CHECK(dump.find("Filled in the browser view") == std::string::npos);
  CHECK(dump.find("\"Name\"") != std::string::npos);
  CHECK(dump.find("\"Post\"") != std::string::npos);
  CHECK(dump.find("\"Hire\"") != std::string::npos);
}

TEST_CASE("a picker: its choices from a topic, ← and → through them, its value kept when the list lacks it (D193)") {
  PageView view = loaded("demo", "demo.keepers");
  for (int i = 0; i < 12 && view.focused() != "setting:lantern.oil_kind"; ++i) {
    press(view, Key::down);
  }
  CHECK(view.focused() == "setting:lantern.oil_kind");
  CHECK(press(view, Key::right).kind == Action::Kind::none);  // nothing to choose from yet
  DisplayList empty;
  view.layout(Viewport{}, test::fonts(), empty);
  CHECK(empty.dump().find("Nothing to choose from yet") != std::string::npos);
  CHECK(view.format("lantern.oil_kind", Json("rapeseed")) == "rapeseed");
  view.set_topic("lantern.oils", kOils);
  CHECK(view.format("lantern.oil_kind", Json("rapeseed")) == "Rapeseed oil");
  Action next = press(view, Key::right);
  REQUIRE(next.kind == Action::Kind::command);
  CHECK(next.command.args == Json::object({{"key", "lantern.oil_kind"}, {"value", "olive"}}));
  Action back = press(view, Key::left);
  CHECK(back.command.args["value"] == "rapeseed");
  CHECK(press(view, Key::left).command.args["value"] == "tallow");  // round from the first
  view.set_topic("lantern.oils", Json::parse(R"({"choices": [{"value": "olive", "label": "Olive oil"}]})"));
  CHECK(view.format("lantern.oil_kind", Json("tallow")) == "tallow");  // not in the list: still shown as it is
}

TEST_CASE("the lozenge follows the space's changed keys once it says them; an empty default is not printed (D198)") {
  PageView view = loaded("demo", "demo.keepers");
  const auto lozenges = [&]() {  // the page's own rule draws one; a changed row one more
    DisplayList list;
    view.layout(Viewport{}, test::fonts(), list);
    const std::string dump = list.dump();
    std::size_t n = 0;
    for (std::size_t at = dump.find("diamond gold.deep"); at != std::string::npos; at = dump.find("diamond gold.deep", at + 1)) {
      ++n;
    }
    return n;
  };
  const std::size_t base = lozenges();
  view.set_values(Json::object({{"lantern.oil_kind", "olive"}}));
  CHECK(lozenges() == base + 1);  // a value off its default: changed, until the space says
  view.set_changed({});
  CHECK(lozenges() == base);      // the space says it is not
  view.set_changed({"lantern.oil_kind"});
  CHECK(lozenges() == base + 1);
}

TEST_CASE("a picker whose default is empty offers it first, as Default (D199)") {
  PageView view;
  std::string why;
  const auto& demo = test::space("demo");
  Json pages = Json::parse(demo.pages);
  Json settings = Json::parse(demo.settings);
  for (Json& s : settings["settings"]) {
    if (s["key"] == "lantern.oil_kind") {
      s["default"] = "";
    }
  }
  REQUIRE(view.load(test::decoded<grace::protocol::SpacePages>(pages.dump()),
                    test::decoded<grace::protocol::SettingsDescription>(settings.dump()), why));
  REQUIRE(view.open("demo.keepers"));
  for (int i = 0; i < 12 && view.focused() != "setting:lantern.oil_kind"; ++i) {
    press(view, Key::down);
  }
  view.set_topic("lantern.oils", kOils);
  CHECK(view.format("lantern.oil_kind", Json("")) == "Default");
  Action next = press(view, Key::right);  // from Default to the first choice
  CHECK(next.command.args["value"] == "rapeseed");
  view.set_values(Json::object({{"lantern.oil_kind", "rapeseed"}}));
  CHECK(press(view, Key::left).command.args["value"] == "");  // and back to the space's default
}

TEST_CASE("a row secret's Set only on a row that takes a key (D200, G38 (a))") {
  PageView view = loaded("demo", "demo.keepers");
  const auto sets = [&]() {
    DisplayList list;
    view.layout(Viewport{}, test::fonts(), list);
    const std::string dump = list.dump();
    std::size_t n = 0;
    for (std::size_t at = dump.find("glyphs 3 \"Set\""); at != std::string::npos; at = dump.find("glyphs 3 \"Set\"", at + 1)) {
      ++n;
    }
    return std::make_pair(n, dump);
  };
  view.set_topic("lantern.keepers", Json::parse(R"({"columns": [{"id": "name", "label": "Keeper"},
    {"id": "ward", "label": "Ward word"}], "rows": [{"id": "k3", "name": "Ada Marsh", "ward": "none"}]})"));
  const auto [none, none_dump] = sets();
  CHECK(none_dump.find("Ward word: Not needed") != std::string::npos);
  CHECK(none == 0);
  view.set_topic("lantern.keepers", kKeepers);
  CHECK(sets().first == 3);  // k1, k2 and k3; k4 needs none
}

namespace {

// Focus moved down the page to `id`; false if never reached (the Hotkeys tests' helper, below, does the same).
bool move_to(PageView& view, std::string_view id) {
  for (int i = 0; i < 64 && view.focused() != id; ++i) {
    press(view, Key::down);
  }
  return view.focused() == id;
}

void type(PageView& view, std::string_view text) {
  for (const char c : text) {
    view.typed(static_cast<unsigned char>(c));
  }
}

using EditKey = PageView::EditKey;
using EditInput = PageView::EditInput;

}  // namespace

TEST_CASE("a text setting in GraceUI's own field: Enter edits, Enter sends, Escape drops (G38 (a), D57)") {
  PageView view = loaded("demo", "demo.behaviour");
  press(view, Key::next_tab);
  press(view, Key::next_tab);
  move_to(view, "setting:lantern.keeper");
  REQUIRE(view.focused() == "setting:lantern.keeper");
  CHECK_FALSE(view.editing());
  // Typing does nothing until the field edits; R still resets.
  CHECK(view.typed(U'x').kind == Action::Kind::none);
  CHECK(press(view, Key::activate).kind == Action::Kind::none);
  CHECK(view.editing());
  DisplayList open;
  view.layout(Viewport{}, test::fonts(), open);
  CHECK(view.footer() == "Enter  Save    Esc  Cancel    Ctrl+V  Paste");
  CHECK(view.caret().w > 0);  // the IME's place
  // Its text is the value; Ctrl+Backspace takes a word, then more is typed.
  view.edit(EditInput{EditKey::backspace, false, true});
  type(view, "lighthouse keeper");
  Action sent = view.edit(EditInput{EditKey::enter});
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.name == "settings.set");
  CHECK(sent.command.args == Json::object({{"key", "lantern.keeper"}, {"value", "The north lighthouse keeper"}}));
  CHECK_FALSE(view.editing());
  CHECK(view.caret().w == 0);
  // Escape drops an edit; Enter with nothing changed sends nothing.
  press(view, Key::activate);
  type(view, " and more");
  CHECK(view.edit(EditInput{EditKey::escape}).kind == Action::Kind::none);
  CHECK(view.value("lantern.keeper") == "The north lighthouse keeper");
  press(view, Key::activate);
  CHECK(view.edit(EditInput{EditKey::enter}).kind == Action::Kind::none);
  // A pad's A and B do as Enter and Escape while it edits.
  press(view, Key::activate);
  type(view, "!");
  CHECK(press(view, Key::activate).command.args["value"] == "The north lighthouse keeper!");
  // While it edits, every key is the field's: R types, Q does not change the tab.
  press(view, Key::activate);
  CHECK(press(view, Key::reset).kind == Action::Kind::none);
  CHECK(press(view, Key::next_tab).kind == Action::Kind::none);
  CHECK(view.focused() == "setting:lantern.keeper");
  view.edit(EditInput{EditKey::escape});
}

TEST_CASE("a secret in GraceUI's own field: masked, empty at the start, sent once and never kept or copied (G38 (a))") {
  PageView view = loaded("demo", "demo.behaviour");
  press(view, Key::next_tab);
  press(view, Key::next_tab);
  move_to(view, "setting:lantern.oil_token");
  REQUIRE(view.focused() == "setting:lantern.oil_token");
  view.set_values(Json::object({{"lantern.oil_token", "set"}}));
  press(view, Key::activate);
  REQUIRE(view.editing());
  type(view, "tok-1234");
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  const std::string dump = list.dump();
  CHECK(dump.find("tok-1234") == std::string::npos);  // no glyph of it is laid out
  std::size_t discs = 0;
  for (std::size_t at = dump.find("circle  ink.body"); at != std::string::npos; at = dump.find("circle  ink.body", at + 1)) {
    ++discs;
  }
  CHECK(discs == 8);
  CHECK(view.footer().find("Enter  Set    Esc  Cancel") != std::string::npos);
  view.edit(EditInput{EditKey::select_all});
  view.edit(EditInput{EditKey::copy});
  std::string clipboard;
  CHECK_FALSE(view.take_clipboard(clipboard));  // never a secret's
  view.edit(EditInput{EditKey::end});
  Action sent = view.edit(EditInput{EditKey::enter});
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.args == Json::object({{"key", "lantern.oil_token"}, {"value", "tok-1234"}}));
  CHECK(view.value("lantern.oil_token") == "set");  // the view keeps whether it is set, never the secret
  CHECK(view.format("lantern.oil_token", view.value("lantern.oil_token")) == "Set");
  // An empty secret sends nothing: R is how it is cleared.
  press(view, Key::activate);
  CHECK(view.edit(EditInput{EditKey::enter}).kind == Action::Kind::none);
}

TEST_CASE("a form in game: its fields typed in turn, sent together, kept until the space takes them (D195, G38)") {
  PageView view = loaded("demo", "demo.keepers");
  move_to(view, "field:hire/0/name");
  REQUIRE(view.focused() == "field:hire/0/name");
  press(view, Key::activate);
  type(view, "Orla Venn");
  CHECK(view.edit(EditInput{EditKey::enter}).kind == Action::Kind::none);  // Enter: on to the next field
  CHECK(view.focused() == "field:hire/0/post");
  CHECK_FALSE(view.editing());
  press(view, Key::activate);
  type(view, "North light");
  view.edit(EditInput{EditKey::tab});  // Tab finishes the entry and moves no focus (G41)
  CHECK(view.focused() == "field:hire/0/post");
  CHECK_FALSE(view.editing());
  move_to(view, "submit:hire/0");
  CHECK(view.focused() == "submit:hire/0");
  // The menu closes, and opens again: the fields keep their text (G38 (2)).
  view.cancel_confirmation();
  CHECK(view.form_value("hire/0/name") == "Orla Venn");
  CHECK(view.form_value("hire/0/post") == "North light");
  DisplayList kept;
  view.layout(Viewport{}, test::fonts(), kept);
  CHECK(kept.dump().find("\"Orla Venn\"") != std::string::npos);
  Action sent = press(view, Key::activate);
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.name == "keepers.hire");
  // Every field is sent, an empty one as "" (the browser's form does the same).
  CHECK(sent.command.args == Json::object({{"name", "Orla Venn"}, {"post", "North light"}, {"notes", ""}}));
  // Refused: the space's words under the form, the text kept; accepted: the fields empty.
  view.refused("hire/0", "A keeper of that name is on the roster.");
  DisplayList refused;
  view.layout(Viewport{}, test::fonts(), refused);
  CHECK(refused.dump().find("A keeper of that name is on the roster.") != std::string::npos);
  CHECK(view.focused() == "submit:hire/0");
  CHECK(view.form_value("hire/0/name") == "Orla Venn");
  view.accepted("hire/0");
  CHECK(view.form_value("hire/0/name").empty());
  CHECK(view.form_value("hire/0/post").empty());
  // A field holds no more than its max (32 bytes for Name), and Escape keeps a form field's text.
  move_to(view, "field:hire/0/name");
  press(view, Key::up);
  press(view, Key::up);
  for (int i = 0; i < 10 && view.focused() != "field:hire/0/name"; ++i) {
    press(view, Key::up);
  }
  press(view, Key::activate);
  type(view, "abcdefghijklmnopqrstuvwxyz0123456789");
  view.edit(EditInput{EditKey::escape});
  CHECK(view.form_value("hire/0/name") == "abcdefghijklmnopqrstuvwxyz012345");
}

TEST_CASE("a row secret in game: masked, sent with its row's key, then forgotten (D194, G38 (a))") {
  PageView view = loaded("demo", "demo.keepers");
  view.set_topic("lantern.keepers", kKeepers);
  move_to(view, "secret:lantern.keepers/k2");
  REQUIRE(view.focused() == "secret:lantern.keepers/k2");
  press(view, Key::activate);
  REQUIRE(view.editing());
  type(view, "ember");
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  CHECK(list.dump().find("ember") == std::string::npos);
  Action sent = view.edit(EditInput{EditKey::enter});
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.name == "keepers.ward");
  CHECK(sent.command.args == Json::object({{"id", "k2"}, {"value", "ember"}}));
  CHECK_FALSE(view.editing());
  press(view, Key::activate);
  type(view, "ash");
  CHECK(view.edit(EditInput{EditKey::escape}).kind == Action::Kind::none);  // dropped
  view.refused("lantern.keepers/k2", "That word is already in use.");
  DisplayList refused;
  view.layout(Viewport{}, test::fonts(), refused);
  CHECK(refused.dump().find("That word is already in use.") != std::string::npos);
}

TEST_CASE("search in game: `/` or Ctrl+F opens it, its results as it is typed, Enter opens the row (§4.3, G38 (a))") {
  PageView view = loaded("demo", "demo.overview");
  CHECK(view.typed(U'/').kind == Action::Kind::none);
  REQUIRE(view.editing());
  CHECK(view.scope() == Scope::top);
  type(view, "token");
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  CHECK(list.dump().find("\"Oil merchant's token\"") != std::string::npos);
  CHECK(view.footer() == "↑ ↓  Choose    Enter  Open    Esc  Close");
  Action opened = view.edit(EditInput{EditKey::enter});
  CHECK(opened.kind == Action::Kind::opened_page);
  CHECK(opened.page == "demo.behaviour");
  CHECK(view.focused() == "setting:lantern.oil_token");  // on its tab, Keeper, the third
  CHECK_FALSE(view.editing());
  // A setting under a section's Advanced disclosure: the disclosure opens.
  press(view, Key::search);
  type(view, "night");
  view.edit(EditInput{EditKey::enter});
  CHECK(view.focused() == "setting:lantern.night_only");
  // Escape empties it; nothing matching says so.
  CHECK(press(view, Key::search).kind == Action::Kind::none);
  REQUIRE(view.editing());
  type(view, "zzz");
  DisplayList none;
  view.layout(Viewport{}, test::fonts(), none);
  CHECK(none.dump().find("No setting matches.") != std::string::npos);
  view.edit(EditInput{EditKey::escape});
  CHECK_FALSE(view.editing());
  DisplayList closed;
  view.layout(Viewport{}, test::fonts(), closed);
  CHECK(closed.dump().find("\"Search\"") != std::string::npos);
  // ↓ picks the next result.
  press(view, Key::search);
  type(view, "lantern");
  view.edit(EditInput{EditKey::down});
  CHECK(view.edit(EditInput{EditKey::enter}).kind == Action::Kind::opened_page);
}

TEST_CASE("the close routine ends an edit as Escape does; a click moves the caret or leaves the field (G38)") {
  PageView view = loaded("demo", "demo.behaviour");
  press(view, Key::next_tab);
  press(view, Key::next_tab);
  move_to(view, "setting:lantern.keeper");
  press(view, Key::activate);
  type(view, "!");
  view.cancel_confirmation();
  CHECK_FALSE(view.editing());
  CHECK(view.value("lantern.keeper") == "The north keeper");  // dropped
  // A click inside the field places the caret; one elsewhere sends a text's edit.
  press(view, Key::activate);
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  const PageView::Bounds field = view.bounds_of("setting:lantern.keeper");
  REQUIRE(field.control.w > 0);
  view.pointer(Pointer{Pointer::Kind::press, field.control.x + 12, field.control.y + field.control.h / 2});
  type(view, "A ");
  view.layout(Viewport{}, test::fonts(), list);
  Action left = view.pointer(Pointer{Pointer::Kind::press, 5, 5});
  REQUIRE(left.kind == Action::Kind::command);
  CHECK(left.command.args["value"] == "A The north keeper");
}

TEST_CASE("fields drawn in game: a form field edited with a selection, and search's results (G38 (a), goldens)") {
  PageView view = loaded("demo", "demo.keepers");
  view.set_topic("lantern.keepers", kKeepers);
  REQUIRE(move_to(view, "field:hire/0/name"));
  press(view, Key::activate);
  type(view, "Orla Venn");
  view.edit(EditInput{EditKey::left, true, true});  // "Venn" selected
  DisplayList form;
  view.layout(Viewport{}, test::fonts(), form);
  golden("keepers-editing-1920x1080.txt", form.dump());
  view.edit(EditInput{EditKey::escape});
  press(view, Key::search);
  type(view, "keep");
  DisplayList search;
  view.layout(Viewport{}, test::fonts(), search);
  golden("search-1920x1080.txt", search.dump());
}

namespace {

const Json kLamps = Json::parse(R"({"columns": [{"id": "name", "label": "Lamp"}, {"id": "place", "label": "Where"}],
  "rows": [{"id": "harbour-mouth", "name": "Harbour mouth", "place": "The north end of the wall"},
           {"id": "fish-steps", "name": "Fish steps", "place": "Halfway along, above the steps"},
           {"id": "a/b", "name": "Not a member's key", "place": "Never listed"},
           {"id": "old-crane", "name": "Old crane", "place": "The inner end, by the crane"}]})");
const Json kHarbour = Json::parse(R"({"tiles": [{"label": "Flame", "value": "Tall"}, {"label": "Oil", "value": "80%"}],
  "facts": {"items": [{"label": "Glass", "value": "Green"}]}, "sentence": "The harbour-mouth lamp burns tall."})");

bool watches(const PageView& view, std::string_view topic) {
  const auto topics = view.component_topics();
  return std::find(topics.begin(), topics.end(), topic) != topics.end();
}

}  // namespace

TEST_CASE("master and detail in game: the list's rows, the first chosen, its member watched and drawn (D215)") {
  PageView view = loaded("demo", "demo.lamps");
  CHECK(watches(view, "lantern.lamps"));
  CHECK(view.chosen().empty());
  CHECK_FALSE(watches(view, "lantern.lamp"));  // a family is never watched itself
  DisplayList before;
  view.layout(Viewport{}, test::fonts(), before);
  CHECK(before.dump().find("Nothing to show.") != std::string::npos);
  CHECK(before.dump().find("Choose a lamp to see how it burns.") != std::string::npos);
  // An empty list: the space's own words.
  view.set_topic("lantern.lamps", Json::parse(R"({"columns": [], "rows": []})"));
  DisplayList empty;
  view.layout(Viewport{}, test::fonts(), empty);
  CHECK(empty.dump().find("No lamp is lit on the wall yet.") != std::string::npos);
  // Rows: the first is chosen at once, and its member is what the page's blocks watch.
  view.set_topic("lantern.lamps", kLamps);
  CHECK(view.chosen() == "harbour-mouth");
  CHECK(watches(view, "lantern.lamp/harbour-mouth"));
  view.set_topic("lantern.lamp/harbour-mouth", kHarbour);
  DisplayList shown;
  view.layout(Viewport{}, test::fonts(), shown);
  const std::string dump = shown.dump();
  CHECK(dump.find("\"Harbour mouth\"") != std::string::npos);
  CHECK(dump.find("\"The north end of the wall\"") != std::string::npos);
  CHECK(dump.find("Not a member's key") == std::string::npos);  // a key with '/' names no member: not listed
  CHECK(dump.find("\"Tall\"") != std::string::npos);            // the member's tiles, in the detail
  CHECK(dump.find("\"Green\"") != std::string::npos);
  golden("lamps-1920x1080.txt", dump);
}

TEST_CASE("master and detail in game: ↑ ↓ along the list choose, → into the detail, ← and Esc back (D215)") {
  PageView view = loaded("demo", "demo.lamps");
  view.set_topic("lantern.lamps", kLamps);
  view.set_topic("lantern.lamp/harbour-mouth", kHarbour);
  // The page opens on the list's first row; ↓ moves along it, each row chosen as focus comes onto it.
  REQUIRE(view.focused() == "item:lantern.lamps/harbour-mouth");
  press(view, Key::up);  // the first row: ↑ goes up to the list's filter bar (D250), and ↓ comes back
  CHECK(view.focused() == "filter:master:lantern.lamps/search");
  CHECK(view.chosen() == "harbour-mouth");
  press(view, Key::down);
  CHECK(view.focused() == "item:lantern.lamps/harbour-mouth");
  press(view, Key::down);
  press(view, Key::down);
  CHECK(view.focused() == "item:lantern.lamps/old-crane");
  CHECK(view.chosen() == "old-crane");
  CHECK_FALSE(watches(view, "lantern.lamp/harbour-mouth"));  // the last row's member is let go
  CHECK(watches(view, "lantern.lamp/old-crane"));
  press(view, Key::down);  // the list's last row: ↓ stays on it
  CHECK(view.focused() == "item:lantern.lamps/old-crane");
  press(view, Key::up);
  CHECK(view.chosen() == "fish-steps");
  // → into the detail; ↑ from its first stop, Esc and ← come back to the chosen row, not to the rail.
  press(view, Key::right);
  CHECK(view.focused() == "tab:lamp");
  CHECK(view.chosen() == "fish-steps");
  press(view, Key::up);
  CHECK(view.focused() == "item:lantern.lamps/fish-steps");
  press(view, Key::right);
  press(view, Key::back);
  CHECK(view.focused() == "item:lantern.lamps/fish-steps");
  press(view, Key::activate);
  CHECK(view.focused() == "tab:lamp");
  press(view, Key::left);
  CHECK(view.focused() == "item:lantern.lamps/fish-steps");
  CHECK(view.scope() == Scope::page);
  press(view, Key::left);  // from the list, ← goes to the rail
  CHECK(view.scope() == Scope::rail);
  // The page keeps its chosen row: another page, then back, focus on it.
  view.open("demo.log");
  view.open("demo.lamps");
  CHECK(view.chosen() == "fish-steps");
  // A list that loses the chosen row lets it go and chooses its first.
  view.set_topic("lantern.lamps", Json::parse(R"({"columns": [{"id": "name"}], "rows": [{"id": "old-crane", "name": "Old crane"}]})"));
  CHECK(view.chosen() == "old-crane");
}

TEST_CASE("master and detail in game: a click on a row chooses it (D215)") {
  PageView view = loaded("demo", "demo.lamps");
  view.set_topic("lantern.lamps", kLamps);
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  const PageView::Bounds row = view.bounds_of("item:lantern.lamps/fish-steps");
  REQUIRE(row.row.w > 0);
  view.pointer(Pointer{Pointer::Kind::press, row.row.x + 10, row.row.y + 10});
  CHECK(view.chosen() == "fish-steps");
  CHECK(view.focused() == "item:lantern.lamps/fish-steps");
}

TEST_CASE("a slow command's control reads Working… and takes no press until its answer comes (D216)") {
  PageView view = loaded("demo", "demo.keepers");
  REQUIRE(move_to(view, "section-action:oil/order"));
  const Action sent = press(view, Key::activate);
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.name == "oil.order");
  CHECK(view.running("section-action:oil/order"));
  DisplayList waiting;
  view.layout(Viewport{}, test::fonts(), waiting);
  CHECK(waiting.dump().find("\"Working…\"") != std::string::npos);
  CHECK(press(view, Key::activate).kind == Action::Kind::none);  // no second press while it waits
  view.finished("oil.order");
  CHECK_FALSE(view.running("section-action:oil/order"));
  DisplayList done;
  view.layout(Viewport{}, test::fonts(), done);
  CHECK(done.dump().find("\"Order oil\"") != std::string::npos);
  // A command the space does not name slow leaves nothing waiting.
  PageView other = loaded("demo", "demo.keepers");
  REQUIRE(move_to(other, "submit:hire/0"));
  REQUIRE(press(other, Key::activate).kind == Action::Kind::command);
  CHECK_FALSE(other.running("submit:hire/0"));
}

TEST_CASE("Ctrl+C and Ctrl+X put a field's selection aside for the clipboard; a paste is one paragraph (G38 (a))") {
  PageView view = loaded("demo", "demo.keepers");
  move_to(view, "field:hire/0/post");
  press(view, Key::activate);
  view.pasted("Harbour\r\nwall");
  CHECK(view.form_value("hire/0/post") == "Harbour wall");
  view.edit(EditInput{EditKey::left, true, true});  // Ctrl+Shift+←: the word
  view.edit(EditInput{EditKey::cut});
  std::string clipboard;
  REQUIRE(view.take_clipboard(clipboard));
  CHECK(clipboard == "wall");
  CHECK(view.form_value("hire/0/post") == "Harbour ");
  CHECK_FALSE(view.take_clipboard(clipboard));  // taken once
}

TEST_CASE("a section's chip, live status and own actions: drawn in its head, a destructive one after its second press (D202)") {
  PageView view = loaded("demo", "demo.keepers");
  const auto topics = view.component_topics();
  CHECK(std::find(topics.begin(), topics.end(), "lantern.oils") != topics.end());
  DisplayList before;
  view.layout(Viewport{}, test::fonts(), before);
  CHECK(before.dump().find("\"store\"") != std::string::npos);  // the chip
  CHECK(before.dump().find("\"Low\"") == std::string::npos);    // no status before its topic speaks (invariant 2)
  view.set_topic("lantern.oils", Json::parse(R"({"choices": [], "stock": {"word": "Low", "tone": "ember"}})"));
  DisplayList after;
  view.layout(Viewport{}, test::fonts(), after);
  CHECK(after.dump().find("\"Low\"") != std::string::npos);
  CHECK(after.dump().find("circle  ember") != std::string::npos);
  for (int i = 0; i < 12 && view.focused() != "section-action:roster/dismiss_all"; ++i) {
    press(view, Key::up);
  }
  REQUIRE(view.focused() == "section-action:roster/dismiss_all");
  CHECK(press(view, Key::activate).kind == Action::Kind::none);  // it asks first
  const Action sent = press(view, Key::activate);
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.name == "keepers.dismiss_all");
  CHECK(sent.command.args == Json::object({{"confirm", true}}));
  for (int i = 0; i < 20 && view.focused() != "section-action:oil/order"; ++i) {
    press(view, Key::down);
  }
  REQUIRE(view.focused() == "section-action:oil/order");
  const Action order = press(view, Key::activate);  // not destructive: at once
  CHECK(order.command.name == "oil.order");
  CHECK(order.command.args == Json::object());
}

namespace {

Json hotkeys_body() {
  return {{"columns", Json::array({{{"id", "mod"}, {"label", "Mod"}},
                                   {{"id", "action"}, {"label", "Action"}},
                                   {{"id", "key"}, {"label", "Key"}},
                                   {{"id", "chord"}, {"label", "Pad chord"}},
                                   {{"id", "state"}, {"label", "State"}}})},
          {"rows", Json::array({{{"id", ""}, {"mod", "GraceUI"}, {"action", "Open the menu"}, {"key", "F5"},
                                 {"chord", "No pad chord"}, {"state", "Bound"}},
                                {{"id", "tide-charts/tide-table"}, {"mod", "Tide Charts"},
                                 {"action", "Show the tide table"}, {"key", "Ctrl+T"},
                                 {"chord", "D-pad Down, then A"}, {"state", "Bound"}},
                                {{"id", "quiet-camp/pitch-tent"}, {"mod", "Quiet Camp"}, {"action", "Pitch the tent"},
                                 {"key", "No key"}, {"chord", "No pad chord"},
                                 {"state", "Unbound: no key or pad chord"}}})}};
}

// Focus moved down the page to `id` (a focus name, "row:…" or "setting:…"); false if never reached.
bool focus_on(PageView& view, std::string_view id) {
  for (int i = 0; i < 64 && view.focused() != id; ++i) {
    press(view, Key::down);
  }
  return view.focused() == id;
}

}  // namespace

TEST_CASE("the Hotkeys page in game (D206): the menu's key, and every hotkey with its chord, the menu's row without "
          "actions") {
  PageView view = loaded("graceui", "graceui.hotkeys");
  view.set_increment("0.2");
  REQUIRE(view.present("graceui.hotkeys"));
  view.set_topic("graceui.hotkeys", hotkeys_body());
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  const std::string dump = list.dump();
  CHECK(dump.find("Ctrl+T  ·  D-pad Down, then A  ·  Bound") != std::string::npos);  // a row's cells, in order
  CHECK(dump.find("F5  ·  No pad chord") != std::string::npos);
  CHECK(dump.find("Later version") == std::string::npos);  // the menu's key is live since D208
  CHECK(dump.find("Change key") != std::string::npos);
  view.open("graceui.hotkeys");
  CHECK(view.focused() == "action:reset-all");  // the page's action first
  REQUIRE(focus_on(view, "setting:hotkeys.menu"));
  // The menu's own row (no id) takes no row actions: the first row stop after it is Tide Charts'.
  for (int i = 0; i < 8 && view.focused().rfind("row:", 0) != 0; ++i) {
    press(view, Key::down);
  }
  CHECK(view.focused() == "row:graceui.hotkeys/tide-charts/tide-table/key");
  golden("hotkeys-1920x1080.txt", dump);
}

TEST_CASE("a row's key captured in game: the next key, sent with the row's key; a taken one asked, then moved (D209)") {
  PageView view = loaded("graceui", "graceui.hotkeys");
  view.set_increment("0.2");
  view.set_topic("graceui.hotkeys", hotkeys_body());
  view.open("graceui.hotkeys");
  REQUIRE(focus_on(view, "row:graceui.hotkeys/tide-charts/tide-table/key"));
  CHECK(press(view, Key::activate).kind == Action::Kind::none);
  REQUIRE(view.capture_open());
  CHECK(view.awaiting_key());
  CHECK(press(view, Key::down).kind == Action::Kind::none);  // the menu's own keys do nothing meanwhile
  Action sent = view.captured("F9");
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.space == "graceui");
  CHECK(sent.command.name == "hotkeys.key");
  CHECK(sent.command.args == Json::object({{"id", "tide-charts/tide-table"}, {"value", "F9"}}));
  CHECK_FALSE(view.awaiting_key());
  view.capture_answered(false, true, "F9 is Quiet Camp's Pitch the tent. Use it here, and leave that one with no key?");
  DisplayList asking;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), asking);
  CHECK(asking.dump().find("Quiet Camp's Pitch the tent") != std::string::npos);
  CHECK(view.footer().find("Enter  Use it here") != std::string::npos);
  CHECK(asking.dump().find("\"Use it here\"") != std::string::npos);  // in the modal's card too, by its key cap
  golden("hotkeys-asking-1920x1080.txt", asking.dump());
  const Action moved = press(view, Key::activate);
  REQUIRE(moved.kind == Action::Kind::command);
  CHECK(moved.command.args == Json::object({{"id", "tide-charts/tide-table"}, {"value", "F9"}, {"confirm", true}}));
  view.capture_answered(true, false, "");
  CHECK_FALSE(view.capture_open());

  // Backspace leaves a row's hotkey with no key; a refusal's words show and the capture waits again; Escape cancels.
  REQUIRE(focus_on(view, "row:graceui.hotkeys/tide-charts/tide-table/key"));
  press(view, Key::activate);
  const Action cleared = view.captured("Backspace");
  CHECK(cleared.command.args["value"] == "");
  view.capture_answered(false, false, "That hotkey has gone.");
  CHECK(view.awaiting_key());
  DisplayList refused;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), refused);
  CHECK(refused.dump().find("That hotkey has gone.") != std::string::npos);
  press(view, Key::back);
  CHECK_FALSE(view.capture_open());
  CHECK(view.captured("F9").kind == Action::Kind::none);  // nothing waits now
}

TEST_CASE("a pad chord chosen in game with two pickers; R leaves none (D209)") {
  PageView view = loaded("graceui", "graceui.hotkeys");
  view.set_increment("0.2");
  view.set_topic("graceui.hotkeys", hotkeys_body());
  view.open("graceui.hotkeys");
  REQUIRE(focus_on(view, "row:graceui.hotkeys/quiet-camp/pitch-tent/chord"));
  press(view, Key::activate);
  REQUIRE(view.capture_open());
  CHECK_FALSE(view.awaiting_key());  // the pickers take the menu's own keys
  DisplayList open;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), open);
  CHECK(open.dump().find("←  D-pad Up  →") != std::string::npos);
  CHECK(open.dump().find("←  View  →") != std::string::npos);
  golden("hotkeys-chord-1920x1080.txt", open.dump());
  press(view, Key::right);  // the button held first: D-pad Down
  press(view, Key::down);
  press(view, Key::left);  // then pressed: RS, before View
  const Action used = press(view, Key::activate);
  REQUIRE(used.kind == Action::Kind::command);
  CHECK(used.command.name == "hotkeys.chord");
  CHECK(used.command.args == Json::object({{"id", "quiet-camp/pitch-tent"}, {"value", (0x0002u << 16) | 0x0080u}}));
  view.capture_answered(true, false, "");
  REQUIRE(focus_on(view, "row:graceui.hotkeys/quiet-camp/pitch-tent/chord"));
  press(view, Key::activate);
  for (int i = 0; i < 12; ++i) {
    press(view, Key::right);  // the button held first round to View, the same as the second: refused in words
  }
  CHECK(press(view, Key::activate).kind == Action::Kind::none);
  DisplayList same;
  view.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), same);
  CHECK(same.dump().find("Choose two different buttons.") != std::string::npos);
  const Action none = press(view, Key::reset);
  REQUIRE(none.kind == Action::Kind::command);
  CHECK(none.command.args["value"] == 0);
}

TEST_CASE("the menu's key captured in game: one key, sent as its setting (D209)") {
  PageView view = loaded("graceui", "graceui.hotkeys");
  view.set_increment("0.2");
  view.open("graceui.hotkeys");
  REQUIRE(focus_on(view, "setting:hotkeys.menu"));
  press(view, Key::activate);
  REQUIRE(view.awaiting_key());
  const Action sent = view.captured("F6");
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.name == "settings.set");
  CHECK(sent.command.args == Json::object({{"key", "hotkeys.menu"}, {"value", "F6"}}));
  view.capture_answered(false, false, "The menu's key takes no Ctrl, Shift or Alt.");
  CHECK(view.awaiting_key());
  CHECK(view.captured("Backspace").command.args["value"] == "Backspace");  // a setting's capture keeps Backspace
}

namespace {

// The demo's note on the harbour mouth lamp, and its oils, from the space's fixtures (D217).
Json demo_topic(std::string_view id) {
  std::ifstream file(std::string(GRACE_VIEW_GOLDEN) + "/../../../../fixtures/demo/topics.json");
  const Json all = Json::parse(file);
  return all.at(std::string(id));
}

// The Lamps page on its Your notes tab, the harbour mouth chosen and its note published.
PageView notes_view() {
  PageView view = loaded("demo", "demo.lamps");
  view.set_topic("lantern.lamps", kLamps);
  view.set_topic("lantern.lamp/harbour-mouth", kHarbour);
  view.set_topic("lantern.oils", demo_topic("lantern.oils"));
  view.set_topic("lantern.note/harbour-mouth", demo_topic("lantern.note/harbour-mouth"));
  press(view, Key::right);  // into the detail
  press(view, Key::next_tab);
  press(view, Key::next_tab);
  REQUIRE(view.focused() == "tab:yours");
  return view;
}

grace::protocol::Result answer(const char* body) {
  grace::protocol::Result r;
  r.ok = true;
  r.body = Json::parse(body);
  return r;
}

const grace::view::EditorState& note_state(const PageView& view) {
  const grace::view::EditorState* st = view.editor_state("lantern.note/harbour-mouth");
  REQUIRE(st != nullptr);
  return *st;
}

}  // namespace

TEST_CASE("the editor in game: its document drawn, its stops in order, and Save sends it with its revision (D217)") {
  PageView view = notes_view();
  CHECK(watches(view, "lantern.note/harbour-mouth"));  // the editor's family, for the chosen row
  CHECK(watches(view, "lantern.oils"));                // a pick's choices, once the document names them
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  const std::string dump = list.dump();
  for (const char* words : {"\"Saved\"", "\"Save\"", "\"Check\"", "\"Restore the previous version\"", "\"Reset all\"",
                            "\"Your rough notes\"", "\"0 / 400\"", "\"Draft\"", "\"Short name\"", "\"10 / 24\"",
                            "\"Mouth lamp\"", "\"19 / 35 words\"", "\"Never let it gutter\"",
                            "\"Locked: Written by hand: change it in the file.\"", "\"In a storm\"", "\"Plain\"",
                            "\"Rapeseed oil\"", "\"12\"", "\"Off\"", "\"Harbour mouth, on the first evening\"",
                            "\"The demo's own: A green lamp at the end of the wall.\"", "\"+ Add\""}) {
    CHECK_MESSAGE(dump.find(words) != std::string::npos, words);
  }
  CHECK(dump.find("Unsaved") == std::string::npos);
  golden("editor-1920x1080.txt", dump);
  // The stops, top to bottom: its buttons, the draft's notes and button, then each field (a list's items and Add).
  std::vector<std::string> stops;
  for (int i = 0; i < 40; ++i) {
    press(view, Key::down);
    if (view.focused().rfind("editor:", 0) == 0 &&
        (stops.empty() || stops.back() != view.focused())) {
      stops.push_back(view.focused());
    }
  }
  const std::vector<std::string> want = {
      "editor:your_notes/0/save",          "editor:your_notes/0/check",          "editor:your_notes/0/restore",
      "editor:your_notes/0/reset",         "editor:your_notes/0/notes",          "editor:your_notes/0/draft",
      "editor:your_notes/0/f/brief",       "editor:your_notes/0/f/public.summary", "editor:your_notes/0/i/sayings/0",
      "editor:your_notes/0/i/sayings/1",   "editor:your_notes/0/i/sayings/2",    "editor:your_notes/0/add/sayings",
      "editor:your_notes/0/i/watch/0",     "editor:your_notes/0/add/watch",      "editor:your_notes/0/f/register",
      "editor:your_notes/0/f/oil",         "editor:your_notes/0/f/words_max",    "editor:your_notes/0/f/lit_at_dawn"};
  CHECK(stops == want);
  // Save: the document as published, every field but the shown one, its locked item and its keys sent back.
  PageView fresh = notes_view();
  REQUIRE(move_to(fresh, "editor:your_notes/0/save"));
  const Action save = press(fresh, Key::activate);
  REQUIRE(save.kind == Action::Kind::command);
  CHECK(save.command.name == "note.save");
  CHECK(save.command.args["target"] == "harbour-mouth");
  CHECK(save.command.args["rev"] == 1);
  const Json& doc = save.command.args["doc"];
  CHECK_FALSE(doc.contains("met"));
  CHECK(doc["brief"] == "Mouth lamp");
  CHECK(doc["sayings"][1] == Json::parse(R"({"text": "Green for going home", "key": "s2"})"));
  CHECK(doc["sayings"][2]["locked"] == "Written by hand: change it in the file.");
  CHECK(doc["watch"][0]["text"] == "Trimmed short");
  CHECK(doc["watch"][0]["choice"] == "storm");
  CHECK(doc["watch"][0]["key"] == "w1");
  CHECK(doc["words_max"] == 12);
  CHECK(doc["lit_at_dawn"] == false);
}

TEST_CASE("the editor in game: a line typed is an unsaved edit, kept when the menu closes; R undoes it (D217, G38 (2))") {
  PageView view = notes_view();
  REQUIRE(move_to(view, "editor:your_notes/0/f/brief"));
  press(view, Key::activate);
  REQUIRE(view.editing());
  type(view, "s of the wall, and more");  // the room is 24 characters: the rest is not typed
  view.cancel_confirmation();  // the menu's close routine
  CHECK_FALSE(view.editing());
  REQUIRE(note_state(view).edits.count("brief") == 1);
  CHECK(note_state(view).edits.at("brief") == "Mouth lamps of the wall,");
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  CHECK(list.dump().find("\"Unsaved changes\"") != std::string::npos);
  CHECK(list.dump().find("\"Unsaved\"") != std::string::npos);
  CHECK(list.dump().find("\"24 / 24\"") != std::string::npos);
  // Save sends the edit, over the revision it was made on.
  for (int i = 0; i < 12 && view.focused() != "editor:your_notes/0/save"; ++i) {
    press(view, Key::up);
  }
  const Action save = press(view, Key::activate);
  REQUIRE(save.kind == Action::Kind::command);
  CHECK(save.command.args["doc"]["brief"] == "Mouth lamps of the wall,");
  CHECK(save.command.args["rev"] == 1);
  REQUIRE(move_to(view, "editor:your_notes/0/f/brief"));
  press(view, Key::reset);
  CHECK(note_state(view).edits.empty());
  // A field that may be unset: R leaves it to the space, "not set here", sent as null.
  REQUIRE(move_to(view, "editor:your_notes/0/f/public.summary"));
  press(view, Key::reset);
  CHECK(note_state(view).edits.at("public.summary").is_null());
  DisplayList unset;
  view.layout(Viewport{}, test::fonts(), unset);
  CHECK(unset.dump().find("\"Not set here\"") != std::string::npos);
  press(view, Key::reset);  // and again: the edit undone, as published
  CHECK(note_state(view).edits.empty());
}

TEST_CASE("the editor in game: a list's items added, edited, moved and removed; a locked one stays (D217)") {
  PageView view = notes_view();
  REQUIRE(move_to(view, "editor:your_notes/0/add/sayings"));
  press(view, Key::activate);
  REQUIRE(view.editing());
  CHECK(view.focused() == "editor:your_notes/0/i/sayings/3");
  type(view, "Trim it at dusk");
  view.edit(EditInput{EditKey::enter});
  CHECK_FALSE(view.editing());
  const auto sayings = [&] { return note_state(view).edits.at("sayings"); };
  REQUIRE(sayings().size() == 4);
  CHECK(sayings()[3] == "Trim it at dusk");
  // Four is its most: no Add now.
  press(view, Key::down);
  CHECK(view.focused() == "editor:your_notes/0/i/watch/0");
  press(view, Key::up);
  // Shift+↑: the item moves up, past the locked one, focus with it.
  press(view, Key::up, true);
  CHECK(view.focused() == "editor:your_notes/0/i/sayings/2");
  CHECK(sayings()[2] == "Trim it at dusk");
  CHECK(sayings()[3]["key"] == "s3");
  // R on the locked item does nothing; on another it removes it.
  press(view, Key::down);
  press(view, Key::reset);
  CHECK(sayings().size() == 4);
  REQUIRE(move_to(view, "editor:your_notes/0/add/sayings") == false);  // none while there are four
  // An item added and left empty goes again.
  PageView other = notes_view();
  REQUIRE(move_to(other, "editor:your_notes/0/i/sayings/0"));
  press(other, Key::reset);
  CHECK(note_state(other).edits.at("sayings").size() == 2);
  REQUIRE(move_to(other, "editor:your_notes/0/add/sayings"));
  press(other, Key::activate);
  other.edit(EditInput{EditKey::escape});
  CHECK(note_state(other).edits.at("sayings").size() == 2);
  // An item's choice: ← → through the field's choices.
  REQUIRE(move_to(other, "editor:your_notes/0/i/watch/0"));
  press(other, Key::right);
  CHECK(note_state(other).edits.at("watch")[0]["choice"] == "dusk");  // past the last, back to the first
}

TEST_CASE("the editor in game: a choice, a pick, a number and a switch by ← → and Enter (D217)") {
  PageView view = notes_view();
  REQUIRE(move_to(view, "editor:your_notes/0/f/register"));
  press(view, Key::right);
  CHECK(note_state(view).edits.at("register") == "warm");
  press(view, Key::left);
  press(view, Key::left);  // before Plain: "not set here", for a field that may be unset
  CHECK(note_state(view).edits.at("register").is_null());
  press(view, Key::down);
  press(view, Key::right);
  CHECK(note_state(view).edits.at("oil") == "olive");  // from the topic's choices
  press(view, Key::down);
  press(view, Key::right, true);
  CHECK(note_state(view).edits.at("words_max") == 20);  // 12 and ten, within its most
  press(view, Key::down);
  press(view, Key::activate);
  CHECK(note_state(view).edits.at("lit_at_dawn") == true);
}

TEST_CASE("the editor in game: a check's messages, a save's conflict and Reload, a draft's proposals (D217)") {
  PageView view = notes_view();
  REQUIRE(move_to(view, "editor:your_notes/0/check"));
  REQUIRE(press(view, Key::activate).kind == Action::Kind::command);
  view.editor_answer("editor:your_notes/0/check",
                     answer(R"({"messages": [{"field": "brief", "level": "error", "words": "At most 24 characters."},
                                {"field": "sayings", "item": 1, "level": "warning", "words": "A word of the game's."},
                                {"level": "warning", "words": "The file is long."}]})"));
  DisplayList checked;
  view.layout(Viewport{}, test::fonts(), checked);
  CHECK(checked.dump().find("\"Error: At most 24 characters.\"") != std::string::npos);
  CHECK(checked.dump().find("\"Warning: A word of the game's.\"") != std::string::npos);
  CHECK(checked.dump().find("\"Warning: The file is long.\"") != std::string::npos);
  // A save over a revision the space has moved on from: the conflict, its band and Reload, which asks first.
  REQUIRE(move_to(view, "editor:your_notes/0/f/brief"));
  press(view, Key::activate);
  type(view, "!");
  view.edit(EditInput{EditKey::enter});
  for (int i = 0; i < 12 && view.focused() != "editor:your_notes/0/save"; ++i) {
    press(view, Key::up);
  }
  REQUIRE(view.focused() == "editor:your_notes/0/save");
  REQUIRE(press(view, Key::activate).kind == Action::Kind::command);
  grace::protocol::Result conflict;
  conflict.error = grace::protocol::ErrorCode::conflict;
  view.editor_answer("editor:your_notes/0/save", conflict);
  CHECK(note_state(view).conflict);
  DisplayList band;
  view.layout(Viewport{}, test::fonts(), band);
  CHECK(band.dump().find("\"Changed elsewhere since you opened it.\"") != std::string::npos);
  press(view, Key::up);
  REQUIRE(view.focused() == "editor:your_notes/0/reload");
  press(view, Key::activate);
  CHECK(note_state(view).dirty());  // asked first: the edit is still there
  press(view, Key::activate);
  CHECK_FALSE(note_state(view).dirty());
  CHECK_FALSE(note_state(view).conflict);
  // A draft: the notes typed, sent, Working… while it runs; its proposals beside their fields.
  REQUIRE(move_to(view, "editor:your_notes/0/notes"));
  press(view, Key::activate);
  type(view, "tall and green");
  view.edit(EditInput{EditKey::escape});
  CHECK(note_state(view).notes == "tall and green");
  press(view, Key::down);
  const Action draft = press(view, Key::activate);
  REQUIRE(draft.kind == Action::Kind::command);
  CHECK(draft.command.name == "note.draft");
  CHECK(draft.command.args == Json::parse(R"({"target": "harbour-mouth", "notes": "tall and green"})"));
  CHECK(view.running("editor:your_notes/0/draft"));
  view.finished("note.draft");
  view.editor_answer("editor:your_notes/0/draft",
                     answer(R"({"draft": {"brief": "Sea lamp", "public.summary": "A tall green lamp.", "met": "no"},
                                "notice": ""})"));
  CHECK(note_state(view).draft.size() == 2);
  DisplayList drafted;
  view.layout(Viewport{}, test::fonts(), drafted);
  CHECK(drafted.dump().find("\"Use all\"") != std::string::npos);
  CHECK(drafted.dump().find("\"Sea lamp\"") != std::string::npos);
  REQUIRE(move_to(view, "editor:your_notes/0/use/brief"));
  press(view, Key::activate);
  CHECK(note_state(view).edits.at("brief") == "Sea lamp");
  REQUIRE(move_to(view, "editor:your_notes/0/keep/public.summary"));
  press(view, Key::activate);
  CHECK(note_state(view).draft.empty());
  CHECK(note_state(view).edits.count("public.summary") == 0);
  // Leaving the page lets a draft go; the edits stay for the session.
  view.editor_answer("editor:your_notes/0/draft", answer(R"({"draft": {"brief": "Wall lamp"}})"));
  view.open("demo.log");
  view.open("demo.lamps");
  CHECK(note_state(view).draft.empty());
  CHECK(note_state(view).edits.at("brief") == "Sea lamp");
}

TEST_CASE("the editor in game: Draft greyed with the space's words while it says why not (D217)") {
  PageView view = notes_view();
  view.set_topic("lantern.note/harbour-mouth",
                 Json::parse(R"({"target": "harbour-mouth", "rev": 2, "draft_blocked": "The drafter is resting.",
                                 "fields": [{"id": "brief", "label": "Short name", "kind": "line", "value": ""}]})"));
  REQUIRE(move_to(view, "editor:your_notes/0/draft"));
  CHECK(press(view, Key::activate).kind == Action::Kind::none);
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  CHECK(list.dump().find("\"The drafter is resting.\"") != std::string::npos);
}

TEST_CASE("the editor in game: a prose field wraps, and ↑ ↓ move the caret by line (D217, one paragraph)") {
  PageView view = notes_view();
  REQUIRE(move_to(view, "editor:your_notes/0/f/public.summary"));
  press(view, Key::activate);
  REQUIRE(view.editing());
  view.pasted(" Its glass was poured in the old way, thick at the foot and thin at the top, and it hums in a gale.");
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  const Rect first = view.caret();
  view.edit(EditInput{EditKey::home});
  view.layout(Viewport{}, test::fonts(), list);
  const Rect home = view.caret();
  REQUIRE(home.y < first.y);  // more than one line
  view.edit(EditInput{EditKey::end});
  view.layout(Viewport{}, test::fonts(), list);
  CHECK(view.caret().y == first.y);  // the caret was at the end
  view.edit(EditInput{EditKey::up});
  view.layout(Viewport{}, test::fonts(), list);
  const Rect up = view.caret();
  CHECK(up.y < first.y);  // a line up
  view.edit(EditInput{EditKey::up});
  view.edit(EditInput{EditKey::up});
  view.edit(EditInput{EditKey::up});
  view.layout(Viewport{}, test::fonts(), list);
  CHECK(view.caret().y == home.y);
  CHECK(view.caret().x == home.x);  // from the first line, ↑ goes to the start
  // Enter ends the edit: a prose field is one paragraph.
  view.edit(EditInput{EditKey::enter});
  CHECK_FALSE(view.editing());
}

TEST_CASE("the editor in game: Reset all asks first, by key and by the pointer alike (D217)") {
  PageView view = notes_view();
  REQUIRE(move_to(view, "editor:your_notes/0/reset"));
  CHECK(press(view, Key::activate).kind == Action::Kind::none);
  const Action reset = press(view, Key::activate);
  REQUIRE(reset.kind == Action::Kind::command);
  CHECK(reset.command.name == "note.reset");
  CHECK(reset.command.args == Json::parse(R"({"target": "harbour-mouth", "all": true, "confirm": true})"));
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  const PageView::Bounds button = view.bounds_of("editor:your_notes/0/reset");
  REQUIRE(button.row.w > 0);
  const Pointer click{Pointer::Kind::press, button.row.x + 5, button.row.y + 5};
  CHECK(view.pointer(click).kind == Action::Kind::none);
  view.layout(Viewport{}, test::fonts(), list);
  CHECK(list.dump().find("\"Press again to confirm\"") != std::string::npos);
  CHECK(view.pointer(click).kind == Action::Kind::command);
}

TEST_CASE("a save's answer names the row to choose; taken once the list has it, unless chosen by hand first (D218)") {
  PageView view = notes_view();
  REQUIRE(move_to(view, "editor:your_notes/0/save"));
  REQUIRE(press(view, Key::activate).kind == Action::Kind::command);
  // The answer first, its row not listed yet: kept; then the list with it, chosen, and its member watched.
  view.editor_answer("editor:your_notes/0/save", answer(R"({"chosen": "lower-steps"})"));
  CHECK(view.chosen() == "harbour-mouth");
  Json lamps = kLamps;
  lamps["rows"].push_back(Json::parse(R"({"id": "lower-steps", "name": "Lower steps", "place": "By the water"})"));
  view.set_topic("lantern.lamps", lamps);
  CHECK(view.chosen() == "lower-steps");
  CHECK(watches(view, "lantern.note/lower-steps"));
  // Its document comes: focus is back where the player left it.
  Json note = demo_topic("lantern.note/harbour-mouth");
  note["target"] = "lower-steps";
  view.set_topic("lantern.note/lower-steps", note);
  CHECK(view.focused() == "editor:your_notes/0/save");
  // A row already listed: chosen at once. A key that names no member, or a refused save: nothing.
  REQUIRE(press(view, Key::activate).kind == Action::Kind::command);
  view.editor_answer("editor:your_notes/0/save", answer(R"({"chosen": "fish-steps"})"));
  CHECK(view.chosen() == "fish-steps");
  view.set_topic("lantern.note/fish-steps", demo_topic("lantern.note/fish-steps"));
  REQUIRE(view.focused() == "editor:your_notes/0/save");
  REQUIRE(press(view, Key::activate).kind == Action::Kind::command);
  view.editor_answer("editor:your_notes/0/save", answer(R"({"chosen": "Not/a key"})"));
  CHECK(view.chosen() == "fish-steps");
  // Chosen by hand before the row comes: the save's choice goes.
  REQUIRE(press(view, Key::activate).kind == Action::Kind::command);
  view.editor_answer("editor:your_notes/0/save", answer(R"({"chosen": "far-end"})"));
  for (int i = 0; i < 16 && view.focused().rfind("item:", 0) != 0; ++i) {
    press(view, Key::up);
  }
  REQUIRE(view.focused().rfind("item:", 0) == 0);
  press(view, Key::down);
  const std::string by_hand = view.chosen();
  lamps["rows"].push_back(Json::parse(R"({"id": "far-end", "name": "Far end", "place": "Past the crane"})"));
  view.set_topic("lantern.lamps", lamps);
  CHECK(view.chosen() == by_hand);
}

TEST_CASE("search across spaces in game: another space's settings found, named by it; opening one switches (D219)") {
  PageView demo = loaded("demo", "demo.overview");
  PageView own = loaded("graceui", "graceui.menu");
  demo.set_search_others({{"graceui", "GraceUI", &own}});
  own.set_search_others({{"demo", "The demo space", &demo}});
  // GraceUI's Menu size, found from the demo space, with its space's name before its page's.
  const auto hits = demo.search_all("menu size", 8);
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].key == "ui.scale");
  CHECK(hits[0].space == "graceui");
  CHECK(hits[0].where == "Menu");
  press(demo, Key::search);
  type(demo, "menu size");
  DisplayList list;
  demo.layout(Viewport{}, test::fonts(), list);
  CHECK(list.dump().find("\"GraceUI › Menu\"") != std::string::npos);
  // Enter: the host is asked to switch, with the page and the row; the view's search closes.
  const Action open = demo.edit(EditInput{EditKey::enter});
  REQUIRE(open.kind == Action::Kind::switch_space);
  CHECK(open.space == "graceui");
  CHECK(open.page == "graceui.menu");
  CHECK(open.key == "ui.scale");
  CHECK_FALSE(demo.editing());
  own.reveal("ui.scale");
  CHECK(own.focused() == "setting:ui.scale");
  // A label in both: the view's own first; the cap holds across spaces.
  CHECK(demo.search_all("a", 3).size() == 3);
  const auto both = own.search_all("e", 64);
  for (std::size_t i = 1; i < both.size(); ++i) {
    CHECK(both[i - 1].label <= both[i].label);
  }
}

TEST_CASE("a table's filter bar in game: its selects, its sort and direction, its search, Clear filters (G43, D250)") {
  PageView view = loaded("demo", "demo.keepers");
  view.set_topic("lantern.keepers", kKeepers);
  const auto walk_to = [&](const std::string& name) {
    for (int i = 0; i < 40 && view.focused() != name; ++i) {
      press(view, Key::down);
    }
    return view.focused() == name;
  };
  // The rows' focus stops, in the order they show.
  const auto rows = [&] {
    std::vector<std::string> out;
    const std::string before = view.focused();
    for (int i = 0; i < 60; ++i) {
      const std::string at = view.focused();
      if (at.rfind("row:lantern.keepers/", 0) == 0) {
        out.push_back(at.substr(20, at.find('/', 20) - 20));
      }
      press(view, Key::down);
      if (view.focused() == at) {
        break;
      }
    }
    while (view.focused() != before) {
      press(view, Key::up);
    }
    return out;
  };
  const auto dump = [&] {
    DisplayList list;
    view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
    return list.dump();
  };
  REQUIRE(walk_to("filter:lantern.keepers/f0"));
  CHECK(dump().find("\"Post\"") != std::string::npos);
  CHECK(dump().find("\"All\"") != std::string::npos);
  CHECK(rows() == std::vector<std::string>{"k1", "k2", "k3", "k4"});  // no Time kept here: the space's order
  // → takes the first post the rows hold: one row left, and the count line says so.
  press(view, Key::right);
  CHECK(view.focused() == "filter:lantern.keepers/f0");
  CHECK(rows() == std::vector<std::string>{"k1"});
  CHECK(dump().find("\"1 of 4 shown\"") != std::string::npos);
  for (int i = 0; i < 6; ++i) {
    press(view, Key::right);  // past the last post it stays there
  }
  CHECK(rows() == std::vector<std::string>{"k4"});
  for (int i = 0; i < 6; ++i) {
    press(view, Key::left);  // back to All
  }
  CHECK(rows().size() == 4);
  CHECK(dump().find("of 4 shown") == std::string::npos);
  // Sort by Name, then reversed.
  REQUIRE(walk_to("filter:lantern.keepers/sort"));
  press(view, Key::right);
  CHECK(rows() == std::vector<std::string>{"k3", "k4", "k1", "k2"});  // Ada, Bran, Orla, Tamsin
  REQUIRE(walk_to("filter:lantern.keepers/dir"));
  press(view, Key::activate);
  CHECK(rows() == std::vector<std::string>{"k2", "k1", "k4", "k3"});
  CHECK(dump().find("tri     ink.hi") != std::string::npos);  // its arrow, drawn
  // The search, as it is typed; nothing matching offers Clear filters, which brings every row back.
  REQUIRE(walk_to("filter:lantern.keepers/search"));
  press(view, Key::activate);
  type(view, "ra");
  CHECK(dump().find("\"Bran Teel\"") != std::string::npos);  // as it is typed
  CHECK(dump().find("\"Orla Venn\"") == std::string::npos);
  press(view, Key::activate);  // done typing
  CHECK(rows() == std::vector<std::string>{"k4"});  // Bran
  press(view, Key::activate);
  type(view, "zz");
  press(view, Key::activate);
  CHECK(rows().empty());
  CHECK(dump().find("\"Nothing matches these filters.\"") != std::string::npos);
  REQUIRE(walk_to("filter:lantern.keepers/clear"));
  press(view, Key::activate);
  CHECK(rows().size() == 4);
  CHECK(view.focused() == "filter:lantern.keepers/f0");
  // The rows' commands still name their own rows, whatever order they show in.
  REQUIRE(walk_to("row:lantern.keepers/k3/dismiss"));
  CHECK(press(view, Key::activate).kind == Action::Kind::none);  // it asks first
  const Action sent = press(view, Key::activate);
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.args["id"] == "k3");
}

TEST_CASE("a master list's filter bar in game: above its rows, and ↑ from the first row reaches it (D250)") {
  PageView view = loaded("demo", "demo.lamps");
  view.set_topic("lantern.lamps", Json::parse(R"({"columns": [{"id": "name", "label": "Lamp"},
    {"id": "place", "label": "Where"}], "rows": [
    {"id": "harbour-mouth", "name": "Harbour mouth", "place": "The north end", "along_m": 0},
    {"id": "fish-steps", "name": "Fish steps", "place": "Halfway along", "along_m": 120},
    {"id": "old-crane", "name": "Old crane", "place": "The inner end", "along_m": 260}]})"));
  // The rows along the wall (its first sort), and ↑ from the first row reaches the bar above them.
  for (int i = 0; i < 6 && view.focused() != "item:lantern.lamps/harbour-mouth"; ++i) {
    press(view, Key::up);
  }
  REQUIRE(view.focused() == "item:lantern.lamps/harbour-mouth");
  press(view, Key::down);
  CHECK(view.focused() == "item:lantern.lamps/fish-steps");
  press(view, Key::up);
  press(view, Key::up);
  CHECK(view.focused() == "filter:master:lantern.lamps/search");
  // Find a lamp: "crane" leaves one row; ↑ from it goes back to the bar.
  press(view, Key::activate);
  type(view, "crane");
  press(view, Key::activate);
  press(view, Key::down);
  CHECK(view.focused() == "item:lantern.lamps/old-crane");
  press(view, Key::up);
  CHECK(view.focused() == "filter:master:lantern.lamps/search");
}

namespace {

const Json kGuides = Json::parse(R"({"columns": [{"id": "title", "label": "Guide"}, {"id": "headings", "label": "Covers"}],
  "rows": [{"id": "lighting", "title": "Lighting the lamp", "headings": "Before dusk"},
           {"id": "oil", "title": "Oil", "headings": "Which oil"}]})");
const Json kLighting = Json::parse(R"({"title": "Lighting the lamp", "blocks": [
  {"paragraph": [{"text": "Read "}, {"text": "Oil", "doc": "oil", "heading": "Which oil"}, {"text": " first; "},
                 {"text": "Signals", "doc": "signals"}, {"text": " is not written yet."}]},
  {"heading": "Before dusk"},
  {"list": ["Clean the glass.", "Fill the oil."], "ordered": true},
  {"note": "Never leave a lit lamp alone."},
  {"code": "dusk  trimmed\nnight lit"},
  {"table": {"columns": [{"id": "oil", "label": "Oil"}], "rows": [{"oil": "Rapeseed"}]}}]})");
const Json kReport = Json::parse(R"({"parts": [
  {"id": "summary", "label": "Summary", "optional": false, "fields": [{"label": "Lamp", "value": "North light"}]},
  {"id": "log", "label": "Recent log", "optional": true, "included": true,
   "fields": [{"label": "Lines", "value": "19:44  A moth.\n19:48  The wind rose.", "code": true}]},
  {"id": "notes", "label": "Your notes", "optional": true, "included": false,
   "fields": [{"label": "Notes", "value": "The glass wants a new seal."}]}]})");

}  // namespace

TEST_CASE("a document in game: its blocks drawn, a link to a listed guide a stop that chooses it, none to the rest (D251)") {
  PageView view = loaded("demo", "demo.handbook");
  view.set_topic("lantern.guides", kGuides);
  // The chosen guide's member is one of the page's topics, so that the host watches it (EldenNet's 0.2.44 pin found it
  // left out: the reader said "Nothing to show." for every document).
  const auto watched = view.component_topics();
  CHECK(std::find(watched.begin(), watched.end(), "lantern.guide/lighting") != watched.end());
  view.set_topic("lantern.guide/lighting", kLighting);
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  const std::string dump = list.dump();
  for (const char* words : {"\"Lighting the lamp\"", "\"Before dusk\"", "\"1.\"", "\"Clean the glass.\"",
                            "\"Never leave a lit lamp alone.\"", "\"dusk  trimmed\"", "\"night lit\"", "\"Rapeseed\""}) {
    CHECK(dump.find(words) != std::string::npos);
  }
  CHECK(dump.find("text    gold.hi") != std::string::npos);  // the link to Oil, in gold
  // One stop: the link to Oil; Signals is not in the list, so its words are plain. → goes into the detail.
  press(view, Key::right);
  std::vector<std::string> links;
  for (int i = 0; i < 30; ++i) {
    const std::string at = view.focused();
    if (at.rfind("link:", 0) == 0 && std::find(links.begin(), links.end(), at) == links.end()) {
      links.push_back(at);
    }
    press(view, Key::down);
    if (view.focused() == at) {
      break;
    }
  }
  REQUIRE(links == std::vector<std::string>{"link:lantern.guide/lighting/0"});
  while (view.focused() != links[0]) {
    press(view, Key::up);
  }
  CHECK(press(view, Key::activate).kind == Action::Kind::none);
  CHECK(view.chosen() == "oil");
}

TEST_CASE("a link to a heading in game: brought to the top; one the document lacks leaves it at its top, and is not kept (D251)") {
  // A long guide: its heading far down the page.
  const auto guide = [](bool with_heading) {
    Json blocks = Json::array();
    for (int i = 0; i < 40; ++i) {
      blocks.push_back(Json::object({{"paragraph", "Pour slowly, and wipe the rim after every fill."}}));
    }
    if (with_heading) {
      blocks.push_back(Json::object({{"heading", "Which oil"}}));
    }
    for (int i = 0; i < 40; ++i) {
      blocks.push_back(Json::object({{"paragraph", "Rapeseed burns clean; tallow smokes."}}));
    }
    return Json::object({{"title", "Oil and its keeping"}, {"blocks", blocks}});
  };
  const auto follow = [](PageView& view) {
    view.set_topic("lantern.guides", kGuides);
    view.set_topic("lantern.guide/lighting", kLighting);
    DisplayList list;
    view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
    press(view, Key::right);
    for (int i = 0; i < 30 && view.focused() != "link:lantern.guide/lighting/0"; ++i) {
      press(view, Key::down);
    }
    REQUIRE(view.focused() == "link:lantern.guide/lighting/0");
    press(view, Key::activate);
    REQUIRE(view.chosen() == "oil");
    CHECK(view.focused() == "item:lantern.guides/oil");  // on its row, where the player now is
  };
  const auto y_of = [](PageView& view, const char* words) {
    DisplayList list;
    view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
    view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);  // a scroll asked for shows at the next layout
    for (const Item& item : list.items) {
      if (item.shape == Shape::text && std::string_view(list.text).substr(item.text_first, item.text_size) == words &&
          item.rect.x > 300) {
        return item.rect.y;  // in the detail, right of the rail and the list's rows
      }
    }
    return -1e9f;
  };
  PageView found = loaded("demo", "demo.handbook");
  follow(found);
  found.set_topic("lantern.guide/oil", guide(true));
  CHECK(y_of(found, "Which oil") < 200);  // at the page's top, the guide's title above it, scrolled away
  PageView lacking = loaded("demo", "demo.handbook");
  follow(lacking);
  lacking.set_topic("lantern.guide/oil", guide(false));
  const float top = y_of(lacking, "Oil and its keeping");
  CHECK(top > 0);
  CHECK(top < 400);
  // The guide gains that heading later: the link is long followed, so nothing moves.
  lacking.set_topic("lantern.guide/oil", guide(true));
  CHECK(y_of(lacking, "Oil and its keeping") == top);
  CHECK(y_of(lacking, "Which oil") > 1080);
}

TEST_CASE("a report preview in game: its parts, a left-out part's fields hidden, an optional part's switch sends (D251)") {
  PageView view = loaded("demo", "demo.log");
  view.set_topic("lantern.report", kReport);
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  const std::string dump = list.dump();
  CHECK(dump.find("\"Summary\"") != std::string::npos);
  CHECK(dump.find("\"19:48  The wind rose.\"") != std::string::npos);  // a code field's second line
  CHECK(dump.find("\"Left out\"") != std::string::npos);
  CHECK(dump.find("The glass wants a new seal.") == std::string::npos);  // the left-out part's field
  // The switches: Recent log's and Your notes', not Summary's.
  std::vector<std::string> parts;
  for (int i = 0; i < 40; ++i) {
    const std::string at = view.focused();
    if (at.rfind("part:", 0) == 0 && std::find(parts.begin(), parts.end(), at) == parts.end()) {
      parts.push_back(at);
    }
    press(view, Key::down);
    if (view.focused() == at) {
      break;
    }
  }
  CHECK(parts == std::vector<std::string>{"part:lantern.report/log", "part:lantern.report/notes"});
  while (view.focused() != "part:lantern.report/log") {
    press(view, Key::up);
  }
  Action sent = press(view, Key::activate);
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.name == "report.part");
  CHECK(sent.command.args == Json::object({{"id", "log"}, {"on", false}}));
  press(view, Key::down);
  sent = press(view, Key::right);
  REQUIRE(sent.kind == Action::Kind::command);
  CHECK(sent.command.args == Json::object({{"id", "notes"}, {"on", true}}));
}

TEST_CASE("a form's prose field in game: the editor's prose well, several lines, sent with the rest (D251)") {
  PageView view = loaded("demo", "demo.keepers");
  view.set_topic("lantern.keepers", kKeepers);
  std::string notes;
  for (int i = 0; i < 60 && notes.empty(); ++i) {
    const std::string at = view.focused();
    if (at.rfind("field:", 0) == 0 && at.ends_with("/notes")) {
      notes = at;
      break;
    }
    press(view, Key::down);
  }
  REQUIRE(!notes.empty());
  press(view, Key::activate);
  type(view, "Knows the tide and the wind both");
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  CHECK(list.dump().find("\"Knows the tide and the wind both\"") != std::string::npos);
  CHECK(view.footer().find("Line") != std::string::npos);  // ↑ ↓ move by line in it
}

TEST_CASE("quick switches at registration: a switch's or a choice's shortcut by its key alone, or a session switch (D252)") {
  const auto settings = test::decoded<grace::protocol::SettingsDescription>(test::space("demo").settings);
  const auto demo = [] { return test::decoded<grace::protocol::SpacePages>(test::space("demo").pages); };
  PageView view;
  std::string why;
  auto pages = demo();
  REQUIRE(pages.quick);
  CHECK(pages.quick->size() == 3);
  CHECK(view.load(pages, settings, why));
  pages = demo();
  pages.quick->push_back(grace::protocol::QuickSwitch{.key = "lantern.keeper"});  // a text setting
  CHECK_FALSE(view.load(pages, settings, why));
  CHECK(why == "quick switch lantern.keeper is a shortcut to a switch or a choice of the space's, by its key alone");
  pages = demo();
  (*pages.quick)[2].command = "oil.order";  // a slow command
  CHECK_FALSE(view.load(pages, settings, why));
  CHECK(why.rfind("quick switch hush is a session switch", 0) == 0);
  pages = demo();
  (*pages.quick)[2].state->topic = "lantern.log";  // a stream
  CHECK_FALSE(view.load(pages, settings, why));
  pages = demo();
  (*pages.quick)[2].home = "demo.nowhere";
  CHECK_FALSE(view.load(pages, settings, why));
}

namespace {

std::string laid_out(PageView& view) {
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  return list.dump();
}

bool shows(const std::string& dump, const std::string& words) { return dump.find("\"" + words + "\"") != std::string::npos; }

}  // namespace

TEST_CASE("quick switches in game: Start opens them; a setting's switch and choice; a session switch once its state came (D252)") {
  PageView view = loaded("demo", "demo.keepers");  // not a live page
  std::string dump = laid_out(view);
  CHECK(shows(dump, "Quick switches"));
  CHECK(dump.find("Updates:") == std::string::npos);
  const auto watched = view.component_topics();
  CHECK(std::find(watched.begin(), watched.end(), "lantern.status") != watched.end());  // the session switch's state
  press(view, Key::quick);
  REQUIRE(view.quick_open());
  dump = laid_out(view);
  for (const char* words : {"Lantern lit", "Flame colour", "Amber", "Hush the bell for this session", "Not known yet",
                            "Lives in Behaviour", "Lives in Overview"}) {
    CHECK(shows(dump, words));
  }
  CHECK(view.footer() == "↑ ↓  Choose    Enter  Flip    Esc  Close");
  Action a = press(view, Key::activate);  // the lantern's switch, as its row turns it
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.name == "settings.set");
  CHECK(a.command.args["key"] == "lantern.enabled");
  press(view, Key::down);
  laid_out(view);
  CHECK(view.footer() == "↑ ↓  Choose    ← →  Change    Esc  Close");
  a = press(view, Key::right);  // the colour, a step on
  CHECK(a.command.args == Json::object({{"key", "lantern.colour"}, {"value", "white"}}));
  press(view, Key::down);
  CHECK(press(view, Key::activate).kind == Action::Kind::none);  // its state has not come: nothing sent
  view.set_topic("lantern.status", Json::object({{"sentence", "Steady."}, {"hushed", false}}));
  a = press(view, Key::activate);
  REQUIRE(a.kind == Action::Kind::command);
  CHECK(a.command.name == "bell.hush");
  CHECK(a.command.args == Json::object({{"on", true}}));
  CHECK(view.quick_open());  // it stays open: the space's next state shows the change (invariant 1)
  view.set_topic("lantern.status", Json::object({{"sentence", "Steady."}, {"hushed", true}}));
  CHECK_FALSE(shows(laid_out(view), "Not known yet"));
  press(view, Key::back);
  CHECK_FALSE(view.quick_open());
  // From the top bar: → from search reaches Quick switches, and Enter opens them.
  press(view, Key::search);
  view.edit(EditInput{EditKey::escape});
  REQUIRE(view.focused() == "search");
  press(view, Key::right);
  CHECK(view.focused() == "top:quick");
  press(view, Key::activate);
  CHECK(view.quick_open());
  press(view, Key::quick);
  CHECK_FALSE(view.quick_open());
  // A space with none: no pill, and Start does nothing.
  PageView own = loaded("graceui", "graceui.menu");
  CHECK_FALSE(shows(laid_out(own), "Quick switches"));
  press(own, Key::quick);
  CHECK_FALSE(own.quick_open());
}

TEST_CASE("live updates in game: Every 5 s and Paused hold a live page's topics; Live, a command or leaving apply them (D252)") {
  const auto status = [](const char* sentence, bool hushed) {
    return Json::object({{"sentence", sentence}, {"hushed", hushed}});
  };
  PageView view = loaded("demo", "demo.overview");
  view.tick(0);
  view.set_live_mode(PageView::LiveMode::paused);
  view.set_topic("lantern.status", status("The lantern burns steady.", false));  // a first body is always drawn
  std::string dump = laid_out(view);
  CHECK(shows(dump, "The lantern burns steady."));
  CHECK(shows(dump, "Updates: Paused"));
  view.set_topic("lantern.status", status("The wind rose.", true));
  view.set_topic("lantern.status", status("The wind fell.", true));
  dump = laid_out(view);
  CHECK(shows(dump, "The lantern burns steady."));
  CHECK(shows(dump, "Updates: Paused · 2 new"));
  CHECK(view.holding());
  press(view, Key::quick);  // the menu shows the newest state, held or not
  CHECK_FALSE(shows(laid_out(view), "Not known yet"));
  press(view, Key::quick);
  // The top bar's Live updates: Enter steps Live, Every 5 s, Paused; from Paused to Live, what was held is drawn.
  press(view, Key::search);
  view.edit(EditInput{EditKey::escape});
  press(view, Key::right);
  REQUIRE(view.focused() == "top:live");
  press(view, Key::activate);
  CHECK(view.live_mode() == PageView::LiveMode::live);
  dump = laid_out(view);
  CHECK(shows(dump, "The wind fell."));
  CHECK(shows(dump, "Updates: Live"));
  CHECK_FALSE(view.holding());
  // Every 5 s: at most one body every five seconds.
  press(view, Key::activate);
  CHECK(view.live_mode() == PageView::LiveMode::every_5s);
  view.tick(1000);
  view.set_topic("lantern.status", status("A moth.", true));
  CHECK(view.holding());
  CHECK_FALSE(view.tick(4000));
  CHECK(view.tick(5000));
  CHECK(shows(laid_out(view), "A moth."));
  view.tick(6000);
  view.set_topic("lantern.status", status("Gulls.", true));
  CHECK(shows(laid_out(view), "A moth."));
  // The player's own command applies what is held, and what comes in the next 3 s is not held.
  press(view, Key::quick);
  CHECK(press(view, Key::activate).kind == Action::Kind::command);
  press(view, Key::quick);
  CHECK(shows(laid_out(view), "Gulls."));
  view.set_topic("lantern.status", status("Rain.", true));
  CHECK(shows(laid_out(view), "Rain."));
  view.tick(9500);
  view.set_topic("lantern.status", status("Fog.", true));
  CHECK(shows(laid_out(view), "Rain."));
  // Leaving the page applies what it held; a page that is not live holds nothing.
  REQUIRE(view.open("demo.keepers"));
  CHECK_FALSE(view.holding());
  view.set_live_mode(PageView::LiveMode::paused);
  view.set_topic("lantern.status", status("Clear.", false));
  CHECK_FALSE(view.holding());
  CHECK(laid_out(view).find("Updates:") == std::string::npos);
}

TEST_CASE("a report preview's field in game: its words wrapped to its column, its line breaks kept (D251)") {
  PageView view = loaded("demo", "demo.log");
  const std::string happened =
      "The lamp went out at dusk.\nThe keeper could not light it again until the wind dropped, long after the bell "
      "had rung twice for the harbour and the boats were in, and the glass had to be cleaned of soot before morning.";
  view.set_topic("lantern.report", Json::parse(R"({"parts": [{"id": "summary", "label": "Summary", "optional": false,
    "fields": [{"label": "What happened", "value": )" + Json(happened).dump() + "}]}]}"));
  DisplayList list;
  view.layout(Viewport{1280, 720, 1.0f}, test::fonts(), list);
  std::vector<std::string> lines;
  for (const Item& item : list.items) {
    if (item.shape == Shape::text) {
      const std::string words(std::string_view(list.text).substr(item.text_first, item.text_size));
      if (happened.find(words) != std::string::npos && words.size() > 8) {
        lines.push_back(words);
      }
    }
  }
  REQUIRE(lines.size() >= 3);  // its first line, then the second wrapped onto two or more
  CHECK(lines.front() == "The lamp went out at dusk.");
  CHECK(lines.back().ends_with("before morning."));
  std::string joined;
  for (std::size_t i = 1; i < lines.size(); ++i) {
    joined += (i > 1 ? " " : "") + lines[i];
  }
  CHECK(joined == happened.substr(happened.find('\n') + 1));  // every word, in order
}

namespace {

const Json kSeasons = Json::parse(R"({"choices": [{"id": "winter", "label": "This winter"},
  {"id": "autumn", "label": "Last autumn", "read_only": true}], "current": "winter"})");

grace::protocol::Result page_of(std::initializer_list<int> nights, const char* next, int total) {
  grace::protocol::Result r;
  r.ok = true;
  Json rows = Json::array();
  for (const int n : nights) {
    rows.push_back(Json::object({{"id", "l" + std::to_string(n)}, {"lit", "Night " + std::to_string(n)},
                                 {"lamp", "North light"}, {"keeper", "Orla Venn"}, {"at_ms", n}}));
  }
  Json body = Json::object({{"columns", Json::array({Json::object({{"id", "lit"}, {"label", "Lit"}}),
                                                     Json::object({{"id", "lamp"}, {"label", "Lamp"}})})},
                            {"rows", rows}, {"total", total}});
  if (next != nullptr) {
    body["next"] = next;
  }
  r.body = body;
  return r;
}

bool has(const std::vector<std::string>& topics, const char* topic) {
  return std::find(topics.begin(), topics.end(), topic) != topics.end();
}

}  // namespace

TEST_CASE("a scoped page in game: the scope's chooser, its members, a list read a page at a time, a read-only scope (D254)") {
  PageView view = loaded("demo", "demo.lightings");
  CHECK(view.take_page_requests().empty());  // its scope has not come: nothing is asked
  CHECK(has(view.component_topics(), "lantern.seasons"));
  view.set_topic("lantern.seasons", kSeasons);
  CHECK(view.scope_id() == "winter");
  CHECK(shows(laid_out(view), "Season: This winter"));
  CHECK(shows(laid_out(view), "Loading\u2026"));
  auto asked = view.take_page_requests();
  REQUIRE(asked.size() == 1);
  CHECK(asked[0].where == "page:demo.lightings.list#1");
  CHECK(asked[0].command.name == "lightings.page");
  CHECK(asked[0].command.args == Json::object({{"view", "list"},
                                               {"sort", Json::object({{"key", "at_ms"}, {"direction", "descending"}})},
                                               {"scope", "winter"}}));
  CHECK(view.take_page_requests().empty());  // asked once
  view.page_answer(asked[0].where, page_of({60, 59, 58}, "c2", 40));
  std::string dump = laid_out(view);
  CHECK(shows(dump, "Night 60"));
  CHECK(shows(dump, "Show more"));
  CHECK(shows(dump, "40 in all"));
  CHECK(view.chosen() == "l60");  // the first row chosen, as a topic's list does (D215)
  const auto topics = view.component_topics();
  CHECK(has(topics, "lantern.lighting/l60"));  // the detail family: the chosen row's member
  CHECK(has(topics, "lantern.season/winter"));  // any other family: the scope's member
  CHECK(has(topics, "lantern.status"));         // its changes topic
  // Show more: the next page asked for with the space's cursor, its rows added below.
  CHECK(move_to(view, "paging:demo.lightings.list/more"));
  press(view, Key::activate);
  asked = view.take_page_requests();
  REQUIRE(asked.size() == 1);
  CHECK(asked[0].command.args["cursor"] == "c2");
  view.page_answer(asked[0].where, page_of({57, 56}, nullptr, 40));
  dump = laid_out(view);
  CHECK(shows(dump, "Night 56"));
  CHECK_FALSE(shows(dump, "Show more"));
  // New rows: past the first page, said at the top and nothing moved until asked; then the first page again.
  view.set_topic("lantern.status", Json::object({{"sentence", "Steady."}, {"hushed", false}}));
  view.set_topic("lantern.status", Json::object({{"sentence", "A lamp was lit."}, {"hushed", false}}));
  CHECK(view.take_page_requests().empty());
  CHECK(shows(laid_out(view), "New rows \u00B7 Show"));
  // The scope: Enter steps to the next choice; the page's members and its rows follow; a read-only one greys it.
  press(view, Key::search);
  view.edit(EditInput{EditKey::escape});
  press(view, Key::right);
  press(view, Key::right);
  REQUIRE(view.focused() == "top:scope");
  press(view, Key::activate);
  CHECK(view.scope_id() == "autumn");
  dump = laid_out(view);
  CHECK(shows(dump, "Season: Last autumn"));
  CHECK(shows(dump, "Last autumn is read only."));
  CHECK(has(view.component_topics(), "lantern.season/autumn"));
  asked = view.take_page_requests();
  REQUIRE(asked.size() == 1);
  CHECK(asked[0].command.args["scope"] == "autumn");
  CHECK_FALSE(asked[0].command.args.contains("cursor"));
  // A late answer to a page asked before is dropped; a refusal's words stand in for the rows.
  CHECK(asked[0].where == "page:demo.lightings.list#3");
  dump = laid_out(view);
  CHECK_FALSE(shows(dump, "Night 60"));  // another season's rows are not kept while its own are asked for
  CHECK(shows(dump, "Loading\u2026"));
  view.page_answer("page:demo.lightings.list#2", page_of({1}, nullptr, 1));
  CHECK_FALSE(shows(laid_out(view), "Night 1"));
  grace::protocol::Result refused;
  refused.error = grace::protocol::ErrorCode::not_now;
  refused.detail = "Load a lantern to see this.";
  view.page_answer(asked[0].where, refused);
  CHECK(shows(laid_out(view), "Load a lantern to see this."));
  // The menu opens again: the current season, and the pages asked for afresh.
  view.cancel_confirmation();
  CHECK(view.scope_id() == "winter");
  CHECK(view.take_page_requests().size() == 1);
}

TEST_CASE("a table read a page at a time in game: its filter's choices from a topic, sent to the space; changes ask again (D254)") {
  PageView view = loaded("demo", "demo.log");
  view.set_topic("lantern.status", Json::object({{"sentence", "Steady."}, {"hushed", false},
                                                 {"lamps", Json::array({"North light", "Harbour wall"})}}));
  auto asked = view.take_page_requests();
  REQUIRE(asked.size() == 1);
  CHECK(asked[0].where == "page:page.lightings.page.table#1");
  CHECK(asked[0].command.args == Json::object({{"view", "table"},
                                               {"sort", Json::object({{"key", "at_ms"}, {"direction", "descending"}})}}));
  view.page_answer(asked[0].where, page_of({60, 59}, "c2", 1234));
  CHECK(shows(laid_out(view), "Night 60"));
  CHECK(shows(laid_out(view), "1,234 in all"));
  // Its filter: All, then the topic's lamps; a choice asks for the first page again, filtered by the space.
  REQUIRE(move_to(view, "filter:page.lightings.page.table/f0"));
  press(view, Key::right);
  CHECK(shows(laid_out(view), "North light"));
  asked = view.take_page_requests();
  REQUIRE(asked.size() == 1);
  CHECK(asked[0].command.args["filters"] == Json::object({{"lamp", "North light"}}));
  view.page_answer(asked[0].where, page_of({60}, nullptr, 20));
  // Its changes topic: showing its first page, it asks for it again.
  view.set_topic("lantern.status", Json::object({{"sentence", "A lamp was lit."}, {"hushed", false},
                                                 {"lamps", Json::array({"North light", "Harbour wall"})}}));
  asked = view.take_page_requests();
  REQUIRE(asked.size() == 1);
  view.page_answer(asked[0].where, page_of({60}, nullptr, 20));
  // Its search goes to the space when the field closes, not at each key.
  REQUIRE(move_to(view, "filter:page.lightings.page.table/search"));
  press(view, Key::activate);
  type(view, " Orla ");
  CHECK(view.take_page_requests().empty());
  press(view, Key::activate);  // done typing
  asked = view.take_page_requests();
  REQUIRE(asked.size() == 1);
  CHECK(asked[0].command.args["search"] == "Orla");  // trimmed, as the browser sends it
}
