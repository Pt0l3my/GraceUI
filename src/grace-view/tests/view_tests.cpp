// grace-view (D162): the keyboard's and pad's navigation and its commands on GraceUI's own space and the demo space,
// search, and the layout's structure dumps against golden files (§9.3); and two goldens the web view's tests read, every
// page's rows in their focus order and every setting's formats and commands (D166). Set GRACE_UPDATE_GOLDEN=1 to write
// them anew.
#include <algorithm>
#include <doctest/doctest.h>

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
  CHECK(before.dump().find("Nothing to show yet.") != std::string::npos);
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
  CHECK(empty.dump().find("Nothing to show yet.") != std::string::npos);
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
  CHECK(left.dump() == before);
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
  CHECK(list.dump().find("Nothing to show yet.") != std::string::npos);  // nothing published yet (invariant 2)
  about.set_topic("graceui.about", Json{{"items", Json::array({{{"label", "Version"}, {"value", "GraceUI 9.9.9"}},
                                                               {{"label", "Licence"}, {"value", "MIT"}},
                                                               {{"label", "No value"}}})}});
  about.layout(Viewport{1920, 1080, 1.25f}, test::fonts(), list);
  std::string dump = list.dump();
  CHECK(dump.find("\"Version\"") != std::string::npos);
  CHECK(dump.find("\"GraceUI 9.9.9\"") != std::string::npos);
  CHECK(dump.find("\"No value\"") == std::string::npos);  // an item without its value is left out, as the browser does
  CHECK(dump.find("Nothing to show yet.") == std::string::npos);

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
  CHECK(log.component_topics().empty());  // a live feed and a log tail: streams, the browser's for now
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
  CHECK(list.dump().find("Nothing to show yet.") != std::string::npos);  // nothing published: never "All quiet."
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
  CHECK(view.component_topics() == std::vector<std::string>{"space.state"});
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
  // labels and its button (G38 (a)): nothing is left to the browser view.
  const std::string dump = refused.dump();
  CHECK(dump.find("Ward word: Set") != std::string::npos);
  CHECK(dump.find("Ward word: Missing") != std::string::npos);
  CHECK(dump.find("Ward word: Refused") != std::string::npos);  // D197
  CHECK(dump.find("Ward word: Not needed") != std::string::npos);
  CHECK(dump.find("in the browser view") == std::string::npos);
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
  CHECK(open.dump().find("Enter  Save    Esc  Cancel") != std::string::npos);
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
  CHECK(dump.find("Enter  Set    Esc  Cancel") != std::string::npos);
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
  view.edit(EditInput{EditKey::tab});
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
  CHECK(sent.command.args == Json::object({{"name", "Orla Venn"}, {"post", "North light"}}));
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
  CHECK(list.dump().find("↑ ↓  Choose    Enter  Open    Esc  Close") != std::string::npos);
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
  CHECK(before.dump().find("Nothing to show yet.") != std::string::npos);
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
  press(view, Key::up);  // the first row: ↑ stays on it
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
  CHECK(asking.dump().find("Enter  Use it here") != std::string::npos);
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
