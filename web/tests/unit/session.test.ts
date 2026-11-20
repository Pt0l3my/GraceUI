// SPDX-License-Identifier: MIT
// The session's commands (§5.7, §6.6, §6.7; invariant 3): every edit one command of the shape grace-view sends, pending
// until its answer, the space's value unchanged until then; a refusal clears it, gives its reason in words and asks for
// focus on its row; a locked setting sends nothing; edits made while one is on its way coalesce; a secret is never held.
import { test } from "node:test";
import assert from "node:assert/strict";
import { Session, refusalWords } from "../../src/model/session.ts";
import { FixtureTransport } from "../../src/transport/fixture.ts";
import type { Ended, Transport, TransportEvents } from "../../src/transport/transport.ts";
import { builtinSpaces } from "../support/fixtures.ts";

const tick = (ms = 10) => new Promise((resolve) => setTimeout(resolve, ms));

/** A transport that ends at once, as a connection that cannot be made. */
function ending(reason: Ended["reason"]): Transport {
  return {
    connect: (events: TransportEvents) => {
      setTimeout(() => events.ended({ reason } as Ended), 0);
      return Promise.resolve(false);
    },
    subscribe: () => 0,
    unsubscribe: () => {},
    command: () => Promise.resolve({ ok: false, error: "not_now" }),
    close: () => {},
  };
}

test("a lost connection is stale, and tried again after its delay", async () => {
  let made = 0;
  const session = new Session({ transport: () => (made++, ending("lost")), reconnectDelay: (n) => (n < 2 ? 5 : null) });
  await session.start();
  await tick(60);
  assert.equal(session.connection.value, "lost");
  assert.equal(made, 3, "the first try and two more");
  session.stop();
});

test("a refused sign-in loads the page again once; an old page is told to reload", async () => {
  let reloads = 0;
  const session = new Session({ transport: () => ending("unauthorised"), reload: () => reloads++ });
  await session.start();
  await tick(20);
  assert.equal(reloads, 1);
  assert.equal(session.connection.value, "ended");
  const old = new Session({ transport: () => ending("version") });
  await old.start();
  await tick(20);
  assert.equal(old.notice.value, "This page needs updating: reload it.");
});

async function started(latency = 0) {
  const transport = new FixtureTransport(builtinSpaces(), { latency });
  const session = new Session({ transport: () => transport, reconnectDelay: () => null });
  assert.equal(await session.start(), true);
  await tick(latency * 3 + 10);
  return { session, transport };
}

test("the view opens the first content space's first page; GraceUI's space sits at the foot", async () => {
  const { session } = await started();
  assert.deepEqual(session.route.value, { space: "demo", page: "demo.overview", tab: 0 });
  assert.deepEqual(session.contentSpaces().map((s) => s.info.id), ["demo"]);
  assert.equal(session.footSpace()?.info.id, "graceui");
  assert.equal(session.railSpace.value, "demo");
  session.open("graceui", "graceui.menu");
  assert.equal(session.railSpace.value, "demo");  // the foot's page keeps the content space's rail
});

test("settings.set and settings.reset: grace-view's shapes; pending until the answer, then the space's value", async () => {
  const { session, transport } = await started(20);
  session.set("demo", "lantern.gap_s", 3);
  assert.equal(session.stored("demo", "lantern.gap_s"), 2.5, "the space's value is unchanged until it answers");
  assert.equal(session.shown("demo", "lantern.gap_s"), 3);
  assert.equal(session.edit("demo", "lantern.gap_s")?.waiting, true);
  await tick(100);
  assert.equal(session.edit("demo", "lantern.gap_s"), undefined);
  assert.equal(session.stored("demo", "lantern.gap_s"), 3);
  session.reset("demo", "lantern.gap_s");
  await tick(100);
  assert.equal(session.stored("demo", "lantern.gap_s"), 2.5);
  assert.deepEqual(transport.sent, [
    { space: "demo", name: "settings.set", args: { key: "lantern.gap_s", value: 3 } },
    { space: "demo", name: "settings.reset", args: { scope: "key", target: "lantern.gap_s" } },
  ]);
});

test("a refusal: the reason in words, the value as the space holds it, focus asked for the row", async () => {
  const { session } = await started();
  session.set("demo", "lantern.gap_s", 99);
  await tick(30);
  assert.equal(session.edit("demo", "lantern.gap_s"), undefined);
  assert.equal(session.refusal("demo", "lantern.gap_s"), "Choose a value from 0.5 s to 10.0 s.");
  assert.equal(session.shown("demo", "lantern.gap_s"), 2.5);
  assert.equal(session.focusRequest.value?.key, "lantern.gap_s");
  session.set("demo", "lantern.gap_s", 3);
  assert.equal(session.refusal("demo", "lantern.gap_s"), undefined, "a new edit clears the old reason");
});

