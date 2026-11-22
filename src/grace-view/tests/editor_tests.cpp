// SPDX-License-Identifier: MIT
// editor_tests.cpp: the editor block's model (editor.h; D217, proposal 0010 §3.3 and §7).
#include <doctest/doctest.h>

#include "grace/view/editor.h"

using grace::protocol::Json;
using grace::view::EditorDocument;
using grace::view::EditorField;
using grace::view::EditorState;
using grace::view::FieldKind;

namespace {

const Json kNote = Json::parse(R"({"target": "harbour-mouth", "rev": 3,
  "state": {"word": "Saved", "tone": "moss"}, "draft_help": "Sent to the demo's own drafter.",
  "fields": [
    {"id": "brief", "label": "Short name", "kind": "line", "max_chars": 24, "value": "Mouth lamp"},
    {"id": "public.summary", "label": "How it looks", "kind": "prose", "max_words": 35, "nullable": true,
     "value": null, "reference": "A green lamp."},
    {"id": "sayings", "label": "Sayings", "kind": "list", "max_items": 4,
     "value": ["Never let it gutter", {"text": "Green for going home", "key": "s2"},
               {"text": "The old ward line", "key": "s3", "locked": "By hand."}]},
    {"id": "register", "label": "Tone", "kind": "choice", "value": "plain",
     "choices": [{"value": "plain", "label": "Plain"}, {"value": "warm", "label": "Warm"}]},
    {"id": "words_max", "label": "Longest line", "kind": "number", "min": 6, "max": 20, "value": 12},
    {"id": "lit", "label": "Lit at dawn", "kind": "switch", "value": false},
    {"id": "met", "label": "Met", "kind": "shown", "value": "On the first evening"},
    {"id": "bad", "label": "A number as text", "kind": "number", "value": "twelve"},
    {"id": "brief", "label": "Twice", "kind": "line", "value": "again"},
    {"id": "odd", "label": "No kind", "kind": "wheel", "value": 1},
    {"label": "No id", "kind": "line", "value": ""}
  ]})");

}  // namespace

TEST_CASE("a document: its fields read, a malformed one left out, never guessed at (D217)") {
  EditorDocument doc;
  REQUIRE(EditorDocument::parse(kNote, doc));
  CHECK(doc.target == "harbour-mouth");
  CHECK(doc.rev == 3);
  CHECK(doc.state_word == "Saved");
  CHECK(doc.draft_help == "Sent to the demo's own drafter.");
  REQUIRE(doc.fields.size() == 7);  // the number given as text, the second "brief", the unknown kind, no id: out
  CHECK(doc.field("public.summary") != nullptr);  // a dotted id
  CHECK(doc.field("public.summary")->nullable);
  CHECK(doc.field("public.summary")->value.is_null());  // "not set here"
  CHECK(doc.field("sayings")->kind == FieldKind::list);
  CHECK(doc.field("lit")->kind == FieldKind::toggle);
  CHECK(doc.field("bad") == nullptr);
  EditorDocument none;
  CHECK_FALSE(EditorDocument::parse(Json::parse(R"({"fields": []})"), none));  // no target
  CHECK_FALSE(EditorDocument::parse(Json::parse(R"({"target": "x"})"), none));  // no fields
}

TEST_CASE("edits over the document: set, back to as published, and the doc that check and save send") {
  EditorDocument doc;
  REQUIRE(EditorDocument::parse(kNote, doc));
  EditorState state;
  CHECK_FALSE(state.dirty());
  const EditorField& brief = *doc.field("brief");
  state.set(brief, Json("Green lamp"));
  CHECK(state.dirty());
  CHECK(state.value(brief) == "Green lamp");
  state.set(brief, Json("Mouth lamp"));  // as published again
  CHECK_FALSE(state.dirty());
  state.set(*doc.field("public.summary"), Json("Tall and green."));
  state.set(*doc.field("met"), Json("Never"));  // a shown field takes no edit
  const Json sent = state.doc(doc);
  CHECK(sent["brief"] == "Mouth lamp");
  CHECK(sent["public.summary"] == "Tall and green.");
  CHECK_FALSE(sent.contains("met"));
  CHECK(sent["sayings"][2]["locked"] == "By hand.");  // a locked item goes back as published, its key with it
  CHECK(sent["sayings"][1]["key"] == "s2");
  state.set(*doc.field("public.summary"), Json());  // back to "not set here": null, sent as null
  CHECK(state.doc(doc)["public.summary"].is_null());
}

TEST_CASE("an answer's messages and a draft: kept per field, a field the document lacks dropped") {
  EditorDocument doc;
  REQUIRE(EditorDocument::parse(kNote, doc));
  EditorState state;
  state.take_messages(Json::parse(R"({"messages": [
    {"field": "brief", "level": "error", "words": "At most 24 characters."},
    {"field": "sayings", "item": 1, "level": "warning", "words": "A game word."},
    {"level": "error", "words": "The file is over 8 KB."},
    {"field": "brief", "level": "error"}]})"));
  REQUIRE(state.messages.size() == 3);  // one with no words is left out
  CHECK(state.messages[0].error);
  CHECK_FALSE(state.messages[1].error);
  CHECK(state.messages[1].item == 1u);
  CHECK(state.messages[2].field.empty());  // about the whole file
  state.take_draft(doc, Json::parse(R"({"draft": {"brief": "Sea lamp", "public.summary": "A lamp that sees the sea.",
    "met": "Ignored", "nowhere": "Ignored", "words_max": "Ignored"}, "notice": ""})"));
  CHECK(state.draft.size() == 2);  // a shown field, an unknown one and a value of another kind: dropped
  state.use(*doc.field("brief"));
  CHECK(state.value(*doc.field("brief")) == "Sea lamp");
  CHECK(state.draft.size() == 1);
  state.keep("public.summary");
  CHECK(state.draft.empty());
  CHECK(state.dirty());
  state.clear();
  CHECK_FALSE(state.dirty());
  CHECK(state.messages.empty());
}

TEST_CASE("words and characters: as the counters count them") {
  CHECK(grace::view::count_words("") == 0);
  CHECK(grace::view::count_words(" a  tall lamp ") == 3);
  CHECK(grace::view::count_chars("aé\U0001F56F") == 3);
}

TEST_CASE("a list item's parts, and back to JSON: a plain item stays a string") {
  const auto plain = grace::view::list_item(Json("Never let it gutter"));
  CHECK(plain.text == "Never let it gutter");
  CHECK(grace::view::list_json(plain, false) == Json("Never let it gutter"));
  auto keyed = grace::view::list_item(Json::parse(R"({"text": "Trimmed", "choice": "storm", "key": "w1"})"));
  CHECK(keyed.choice == "storm");
  keyed.text = "Trimmed short";
  CHECK(grace::view::list_json(keyed, true) == Json::parse(R"({"text": "Trimmed short", "choice": "storm", "key": "w1"})"));
}
