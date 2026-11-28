// SPDX-License-Identifier: MIT
// A space's flow (D278) as the browser reads it, as grace-view does: the step its state names or the first, an optional
// step never the last, the space's words for what Next waits for, the steps done, start, and where Done leads.
import { test } from "node:test";
import assert from "node:assert/strict";
import { flowBlocked, flowDone, flowOptional, flowStarts, flowStep, flowThen, flowsOf } from "../../src/model/flow.ts";
import { pageById } from "../../src/model/layout.ts";
import { space } from "../support/fixtures.ts";

const demo = space("demo").pages;
const setup = pageById(demo, "demo.setup")!;

test("a flow's step: the one its state names, else the first", () => {
  assert.deepEqual(flowsOf(demo).map((p) => p.id), ["demo.setup"]);
  assert.equal(flowStep(setup, undefined), 0);
  assert.equal(flowStep(setup, { step: "trim" }), 2);
  assert.equal(flowStep(setup, { step: "dusk" }), 0);  // an unknown step: the first
  assert.equal(flowStep(setup, { step: 3 }), 0);
});

test("an optional step, never the last; what Next waits for; the steps done; start", () => {
  assert.equal(flowOptional(setup, 2), true);
  assert.equal(flowOptional(setup, 1), false);
  assert.equal(flowOptional({ ...setup, flow: { ...setup.flow!, optional: ["ready"] } }, 3), false);
  assert.equal(flowBlocked({ blocked: "Trim the wick first." }), "Trim the wick first.");
  assert.equal(flowBlocked({ blocked: "" }), null);
  assert.equal(flowBlocked({ blocked: "x".repeat(161) }), null);  // past the lint's 160: not shown
  assert.deepEqual([...flowDone({ done: ["welcome", 4, "flame"] })], ["welcome", "flame"]);
  assert.equal(flowStarts({ start: true }), true);
  assert.equal(flowStarts({ start: "yes" }), false);
});

test("Done leads to the flow's then, or the space's first page on the rail", () => {
  assert.equal(flowThen(demo, setup), "demo.overview");
  const bare = { ...setup, flow: { topic: "lantern.setup", command: "setup.step", then: "demo.gone" } };
  assert.equal(flowThen(demo, bare), demo.categories[0]!.pages[0]);
});
