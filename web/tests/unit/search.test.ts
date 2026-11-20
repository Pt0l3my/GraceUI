// SPDX-License-Identifier: MIT
// Global search (§4.3): grace-view's hits (view_tests.cpp's search case) always found and first among settings; pages
// by name and description; help; breadcrumbs; every space badged; nothing a view does not show.
import { test } from "node:test";
import assert from "node:assert/strict";
import { gameViewHits, search } from "../../src/model/search.ts";
import type { SearchSpace } from "../../src/model/search.ts";
import { builtinSpaces } from "../support/fixtures.ts";

const spaces: SearchSpace[] = builtinSpaces().map((s) => ({
  id: s.pages.space, name: s.pages.name, pages: s.pages, settings: s.settings.settings,
}));
const graceui = spaces[0];
assert.ok(graceui);

test("grace-view's own cases", () => {
  assert.deepEqual(gameViewHits(graceui.settings, "contrast"), ["ui.menu.theme"]);
  assert.equal(gameViewHits(graceui.settings, "PORT").length, 1);
  assert.equal(gameViewHits(graceui.settings, "dashboard.").length, 3);
  assert.deepEqual(gameViewHits(graceui.settings, ""), []);
  const hits = search([graceui], "contrast").filter((r) => r.kind === "setting");
  assert.equal(hits[0]?.key, "ui.menu.theme");
  assert.equal(hits[0]?.page, "graceui.menu");
  assert.deepEqual(search(spaces, ""), []);
  assert.deepEqual(search(spaces, "   "), []);
});

test("every query finds what grace-view finds, ranked before help-only matches", () => {
  for (const query of ["light", "gap", "key", "shield", "port", "lantern.", "wick", "menu", "Size", "x"]) {
    for (const space of spaces) {
      const ours = search([space], query, 1000).filter((r) => r.kind === "setting");
      const theirs = gameViewHits(space.settings, query);
      const first = ours.filter((r) => r.rank <= 2).map((r) => r.key);
      assert.deepEqual([...first].sort(), [...theirs].sort(), `${space.id} "${query}"`);
      const lastStrong = Math.max(-1, ...ours.map((r, i) => (r.rank <= 2 ? i : -1)));
      const firstWeak = ours.findIndex((r) => r.rank > 2);
      assert.ok(firstWeak === -1 || firstWeak > lastStrong, "label, synonym and key matches come first");
    }
  }
});

test("pages by name and description, settings by help, each with its breadcrumb and its space", () => {
  const behaviour = search(spaces, "behaviour");
  assert.equal(behaviour[0]?.kind, "page");
  assert.equal(behaviour[0]?.page, "demo.behaviour");
  assert.deepEqual(behaviour[0]?.breadcrumb, ["Lanterns", "Behaviour"]);
  assert.equal(behaviour[0]?.spaceName, "Demo space");
  const theme = search(spaces, "theme").find((r) => r.key === "ui.menu.theme");
  assert.deepEqual(theme?.breadcrumb, ["GraceUI", "Menu", "Theme"]);
  const byHelp = search(spaces, "glimmer");
  assert.equal(byHelp[0]?.key, "lantern.brightness");
  assert.equal(byHelp[0]?.rank, 3);
  const byDescription = search(spaces, "connected");
  assert.ok(byDescription.some((r) => r.kind === "page" && r.page === "graceui.browser"));
});

test("a setting whose home is not in the view is not found", () => {
  const demo = spaces[1];
  assert.ok(demo);
  const pages = { ...demo.pages, pages: demo.pages.pages.filter((p) => p.id !== "demo.festival") };
  const hits = search([{ ...demo, pages }], "festival");
  assert.deepEqual(hits, []);
});
