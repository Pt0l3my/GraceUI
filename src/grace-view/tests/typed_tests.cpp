// SPDX-License-Identifier: MIT
// A slider's typed entry (§3.8, D266): fixtures/typed-cases.json's texts, which the browser view's
// web/tests/unit/typed.test.ts reads too, so that both views take or refuse the same text alike.
#include <doctest/doctest.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "grace/view/typed.h"

using namespace grace::view;
using grace::protocol::Json;
using grace::protocol::Setting;

namespace {

Json shared_cases() {
  std::ifstream file(GRACE_TYPED_CASES);
  REQUIRE(file.good());
  std::stringstream text;
  text << file.rdbuf();
  return Json::parse(text.str());
}

}  // namespace

TEST_CASE("a typed number: taken on its step within its bounds, or refused in words (D266)") {
  const Json cases = shared_cases();
  std::map<std::string, Setting> settings;
  for (const Json& spec : cases["settings"]) {
    Setting s;
    grace::protocol::DecodeError e;
    REQUIRE(grace::protocol::decode(spec, s, "setting", e));
    settings[s.key] = s;
  }
  REQUIRE(cases["cases"].size() > 40);
  for (const Json& c : cases["cases"]) {
    const Setting& s = settings.at(c["setting"].get<std::string>());
    const std::string text = c["text"].get<std::string>();
    CAPTURE(s.key);
    CAPTURE(text);
    const Typed typed = typed_value(s, text);
    if (c.contains("value")) {
      REQUIRE(typed.ok);
      CHECK(typed.value == c["value"]);
      CHECK(typed.words.empty());
      CHECK(typed.value.is_number_integer() == (s.type == grace::protocol::SettingType::integer));
      CHECK_FALSE(typed.value.dump().starts_with("-0"));  // never a negative zero
    } else if (c.contains("words")) {
      CHECK_FALSE(typed.ok);
      CHECK(typed.words == c["words"].get<std::string>());
    } else {
      REQUIRE(c["nothing"] == true);
      CHECK_FALSE(typed.ok);
      CHECK(typed.words.empty());
    }
  }
}

TEST_CASE("a slider's field opens with its value at its step's decimals, without its unit (D266)") {
  Setting s;
  s.type = grace::protocol::SettingType::number;
  s.step = 0.05;
  s.unit = "×";
  CHECK(plain_number(s, 1.25) == "1.25");
  CHECK(format_number(s, 1.25) == "1.25×");
  s.step = 0.5;
  s.unit = "s";
  CHECK(plain_number(s, 2.5) == "2.5");
  CHECK(format_number(s, 2.5) == "2.5 s");
  s.step.reset();
  s.unit.reset();
  CHECK(plain_number(s, 8642) == "8642");
  CHECK(step_decimals(0.25) == 2);
  CHECK(step_decimals(1) == 0);
}
