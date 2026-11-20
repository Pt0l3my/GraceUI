// grace-pages' settings store (D167): the one entry point checks every value against its description, answers each
// refusal with the protocol's code and words that never quote a value, keeps a secret write-only (invariant 5), and
// resets a key, a page or everything, the last two only with a confirmation.
#include <doctest/doctest.h>

#include <string>

#include "grace/pages/builtin.h"
#include "grace/pages/settings_store.h"

namespace {

using namespace grace::protocol;
using grace::pages::Outcome;
using grace::pages::SettingsStore;

SettingsStore store_of(std::string_view id) {
  for (const auto& space : grace::pages::builtin_spaces()) {
    if (space.id == id) {
      SettingsDescription description;
      DecodeError error;
      REQUIRE(decode(Json::parse(space.settings), description, "", error));
      SettingsStore store;
      std::string why;
      REQUIRE_MESSAGE(store.load(description, why), why);
      return store;
    }
  }
  FAIL("no built-in space ", id);
  return {};
}

Cmd command(std::string name, Json args) {
  Cmd cmd;
  cmd.space = "graceui";
  cmd.id = 1;
  cmd.name = std::move(name);
  cmd.args = std::move(args);
  return cmd;
}

}  // namespace

TEST_CASE("GraceUI's own settings start at their defaults and take what their descriptions allow") {
  SettingsStore store = store_of("graceui");
  CHECK(store.held("ui.scale") == 1.0);
  CHECK(store.changed().empty());
  Outcome out = store.set("ui.scale", Json(1.05));
  CHECK(out.ok);
  CHECK(out.changed == std::vector<std::string>{"ui.scale"});
  CHECK(store.set("ui.scale", Json(1.05)).changed.empty());  // the same value changes nothing
  CHECK(store.changed() == std::vector<std::string>{"ui.scale"});
  out = store.set("ui.menu.theme", Json("clear"));
  CHECK(out.ok);
  CHECK(store.values()["ui.menu.theme"] == "clear");
}

TEST_CASE("each refusal has its code, and its words never quote the value") {
  SettingsStore store = store_of("graceui");
  struct Case {
    const char* key;
    Json value;
    ErrorCode code;
  };
  const Case cases[] = {
      {"ui.nowhere", Json(true), ErrorCode::unknown_key},
      {"ui.scale", Json("big"), ErrorCode::invalid_value},
      {"ui.scale", Json(9.0), ErrorCode::invalid_value},     // outside its range
      {"ui.scale", Json(1.013), ErrorCode::invalid_value},   // off its step
      {"ui.menu.theme", Json("neon"), ErrorCode::invalid_value},
      {"ui.menu.reduce_motion", Json(1), ErrorCode::invalid_value},
      {"dashboard.lan", Json(true), ErrorCode::locked},      // later, and locked
      {"dashboard.port", Json(8080.5), ErrorCode::invalid_value},
  };
  for (const Case& c : cases) {
    CAPTURE(c.key);
    const Outcome out = store.set(c.key, c.value);
    CHECK_FALSE(out.ok);
    CHECK(out.error == c.code);
    CHECK(out.detail.find(c.value.dump()) == std::string::npos);
  }
  CHECK(store.changed().empty());
}

TEST_CASE("the demo's kinds: whole numbers, steps, the inherit tri-state, a key's name") {
  SettingsStore store = store_of("demo");
  CHECK(store.set("lantern.wicks", Json(8)).ok);
  CHECK(store.set("lantern.wicks", Json(8.0)).ok);  // whole, though written as a float
  CHECK(store.set("lantern.wicks", Json(2.5)).error == ErrorCode::invalid_value);
  CHECK(store.set("lantern.gap_s", Json(3.0)).ok);
  CHECK(store.set("lantern.gap_s", Json(3.2)).error == ErrorCode::invalid_value);
  CHECK(store.set("lantern.shield", Json("on")).ok);
  CHECK(store.set("lantern.shield", Json("inherit")).ok);
  CHECK(store.set("lantern.shield", Json("auto")).error == ErrorCode::invalid_value);
  CHECK(store.set("lantern.trim_key", Json("Ctrl+Shift+F9")).ok);
  CHECK(store.set("lantern.trim_key", Json("Hyper+F9")).error == ErrorCode::invalid_value);
  CHECK(store.set("lantern.night_only", Json(true)).error == ErrorCode::not_now);  // its sundial's check holds it
  CHECK(store.set("lantern.festival", Json(true)).error == ErrorCode::locked);     // later
}

TEST_CASE("a secret is write-only (invariant 5)") {
  SettingsStore store = store_of("demo");
  CHECK(store.values()["lantern.oil_token"] == "");
  CHECK(store.set("lantern.oil_token", Json("a-secret-value")).ok);
  const Json values = store.values();
  CHECK(values["lantern.oil_token"] == "set");
  CHECK(values.dump().find("a-secret-value") == std::string::npos);
  CHECK(store.held("lantern.oil_token") == "a-secret-value");  // the space's own use
}

TEST_CASE("reset: a key at once; a page or everything only when confirmed; locked keeps its value") {
  SettingsStore store = store_of("graceui");
  REQUIRE(store.set("ui.scale", Json(1.2)).ok);
  REQUIRE(store.set("ui.menu.theme", Json("clear")).ok);
  REQUIRE(store.set("dashboard.port", Json(9000)).ok);
  const std::string scale = "ui.scale";
  Outcome out = store.reset(ResetScopeKind::key, &scale, false);
  CHECK(out.ok);
  CHECK(out.changed == std::vector<std::string>{"ui.scale"});
  const std::string menu = "graceui.menu";
  CHECK(store.reset(ResetScopeKind::page, &menu, false).error == ErrorCode::confirm_required);
  out = store.reset(ResetScopeKind::page, &menu, true);
  CHECK(out.changed == std::vector<std::string>{"ui.menu.theme"});
  CHECK(store.held("dashboard.port") == 9000);  // another page's
  CHECK(store.reset(ResetScopeKind::all, nullptr, false).error == ErrorCode::confirm_required);
  CHECK(store.reset(ResetScopeKind::all, nullptr, true).changed == std::vector<std::string>{"dashboard.port"});
  const std::string lan = "dashboard.lan";
  CHECK(store.reset(ResetScopeKind::key, &lan, false).error == ErrorCode::locked);
  const std::string nowhere = "ui.nowhere";
  CHECK(store.reset(ResetScopeKind::key, &nowhere, false).error == ErrorCode::unknown_key);
}

TEST_CASE("apply takes the framework's commands, their arguments checked by the protocol") {
  SettingsStore store = store_of("graceui");
  CHECK(store.apply(command("settings.set", Json::object({{"key", "ui.scale"}, {"value", 1.1}}))).ok);
  CHECK(store.apply(command("settings.reset", Json::object({{"scope", "key"}, {"target", "ui.scale"}}))).ok);
  CHECK(store.held("ui.scale") == 1.0);
  const Outcome bad = store.apply(command("settings.set", Json::object({{"value", 1.1}})));
  CHECK_FALSE(bad.ok);
  CHECK(bad.error == ErrorCode::invalid_value);
  CHECK(store.apply(command("lantern.polish", Json::object())).error == ErrorCode::unknown_command);
}
