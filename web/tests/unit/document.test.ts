// SPDX-License-Identifier: MIT
// A document block's body (G44, D251): fixtures/document-cases.json, which grace-view's document tests run too, and the
// size limits.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import type { Json } from "../../src/protocol.ts";
import { MAX_BLOCKS, MAX_TEXT, parseDocument } from "../../src/model/document.ts";
import { ROOT } from "../support/fixtures.ts";

const shared = JSON.parse(readFileSync(join(ROOT, "fixtures", "document-cases.json"), "utf8")) as
  { cases: { name: string; body: Json; expect: Json }[] };

for (const c of shared.cases) {
  test(`shared case: ${c.name}`, () => {
    assert.deepEqual(JSON.parse(JSON.stringify(parseDocument(c.body))), c.expect);
  });
}

test("the limits: 400 blocks drawn, a paragraph of 2000 characters, a document of 64 KiB", () => {
  const many = { blocks: Array.from({ length: MAX_BLOCKS + 5 }, (_, i) => ({ paragraph: `Line ${i}` })) };
  assert.equal(parseDocument(many).blocks.length, MAX_BLOCKS);
  const long = { blocks: [{ paragraph: "é".repeat(MAX_TEXT) }, { paragraph: "é".repeat(MAX_TEXT + 1) }] };
  assert.equal(parseDocument(long).blocks.length, 1);  // characters, not bytes
  const huge = { blocks: [{ code: "x".repeat(16 * 1024) }, { code: "y".repeat(16 * 1024) }, { code: "z".repeat(16 * 1024) },
                          { code: "w".repeat(16 * 1024) }] };
  assert.equal(parseDocument(huge).error, "too_large");
});
