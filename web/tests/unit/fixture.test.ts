// SPDX-License-Identifier: MIT
// The fixture transport and the space it plays (§9.2, §6.6, §7.3): welcome, each topic's first frame, and every
// command checked as a space's one entry point checks it: unknown commands and keys, values of the wrong type or out of
// bounds, locked settings, confirmations, the in-game-only command, and a secret never kept or published.
import { test } from "node:test";
import assert from "node:assert/strict";
import type { Rows, SettingsBody, State, Welcome } from "../../src/protocol.ts";
import { FixtureSpace } from "../../src/transport/fixture-space.ts";
import { FixtureTransport } from "../../src/transport/fixture.ts";
import type { TransportEvents } from "../../src/transport/transport.ts";
import { builtinSpaces, space } from "../support/fixtures.ts";

function recorder() {
  const got: { welcome: Welcome[]; state: State[]; rows: Rows[]; errors: string[] } = {
    welcome: [], state: [], rows: [], errors: [],
  };
  const events: TransportEvents = {
    welcome: (w) => got.welcome.push(w),
    state: (s) => got.state.push(s),
    rows: (r) => got.rows.push(r),
    error: (code, where) => got.errors.push(`${where ?? ""}:${code}`),
    ended: () => {},
  };
  return { got, events };
}

const tick = (ms = 5) => new Promise((resolve) => setTimeout(resolve, ms));

test("welcome names both spaces, with their settings schemas", async () => {
  const transport = new FixtureTransport(builtinSpaces());
  const { got, events } = recorder();
  assert.equal(await transport.connect(events), true);
  const welcome = got.welcome[0];
  assert.ok(welcome);
  assert.equal(welcome.protocol, "1.1");
  assert.deepEqual(welcome.spaces.map((s) => s.id), ["graceui", "demo"]);
  assert.equal(welcome.spaces[1]?.settings_schema.hash, space("demo").settings.schema.hash);
});

test("each topic's first frame: pages, settings, a state fixture, a stream's rows; nothing for a topic with none", async () => {
  const spaces = builtinSpaces();
  delete spaces[0]?.topics?.["graceui.about"];  // a topic the space has published nothing on
  const transport = new FixtureTransport(spaces);
  const { got, events } = recorder();
  await transport.connect(events);
  transport.subscribe("demo", "pages");
  transport.subscribe("demo", "settings");
  transport.subscribe("demo", "lantern.status");
  transport.subscribe("demo", "lantern.log");
  transport.subscribe("demo", "lantern.log", 3);
  transport.subscribe("graceui", "graceui.about");  // declared, but no fixture: nothing (invariant 2)
  transport.subscribe("demo", "not.a.topic");
  await tick();
  assert.deepEqual(got.state.map((s) => s.topic), ["pages", "settings", "lantern.status"]);
  const body = got.state[1]?.body as unknown as SettingsBody;
  assert.equal(body.values["lantern.gap_s"], 2.5);
  assert.deepEqual(body.changed, []);
  assert.deepEqual(body.inherited?.map((i) => i.key), ["lantern.shield"]);
  assert.equal(got.rows[0]?.rows.length, 3);
  assert.equal(got.rows[0]?.from_seq, 1);
  assert.equal(got.rows[1]?.from_seq, 3);
  assert.deepEqual(got.errors, ["demo:unknown_topic"]);
});

test("settings.set: the answer, then the changed state with a new revision", async () => {
  const transport = new FixtureTransport(builtinSpaces());
  const { got, events } = recorder();
  await transport.connect(events);
  transport.subscribe("demo", "settings");
  await tick();
  const answer = await transport.command("demo", "settings.set", { key: "lantern.gap_s", value: 3 });
  assert.deepEqual(answer, { ok: true });
  await tick();
  const last = got.state[got.state.length - 1];
  assert.equal(last?.rev, 2);
  const body = last?.body as unknown as SettingsBody;
  assert.equal(body.values["lantern.gap_s"], 3);
  assert.deepEqual(body.changed, ["lantern.gap_s"]);
  assert.deepEqual(transport.sent, [{ space: "demo", name: "settings.set", args: { key: "lantern.gap_s", value: 3 } }]);
});

