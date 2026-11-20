// SPDX-License-Identifier: MIT
// websocket.ts: the WebSocket transport (DESIGN §6.2 to §6.8, §7.2 items 3, 4, 6 to 11; invariant 6). It reaches only
// the page's own origin: a ticket by a same-origin POST that carries the session cookie (the page never sees or stores
// the cookie), then one socket to the same host, whose first frame is hello with the ticket. Every frame is one JSON
// object of the kinds protocol.ts defines; anything else is dropped. Liveness by ping every 15 s, two missed pongs
// closing the socket; at most 20 commands a second and 16 KiB a frame from the page.
import type {
  ByeReason, Cmd, ErrorCode, FrameFromPage, Hello, Json, JsonObject, Rows, State, Sub, Unsub, Welcome,
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
}

export interface WebSocketOptions {
  /** The web app's version, which hello names as its bundle. */
  bundle: string;
  /** The page's own origin's parts; the browser's location by default. */
  location?: { protocol: string; host: string };
  ticketPath?: string;
  socketPath?: string;
  fetch?: (path: string, init: RequestInit) => Promise<TicketResponse>;
  socket?: (url: string) => SocketLike;
  /** The answer a command waits for before the view gives up on it, in ms. */
  answerTimeout?: number;
  welcomeTimeout?: number;
}

const OPEN = 1;
export const DEFAULT_TICKET_PATH = "/api/ticket";
export const DEFAULT_SOCKET_PATH = "/api/ws";  // grace-serve's route (D161)

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
  private readonly sentAt: number[] = [];
  private readonly queue: string[] = [];
  private queueTimer: ReturnType<typeof setTimeout> | null = null;
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private welcomeTimer: ReturnType<typeof setTimeout> | null = null;
  private missedPongs = 0;
  private resolveConnect: ((welcomed: boolean) => void) | null = null;

  constructor(options: WebSocketOptions) {
    const ticket = options.ticketPath ?? DEFAULT_TICKET_PATH;
    const socket = options.socketPath ?? DEFAULT_SOCKET_PATH;
    if (!samePath(ticket) || !samePath(socket)) {
      throw new Error("the transport reaches only paths on its own origin");  // invariant 6
    }
    this.options = { ...options, ticketPath: ticket, socketPath: socket };
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
    } else if (frame.t === "result") {
      const id = frame.id;
      const waiting = typeof id === "number" ? this.pending.get(id) : undefined;
      if (waiting === undefined || typeof id !== "number") {
        return;
      }
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

  private sendNow(text: string): void {
    if (this.socket && this.socket.readyState === OPEN) {
      this.socket.send(text);
    }
  }

  // Commands keep to the rate a connection may send (§6.8): past it they wait their turn, in order.
  private sendCommand(text: string): void {
    this.queue.push(text);
    this.drain();
  }

  private drain(): void {
    const now = Date.now();
    while (this.sentAt.length > 0 && now - (this.sentAt[0] ?? 0) >= 1000) {
      this.sentAt.shift();
    }
    while (this.queue.length > 0 && this.sentAt.length < LIMITS.commandsPerSecond) {
      const text = this.queue.shift();
      if (text !== undefined) {
        this.sentAt.push(now);
        this.sendNow(text);
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
      this.sendNow(text);
    }
    return id;
  }

  unsubscribe(space: string, id: number, topic: string): void {
    if (!this.subs.delete(id)) {
      return;
    }
    const unsub: Unsub = { v: PROTOCOL.major, t: "unsub", space, id, topic };
    const text = this.frame(unsub);
    if (text !== null) {
      this.sendNow(text);
    }
  }

  command(space: string, name: string, args: JsonObject): Promise<Answer> {
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
      }, this.options.answerTimeout ?? 15000);
      this.pending.set(id, { resolve, timer });
      this.sendCommand(text);
    });
  }

  close(): void {
    this.end({ reason: "shutdown" });
  }
}
