// SPDX-License-Identifier: MIT
// fake-host.ts: a small stand-in for grace-serve, for the DOM tests only (grace-dev, DESIGN §9.2, comes later and
// links the real server). It serves the built dist/ with §7.2 item 10's headers, exactly its CSP among them, only the
// files the build wrote, the session cookie on a page load, a ticket by a same-origin POST that carries it, and one
// WebSocket at /api/ws whose first frame must be hello with that ticket; then it plays the fixture spaces as the fixture
// transport does, with the same FixtureSpace. It checks Host, Origin and Fetch Metadata as the server does, and records
// every request and frame for the tests. An upload (G28) gets its one-use ticket over the socket and its bytes by POST
// /upload, checked as the server checks them (Origin, Fetch Metadata, the cookie, the media type, the ticket, the size
// and the SHA-256), and its answer back on the socket by the command's id. Loopback only.
import { createHash, randomBytes } from "node:crypto";
import { readdirSync, readFileSync, statSync } from "node:fs";
import { createServer } from "node:http";
import type { IncomingMessage, ServerResponse } from "node:http";
import type { AddressInfo, Socket } from "node:net";
import { join, relative, sep } from "node:path";
import type { JsonObject } from "../../src/protocol.ts";
import { FixtureSpace } from "../../src/transport/fixture-space.ts";
import type { FixtureSpaceData } from "../../src/transport/fixture-space.ts";

export const CSP = "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; " +
  "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'";

const HEADERS: { [name: string]: string } = {
  "Content-Security-Policy": CSP,
  "X-Content-Type-Options": "nosniff",
  "Referrer-Policy": "no-referrer",
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Resource-Policy": "same-origin",
};

const TYPES: { [ext: string]: string } = {
  ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8",
  ".txt": "text/plain; charset=utf-8", ".woff2": "font/woff2",
};

export interface Frame {
  [key: string]: unknown;
}

export interface FakeHost {
  origin: string;
  /** Every HTTP request's method and path, in order. */
  requests: string[];
  /** Every frame the page sent, parsed. */
  received: Frame[];
  spaces: Map<string, FixtureSpace>;
  /** Makes the next answers to a command refused with a code (the tests' refusals). */
  refuse: Map<string, string>;
  /** Fields added to an accepted command's answer body (a space's own answer, such as a save's chosen row, D218). */
  bodies: Map<string, JsonObject>;
  /** Every upload's bytes as the space received them (G28), with its command. */
  uploads: { name: string; bytes: Buffer }[];
  /** The view welcome names from the next connection on (1.9): this PC's browser, or a paired device. */
  setView(view: "browser" | "device"): void;
  close(): Promise<void>;
}

function token(): string {
  return randomBytes(32).toString("base64url");
}

function listFiles(root: string): Map<string, string> {
  const files = new Map<string, string>();
  const walk = (dir: string) => {
    for (const name of readdirSync(dir)) {
      const path = join(dir, name);
      if (statSync(path).isDirectory()) {
        walk(path);
      } else {
        files.set("/" + relative(root, path).split(sep).join("/"), path);
      }
    }
  };
  walk(root);
  return files;
}

// ---- RFC 6455, as much as a test needs: masked client frames in, unmasked text frames out.

function encodeFrame(text: string, opcode = 1): Buffer {
  const payload = Buffer.from(text, "utf8");
  const n = payload.length;
  const head = n < 126 ? Buffer.from([0x80 | opcode, n])
    : n < 65536 ? Buffer.from([0x80 | opcode, 126, n >> 8, n & 255])
    : Buffer.concat([Buffer.from([0x80 | opcode, 127]), Buffer.alloc(4), Buffer.from([n >>> 24, (n >> 16) & 255, (n >> 8) & 255, n & 255])]);
  return Buffer.concat([head, payload]);
}

function decodeFrames(buffer: Buffer): { frames: { opcode: number; payload: Buffer }[]; rest: Buffer } {
  const frames: { opcode: number; payload: Buffer }[] = [];
  let at = 0;
  while (buffer.length - at >= 2) {
    const b0 = buffer[at] ?? 0;
    const b1 = buffer[at + 1] ?? 0;
    let length = b1 & 127;
    let offset = at + 2;
    if (length === 126) {
      if (buffer.length - offset < 2) break;
      length = buffer.readUInt16BE(offset);
      offset += 2;
    } else if (length === 127) {
      if (buffer.length - offset < 8) break;
      length = Number(buffer.readBigUInt64BE(offset));
      offset += 8;
    }
    if ((b1 & 128) === 0) {
      throw new Error("an unmasked client frame");
    }
    if (buffer.length - offset < 4 + length) break;
    const mask = buffer.subarray(offset, offset + 4);
    const payload = Buffer.from(buffer.subarray(offset + 4, offset + 4 + length));
    for (let i = 0; i < payload.length; ++i) {
      payload[i] = (payload[i] ?? 0) ^ (mask[i % 4] ?? 0);
    }
    frames.push({ opcode: b0 & 15, payload });
    at = offset + 4 + length;
  }
  return { frames, rest: buffer.subarray(at) };
}

