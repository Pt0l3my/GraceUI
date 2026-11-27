// SPDX-License-Identifier: MIT
// An action's confirmation that waits before its second press counts (D274), as in game.
import { test } from "node:test";
import assert from "node:assert/strict";
import { confirmWait } from "../../src/model/confirm.ts";

test("a confirmation waits its action's confirm_after_ms, at most 3 s; none, or anything odd, at once", () => {
  assert.equal(confirmWait({}), 0);
  assert.equal(confirmWait({ confirm_after_ms: 1000 }), 1000);
  assert.equal(confirmWait({ confirm_after_ms: 3000 }), 3000);
  assert.equal(confirmWait({ confirm_after_ms: 60000 }), 3000);
  assert.equal(confirmWait({ confirm_after_ms: 0 }), 0);
  assert.equal(confirmWait({ confirm_after_ms: -5 }), 0);
  assert.equal(confirmWait({ confirm_after_ms: Number.NaN }), 0);
});