test("the space refuses what its description refuses", () => {
  const demo = new FixtureSpace(space("demo"));
  const graceui = new FixtureSpace(space("graceui"));
  const refusal = (s: FixtureSpace, name: string, args: object) => {
    const answer = s.handle(name, args as never).answer;
    return answer.ok ? "ok" : answer.error;
  };
  assert.equal(refusal(demo, "settings.set", { key: "lantern.festival", value: true }), "locked");
  assert.equal(refusal(graceui, "settings.set", { key: "dashboard.lan", value: true }), "locked");
  assert.equal(refusal(demo, "settings.set", { key: "lantern.nope", value: 1 }), "unknown_key");
  assert.equal(refusal(demo, "settings.set", { key: "lantern.gap_s", value: 11 }), "invalid_value");
  assert.equal(refusal(demo, "settings.set", { key: "lantern.gap_s", value: "3" }), "invalid_value");
  assert.equal(refusal(demo, "settings.set", { key: "lantern.wicks", value: 2.5 }), "invalid_value");
  assert.equal(refusal(demo, "settings.set", { key: "lantern.colour", value: "green" }), "invalid_value");
  assert.equal(refusal(demo, "settings.set", { key: "lantern.mode", value: "inherit" }), "invalid_value");
  assert.equal(refusal(demo, "settings.set", { key: "lantern.shield", value: "auto" }), "invalid_value");
  assert.equal(refusal(demo, "settings.set", { key: "lantern.enabled", value: 1 }), "invalid_value");
  assert.equal(refusal(demo, "settings.set", { key: "lantern.keeper", value: "x".repeat(257) }), "invalid_value");
  assert.equal(refusal(demo, "settings.set", { key: "lantern.trim_key", value: "Page Up" }), "invalid_value");
  assert.equal(refusal(demo, "settings.set", { key: "lantern.gap_s" }), "bad_message");
  assert.equal(refusal(demo, "settings.reset", { scope: "page", target: "demo.behaviour" }), "confirm_required");
  assert.equal(refusal(demo, "settings.reset", { scope: "all" }), "confirm_required");
  assert.equal(refusal(demo, "settings.reset", { scope: "all", confirm: true }), "ok");
  assert.equal(refusal(demo, "settings.reset", { scope: "key", target: "lantern.festival" }), "locked");
  assert.equal(refusal(demo, "dashboard.disconnect", {}), "unknown_command");  // not the demo space's
  assert.equal(refusal(graceui, "menu.close", {}), "not_now");  // in game only
  assert.equal(refusal(graceui, "dashboard.disconnect", {}), "ok");
  assert.equal(refusal(demo, "lantern.clear_log", {}), "ok");
});

test("a secret is kept only as set, and never published", () => {
  const demo = new FixtureSpace(space("demo"));
  const outcome = demo.handle("settings.set", { key: "lantern.oil_token", value: "Zq7-wick-tallow-99" });
  assert.deepEqual(outcome.answer, { ok: true });
  assert.equal(demo.value("lantern.oil_token"), "set");
  assert.ok(!JSON.stringify(outcome.publish).includes("Zq7-wick"));
  assert.ok(!JSON.stringify(demo.settingsBody()).includes("Zq7-wick"));
  demo.handle("settings.reset", { scope: "key", target: "lantern.oil_token" });
  assert.equal(demo.value("lantern.oil_token"), "");
});

test("a reset of a page needs its confirmation, and resets only that page's unlocked settings", () => {
  const demo = new FixtureSpace(space("demo"));
  demo.handle("settings.set", { key: "lantern.gap_s", value: 4 });
  const outcome = demo.handle("settings.reset", { scope: "page", target: "demo.behaviour", confirm: true });
  assert.deepEqual(outcome.answer, { ok: true });
  assert.equal(demo.value("lantern.gap_s"), 2.5);
  assert.equal(demo.handle("settings.reset", { scope: "page", target: "demo.nowhere", confirm: true }).answer.ok, false);
});