export async function startHost(root: string, data: FixtureSpaceData[]): Promise<FakeHost> {
  const files = listFiles(root);
  const cookie = token();
  const tickets = new Set<string>();
  interface Pending {
    space: FixtureSpace; id: unknown; name: string; args: JsonObject; size: number; sha256: string;
    send: (frame: object) => void;
  }
  const uploadTickets = new Map<string, Pending>();
  const uploads: { name: string; bytes: Buffer }[] = [];
  const requests: string[] = [];
  const received: Frame[] = [];
  const spaces = new Map(data.map((d) => [d.pages.space, new FixtureSpace(d)]));
  const refuse = new Map<string, string>();
  const bodies = new Map<string, JsonObject>();
  const sockets = new Set<Socket>();
  let origin = "";
  let host = "";
  let view: "browser" | "device" = "browser";

  const hasCookie = (req: IncomingMessage) =>
    (req.headers.cookie ?? "").split(";").some((part) => part.trim() === `en_session=${cookie}`);

  const answer = (res: ServerResponse, status: number, body: string, type = "text/plain; charset=utf-8",
                  extra: { [name: string]: string } = {}) => {
    res.writeHead(status, { ...HEADERS, "Content-Type": type, "Content-Length": Buffer.byteLength(body), ...extra });
    res.end(body);
  };

  const server = createServer((req, res) => {
    const path = req.url ?? "/";
    requests.push(`${req.method} ${path}`);
    if (req.headers.host !== host) {
      answer(res, 421, "Misdirected request");
      return;
    }
    if (req.method === "GET") {
      const file = files.get(path === "/" ? "/index.html" : path);
      if (!file || path.includes("?")) {
        answer(res, 404, "Not found");
        return;
      }
      const ext = file.slice(file.lastIndexOf("."));
      const html = ext === ".html";
      const navigate = req.headers["sec-fetch-mode"] === "navigate" || req.headers["sec-fetch-mode"] === undefined;
      const extra: { [name: string]: string } = html ? { "Cache-Control": "no-store" } : {};
      if (html && navigate) {
        extra["Set-Cookie"] = `en_session=${cookie}; Path=/; HttpOnly; SameSite=Strict`;
      }
      res.writeHead(200, { ...HEADERS, "Content-Type": TYPES[ext] ?? "application/octet-stream", ...extra });
      res.end(readFileSync(file));
      return;
    }
    if (req.method === "POST" && path === "/api/ticket") {
      const site = req.headers["sec-fetch-site"];
      if (req.headers.origin !== origin || (site !== undefined && site !== "same-origin") || !hasCookie(req) ||
          !(req.headers["content-type"] ?? "").startsWith("application/json")) {
        answer(res, 403, "Forbidden", "text/plain; charset=utf-8", { "Cache-Control": "no-store" });
        return;
      }
      req.resume();
      const ticket = token();
      tickets.add(ticket);
      answer(res, 200, JSON.stringify({ ticket }), "application/json", { "Cache-Control": "no-store" });
      return;
    }
    if (req.method === "POST" && path === "/upload") {
      const site = req.headers["sec-fetch-site"];
      const ticket = req.headers["grace-upload-ticket"];
      const pending = typeof ticket === "string" ? uploadTickets.get(ticket) : undefined;
      if (req.headers.origin !== origin || (site !== undefined && site !== "same-origin") || !hasCookie(req) ||
          req.headers["content-type"] !== "application/octet-stream" || pending === undefined ||
          typeof ticket !== "string") {
        req.resume();
        answer(res, 403, "The file was refused.", "text/plain; charset=utf-8", { "Cache-Control": "no-store" });
        return;
      }
      uploadTickets.delete(ticket);  // used once
      const chunks: Buffer[] = [];
      req.on("data", (chunk: Buffer) => chunks.push(chunk));
      req.on("end", () => {
        const bytes = Buffer.concat(chunks);
        if (bytes.length !== pending.size) {
          answer(res, 400, "The file's size differs from the one declared.", "text/plain; charset=utf-8",
                 { "Cache-Control": "no-store" });
          return;
        }
        if (createHash("sha256").update(bytes).digest("hex") !== pending.sha256) {
          answer(res, 400, "The file changed on its way.", "text/plain; charset=utf-8", { "Cache-Control": "no-store" });
          return;
        }
        answer(res, 202, "", "text/plain; charset=utf-8", { "Cache-Control": "no-store" });
        uploads.push({ name: pending.name, bytes });
        const outcome = pending.space.handleFile(pending.name, pending.args, bytes.toString("utf8"));
        const body = outcome.answer.body !== undefined ? { body: outcome.answer.body } : {};
        pending.send(outcome.answer.ok
          ? { v: 1, t: "result", space: pending.space.id, id: pending.id, ok: true, ...body }
          : { v: 1, t: "result", space: pending.space.id, id: pending.id, error: outcome.answer.error, ...body,
              ...(outcome.answer.detail !== undefined ? { detail: outcome.answer.detail } : {}) });
      });
      return;
    }
    answer(res, 405, "Method not allowed", "text/plain; charset=utf-8", { Allow: "GET, POST" });
  });

  server.on("connection", (socket) => {
    sockets.add(socket);
    socket.on("close", () => sockets.delete(socket));
  });

  server.on("upgrade", (req, socket: Socket) => {
    requests.push(`UPGRADE ${req.url ?? ""}`);
    const key = req.headers["sec-websocket-key"];
    if (req.url !== "/api/ws" || req.headers.host !== host || req.headers.origin !== origin || !hasCookie(req) ||
        typeof key !== "string") {
      socket.end("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
      return;
    }
    const accept = createHash("sha1").update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest("base64");
    socket.write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" +
                 `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
    const send = (frame: object) => socket.write(encodeFrame(JSON.stringify(frame)));
    const subs = new Map<number, { space: string; topic: string }>();
    let welcomed = false;
    let pending = Buffer.alloc(0);
    const helloTimer = setTimeout(() => socket.end(), 2000);
    const publish = (space: string, frames: object[]) => {
      for (const frame of frames as { topic: string }[]) {
        if ([...subs.values()].some((s) => s.space === space && s.topic === frame.topic)) {
          send(frame);
        }
      }
    };
    socket.on("data", (chunk: Buffer) => {
      let decoded;
      try {
        decoded = decodeFrames(Buffer.concat([pending, chunk]));
      } catch {
        socket.destroy();
        return;
      }
      pending = decoded.rest;
      for (const { opcode, payload } of decoded.frames) {
        if (opcode === 8) {
          socket.end(encodeFrame("", 8));
          return;
        }
        if (opcode !== 1) {
          continue;
        }
        const frame = JSON.parse(payload.toString("utf8")) as Frame;
        received.push(frame);
        if (!welcomed) {
          if (frame.t !== "hello" || typeof frame.ticket !== "string" || !tickets.delete(frame.ticket)) {
            send({ v: 1, t: "bye", reason: "unauthorised" });
            socket.end();
            return;
          }
          clearTimeout(helloTimer);
          welcomed = true;
          send({ v: 1, t: "welcome", protocol: "1.9", caps: [], graceui: "0.2.0",
                 spaces: [...spaces.values()].map((s) => s.info()), session: "fake-host", view });
          continue;
        }
        const space = typeof frame.space === "string" ? spaces.get(frame.space) : undefined;
        if (frame.t === "ping") {
          send({ v: 1, t: "pong" });
        } else if (frame.t === "sub" && space && typeof frame.id === "number" && typeof frame.topic === "string") {
          subs.set(frame.id, { space: space.id, topic: frame.topic });
          const first = space.subscribe(frame.topic, typeof frame.since === "number" ? frame.since : undefined);
          if (first && "error" in first) {
            send({ v: 1, t: "error", space: space.id, code: first.error, detail: "no such topic" });
          } else if (first) {
            send(first);
          }
        } else if (frame.t === "unsub" && typeof frame.id === "number") {
          subs.delete(frame.id);
        } else if (frame.t === "cmd" && space && typeof frame.name === "string") {
          const forced = refuse.get(frame.name);
          if (forced) {
            send({ v: 1, t: "result", space: space.id, id: frame.id, error: forced });
            continue;
          }
          const outcome = space.handle(frame.name, (frame.args ?? {}) as JsonObject);
          const extra = outcome.answer.ok ? bodies.get(frame.name) : undefined;
          const merged = extra !== undefined ? { ...(outcome.answer.body ?? {}), ...extra } : outcome.answer.body;
          const body = merged !== undefined ? { body: merged } : {};  // D169: to the one view
          const result = outcome.answer.ok
            ? { v: 1, t: "result", space: space.id, id: frame.id, ok: true, ...body }
            : { v: 1, t: "result", space: space.id, id: frame.id, error: outcome.answer.error, ...body,
                ...(outcome.answer.detail !== undefined ? { detail: outcome.answer.detail } : {}) };
          send(result);
          publish(space.id, outcome.publish);
        } else if (frame.t === "upload" && space && typeof frame.name === "string" && typeof frame.size === "number" &&
                   typeof frame.sha256 === "string") {
          const forced = refuse.get(frame.name);
          if (forced) {
            send({ v: 1, t: "result", space: space.id, id: frame.id, error: forced });
            continue;
          }
          const ticket = token();
          uploadTickets.set(ticket, { space, id: frame.id, name: frame.name, args: (frame.args ?? {}) as JsonObject,
                                      size: frame.size, sha256: frame.sha256, send });
          send({ v: 1, t: "upload_ticket", space: space.id, id: frame.id, ticket });
        }
      }
    });
    socket.on("error", () => socket.destroy());
  });

  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  const port = (server.address() as AddressInfo).port;
  host = `127.0.0.1:${port}`;
  origin = `http://${host}`;
  return {
    origin, requests, received, spaces, refuse, bodies, uploads,
    setView: (v) => {
      view = v;
    },
    close: () => new Promise<void>((resolve) => {
      for (const socket of sockets) {
        socket.destroy();
      }
      server.close(() => resolve());
    }),
  };
}
