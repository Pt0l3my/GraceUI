// SPDX-License-Identifier: MIT
// transport.ts: the one interface the browser view reaches every space through (DESIGN §6.1, §6.2; invariant 3). Topics
// come in (welcome, then each subscription's state or rows), commands go out and each gets one answer, ok or refused.
// Two implementations: the fixture transport (fixture.ts: the tests and the dev page) and the WebSocket transport
// (websocket.ts: grace-serve, in the game or in a host outside it, and grace-dev).
import type { ByeReason, ErrorCode, JsonObject, Rows, State, Welcome } from "../protocol.ts";

/** One command's answer: ok with an optional body, or refused with the space's error code. */
export type Answer =
  | { ok: true; body?: JsonObject }
  | {
      ok: false;
      error: ErrorCode;
      body?: JsonObject;
      /** the space's reason in words (protocol 1.1) */ detail?: string;
      /** a reason the view itself gave (no space answered) */ local?: string;
    };

/** Why the transport stopped: GraceUI's bye, or the connection lost. */
export type Ended = { reason: ByeReason } | { reason: "lost" };

export interface TransportEvents {
  welcome(welcome: Welcome): void;
  state(state: State): void;
  /** A stream's new rows (the delta to what the view holds). */
  rows(rows: Rows): void;
  /** A protocol-level error not tied to a command (§6.7). */
  error(code: ErrorCode, space?: string): void;
  ended(ended: Ended): void;
}

export interface Transport {
  /** Opens the connection; events follow, welcome first. Resolves true once welcome has arrived, false if the
   * connection ended first (`ended` then says why). */
  connect(events: TransportEvents): Promise<boolean>;
  /** Watches a topic of a space `welcome` named; returns the subscription's id. */
  subscribe(space: string, topic: string, since?: number): number;
  unsubscribe(space: string, id: number, topic: string): void;
  /** Sends one command to its space; resolves with its answer, never rejects. A slow one (D216) is waited for longer. */
  command(space: string, name: string, args: JsonObject, slow?: boolean): Promise<Answer>;
  /** Sends one file with a file command of a space's (G28, D226): its one-use ticket asked over the connection, then
   * its bytes by POST /upload on this page's own origin. Resolves with the command's answer, never rejects; the file's
   * name is never sent. */
  upload(space: string, name: string, args: JsonObject, file: Blob): Promise<Answer>;
  close(): void;
}

/** The framework's own state topics for every space: its page data and its settings (§2.5, §6.5). */
export const PAGES_TOPIC = "pages";
export const SETTINGS_TOPIC = "settings";
