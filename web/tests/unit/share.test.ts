// SPDX-License-Identifier: MIT
// A report preview's shareable text (D263): what both views take of a body's share, and what they drop.
import { test } from "node:test";
import assert from "node:assert/strict";
import { shareOf } from "../../src/model/share.ts";

test("a share's text and file name are taken as the space gave them", () => {
  assert.deepEqual(shareOf({ share: { text: "Lamp report\n", file: "lamp-report-2026-11-25.txt" } }),
                   { text: "Lamp report\n", file: "lamp-report-2026-11-25.txt" });
});

test("no share, an empty text, a text past 256 KiB or a file name GraceUI does not take: nothing to share", () => {
  assert.deepEqual(shareOf({}), { blocked: null });
  assert.deepEqual(shareOf({ share: { text: "", file: "a.txt" } }), { blocked: null });
  assert.deepEqual(shareOf({ share: { text: "x".repeat(262145), file: "a.txt" } }), { blocked: null });
  assert.deepEqual(shareOf({ share: { text: "é".repeat(131073), file: "a.txt" } }), { blocked: null });  // bytes, not characters
  for (const file of ["../a.txt", "a.TXT", "A.txt", "a.md", ".txt", "a b.txt", `${"a".repeat(61)}.txt`, "dir\\a.txt"]) {
    assert.deepEqual(shareOf({ share: { text: "Lamp", file } }), { blocked: null }, file);
  }
  assert.deepEqual(shareOf({ share: { text: "Lamp", file: `${"a".repeat(60)}.txt` } }),
                   { text: "Lamp", file: `${"a".repeat(60)}.txt` });  // 64 in all
});

test("blocked: the space's words, at most 400 bytes; past that, none", () => {
  assert.deepEqual(shareOf({ share: { blocked: "Remove the path in What happened before sharing." } }),
                   { blocked: "Remove the path in What happened before sharing." });
  assert.deepEqual(shareOf({ share: { blocked: "x".repeat(401) } }), { blocked: null });
  assert.deepEqual(shareOf({ share: { blocked: "", text: "Lamp", file: "a.txt" } }), { blocked: null });  // blocked wins
});
