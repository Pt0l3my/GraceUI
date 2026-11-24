// A table's filter bar (G43, D250): fixtures/filter-cases.json's rows, choices and cases, which the browser view's
// web/tests/unit/filter.test.ts runs too, so that both views show the same rows in the same order.
#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "grace/view/filter.h"

using namespace grace::view;
using grace::protocol::Json;

namespace {

Json shared_cases() {
  std::ifstream file(GRACE_FILTER_CASES);
  REQUIRE(file.good());
  std::stringstream text;
  text << file.rdbuf();
  return Json::parse(text.str());
}

grace::protocol::FilterBar bar_from(const Json& spec) {
  grace::protocol::FilterBar bar;
  grace::protocol::DecodeError e;
  REQUIRE(grace::protocol::decode(spec, bar, "spec", e));
  return bar;
}

FilterState state_from(const Json& j) {
  FilterState st;
  for (const Json& c : j["chosen"]) {
    st.chosen.push_back(c.is_null() ? std::nullopt : std::optional<std::string>(c.get<std::string>()));
  }
  st.sort = j["sort"].get<std::size_t>();
  st.reversed = j["reversed"].get<bool>();
  st.search = j["search"].get<std::string>();
  return st;
}

}  // namespace

TEST_CASE("a filter's choices: the values its rows hold, once each, in the rows' order (D250)") {
  const Json cases = shared_cases();
  for (const Json& c : cases["choices"]) {
    std::vector<std::string> expect;
    for (const Json& v : c["expect"]) {
      expect.push_back(v.get<std::string>());
    }
    CHECK(filter_choices(cases["rows"], c["column"].get<std::string>()) == expect);
  }
}

TEST_CASE("the shared cases: the same rows in the same order as the browser's (D250)") {
  const Json cases = shared_cases();
  for (const Json& c : cases["cases"]) {
    INFO(c["name"].get<std::string>());
    const grace::protocol::FilterBar bar = bar_from(c["spec"]);
    const FilterState st = state_from(c["state"]);
    std::vector<std::size_t> expect;
    for (const Json& i : c["expect"]) {
      expect.push_back(i.get<std::size_t>());
    }
    CHECK(visible_rows(cases["rows"], bar, st) == expect);
    CHECK(narrowed(bar, st) == c["narrowed"].get<bool>());
  }
}

TEST_CASE("ASCII letters fold; every other byte stays as it is (D250)") {
  CHECK(fold("\xC3\x89LI Sand") == "\xC3\x89li sand");
  CHECK(fold("ABC xyz 09") == "abc xyz 09");
}
