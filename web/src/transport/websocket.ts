// SPDX-License-Identifier: MIT
// websocket.ts: the WebSocket transport (DESIGN §6.2 to §6.8, §7.2 items 3, 4, 6 to 11; invariant 6). It reaches only
// the page's own origin: a ticket by a same-origin POST that carries the session cookie (the page never sees or stores
// the cookie), then one socket to the same host, whose first frame is hello with the ticket. Every frame is one JSON
// object of the kinds protocol.ts defines; anything else is dropped. Liveness by ping every 15 s, two missed pongs
// closing the socket; at most 16 frames a second (the server's 20, less a margin) and 16 KiB a frame from the page.
import type {
  ByeReason, Cmd, ErrorCode, FrameFromPage, Hello, Json, JsonObject, Rows, State, Sub, Unsub, Upload, Welcome,
} from "../protocol.ts";
import { BYE_REASONS, ERROR_CODES, LIMITS, PROTOCOL } from "../protocol.ts";
import type { Answer, Ended, Transport, TransportEvents } from "./transport.ts";

/** What the transport needs of a WebSocket: the browser's, or a test's fake. */
export interface SocketLike {
  readonly readyState: number;
  send(data: string): void;
  close(code?: number, reason?: string): void;
  onopen: ((event: unknown) => void) | null;
  onmessage: ((event: { data: unknown }) => void) | null;
  onclose: ((event: unknown) => void) | null;
  onerror: ((event: unknown) => void) | null;
}

export interface TicketResponse {
  ok: boolean;
  status: number;
  json(): Promise<unknown>;
  /** An upload's answer in words, when it was refused (G28). */
  text?(): Promise<string>;
}

export interface WebSocketOptions {
  /** The web app's version, which hello names as its bundle. */
  bundle: string;
  /** The page's own origin's parts; the browser's location by default. */
  location?: { protocol: string; host: string };
  ticketPath?: string;
  socketPath?: string;
  uploadPath?: string;
  fetch?: (path: string, init: RequestInit) => Promise<TicketResponse>;
  socket?: (url: string) => SocketLike;
  /** The answer a command waits for before the view gives up on it, in ms. */
  answerTimeout?: number;
  /** A slow command's (D216): the space's 90 s and a margin. */
  slowAnswerTimeout?: number;
  welcomeTimeout?: number;
  /** An upload's whole wait: its ticket, its bytes and the space's answer (G28). */
  uploadTimeout?: number;
}

const OPEN = 1;
export const DEFAULT_TICKET_PATH = "/api/ticket";
export const DEFAULT_SOCKET_PATH = "/api/ws";  // grace-serve's route (D161)
export const DEFAULT_UPLOAD_PATH = "/upload";  // the upload route, on this PC's loopback listener alone (G28)

function hex(digest: ArrayBuffer): string {
  return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
}

/** A path on this origin: starts with one "/", and names no other host or scheme. */
export function samePath(path: string): boolean {
  return /^\/(?![/\\])[A-Za-z0-9._~\-/]*$/.test(path);
}

