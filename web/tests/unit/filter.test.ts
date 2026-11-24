// SPDX-License-Identifier: MIT
// A table's filter bar (G43, D250): fixtures/filter-cases.json's rows, choices and cases, which grace-view's filter
// tests run too, so that both views show the same rows in the same order.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import type { Json } from "../../src/protocol.ts";
import { choices, compareText, fold, narrowed, visible } from "../../src/model/filter.ts";
import type { FilterBarSpec, FilterState } from "../../src/model/filter.ts";
import { ROOT } from "../support/fixtures.ts";

interface Cases {
  rows: Json[];
  choices: { column: string; expect: string[] }[];
  cases: { name: string; spec: FilterBarSpec; state: FilterState; expect: number[]; narrowed: boolean }[];
}

const shared = JSON.parse(readFileSync(join(ROOT, "fixtures", "filter-cases.json"), "utf8")) as Cases;

test("a filter's choices: the values its rows hold, once each, in the rows' order", () => {
  for (const c of shared.choices) {
    assert.deepEqual(choices(shared.rows, c.column), c.expect, c.column);
  }
});

for (const c of shared.cases) {
  test(`shared case: ${c.name}`, () => {
    assert.deepEqual(visible(shared.rows, c.spec, c.state), c.expect);
    assert.equal(narrowed(c.spec, c.state), c.narrowed);
  });
}

test("text: ASCII letters folded only, compared by code point (as the game's UTF-8 bytes order it)", () => {
  assert.equal(fold("ÉLI Sand"), "Éli sand");
  assert.ok(compareText("a", "b") < 0);
  assert.ok(compareText("z", "é") < 0);
  assert.ok(compareText("￿", "\u{1f600}") < 0);  // UTF-16 code units would order these the other way
  assert.equal(compareText("same", "same"), 0);
  assert.ok(compareText("ab", "abc") < 0);
});
