// SPDX-License-Identifier: MIT
// The page structure from the descriptions (invariant 12): each page's rows in grace-view's focus order, against the
// golden grace_view_tests writes (src/grace-view/tests/golden/rows-order.txt), the rail's order, and every setting's
// place on its home page.
import { test } from "node:test";
import assert from "node:assert/strict";
import { orderLine, pageById, placeOf, railOrder, tabCount, withinIncrement } from "../../src/model/layout.ts";
import { builtinSpaces, goldenLines } from "../support/fixtures.ts";

test("every page's rows in grace-view's order, every section and Advanced open (§9.3)", () => {
  const ours: string[] = [];
  for (const space of builtinSpaces()) {
    for (const page of space.pages.pages) {
      for (let tab = 0; tab < tabCount(page); ++tab) {
        ours.push(orderLine(space.pages.space, page, tab));
      }
    }
  }
  assert.deepEqual(ours.map((l) => l.trimEnd()), goldenLines("rows-order.txt").map((l) => l.trimEnd()));
});

test("Page Up and Page Down go in rail order across categories, as grace-view's", () => {
  const demo = builtinSpaces()[1];
  assert.ok(demo);
  assert.deepEqual(railOrder(demo.pages).map((p) => p.id), ["demo.overview", "demo.behaviour", "demo.log", "demo.festival"]);
});

test("every setting sits on exactly its home page, on one tab", () => {
  for (const space of builtinSpaces()) {
    for (const s of space.settings.settings) {
      const home = pageById(space.pages, s.home);
      assert.ok(home, `${s.key}'s home exists`);
      const place = placeOf(home, s.key);
      assert.ok(place, `${s.key} is on ${s.home}`);
      for (const page of space.pages.pages) {
        if (page.id !== s.home) {
          assert.equal(placeOf(page, s.key), undefined, `${s.key} is on no other page as a setting`);
        }
      }
    }
  }
  const demo = builtinSpaces()[1];
  assert.ok(demo);
  const behaviour = pageById(demo.pages, "demo.behaviour");
  assert.ok(behaviour);
  assert.deepEqual(placeOf(behaviour, "lantern.night_only"), { tab: 1, section: "rhythm", advanced: true });
  assert.deepEqual(placeOf(behaviour, "lantern.oil_token"), { tab: 2, section: "keeper", advanced: false });
});

test("a page of a later increment is absent, as grace-view's set_increment leaves it (§4.2.2, D173)", () => {
  const own = builtinSpaces()[0]!.pages;
  const shown = withinIncrement(own, "0.2");
  assert.deepEqual(shown.pages.map((p) => p.id), ["graceui.menu", "graceui.browser", "graceui.about"]);
  assert.deepEqual(shown.categories.map((c) => c.pages), [["graceui.menu", "graceui.browser", "graceui.about"]]);
  assert.ok(shown.pages.every((p) => p.related.every((id) => shown.pages.some((q) => q.id === id))));
  assert.equal(withinIncrement(own, "0.6").pages.length, own.pages.length);
  assert.equal(withinIncrement(own, "").pages.length, own.pages.length, "an increment that does not parse hides nothing");
});
