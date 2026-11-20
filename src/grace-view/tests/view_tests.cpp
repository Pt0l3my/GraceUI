// grace-view (D162): the keyboard's and pad's navigation and its commands on GraceUI's own space and the demo space,
// search, and the layout's structure dumps against golden files (§9.3). Set GRACE_UPDATE_GOLDEN=1 to write them anew.
#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

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
  CHECK(a.command.args == Json::object({{"key", "ui.scale"}, {"value", 1.05}}));
  CHECK(view.format("ui.scale", view.value("ui.scale")) == "1.05×");
  a = press(view, Key::right, true);  // ten steps, held to the maximum
  CHECK(a.command.args["value"].get<double>() == doctest::Approx(1.5));
  a = press(view, Key::reset);
  CHECK(a.command.name == "settings.reset");
  CHECK(a.command.args == Json::object({{"scope", "key"}, {"target", "ui.scale"}}));
  CHECK(view.value("ui.scale").get<double>() == 1.0);
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
}

TEST_CASE("a locked setting does not change; a refusal returns focus with its reason") {
  PageView view = loaded("graceui", "graceui.browser");
  while (view.focused() != "setting:dashboard.lan") {
    press(view, Key::down);
  }
  CHECK(press(view, Key::right).kind == Action::Kind::none);
  CHECK(view.value("dashboard.lan") == false);
  press(view, Key::up);
  view.refused("dashboard.lan", "Only in a later version.");
  CHECK(view.focused() == "setting:dashboard.lan");
  DisplayList list;
  view.layout(Viewport{}, test::fonts(), list);
  CHECK(list.dump().find("rot          ") != std::string::npos);
}

TEST_CASE("Page Up and Page Down cross categories in rail order") {
  PageView view = loaded("demo", "demo.behaviour");
  Action a = press(view, Key::next_page);
  CHECK(a.page == "demo.log");
  a = press(view, Key::previous_page);
  CHECK(a.page == "demo.behaviour");
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

TEST_CASE("the Menu page's layout, as its structure dump says (§9.3)") {
  PageView view = loaded("graceui", "graceui.menu");
  DisplayList list;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), list);
  golden("menu-1920x1080.txt", list.dump());
  DisplayList large;
  view.layout(Viewport{1280, 800, 1.5f}, test::fonts(), large);
  golden("menu-1280x800-150.txt", large.dump());
  // A value changed from its default gains the lozenge, and nothing else moves (the theme itself is the palette the
  // host draws with: the items name their colours by role, grace-draw's tests).
  view.set_values(Json::object({{"ui.menu.theme", "clear"}}));
  DisplayList again;
  view.layout(Viewport{1920, 1080, 1.0f}, test::fonts(), again);
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