function isObject(value: unknown): value is JsonObject {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isCode(value: unknown): value is ErrorCode {
  return typeof value === "string" && (ERROR_CODES as readonly string[]).includes(value);
}

function byteLength(text: string): number {
  return text.length * 3 <= LIMITS.frameToPage ? text.length : new TextEncoder().encode(text).length;
}

// What a page sends a second: the server's limit less a margin, since frames sent a second apart may arrive closer.
const PACE = LIMITS.commandsPerSecond - 4;

export class WebSocketTransport implements Transport {
  private readonly options: WebSocketOptions;
  private events: TransportEvents | null = null;
  private socket: SocketLike | null = null;
  private welcomed = false;
  private ended = false;
  private nextSub = 1;
  private nextCmd = 1;
  private readonly subs = new Set<number>();
  private readonly pending = new Map<number, { resolve: (answer: Answer) => void; timer: ReturnType<typeof setTimeout> }>();
  private readonly tickets = new Map<number, (ticket: string | null) => void>();  // an upload's, by its command's id
  private readonly sentAt: number[] = [];
  private readonly queue: { text: string; sub?: number }[] = [];
  private queueTimer: ReturnType<typeof setTimeout> | null = null;
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private welcomeTimer: ReturnType<typeof setTimeout> | null = null;
  private missedPongs = 0;
  private resolveConnect: ((welcomed: boolean) => void) | null = null;

  constructor(options: WebSocketOptions) {
    const ticket = options.ticketPath ?? DEFAULT_TICKET_PATH;
    const socket = options.socketPath ?? DEFAULT_SOCKET_PATH;
    const upload = options.uploadPath ?? DEFAULT_UPLOAD_PATH;
    if (!samePath(ticket) || !samePath(socket) || !samePath(upload)) {
      throw new Error("the transport reaches only paths on its own origin");  // invariant 6
    }
    this.options = { ...options, ticketPath: ticket, socketPath: socket, uploadPath: upload };
  }

  private origin(): { protocol: string; host: string } {
    const where = this.options.location ?? globalThis.location;
    return { protocol: where.protocol, host: where.host };
  }

  connect(events: TransportEvents): Promise<boolean> {
    this.events = events;
    return new Promise<boolean>((resolve) => {
      this.resolveConnect = resolve;
      void this.open();
    });
  }

  private async open(): Promise<void> {
    let ticket: string;
    try {
      const fetcher = this.options.fetch ?? ((path: string, init: RequestInit) => fetch(path, init));
      const response = await fetcher(this.options.ticketPath ?? DEFAULT_TICKET_PATH, {
        method: "POST", credentials: "same-origin", cache: "no-store", redirect: "error", referrerPolicy: "no-referrer",
        headers: { "Content-Type": "application/json" }, body: "{}",
      });
      if (!response.ok) {
        this.end({ reason: response.status === 401 || response.status === 403 ? "unauthorised" : "lost" });
        return;
      }
      const body = await response.json();
      if (!isObject(body) || typeof body.ticket !== "string" || body.ticket.length === 0 || body.ticket.length > 128) {
        this.end({ reason: "lost" });
        return;
      }
      ticket = body.ticket;
    } catch {
      this.end({ reason: "lost" });
      return;
    }
    if (this.ended) {
      return;
    }
    const { protocol, host } = this.origin();
    const url = (protocol === "https:" ? "wss://" : "ws://") + host + (this.options.socketPath ?? DEFAULT_SOCKET_PATH);
    let socket: SocketLike;
    try {
      socket = this.options.socket ? this.options.socket(url) : (new WebSocket(url) as unknown as SocketLike);
    } catch {
      this.end({ reason: "lost" });
      return;
    }
    this.socket = socket;
    this.welcomeTimer = setTimeout(() => {
      this.welcomeTimer = null;
      if (!this.welcomed) {
        this.socket?.close(1000);
        this.end({ reason: "lost" });
      }
    }, this.options.welcomeTimeout ?? 5000);
    socket.onopen = () => {
      const hello: Hello = {
        v: PROTOCOL.major, t: "hello", protocol: `${PROTOCOL.major}.${PROTOCOL.minor}`, bundle: this.options.bundle,
        caps: [], ticket,
      };
      this.sendNow(JSON.stringify(hello));
    };
    socket.onmessage = (event) => {
      if (typeof event.data === "string") {
        this.receive(event.data);
      }
    };
    socket.onerror = () => {};
    socket.onclose = () => this.end({ reason: "lost" });
  }

  private end(ended: Ended): void {
    if (this.ended) {
      return;
    }
    this.ended = true;
    if (this.welcomeTimer !== null) {
      clearTimeout(this.welcomeTimer);
    }
    if (this.pingTimer !== null) {
      clearInterval(this.pingTimer);
    }
    if (this.queueTimer !== null) {
      clearTimeout(this.queueTimer);
    }
    for (const [, waiting] of this.pending) {
      clearTimeout(waiting.timer);
      waiting.resolve({ ok: false, error: "internal", local: "The connection was lost before an answer came." });
    }
    this.pending.clear();
    this.tickets.clear();
    this.queue.length = 0;
    this.resolveConnect?.(false);
    this.resolveConnect = null;
    const socket = this.socket;
    this.socket = null;
    if (socket && socket.readyState === OPEN) {
      socket.close(1000);
    }
    this.events?.ended(ended);
  }

  private receive(text: string): void {
    if (byteLength(text) > LIMITS.frameToPage) {
      return;
    }
    let frame: unknown;
    try {
      frame = JSON.parse(text);
    } catch {
      return;  // a malformed frame is dropped (§6.7)
    }
    if (!isObject(frame) || typeof frame.t !== "string") {
      return;
    }
    if (frame.v !== PROTOCOL.major) {
      this.end({ reason: "version" });
      return;
    }
    switch (frame.t) {
      case "welcome":
        this.onWelcome(frame);
        return;
      case "bye": {
        const reason = frame.reason;
        this.end({ reason: (BYE_REASONS as readonly string[]).includes(String(reason)) ? (reason as ByeReason) : "fault" });
        return;
      }
      case "ping":
        this.sendNow(JSON.stringify({ v: PROTOCOL.major, t: "pong" }));
        return;
      case "pong":
        this.missedPongs = 0;
        return;
      case "error":
        if (isCode(frame.code)) {
          this.events?.error(frame.code, typeof frame.space === "string" ? frame.space : undefined);
        }
        return;
      default:
        break;
    }
    if (!this.welcomed) {
      return;  // a space's frames come only after the handshake
    }
    if (frame.t === "state") {
      if (typeof frame.space === "string" && typeof frame.topic === "string" && typeof frame.rev === "number" &&
          isObject(frame.body)) {
        this.events?.state(frame as unknown as State);
      }
    } else if (frame.t === "rows") {
      if (typeof frame.space === "string" && typeof frame.topic === "string" && typeof frame.from_seq === "number" &&
          Array.isArray(frame.rows) && frame.rows.every(isObject) && typeof frame.dropped === "number") {
        this.events?.rows(frame as unknown as Rows);
      }
    } else if (frame.t === "upload_ticket") {
      const id = frame.id;
      const take = typeof id === "number" ? this.tickets.get(id) : undefined;
      if (take !== undefined && typeof id === "number" && typeof frame.ticket === "string" && frame.ticket.length > 0 &&
          frame.ticket.length <= 128) {
        this.tickets.delete(id);
        take(frame.ticket);
      }
    } else if (frame.t === "result") {
      const id = frame.id;
      const waiting = typeof id === "number" ? this.pending.get(id) : undefined;
      if (waiting === undefined || typeof id !== "number") {
        return;
      }
      this.tickets.delete(id);  // an upload refused before its ticket: nothing to send
      this.pending.delete(id);
      clearTimeout(waiting.timer);
      const body = isObject(frame.body) ? frame.body : undefined;
      if (frame.ok === true && frame.error === undefined) {
        waiting.resolve(body ? { ok: true, body } : { ok: true });
      } else {
        const answer: Answer = { ok: false, error: isCode(frame.error) ? frame.error : "internal" };
        if (body) {
          answer.body = body;
        }
        if (typeof frame.detail === "string") {
          answer.detail = frame.detail;
        }
        waiting.resolve(answer);
      }
    }
  }

  private onWelcome(frame: JsonObject): void {
    if (this.welcomed) {
      return;
    }
    const protocol = typeof frame.protocol === "string" ? frame.protocol : "";
    if (protocol.split(".")[0] !== String(PROTOCOL.major)) {
      this.end({ reason: "version" });
      return;
    }
    if (!Array.isArray(frame.spaces) || typeof frame.graceui !== "string" || typeof frame.session !== "string") {
      this.end({ reason: "fault" });
      return;
    }
    this.welcomed = true;
    if (this.welcomeTimer !== null) {
      clearTimeout(this.welcomeTimer);
      this.welcomeTimer = null;
    }
    this.pingTimer = setInterval(() => {
      if (this.missedPongs >= 2) {
        this.socket?.close(1000);
        this.end({ reason: "lost" });
        return;
      }
      this.missedPongs += 1;
      this.sendNow(JSON.stringify({ v: PROTOCOL.major, t: "ping" }));
    }, LIMITS.pingSeconds * 1000);
    this.events?.welcome(frame as unknown as Welcome);
    this.resolveConnect?.(true);
    this.resolveConnect = null;
  }

  // Every frame sent counts against the rate a connection may send (§6.8), hello and pongs included.
  private sendNow(text: string): void {
    if (this.socket && this.socket.readyState === OPEN) {
      this.sentAt.push(Date.now());
      this.socket.send(text);
    }
  }

  // Subs, unsubs and commands keep to that rate, with a margin for the network's unevenness (D203): past it they wait
  // their turn, in order, so that a page opening many topics at once loses none (the server drops what is over the
  // rate unread). Hello and pongs go at once.
  private sendPaced(text: string, sub?: number): void {
    this.queue.push(sub !== undefined ? { text, sub } : { text });
    this.drain();
  }

  private drain(): void {
    const now = Date.now();
    while (this.sentAt.length > 0 && now - (this.sentAt[0] ?? 0) >= 1000) {
      this.sentAt.shift();
    }
    while (this.queue.length > 0 && this.sentAt.length < PACE) {
      const next = this.queue.shift();
      if (next !== undefined) {
        this.sendNow(next.text);
      }
    }
    if (this.queue.length > 0 && this.queueTimer === null) {
      this.queueTimer = setTimeout(() => {
        this.queueTimer = null;
        this.drain();
      }, Math.max(1, 1000 - (now - (this.sentAt[0] ?? now))));
    }
  }

  private frame(frame: FrameFromPage): string | null {
    const text = JSON.stringify(frame);
    return new TextEncoder().encode(text).length <= LIMITS.frameFromPage ? text : null;
  }

  subscribe(space: string, topic: string, since?: number): number {
    const id = this.nextSub++;
    if (this.subs.size >= LIMITS.subscriptions) {
      this.events?.error("rate_limited", space);
      return id;
    }
    const sub: Sub = { v: PROTOCOL.major, t: "sub", space, id, topic };
    if (since !== undefined) {
      sub.since = since;
    }
    const text = this.frame(sub);
    if (text !== null) {
      this.subs.add(id);
      this.sendPaced(text, id);
    }
    return id;
  }

  unsubscribe(space: string, id: number, topic: string): void {
    if (!this.subs.delete(id)) {
      return;
    }
    const waiting = this.queue.findIndex((q) => q.sub === id);
    if (waiting >= 0) {
      this.queue.splice(waiting, 1);  // never sent: nothing to take back, and nothing spent
      return;
    }
    const unsub: Unsub = { v: PROTOCOL.major, t: "unsub", space, id, topic };
    const text = this.frame(unsub);
    if (text !== null) {
      this.sendPaced(text);
    }
  }

  command(space: string, name: string, args: JsonObject, slow = false): Promise<Answer> {
    if (this.ended || !this.welcomed) {
      return Promise.resolve({ ok: false, error: "not_now", local: "Not connected to the game." });
    }
    const id = this.nextCmd++;
    const cmd: Cmd = { v: PROTOCOL.major, t: "cmd", space, id, name, args: args as { [key: string]: Json } };
    const text = this.frame(cmd);
    if (text === null) {
      return Promise.resolve({ ok: false, error: "invalid_value", local: "Too long to send." });
    }
    return new Promise<Answer>((resolve) => {
      const timer = setTimeout(() => {
        if (this.pending.delete(id)) {
          resolve({ ok: false, error: "internal", local: "No answer came." });
        }
      }, slow ? (this.options.slowAnswerTimeout ?? 95000) : (this.options.answerTimeout ?? 15000));
      this.pending.set(id, { resolve, timer });
      this.sendPaced(text);
    });
  }

  async upload(space: string, name: string, args: JsonObject, file: Blob): Promise<Answer> {
    if (this.ended || !this.welcomed) {
      return { ok: false, error: "not_now", local: "Not connected to the game." };
    }
    if (file.size === 0) {
      return { ok: false, error: "invalid_value", local: "The file is empty." };
    }
    if (file.size > LIMITS.uploadMax) {
      return { ok: false, error: "invalid_value", local: "The file is larger than 32 MiB." };
    }
    // Its SHA-256, by the browser's own: a secure context alone has it, as this PC's 127.0.0.1 is and a device's home
    // address over plain HTTP is not (D114), and only this PC may send a file (§7.2 item 18).
    const subtle = globalThis.crypto?.subtle;
    if (subtle === undefined) {
      return { ok: false, error: "not_now", local: "Only on the PC the game runs on." };
    }
    let bytes: ArrayBuffer;
    let sha256: string;
    try {
      bytes = await file.arrayBuffer();
      sha256 = hex(await subtle.digest("SHA-256", bytes));
    } catch {
      return { ok: false, error: "internal", local: "The file could not be read." };
    }
    if (this.ended || !this.welcomed) {
      return { ok: false, error: "not_now", local: "Not connected to the game." };
    }
    const id = this.nextCmd++;
    const request: Upload = {
      v: PROTOCOL.major, t: "upload", space, id, name, args: args as { [key: string]: Json }, size: bytes.byteLength,
      sha256,
    };
    const text = this.frame(request);
    if (text === null) {
      return { ok: false, error: "invalid_value", local: "Too long to send." };
    }
    return new Promise<Answer>((resolve) => {
      const timer = setTimeout(() => {
        this.tickets.delete(id);
        if (this.pending.delete(id)) {
          resolve({ ok: false, error: "internal", local: "No answer came." });
        }
      }, this.options.uploadTimeout ?? 60000);
      this.pending.set(id, { resolve, timer });
      this.tickets.set(id, (ticket) => {
        if (ticket === null) {
          return;
        }
        void this.post(ticket, bytes).then((refused) => {
          // A refused POST answers the command unless the space's result came first (the server sends one for a
          // size or hash that differs, by the command's id).
          const waiting = refused !== null ? this.pending.get(id) : undefined;
          if (waiting !== undefined && refused !== null) {
            this.pending.delete(id);
            clearTimeout(waiting.timer);
            waiting.resolve({ ok: false, error: "invalid_value", local: refused });
          }
        });
      });
      this.sendPaced(text);
    });
  }

  // The bytes, with the ticket, to this origin's upload route: null once the server has handed them to the space (202),
  // else its words.
  private async post(ticket: string, bytes: ArrayBuffer): Promise<string | null> {
    try {
      const fetcher = this.options.fetch ?? ((path: string, init: RequestInit) => fetch(path, init));
      const response = await fetcher(this.options.uploadPath ?? DEFAULT_UPLOAD_PATH, {
        method: "POST", credentials: "same-origin", cache: "no-store", redirect: "error", referrerPolicy: "no-referrer",
        headers: { "Content-Type": "application/octet-stream", "Grace-Upload-Ticket": ticket }, body: bytes,
      });
      if (response.status === 202) {
        return null;
      }
      const words = response.text !== undefined ? (await response.text()).trim().slice(0, 200) : "";
      return words !== "" ? words : "The file did not reach the game.";
    } catch {
      return "The file did not reach the game.";
    }
  }

  close(): void {
    this.end({ reason: "shutdown" });
  }
}
