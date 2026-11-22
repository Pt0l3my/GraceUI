// SPDX-License-Identifier: MIT
// The editor block (D217) in the browser: its document read as grace-view's editor.h reads it, the edits kept in memory
// over it, and the demo space's commands as the fixture space answers them: check's messages, a save over a stale
// revision refused as conflict, a locked item kept, reset, restore and a draft.
import { test } from "node:test";
import assert from "node:assert/strict";
import type { JsonObject, State } from "../../src/protocol.ts";
import {
  countChars, countWords, dirty, docOf, draftOf, editorKey, listItem, listJson, messagesOf, oneParagraph, parseDocument,
  setValue, stateOf, valueOf,
} from "../../src/model/editor.ts";
import { FixtureSpace } from "../../src/transport/fixture-space.ts";
import { space } from "../support/fixtures.ts";

const NOTE = "lantern.note/harbour-mouth";

function demo(): { s: FixtureSpace; body: () => JsonObject } {
  const s = new FixtureSpace(space("demo"));
  return {
    s,
    body: () => (s.subscribe(NOTE) as State).body,
  };
}

test("a document: its fields read, a malformed one left out, never guessed at", () => {
  const doc = parseDocument({
    target: "t", rev: 3, state: { word: "Saved", tone: "moss" },
    fields: [
      { id: "brief", label: "Short name", kind: "line", max_chars: 24, value: "Mouth lamp" },
      { id: "public.summary", label: "How it looks", kind: "prose", nullable: true, value: null },
      { id: "bad", label: "A number as text", kind: "number", value: "twelve" },
      { id: "brief", label: "Twice", kind: "line", value: "again" },
      { id: "odd", label: "No kind", kind: "wheel", value: 1 },
      { label: "No id", kind: "line", value: "" },
    ],
  });
  assert.ok(doc);
  assert.deepEqual(doc.fields.map((f) => f.id), ["brief", "public.summary"]);
  assert.equal(doc.rev, 3);
  assert.equal(parseDocument({ fields: [] }), null);
  assert.equal(parseDocument({ target: "x" }), null);
});

test("edits over the document: set, back to as published, the doc sent, and nothing kept outside memory", () => {
  const { body } = demo();
  const doc = parseDocument(body());
  assert.ok(doc);
  const key = editorKey("demo", NOTE);
  const brief = doc.fields.find((f) => f.id === "brief");
  assert.ok(brief);
  setValue(key, doc, brief, "Green lamp");
  assert.equal(dirty(stateOf(key)), true);
  assert.equal(stateOf(key).baseRev, 1);
  assert.equal(valueOf(stateOf(key), brief), "Green lamp");
  setValue(key, doc, brief, "Mouth lamp");
  assert.equal(dirty(stateOf(key)), false);
  const sent = docOf(stateOf(key), doc);
  assert.equal("met" in sent, false);  // a shown field is never sent
  assert.equal((sent.sayings as JsonObject[])[2]?.locked, "Written by hand: change it in the file.");
  assert.equal(countWords(" a  tall lamp "), 3);
  assert.equal(countChars("aé\u{1F56F}"), 3);
  assert.equal(oneParagraph("old\r\nbridge\tkeeper"), "old  bridge keeper");
  assert.deepEqual(listJson(listItem("plain"), false), "plain");
  assert.deepEqual(messagesOf({ messages: [{ field: "brief", level: "warning", words: "w" }, { field: "x" }] }),
                   [{ field: "brief", item: undefined, error: false, words: "w" }]);
  assert.deepEqual(draftOf(doc, { draft: { brief: "Sea lamp", met: "no", words_max: "x", nowhere: "y" } }),
                   { brief: "Sea lamp" });
});

test("the demo's note commands: check's messages, a stale save's conflict, a save, restore, reset and a draft", () => {
  const { s, body } = demo();
  const doc = parseDocument(body());
  assert.ok(doc);
  const sent = docOf({ baseRev: 0, edits: {}, messages: [], top: "", draft: {}, notice: "", notes: "", conflict: false }, doc);
  // Check: a brief over its 24 characters, an item over its words.
  const long = { ...sent, brief: "A lamp with a very long name indeed", sayings: ["Never let it gutter in the night or the storm or the fog", ...(sent.sayings as JsonObject[]).slice(1)] };
  const checked = s.handle("note.check", { target: "harbour-mouth", rev: 1, doc: long });
  assert.equal(checked.answer.ok, true);
  const messages = messagesOf(checked.answer.body);
  assert.ok(messages.some((m) => m.field === "brief" && m.error && m.words === "At most 24 characters."));
  assert.ok(messages.some((m) => m.field === "sayings" && m.item === 0 && !m.error));
  assert.deepEqual(checked.publish, []);
  // Save: refused while an error stands, its messages in the body; a stale revision is a conflict.
  const refused = s.handle("note.save", { target: "harbour-mouth", rev: 1, doc: long });
  assert.equal(refused.answer.ok, false);
  assert.equal(!refused.answer.ok && refused.answer.error, "invalid_value");
  assert.ok(messagesOf(refused.answer.body).length > 0);
  const stale = s.handle("note.save", { target: "harbour-mouth", rev: 0, doc: sent });
  assert.equal(!stale.answer.ok && stale.answer.error, "conflict");
  // A locked item removed is refused.
  const unlocked = s.handle("note.save", { target: "harbour-mouth", rev: 1, doc: { ...sent, sayings: ["Only this"] } });
  assert.equal(unlocked.answer.ok, false);
  // A good save: published again with its next revision.
  const saved = s.handle("note.save", { target: "harbour-mouth", rev: 1, doc: { ...sent, brief: "Sea lamp" } });
  assert.equal(saved.answer.ok, true);
  assert.equal(saved.publish[0]?.topic, NOTE);
  assert.equal(body().rev, 2);
  assert.equal(parseDocument(body())?.fields.find((f) => f.id === "brief")?.value, "Sea lamp");
  // Restore: the version before the last save; then reset, which needs its confirmation.
  assert.equal(s.handle("note.restore", { target: "harbour-mouth" }).answer.ok, true);
  assert.equal(parseDocument(body())?.fields.find((f) => f.id === "brief")?.value, "Mouth lamp");
  const asked = s.handle("note.reset", { target: "harbour-mouth", all: true });
  assert.equal(!asked.answer.ok && asked.answer.error, "confirm_required");
  assert.equal(s.handle("note.reset", { target: "harbour-mouth", all: true, confirm: true }).answer.ok, true);
  // A draft: the demo's own words from the notes, never a shown or locked field; refused while the space says why not.
  const drafted = s.handle("note.draft", { target: "harbour-mouth", notes: "tall and green" });
  assert.equal(drafted.answer.ok, true);
  const proposals = draftOf(doc, drafted.answer.body);
  assert.equal(proposals.brief, "tall and green");
  assert.equal(proposals["public.summary"], "Drafted from your notes: tall and green.");
  const blocked = s.handle("note.draft", { target: "fish-steps", notes: "" });
  assert.equal(!blocked.answer.ok && blocked.answer.detail, "The drafter is resting: try again later.");
  // A target that is no member's key, or no document.
  assert.equal(s.handle("note.save", { target: "../x", rev: 1, doc: sent }).answer.ok, false);
  assert.equal(s.handle("note.save", { target: "nowhere", rev: 1, doc: sent }).answer.ok, false);
});
