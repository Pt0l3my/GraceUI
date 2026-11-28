// SPDX-License-Identifier: MIT
// The WebSocket transport against an in-process fake socket (§6.3 to §6.8, §7.2 items 3, 4 and 11; invariant 6): the
// ticket by a same-origin POST, hello first with the ticket, welcome, the frames of every kind, the refusals and the
// ends (bye, a version mismatch, a refused ticket, a lost socket), liveness, the rate and the frame limit.
import { afterEach, mock, test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import type { Welcome } from "../../src/protocol.ts";
import { LIMITS } from "../../src/protocol.ts";
import type { Ended, TransportEvents } from "../../src/transport/transport.ts";
import { WebSocketTransport, samePath } from "../../src/transport/websocket.ts";
import type { SocketLike, TicketResponse } from "../../src/transport/websocket.ts";

class FakeSocket implements SocketLike {
  readyState = 0;
  readonly sent: string[] = [];
  onopen: ((event: unknown) => void) | null = null;
  onmessage: ((event: { data: unknown }) => void) | null = null;
  onclose: ((event: unknown) => void) | null = null;
  onerror: ((event: unknown) => void) | null = null;
  readonly url: string;
  constructor(url: string) {
    this.url = url;
  }
  send(data: string): void {
    this.sent.push(data);
  }
  close(): void {
    if (this.readyState === 3) {
      return;
    }
    this.readyState = 3;
    this.onclose?.({});
  }
  open(): void {
    this.readyState = 1;
    this.onopen?.({});
  }
  receive(frame: unknown): void {
    this.onmessage?.({ data: typeof frame === "string" ? frame : JSON.stringify(frame) });
  }
  frames(): { [key: string]: unknown }[] {
    return this.sent.map((s) => JSON.parse(s));
  }
}

const TICKET = "x".repeat(43);
const WELCOME: Welcome = {
  v: 1, t: "welcome", protocol: "1.24", caps: [], graceui: "0.2.0", session: "s1",
  spaces: [{ id: "demo", name: "Demo space", version: "0.2.0", increment: "0.2", settings_schema: { number: 1, hash: "h" } }],
};

const opened: WebSocketTransport[] = [];
afterEach(() => {
  for (const transport of opened.splice(0)) {
    transport.close();  // its timers go with it
  }
});

function harness(options: { status?: number; ticket?: unknown; protocol?: string;
                          upload?: { status: number; text?: string } } = {}) {
  const sockets: FakeSocket[] = [];
  const fetches: { path: string; init: RequestInit }[] = [];
  const ended: Ended[] = [];
  const got: { [kind: string]: unknown[] } = { welcome: [], state: [], rows: [], error: [] };
  const transport = new WebSocketTransport({
    bundle: "0.2.0",
    location: { protocol: options.protocol ?? "http:", host: "127.0.0.1:8642" },
    fetch: async (path: string, init: RequestInit): Promise<TicketResponse> => {
      fetches.push({ path, init });
      if (path === "/upload") {
        const upload = options.upload ?? { status: 202 };
        return { ok: upload.status === 202, status: upload.status, json: async () => ({}),
                 text: async () => upload.text ?? "" };
      }
      const status = options.status ?? 200;
      return { ok: status === 200, status, json: async () => options.ticket ?? { ticket: TICKET } };
    },
    socket: (url: string) => {
      const s = new FakeSocket(url);
      sockets.push(s);
      return s;
    },
  });
  opened.push(transport);
  const events: TransportEvents = {
    welcome: (w) => got.welcome?.push(w),
    state: (s) => got.state?.push(s),
    rows: (r) => got.rows?.push(r),
    error: (code, space) => got.error?.push(`${space ?? ""}:${code}`),
    ended: (e) => ended.push(e),
  };
  return { transport, sockets, fetches, ended, got, events };
}

const settle = async () => {
  for (let i = 0; i < 10; ++i) {
    await Promise.resolve();
  }
};

async function connected(protocol?: string, upload?: { status: number; text?: string }) {
  const h = harness({ ...(protocol ? { protocol } : {}), ...(upload ? { upload } : {}) });
  const done = h.transport.connect(h.events);
  await settle();
  const socket = h.sockets[0];
  assert.ok(socket);
  socket.open();
  socket.receive(WELCOME);
  assert.equal(await done, true);
  return { ...h, socket };
}

test("the handshake: a same-origin POST for the ticket, the socket on this origin, hello with the ticket first", async () => {
  const { fetches, socket, got } = await connected();
  assert.equal(fetches.length, 1);
  const request = fetches[0];
  assert.equal(request?.path, "/api/ticket");
  assert.equal(request?.init.method, "POST");
  assert.equal(request?.init.credentials, "same-origin");
  assert.equal(request?.init.body, "{}");
  assert.deepEqual(request?.init.headers, { "Content-Type": "application/json" });
  assert.equal(socket.url, "ws://127.0.0.1:8642/api/ws");
  assert.deepEqual(socket.frames()[0], { v: 1, t: "hello", protocol: "1.24", bundle: "0.2.0", caps: [], ticket: TICKET });
  assert.equal(got.welcome?.length, 1);
});

test("an https page's socket is wss, on the same host", async () => {
  const { socket } = await connected("https:");
  assert.equal(socket.url, "wss://127.0.0.1:8642/api/ws");
});

test("only paths on this origin, never another host or scheme (invariant 6)", () => {
  assert.equal(samePath("/api/ws"), true);
  assert.equal(samePath("/api/ticket"), true);
  for (const path of ["//evil.example/ws", "ws://evil.example/ws", "ws", "/\\evil", "/ws?x=1", ""]) {
    assert.equal(samePath(path), false, path);
    assert.throws(() => new WebSocketTransport({ bundle: "0", socketPath: path }));
  }
});

test("sub, unsub and cmd frames as protocol.ts defines them; results answer their command", async () => {
  const { transport, socket, got } = await connected();
  const sub = transport.subscribe("demo", "settings");
  const stream = transport.subscribe("demo", "lantern.log", 5);
  transport.unsubscribe("demo", stream, "lantern.log");
  const first = transport.command("demo", "settings.set", { key: "lantern.gap_s", value: 3 });
  const second = transport.command("demo", "settings.set", { key: "lantern.gap_s", value: 99 });
  const third = transport.command("demo", "lantern.trim", {});
  const frames = socket.frames().slice(1);
  assert.deepEqual(frames[0], { v: 1, t: "sub", space: "demo", id: sub, topic: "settings" });
  assert.deepEqual(frames[1], { v: 1, t: "sub", space: "demo", id: stream, topic: "lantern.log", since: 5 });
  assert.deepEqual(frames[2], { v: 1, t: "unsub", space: "demo", id: stream, topic: "lantern.log" });
  assert.deepEqual(frames[3], { v: 1, t: "cmd", space: "demo", id: 1, name: "settings.set",
                                args: { key: "lantern.gap_s", value: 3 } });
  socket.receive({ v: 1, t: "result", space: "demo", id: 2, error: "invalid_value" });
  socket.receive({ v: 1, t: "result", space: "demo", id: 1, ok: true });
  socket.receive({ v: 1, t: "result", space: "demo", id: 3, error: "no_such_code", body: { reason: "words" } });
  assert.deepEqual(await first, { ok: true });
  assert.deepEqual(await second, { ok: false, error: "invalid_value" });
  assert.deepEqual(await third, { ok: false, error: "internal", body: { reason: "words" } });
  socket.receive({ v: 1, t: "state", space: "demo", topic: "settings", rev: 2, body: {} });
  socket.receive({ v: 1, t: "rows", space: "demo", topic: "lantern.log", from_seq: 1, rows: [{ seq: 1 }], dropped: 0 });
  socket.receive({ v: 1, t: "error", space: "demo", code: "unknown_topic", detail: "no such topic" });
  assert.equal(got.state?.length, 1);
  assert.equal(got.rows?.length, 1);
  assert.deepEqual(got.error, ["demo:unknown_topic"]);
});

test("malformed frames are dropped, and a space's frames before welcome are ignored", async () => {
  const h = harness();
  const done = h.transport.connect(h.events);
  await settle();
  const socket = h.sockets[0];
  assert.ok(socket);
  socket.open();
  socket.receive({ v: 1, t: "state", space: "demo", topic: "settings", rev: 1, body: {} });
  socket.receive("not json");
  socket.receive("[1, 2]");
  socket.receive({ v: 1 });
  socket.receive({ v: 1, t: "state", space: "demo", topic: "settings", rev: "1", body: {} });
  socket.receive(WELCOME);
  assert.equal(await done, true);
  socket.receive({ v: 1, t: "state", space: "demo", topic: "settings", rev: "x", body: {} });
  socket.receive({ v: 1, t: "rows", space: "demo", topic: "t", from_seq: 1, rows: [1], dropped: 0 });
  socket.receive({ v: 1, t: "result", id: 99, ok: true });
  assert.equal(h.got.state?.length, 0);
  assert.equal(h.got.rows?.length, 0);
  assert.deepEqual(h.ended, []);
});

test("a ping is answered with a pong", async () => {
  const { socket } = await connected();
  socket.receive({ v: 1, t: "ping" });
  assert.deepEqual(socket.frames().at(-1), { v: 1, t: "pong" });
});

test("bye ends the transport with its reason; a frame of another major, or welcome's, ends it for the version", async () => {
  for (const reason of ["superseded", "shutdown", "unauthorised", "fault"]) {
    const { socket, ended } = await connected();
    socket.receive({ v: 1, t: "bye", reason });
    assert.deepEqual(ended, [{ reason }]);
    assert.equal(socket.readyState, 3);
  }
  const later = await connected();
  later.socket.receive({ v: 2, t: "state" });
  assert.deepEqual(later.ended, [{ reason: "version" }]);
  const h = harness();
  const done = h.transport.connect(h.events);
  await settle();
  h.sockets[0]?.open();
  h.sockets[0]?.receive({ ...WELCOME, protocol: "2.0" });
  assert.equal(await done, false);
  assert.deepEqual(h.ended, [{ reason: "version" }]);
});

test("a refused ticket: unauthorised for 401 and 403, lost otherwise; no socket is opened", async () => {
  for (const [status, reason] of [[403, "unauthorised"], [401, "unauthorised"], [500, "lost"]] as const) {
    const h = harness({ status });
    assert.equal(await h.transport.connect(h.events), false);
    assert.deepEqual(h.ended, [{ reason }]);
    assert.equal(h.sockets.length, 0);
  }
  const bad = harness({ ticket: { ticket: 7 } });
  assert.equal(await bad.transport.connect(bad.events), false);
  assert.equal(bad.sockets.length, 0);
});

test("a lost socket answers every waiting command with a reason, and says it ended", async () => {
  const { transport, socket, ended } = await connected();
  const waiting = transport.command("demo", "settings.set", { key: "lantern.gap_s", value: 3 });
  socket.close();
  const answer = await waiting;
  assert.equal(answer.ok, false);
  assert.equal(!answer.ok && answer.local !== undefined, true);
  assert.deepEqual(ended, [{ reason: "lost" }]);
  const after = await transport.command("demo", "settings.set", { key: "lantern.gap_s", value: 3 });
  assert.equal(after.ok, false);
});

test("a frame over 16 KiB is never sent; the command is refused at once", async () => {
  const { transport, socket } = await connected();
  const before = socket.sent.length;
  const answer = await transport.command("demo", "settings.set", { key: "lantern.keeper", value: "x".repeat(17000) });
  assert.deepEqual(answer, { ok: false, error: "invalid_value", local: "Too long to send." });
  assert.equal(socket.sent.length, before);
});

test("liveness: a ping every 15 s; two missed pongs close the socket", async () => {
  mock.timers.enable({ apis: ["setInterval", "setTimeout"] });
  try {
    const { socket, ended } = await connected();
    mock.timers.tick(15000);
    assert.deepEqual(socket.frames().at(-1), { v: 1, t: "ping" });
    socket.receive({ v: 1, t: "pong" });
    mock.timers.tick(15000);
    mock.timers.tick(15000);
    assert.deepEqual(ended, []);
    mock.timers.tick(15000);
    assert.deepEqual(ended, [{ reason: "lost" }]);
  } finally {
    mock.timers.reset();
  }
});

test("at most 16 frames a second, the server's 20 less a margin: commands wait their turn, in order (D203)", async () => {
  mock.timers.enable({ apis: ["setInterval", "setTimeout", "Date"] });
  try {
    const { transport, socket } = await connected();
    const before = socket.sent.length;
    for (let i = 0; i < 25; ++i) {
      void transport.command("demo", "lantern.trim", {});
    }
    assert.ok(socket.sent.length - before <= 16, `${socket.sent.length - before} sent in the first second`);
    mock.timers.tick(1000);
    mock.timers.tick(1000);
    const ids = socket.frames().slice(before).map((f) => f.id);
    assert.deepEqual(ids, Array.from({ length: 25 }, (_, i) => i + 1));
  } finally {
    mock.timers.reset();
  }
});

test("subs and unsubs keep to the same pace, so a page opening many topics loses none (D203)", async () => {
  mock.timers.enable({ apis: ["setInterval", "setTimeout", "Date"] });
  try {
    const { transport, socket } = await connected();
    const before = socket.sent.length;
    const ids: number[] = [];
    for (let i = 0; i < 30; ++i) {
      ids.push(transport.subscribe("demo", `topic.${i}`));
    }
    const first = socket.sent.length - before;
    assert.ok(first <= 16, `${first} sent in the first second`);
    // An unsub of a sub still waiting takes it back: neither is sent.
    transport.unsubscribe("demo", ids[29] ?? 0, "topic.29");
    for (let i = 0; i < 3; ++i) {
      mock.timers.tick(1000);
    }
    const subs = socket.frames().slice(before);
    assert.equal(subs.length, 29);
    assert.ok(subs.every((f) => f.t === "sub"));
    assert.deepEqual(subs.map((f) => f.topic), Array.from({ length: 29 }, (_, i) => `topic.${i}`));
  } finally {
    mock.timers.reset();
  }
});

// ---- Uploads (G28, D226, §7.2 item 18)

/** Waits, a macrotask at a time, until a condition holds: the digest and the POST settle on other turns. */
async function until(check: () => boolean): Promise<void> {
  for (let i = 0; i < 200 && !check(); ++i) {
    await new Promise((resolve) => setTimeout(resolve, 1));
  }
  assert.ok(check(), "the condition held in time");
}

const UPLOAD_TICKET = "u".repeat(43);

test("an upload: its size and SHA-256 in an upload frame, its bytes POSTed with the ticket to /upload on this origin; " +
     "the result answers it; the file's name is never sent", async () => {
  const { transport, socket, fetches } = await connected();
  const text = "[[keeper]]\nname = \"Maren Holt\"\n";
  const file = new File([text], "private-roster-name.toml", { type: "text/plain" });
  const answer = transport.upload("demo", "keepers.import", {}, file);
  await until(() => socket.frames().some((f) => f.t === "upload"));
  const frame = socket.frames().find((f) => f.t === "upload");
  assert.deepEqual(frame, { v: 1, t: "upload", space: "demo", id: 1, name: "keepers.import", args: {},
                            size: Buffer.byteLength(text), sha256: createHash("sha256").update(text).digest("hex") });
  assert.equal(fetches.filter((f) => f.path === "/upload").length, 0, "nothing is posted before the ticket");
  socket.receive({ v: 1, t: "upload_ticket", space: "demo", id: 1, ticket: UPLOAD_TICKET });
  await until(() => fetches.some((f) => f.path === "/upload"));
  const post = fetches.find((f) => f.path === "/upload");
  assert.equal(post?.init.method, "POST");
  assert.equal(post?.init.credentials, "same-origin");
  assert.equal(post?.init.redirect, "error");
  assert.equal(post?.init.cache, "no-store");
  assert.equal(post?.init.referrerPolicy, "no-referrer");
  assert.deepEqual(post?.init.headers, { "Content-Type": "application/octet-stream", "Grace-Upload-Ticket": UPLOAD_TICKET });
  assert.equal(Buffer.from(post?.init.body as ArrayBuffer).toString("utf8"), text);
  socket.receive({ v: 1, t: "result", space: "demo", id: 1, ok: true, body: { added: 1 } });
  assert.deepEqual(await answer, { ok: true, body: { added: 1 } });
  assert.ok(!socket.sent.join("").includes("private-roster-name"));
  assert.ok(!JSON.stringify(fetches.map((f) => f.init.headers)).includes("private-roster-name"));
});

test("an upload refused: empty, over 32 MiB or before welcome at once; a refused POST in the server's words; a result " +
     "before the ticket sends nothing; a stray or overlong ticket is ignored; a lost socket answers it", async () => {
  const before = harness();
  assert.deepEqual(await before.transport.upload("demo", "keepers.import", {}, new Blob(["x"])),
                   { ok: false, error: "not_now", local: "Not connected to the game." });
  const { transport, socket, fetches } = await connected(undefined, { status: 403, text: "The file was refused.\n" });
  assert.deepEqual(await transport.upload("demo", "keepers.import", {}, new Blob([])),
                   { ok: false, error: "invalid_value", local: "The file is empty." });
  assert.deepEqual(await transport.upload("demo", "keepers.import", {}, new Blob([new Uint8Array(LIMITS.uploadMax + 1)])),
                   { ok: false, error: "invalid_value", local: "The file is larger than 32 MiB." });
  assert.equal(socket.frames().filter((f) => f.t === "upload").length, 0);
  // The server's words for a refused POST, when no result came first.
  const refused = transport.upload("demo", "keepers.import", {}, new Blob(["a"]));
  await until(() => socket.frames().filter((f) => f.t === "upload").length === 1);
  socket.receive({ v: 1, t: "upload_ticket", space: "demo", id: 1, ticket: UPLOAD_TICKET });
  assert.deepEqual(await refused, { ok: false, error: "invalid_value", local: "The file was refused." });
  // Refused by the space before its ticket: its result answers, and a late ticket posts nothing.
  const early = transport.upload("demo", "keepers.import", {}, new Blob(["b"]));
  await until(() => socket.frames().filter((f) => f.t === "upload").length === 2);
  socket.receive({ v: 1, t: "result", space: "demo", id: 2, error: "not_now" });
  assert.deepEqual(await early, { ok: false, error: "not_now" });
  socket.receive({ v: 1, t: "upload_ticket", space: "demo", id: 2, ticket: UPLOAD_TICKET });
  socket.receive({ v: 1, t: "upload_ticket", space: "demo", id: 99, ticket: UPLOAD_TICKET });
  // An overlong ticket is not taken; the upload waits for a proper one, then the socket is lost.
  const waiting = transport.upload("demo", "keepers.import", {}, new Blob(["c"]));
  await until(() => socket.frames().filter((f) => f.t === "upload").length === 3);
  socket.receive({ v: 1, t: "upload_ticket", space: "demo", id: 3, ticket: "t".repeat(129) });
  await settle();
  assert.equal(fetches.filter((f) => f.path === "/upload").length, 1);
  socket.close();
  const lost = await waiting;
  assert.equal(lost.ok, false);
});
