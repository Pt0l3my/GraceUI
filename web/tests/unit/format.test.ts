// SPDX-License-Identifier: MIT
// Formatting and each press's value, against grace-view's own (src/grace-view/tests/golden/formats.txt, which
// grace_view_tests writes from page_view.cpp's format and change): every setting of both built-in spaces (§9.3).
import { test } from "node:test";
import assert from "node:assert/strict";
import { decimalsOf, fixed, formatValue, nextValue } from "../../src/model/format.ts";
import type { Json, Setting } from "../../src/protocol.ts";
import { builtinSpaces, goldenLines } from "../support/fixtures.ts";

const settings = new Map<string, Setting>();
for (const space of builtinSpaces()) {
  for (const s of space.settings.settings) {
    settings.set(`${space.pages.space} ${s.key}`, s);
  }
}

const PRESSES: { [name: string]: [1 | -1, boolean] } = {
  right: [1, false], left: [-1, false], shift_right: [1, true], shift_left: [-1, true],
};

test("the golden covers every setting of both spaces", () => {
  const lines = goldenLines("formats.txt");
  assert.equal(lines.length, settings.size);
});

for (const line of goldenLines("formats.txt")) {
  const [space, key, ...fields] = line.split("\t");
  test(`${space} ${key}: formats and presses as grace-view's`, () => {
    const s = settings.get(`${space} ${key}`);
    assert.ok(s, `${space} ${key} is a setting of the fixtures`);
    for (const field of fields) {
      const at = field.indexOf("=");
      const name = field.slice(0, at);
      const want = field.slice(at + 1);
      if (name === "default") {
        assert.equal(formatValue(s, s.default), want);
      } else if (name === "set" || name === "unset") {
        assert.equal(formatValue(s, name === "set" ? "set" : ""), want);
      } else {
        const press = PRESSES[name];
        assert.ok(press, `a press named ${name}`);
        // grace-view sends nothing for a locked setting, a key, a text or a secret; the web view the same.
        const value = s.locked ? undefined : nextValue(s, s.default, press[0], press[1]);
        if (want === "none") {
          assert.equal(value, undefined);
          continue;
        }
        const bar = want.lastIndexOf("|");
        const args = JSON.parse(want.slice(0, bar)) as { key: string; value: Json };
        assert.deepEqual({ key: s.key, value }, args);
        if (s.type === "integer") {
          assert.ok(Number.isInteger(value), "an integer's value is whole");
        }
        assert.equal(formatValue(s, value), want.slice(bar + 1));
      }
    }
  });
}

test("printf's %.*f: ties to even on the exact value, and negative zero", () => {
  assert.equal(fixed(0.125, 2), "0.12");   // exactly representable: a tie, to even (toFixed gives 0.13)
  assert.equal(fixed(0.375, 2), "0.38");
  assert.equal(fixed(2.5, 0), "2");
  assert.equal(fixed(3.5, 0), "4");
  assert.equal(fixed(1.005, 2), "1.00");   // 1.005 is below its decimal: no tie
  assert.equal(fixed(-0, 1), "-0.0");
  assert.equal(fixed(-1.25, 1), "-1.2");
  assert.equal(fixed(1e-7, 2), "0.00");
  assert.equal(fixed(8642, 0), "8642");
  assert.equal(fixed(1.05, 2), "1.05");
});

test("decimals of a step, as grace-view's decimals_of", () => {
  assert.equal(decimalsOf(0.05), 2);
  assert.equal(decimalsOf(0.5), 1);
  assert.equal(decimalsOf(1), 0);
  assert.equal(decimalsOf(0.25), 2);
  assert.equal(decimalsOf(0.00001), 4);
});

test("a value with no unit, a word unit and a symbol unit", () => {
  const base: Setting = { key: "k", type: "number", default: 1, label: "K", help: "", home: "p", effect: "now",
                          availability: "live", locked: false, step: 0.5 };
  assert.equal(formatValue(base, 2), "2.0");
  assert.equal(formatValue({ ...base, unit: "s" }, 2), "2.0 s");
  assert.equal(formatValue({ ...base, unit: "×" }, 2), "2.0×");
  assert.equal(formatValue({ ...base, unit: "%" }, 2), "2.0%");
  assert.equal(formatValue(base, "not a number"), "0.0");
});
