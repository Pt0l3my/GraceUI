// SPDX-License-Identifier: MIT
// A held key's repeat in the browser view (D270), as grace-view's repeat_acts in game: it moves along and steps a
// slider, and nothing more.
import { test } from "node:test";
import assert from "node:assert/strict";
import { repeatActs, type RepeatTarget } from "../../src/model/repeat.ts";

const on = (role: string | null, more: Partial<RepeatTarget> = {}): RepeatTarget =>
  ({ role, select: false, field: false, capture: false, ...more });

test("a slider steps on with a held arrow; a switch, a choice, a tab, a tree's category or a list's row does not", () => {
  for (const key of ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"]) {
    assert.equal(repeatActs(key, on("slider")), true, key);
    assert.equal(repeatActs(key, on("radio")), false, key);
    assert.equal(repeatActs(key, on(null, { select: true })), false, key);
  }
  for (const role of ["switch", "tab", "treeitem", "option"]) {
    assert.equal(repeatActs("ArrowRight", on(role)), false, role);
    assert.equal(repeatActs("ArrowLeft", on(role)), false, role);
    assert.equal(repeatActs("ArrowDown", on(role)), true, `${role}: along, or the page scrolls`);
  }
  assert.equal(repeatActs("ArrowRight", on(null)), true, "elsewhere the page scrolls");
});

test("Enter, Esc, Space and the view's own keys once a press; a field's keys repeat, a capture takes none", () => {
  for (const key of ["Enter", "Escape", " ", "PageDown", "PageUp", "q", "e", "/", "Home"]) {
    assert.equal(repeatActs(key, on("button")), false, key);
  }
  for (const key of ["Tab", "Shift"]) {
    assert.equal(repeatActs(key, on(null)), true, key);
  }
  for (const key of ["ArrowLeft", "Backspace", "Delete", "a", " "]) {
    assert.equal(repeatActs(key, on(null, { field: true })), true, key);
  }
  assert.equal(repeatActs("Enter", on(null, { field: true })), false);
  assert.equal(repeatActs("Escape", on(null, { field: true })), false);
  assert.equal(repeatActs("k", on(null, { capture: true })), false);
  assert.equal(repeatActs("ArrowUp", on("slider", { capture: true })), false);
});
