// SPDX-License-Identifier: MIT
// flow.ts: a space's flow (protocol 1.23, D278), as grace-view reads it: a page outside the rail whose tabs are its
// steps, the step shown the one its state topic names (`step`; the first while none or an unknown one is published),
// each step in `done` checked, `blocked` the space's words for what Next waits for, and `start` asking to be shown.
import type { Json, Page, SpacePages } from "../protocol.ts";

type Body = { [key: string]: Json } | undefined;

/** The step shown: the one the space publishes, or the first. */
export function flowStep(page: Page, body: Body): number {
  const steps = page.tabs ?? [];
  const step = body?.step;
  const at = typeof step === "string" ? steps.findIndex((t) => t.id === step) : -1;
  return at >= 0 ? at : 0;
}

/** Whether a step may be skipped: one the flow lists, never its last (its button is Done). */
export function flowOptional(page: Page, step: number): boolean {
  const steps = page.tabs ?? [];
  const id = steps[step]?.id;
  return step + 1 < steps.length && id !== undefined && (page.flow?.optional ?? []).includes(id);
}

/** The space's words for what Next waits for, at most 160 characters, or null. */
export function flowBlocked(body: Body): string | null {
  const words = body?.blocked;
  return typeof words === "string" && words.length > 0 && words.length <= 160 ? words : null;
}

/** The steps the space says are done. */
export function flowDone(body: Body): Set<string> {
  const done = body?.done;
  return new Set(Array.isArray(done) ? done.filter((d): d is string => typeof d === "string") : []);
}

/** Whether the state asks for its flow to be shown (start). */
export function flowStarts(body: Body): boolean {
  return body?.start === true;
}

/** A space's flows, in its page data's order. */
export function flowsOf(pages: SpacePages): Page[] {
  return pages.pages.filter((p) => p.flow !== undefined);
}

/** Where Done leads: the flow's then, or the space's first page on the rail. */
export function flowThen(pages: SpacePages, page: Page): string | undefined {
  const then = page.flow?.then;
  if (then !== undefined && pages.pages.some((p) => p.id === then)) {
    return then;
  }
  for (const category of pages.categories) {
    for (const id of category.pages) {
      if (pages.pages.some((p) => p.id === id)) {
        return id;
      }
    }
  }
  return undefined;
}
