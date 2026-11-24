// SPDX-License-Identifier: MIT
// live.ts: the top bar's Live updates (G45, D252), view state for the session, as grace-view keeps it: Live; Every 5 s,
// which draws a live page's topics at most once every five seconds; or Paused, which draws none until Live comes back,
// counting what came meanwhile ("Paused · 3 new"). A topic's first body, an editor's document, a choice list, the
// settings and whatever comes in the 3 s after this view sends a command are never held, and another page draws what
// was held. components.tsx's useLiveTopic reads these.
import { createContext } from "preact";
import { signal } from "@preact/signals";
import type { TopicData } from "../model/session.ts";

export type LiveMode = "live" | "every_5s" | "paused";

export const liveMode = signal<LiveMode>("live");
/** How many bodies came while held, each topic's newest counted once. */
export const heldNew = signal(0);
/** Bumped whenever what is held is to be drawn: Live again, five seconds gone, a command sent, another page. */
export const released = signal(0);
/** Whether the page in view says it is live: its blocks' topics may then be held. */
export const LivePage = createContext(false);

let releaseUntil = 0;
let timer: ReturnType<typeof setInterval> | undefined;
const counted = new Map<string, TopicData>();

export const LIVE_WORDS: Record<LiveMode, string> = { live: "Live", every_5s: "Every 5 s", paused: "Paused" };

export function release(): void {
  counted.clear();
  heldNew.value = 0;
  released.value += 1;
}

export function setLiveMode(mode: LiveMode): void {
  liveMode.value = mode;
  if (timer !== undefined) {
    clearInterval(timer);
    timer = undefined;
  }
  if (mode === "every_5s") {
    timer = setInterval(() => {
      if (heldNew.value > 0) {
        release();
      }
    }, 5000);
  }
  if (mode !== "paused") {
    release();  // Live, or Every 5 s from Paused: what came meanwhile is drawn now
  }
}

export function nextLiveMode(mode: LiveMode): LiveMode {
  return mode === "live" ? "every_5s" : mode === "every_5s" ? "paused" : "live";
}

/** This view sent a command: what is held is drawn, and nothing is held for 3 s while it is answered. */
export function noteSent(now = Date.now()): void {
  releaseUntil = now + 3000;
  release();
}

export function holdsNow(now = Date.now()): boolean {
  return liveMode.value !== "live" && now >= releaseUntil;
}

/** A body that came while held: counted once for its topic, whichever blocks draw it. */
export function countHeld(key: string, data: TopicData): void {
  if (counted.get(key) !== data) {
    counted.set(key, data);
    heldNew.value += 1;
  }
}
