// SPDX-License-Identifier: MIT
// fixture.ts: the fixture transport (DESIGN §9.2): every space played from its fixtures in the page itself, with no
// server, for the tests and the dev page. Frames arrive a turn later, as over a socket, and in the order a space sends
// them: a command's answer, then the state it changed.
import type { JsonObject, Welcome } from "../protocol.ts";
import { PROTOCOL } from "../protocol.ts";
import { FixtureSpace } from "./fixture-space.ts";
import type { FixtureSpaceData } from "./fixture-space.ts";
import type { Answer, Transport, TransportEvents } from "./transport.ts";

export interface FixtureOptions {
  /** GraceUI's version as welcome names it. */
  graceui?: string;
  /** Delay of every frame, in ms (0: the next turn). */
  latency?: number;
}

export class FixtureTransport implements Transport {
  readonly spaces = new Map<string, FixtureSpace>();
  private events: TransportEvents | null = null;
  private nextSub = 1;
  private readonly subs = new Map<number, { space: string; topic: string }>();
  private readonly options: FixtureOptions;
  /** Every command sent, in order, as the space received it (the tests read it); an upload's with its file's size. */
  readonly sent: { space: string; name: string; args: JsonObject; size?: number }[] = [];

  constructor(spaces: FixtureSpaceData[], options: FixtureOptions = {}) {
    for (const data of spaces) {
      const space = new FixtureSpace(data);
      this.spaces.set(space.id, space);
    }
    this.options = options;
  }

  private later(fn: () => void): void {
    setTimeout(fn, this.options.latency ?? 0);
  }

  connect(events: TransportEvents): Promise<boolean> {
    this.events = events;
    const welcome: Welcome = {
      v: PROTOCOL.major, t: "welcome", protocol: `${PROTOCOL.major}.${PROTOCOL.minor}`, caps: [],
      graceui: this.options.graceui ?? "0.2.0", spaces: [...this.spaces.values()].map((s) => s.info()),
      session: "fixture",
    };
    return new Promise((resolve) => {
      this.later(() => {
        this.events?.welcome(welcome);
        resolve(true);
      });
    });
  }

  subscribe(space: string, topic: string, since?: number): number {
    const id = this.nextSub++;
    this.subs.set(id, { space, topic });
    this.later(() => {
      if (!this.subs.has(id)) {
        return;
      }
      const s = this.spaces.get(space);
      if (!s) {
        this.events?.error("unknown_topic", space);
        return;
      }
      const frame = s.subscribe(topic, since);
      if (frame === null) {
        return;
      }
      if ("error" in frame) {
        this.events?.error(frame.error, space);
      } else if (frame.t === "state") {
        this.events?.state(frame);
      } else {
        this.events?.rows(frame);
      }
    });
    return id;
  }

  unsubscribe(_space: string, id: number, _topic: string): void {
    this.subs.delete(id);
  }

  private watching(space: string, topic: string): boolean {
    for (const sub of this.subs.values()) {
      if (sub.space === space && sub.topic === topic) {
        return true;
      }
    }
    return false;
  }

  command(space: string, name: string, args: JsonObject): Promise<Answer> {
    this.sent.push({ space, name, args: structuredClone(args) });
    return new Promise((resolve) => {
      this.later(() => {
        const s = this.spaces.get(space);
        if (!s) {
          resolve({ ok: false, error: "unknown_command" });
          return;
        }
        const outcome = s.handle(name, args);
        resolve(outcome.answer);
        this.later(() => {
          for (const frame of outcome.publish) {
            if (this.watching(space, frame.topic)) {
              this.events?.state(frame);
            }
          }
        });
      });
    });
  }

  upload(space: string, name: string, args: JsonObject, file: Blob): Promise<Answer> {
    this.sent.push({ space, name, args: structuredClone(args), size: file.size });
    return file.text().then((text) => new Promise<Answer>((resolve) => {
      this.later(() => {
        const s = this.spaces.get(space);
        resolve(s ? s.handleFile(name, args, text).answer : { ok: false, error: "unknown_command" });
      });
    }), () => ({ ok: false, error: "internal", local: "The file could not be read." }));
  }

  close(): void {
    this.subs.clear();
    const events = this.events;
    this.events = null;
    events?.ended({ reason: "shutdown" });
  }
}
