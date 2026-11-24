// SPDX-License-Identifier: MIT
// A space's scope and rows read a page at a time (G45, D254): the scope's choices and the one in view, the args a page
// is asked for with (as grace-view's page_args builds them), a page's answer as the view reads it, and the demo space's
// stand-in answer over fixtures/demo/paged.json.
import { test } from "node:test";
import assert from "node:assert/strict";
import type { FilterBar, JsonObject } from "../../src/protocol.ts";
import { FRESH } from "../../src/model/filter.ts";
import {
  PAGE_ROWS_MAX, answerPage, ownChoices, pageArgs, readPage, scopeBody, scopeChoices, scopeId,
} from "../../src/model/paging.ts";
import type { PagedFixture } from "../../src/model/paging.ts";
import { space } from "../support/fixtures.ts";

const seasons: JsonObject = {
  choices: [{ id: "winter", label: "This winter" }, { id: "autumn", label: "Last autumn", read_only: true },
            { id: "a/b", label: "Slashed" }, { id: "", label: "No id" }, { label: "None" }, "text"],
  current: "winter",
};

test("a scope's choices: an id naming a member (no slash), its words or its id, read only if it says", () => {
  assert.deepEqual(scopeChoices(seasons), [
    { id: "winter", label: "This winter", readOnly: false },
    { id: "autumn", label: "Last autumn", readOnly: true },
  ]);
  assert.deepEqual(scopeChoices(null), []);
  const many = { choices: Array.from({ length: 40 }, (_, i) => ({ id: `s${i}` })) };
  assert.equal(scopeChoices(many).length, 32);
  assert.deepEqual(scopeBody({ scope: seasons }, "scope"), seasons);
  assert.equal(scopeBody({ scope: "no" }, "scope"), null);
  assert.equal(scopeBody(undefined, undefined), null);
});

test("the scope in view: the player's choice while listed, else the space's current, else its first", () => {
  const choices = scopeChoices(seasons);
  assert.equal(scopeId(choices, "autumn", seasons), "autumn");
  assert.equal(scopeId(choices, "spring", seasons), "winter");
  assert.equal(scopeId(choices, undefined, { ...seasons, current: "spring" }), "winter");
  assert.equal(scopeId(choices, undefined, { choices: [...(seasons.choices as JsonObject[])].reverse(), current: 3 }),
               "winter");
  assert.equal(scopeId([], undefined, null), null);
});

test("a filter's own choices: its list, or a state topic's strings and {value, label}", () => {
  assert.deepEqual(ownChoices([{ value: "a", label: "" }, { value: "b", label: "Bee" }], undefined),
                   [{ value: "a", label: "a" }, { value: "b", label: "Bee" }]);
  assert.deepEqual(ownChoices(undefined, ["North light", "", 3, { value: "x", label: "Ex" }, { label: "no value" }]),
                   [{ value: "North light", label: "North light" }, { value: "x", label: "Ex" }]);
  assert.deepEqual(ownChoices(undefined, "not a list"), []);
});

test("a page's args: its own, each filter's value by its column, the sort and its direction, the search, the scope", () => {
  const bar: FilterBar = {
    filters: [{ column: "lamp", label: "Lamp" }, { column: "keeper", label: "Keeper" }],
    sort: [{ key: "at_ms", label: "Time", direction: "descending" }, { key: "keeper", label: "Keeper" }],
    search: { columns: ["keeper"], label: "Find" },
  };
  const source = { command: "lightings.page", args: { view: "list" } };
  assert.deepEqual(pageArgs(source, bar, FRESH, "", null),
                   { view: "list", sort: { key: "at_ms", direction: "descending" } });
  assert.deepEqual(pageArgs(source, bar, { chosen: [null, "Ada Marsh"], sort: 1, reversed: true, search: "" }, "  ada ",
                            "autumn"),
                   { view: "list", filters: { keeper: "Ada Marsh" }, sort: { key: "keeper", direction: "descending" },
                     search: "ada", scope: "autumn" });
  assert.deepEqual(pageArgs({ command: "c" }, undefined, FRESH, "words", null), {});  // no bar: no search
  assert.deepEqual(source.args, { view: "list" });  // its own args are not changed
});

test("a page as the view reads it: object rows, at most 200; a cursor and a count only if given", () => {
  const rows = Array.from({ length: 250 }, (_, i) => ({ id: `r${i}` }));
  const page = readPage({ columns: [{ id: "lit" }, "x"], rows: [...rows, 7], next: "c2", total: 1234 });
  assert.equal(page.rows.length, PAGE_ROWS_MAX);
  assert.deepEqual(page.columns, [{ id: "lit" }]);
  assert.equal(page.next, "c2");
  assert.equal(page.total, 1234);
  assert.deepEqual(readPage(undefined), { columns: [], rows: [], next: null, total: null });
  assert.deepEqual(readPage({ rows: [], next: "", total: -1 }), { columns: [], rows: [], next: null, total: null });
});

test("the demo space's answer: a season's lightings, filtered, searched, sorted and cut 25 at a time", () => {
  const fixture = space("demo").paged?.["lightings.page"] as PagedFixture;
  const winter = answerPage(fixture, { view: "list", sort: { key: "at_ms", direction: "descending" }, scope: "winter" });
  const ids = (body: JsonObject) => (body.rows as JsonObject[]).map((r) => r.id);
  assert.equal(winter.total, 40);
  assert.equal((winter.rows as JsonObject[]).length, 25);
  assert.equal(ids(winter)[0], "l60");
  assert.equal(winter.next, "o25");
  const rest = answerPage(fixture, { sort: { key: "at_ms", direction: "descending" }, scope: "winter", cursor: "o25" });
  assert.equal((rest.rows as JsonObject[]).length, 15);
  assert.equal(rest.next, undefined);
  assert.equal(ids(rest).at(-1), "l21");
  const autumn = answerPage(fixture, { sort: { key: "at_ms", direction: "ascending" }, scope: "autumn" });
  assert.equal(autumn.total, 20);
  assert.equal(ids(autumn)[0], "l1");
  const north = answerPage(fixture, { filters: { lamp: "North light" } });
  assert.ok((north.rows as JsonObject[]).every((r) => r.lamp === "North light"));
  assert.equal(north.total, 20);
  const found = answerPage(fixture, { search: "ADA" });
  assert.ok((found.rows as JsonObject[]).every((r) => String(r.keeper).toLowerCase().includes("ada")));
  assert.equal(answerPage(fixture, { cursor: "nonsense" }).next, "o25");  // a cursor it never gave starts again
});
