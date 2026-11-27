// SPDX-License-Identifier: MIT
// A slider's typed entry (D266): fixtures/typed-cases.json's texts, which grace-view's typed_tests.cpp runs too, so that
// both views take or refuse the same text alike.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import type { Json, Setting } from "../../src/protocol.ts";
import { plainNumber, typedValue } from "../../src/model/typed.ts";
import { ROOT } from "../support/fixtures.ts";

interface Cases {
  settings: Setting[];
  cases: { setting: string; text: string; value?: Json; words?: string; nothing?: boolean }[];
}

const shared = JSON.parse(readFileSync(join(ROOT, "fixtures", "typed-cases.json"), "utf8")) as Cases;
const settings = new Map(shared.settings.map((s) => [s.key, s]));

test("a typed number: taken on its step within its bounds, or refused in words, as grace-view's", () => {
  assert.ok(shared.cases.length > 40);
  for (const c of shared.cases) {
    const s = settings.get(c.setting);
    assert.ok(s, c.setting);
    const got = typedValue(s, c.text);
    const at = `${c.setting} "${c.text}"`;
    if (c.value !== undefined) {
      assert.ok(got.ok, at);
      assert.equal(got.ok && got.value, c.value, at);
      assert.ok(!Object.is(got.ok && got.value, -0), `${at}: never a negative zero`);
      if (s.type === "integer") {
        assert.ok(Number.isInteger(got.ok && got.value), `${at}: whole`);
      }
    } else if (c.words !== undefined) {
      assert.deepEqual(got, { ok: false, words: c.words }, at);
    } else {
      assert.equal(c.nothing, true, at);
      assert.deepEqual(got, { ok: false, words: "" }, at);
    }
  }
});

test("a slider's field opens with its value at its step's decimals, without its unit", () => {
  const base = settings.get("scale")!;
  assert.equal(plainNumber(base, 1.25), "1.25");
  assert.equal(plainNumber(settings.get("gap")!, 2.5), "2.5");
  assert.equal(plainNumber(settings.get("port")!, 8642), "8642");
});
