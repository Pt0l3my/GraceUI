// A document block's body (G44, D251): fixtures/document-cases.json, which the browser view's
// web/tests/unit/document.test.ts runs too, and the size limits.
#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "grace/view/document.h"

using namespace grace::view;
using grace::protocol::Json;

TEST_CASE("the shared document cases: the same title and blocks as the browser's (D251)") {
  std::ifstream file(GRACE_DOCUMENT_CASES);
  REQUIRE(file.good());
  std::stringstream text;
  text << file.rdbuf();
  const Json cases = Json::parse(text.str());
  for (const Json& c : cases["cases"]) {
    INFO(c["name"].get<std::string>());
    CHECK(document_json(parse_document(c["body"])) == c["expect"]);
  }
}

TEST_CASE("a document's limits: 400 blocks drawn, 2000 characters a paragraph, 64 KiB a document (D251)") {
  Json many = Json::object({{"blocks", Json::array()}});
  for (int i = 0; i < 405; ++i) {
    many["blocks"].push_back(Json::object({{"paragraph", "Line " + std::to_string(i)}}));
  }
  CHECK(parse_document(many).blocks.size() == kDocMaxBlocks);
  std::string e2000;
  for (std::size_t i = 0; i < kDocMaxText; ++i) {
    e2000 += "\xC3\xA9";
  }
  const Json longs = Json::object({{"blocks", Json::array({Json::object({{"paragraph", e2000}}),
                                                           Json::object({{"paragraph", e2000 + "\xC3\xA9"}})})}});
  CHECK(parse_document(longs).blocks.size() == 1);  // characters, not bytes
  Json huge = Json::object({{"blocks", Json::array()}});
  for (const char c : std::string("xyzw")) {
    huge["blocks"].push_back(Json::object({{"code", std::string(16 * 1024, c)}}));
  }
  CHECK(parse_document(huge).error == DocumentView::Error::too_large);
}