test("a locked setting sends nothing (grace-view's set)", async () => {
  const { session, transport } = await started();
  session.set("demo", "lantern.festival", true);
  session.set("graceui", "dashboard.lan", true);
  session.reset("demo", "lantern.festival");
  await tick(30);
  assert.deepEqual(transport.sent, []);
});

test("edits while one is on its way: the newest is sent once the first is answered", async () => {
  const { session, transport } = await started(20);
  session.set("demo", "lantern.wicks", 4);
  session.set("demo", "lantern.wicks", 5);
  session.set("demo", "lantern.wicks", 6);
  assert.equal(session.shown("demo", "lantern.wicks"), 6);
  assert.equal(transport.sent.length, 1);
  await tick(200);
  assert.deepEqual(transport.sent.map((c) => c.args.value), [4, 6]);
  assert.equal(session.stored("demo", "lantern.wicks"), 6);
  assert.equal(session.edit("demo", "lantern.wicks"), undefined);
});

test("a secret is sent once and never held: the row knows only Set", async () => {
  const { session, transport } = await started(20);
  session.set("demo", "lantern.oil_token", "Zq7-wick-tallow-99");
  assert.equal(session.shown("demo", "lantern.oil_token"), "set");
  assert.ok(!JSON.stringify([...session.edits.value.values()]).includes("Zq7-wick"));
  await tick(100);
  assert.equal(session.stored("demo", "lantern.oil_token"), "set");
  assert.ok(!JSON.stringify(session.space("demo")?.settings.value).includes("Zq7-wick"));
  assert.equal(transport.sent[0]?.args.value, "Zq7-wick-tallow-99", "the one command carries it to the space");
});

test("a setting's home: its tab opened and focus asked for its row; Page Up and Page Down in rail order", async () => {
  const { session } = await started();
  assert.deepEqual(session.openSetting("demo", "lantern.night_only"), { section: "rhythm", advanced: true });
  assert.deepEqual(session.route.value, { space: "demo", page: "demo.behaviour", tab: 1 });
  assert.equal(session.focusRequest.value?.key, "lantern.night_only");
  assert.equal(session.step(1), true);
  assert.equal(session.route.value?.page, "demo.log");
  session.open("demo", "demo.festival");
  assert.equal(session.step(1), false);
  assert.equal(session.open("demo", "demo.nowhere"), false);
});

test("a header action sends its command with no arguments, as grace-view's", async () => {
  const { session, transport } = await started();
  const answer = await session.action("graceui", { id: "disconnect", label: "Disconnect every browser",
                                                   command: "dashboard.disconnect", tone: "destructive" });
  assert.deepEqual(answer, { ok: true });
  assert.deepEqual(transport.sent, [{ space: "graceui", name: "dashboard.disconnect", args: {} }]);
});

test("a topic is watched while shown, and its rows kept to the ring", async () => {
  const { session } = await started();
  const watched = session.watch("demo", "lantern.log");
  await tick(20);
  assert.equal(watched.data.value?.kind, "rows");
  assert.equal(watched.data.value?.kind === "rows" ? watched.data.value.rows.length : 0, 3);
  watched.release();
  watched.release();  // twice is harmless
});

test("a stream starts over at a gap, an empty batch past it clears it, and rows held are not shown twice (1.1)", async () => {
  const { session, transport } = await started();
  const watched = session.watch("demo", "lantern.log");
  await tick(20);
  const seqs = () => (watched.data.value?.kind === "rows" ? watched.data.value.rows.map((row) => row.seq) : []);
  const held = seqs();
  const next = watched.data.value?.kind === "rows" ? watched.data.value.next : 0;
  assert.ok(held.length > 0 && next > 0);
  const events = (transport as unknown as { events: TransportEvents }).events;
  const batch = (from: number, rows: number[]) =>
    events.rows({ v: 1, t: "rows", space: "demo", topic: "lantern.log", from_seq: from,
                  rows: rows.map((seq) => ({ seq, text: "A line." })), dropped: 0 });
  batch(next - 1, [next - 1, next]);  // its first row is held already
  assert.deepEqual(seqs(), [...held, next]);
  batch(next + 6, []);  // a gap, and nothing after it: the space cleared the stream
  assert.deepEqual(seqs(), []);
  batch(next + 6, [next + 6]);
  assert.deepEqual(seqs(), [next + 6]);
  watched.release();
});

test("a refusal's words are the space's own when it gives them (1.1), else the code's", () => {
  assert.equal(refusalWords({ ok: false, error: "not_now", detail: "Not now: the keeper is trimming the wick." }),
               "Not now: the keeper is trimming the wick.");
  assert.equal(refusalWords({ ok: false, error: "not_now" }), "Not possible right now.");
  assert.equal(refusalWords({ ok: false, error: "not_now", detail: "" }), "Not possible right now.");
});
