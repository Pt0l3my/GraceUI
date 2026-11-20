// SPDX-License-Identifier: MIT
// view-state.ts: what the browser view remembers for the session only, never in browser storage (DESIGN §4.2.2,
// §4.2.4; PV3): which sections are folded, which Advanced disclosures are open, the rail's collapse and its open
// category, and the row a search or a shortcut found.
import { signal } from "@preact/signals";
import type { PageSection } from "../protocol.ts";

/** Sections whose open or closed state the player flipped from its description's, by "space/page/section". */
export const flipped = signal<ReadonlySet<string>>(new Set());
/** Advanced disclosures opened, by "space/page/section". */
export const advancedOpen = signal<ReadonlySet<string>>(new Set());
/** The rail's collapse button: null follows the menu's width (§3.13). */
export const railManual = signal<boolean | null>(null);
/** The menu's width in CSS px at the effective scale. */
export const menuWidth = signal<number>(typeof globalThis.innerWidth === "number" ? globalThis.innerWidth : 1920);
/** The rail's open category (the accordion), by "space/category"; follows the current page unless the player opens
 * another. */
export const expanded = signal<string | null>(null);
/** The row a search result or a shortcut opened, focused and tinted, by "space\u0000key". */
export const found = signal<string | null>(null);

export function sectionId(space: string, page: string, section: string): string {
  return `${space}/${page}/${section}`;
}

export function isCollapsed(space: string, page: string, section: PageSection): boolean {
  return (section.collapsed ?? false) !== flipped.value.has(sectionId(space, page, section.id));
}

function toggled(set: ReadonlySet<string>, id: string, on?: boolean): ReadonlySet<string> {
  const next = new Set(set);
  const want = on ?? !next.has(id);
  if (want) {
    next.add(id);
  } else {
    next.delete(id);
  }
  return next;
}

export function toggleSection(space: string, page: string, section: PageSection): void {
  flipped.value = toggled(flipped.value, sectionId(space, page, section.id));
}

/** Opens a section, whichever way its description folds it. */
export function openSection(space: string, page: string, section: PageSection): void {
  if (isCollapsed(space, page, section)) {
    toggleSection(space, page, section);
  }
}

export function toggleAdvanced(id: string, on?: boolean): void {
  advancedOpen.value = toggled(advancedOpen.value, id, on);
}

export const BREAK_RAIL = 1100;  // under it the rail collapses to its icons (§3.13, §4.2.2)

export function railCollapsed(): boolean {
  return railManual.value ?? menuWidth.value < BREAK_RAIL;
}

/** An id safe for an element: letters, digits and dashes. */
export function domId(...parts: string[]): string {
  return parts.join("-").replace(/[^A-Za-z0-9-]/g, "_");
}
