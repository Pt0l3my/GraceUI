// grace-pages (D160): every built-in space's page data and settings description decode through grace-protocol's own
// decoders, and hold what the design says GraceUI's own space holds (D79, D115).
#include <doctest/doctest.h>

#include <set>
#include <string>

#include "grace/pages/builtin.h"
#include "grace/protocol/messages.h"

namespace {

using namespace grace::protocol;

template <class T>
T decoded(std::string_view text) {
  T out;
  DecodeError error;
  const Json json = Json::parse(text.begin(), text.end(), nullptr, false);
  REQUIRE_FALSE(json.is_discarded());
  REQUIRE_MESSAGE(decode(json, out, "", error), error.field, ": ", error.why);
  return out;
}

}  // namespace

TEST_CASE("every built-in space decodes as the protocol's types") {
  const auto spaces = grace::pages::builtin_spaces();
  REQUIRE(spaces.size() == 2);
  CHECK(spaces[0].id == "graceui");
  CHECK(spaces[1].id == "demo");
  for (const auto& space : spaces) {
    CAPTURE(space.id);
    const SpacePages pages = decoded<SpacePages>(space.pages);
    const SettingsDescription settings = decoded<SettingsDescription>(space.settings);
    CHECK(pages.space == space.id);
    CHECK(pages.minor <= kProtocolMinor);  // the minor its components need
    CHECK(settings.schema.hash.size() == 16);
    CHECK(encode(pages) == Json::parse(space.pages));  // nothing lost in the decoding
  }
}

TEST_CASE("GraceUI's own space: its six pages and its own settings (D79, D115)") {
  const SpacePages pages = decoded<SpacePages>(grace::pages::builtin_spaces()[0].pages);
  const SettingsDescription settings = decoded<SettingsDescription>(grace::pages::builtin_spaces()[0].settings);
  std::set<std::string> ids;
  for (const Page& page : pages.pages) {
    ids.insert(page.id);
  }
  CHECK(ids == std::set<std::string>{"graceui.menu", "graceui.hotkeys", "graceui.browser", "graceui.mods",
                                     "graceui.compatibility", "graceui.about"});
  std::set<std::string> keys;
  for (const Setting& setting : settings.settings) {
    keys.insert(setting.key);
    if (setting.key == "ui.scale" || setting.key.rfind("ui.menu.", 0) == 0) {
      CHECK(setting.home == "graceui.menu");
    }
    if (setting.key.rfind("dashboard.", 0) == 0) {
      CHECK(setting.home == "graceui.browser");
    }
  }
  CHECK(keys.count("hotkeys.menu") == 1);
  CHECK(keys.count("ui.scale") == 1);
  CHECK(keys.count("dashboard.port") == 1);
}

TEST_CASE("the demo space uses every setting type") {
  const SettingsDescription settings = decoded<SettingsDescription>(grace::pages::builtin_spaces()[1].settings);
  std::set<SettingType> types;
  for (const Setting& setting : settings.settings) {
    types.insert(setting.type);
  }
  CHECK(types.size() == 9);
}
