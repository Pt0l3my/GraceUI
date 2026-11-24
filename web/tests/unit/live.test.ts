// SPDX-License-Identifier: MIT
// The top bar's Live updates (G45, D252): its modes in order, what releases what is held, and each topic's newest body
// counted once.
import { test } from "node:test";
import assert from "node:assert/strict";
import { countHeld, heldNew, holdsNow, liveMode, nextLiveMode, noteSent, released, setLiveMode } from "../../src/ui/live.ts";
import type { TopicData } from "../../src/model/session.ts";

test("Live, Every 5 s, Paused, in turn; leaving Paused draws what it held", () => {
  assert.equal(nextLiveMode("live"), "every_5s");
  assert.equal(nextLiveMode("every_5s"), "paused");
  assert.equal(nextLiveMode("paused"), "live");
  setLiveMode("paused");
  assert.equal(liveMode.value, "paused");
  const body = { kind: "state", body: { sentence: "A moth." } } as unknown as TopicData;
  countHeld("demo\u0000lantern.status", body);
  countHeld("demo\u0000lantern.status", body);  // the same body, from a second block drawing it
  assert.equal(heldNew.value, 1);
  countHeld("demo\u0000lantern.status", { kind: "state", body: { sentence: "Gulls." } } as unknown as TopicData);
  assert.equal(heldNew.value, 2);
  const before = released.value;
  setLiveMode("live");
  assert.equal(heldNew.value, 0);
  assert.equal(released.value, before + 1);
  assert.equal(holdsNow(), false);
  setLiveMode("live");
});

test("a command this view sends: nothing held for 3 s", () => {
  setLiveMode("paused");
  assert.equal(holdsNow(1000), true);
  noteSent(1000);
  assert.equal(holdsNow(2000), false);
  assert.equal(holdsNow(3999), false);
  assert.equal(holdsNow(4000), true);
  setLiveMode("live");
});
