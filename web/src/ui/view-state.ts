// SPDX-License-Identifier: MIT
// view-state.ts: what the browser view remembers for the session only, never in browser storage (DESIGN §4.2.2,
// §4.2.4; PV3): which sections are folded, which Advanced disclosures are open, the rail's collapse and its open
// category, the row a search or a shortcut found, and each filter bar's choices.
import { signal } from "@preact/signals";
import type { PageSection } from "../protocol.ts";
import type { FilterState } from "../model/filter.ts";

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
/** A master page's chosen row's key, by "space/page" (D215), for the session; never in the address or storage. */
export const chosen = signal<ReadonlyMap<string, string>>(new Map());

export function choose(space: string, page: string, key: string): void {
  const id = `${space}/${page}`;
  if (chosen.value.get(id) !== key) {
    const next = new Map(chosen.value);
    next.set(id, key);
    chosen.value = next;
  }
}

/** A row a save's answer named (D218), by "space/page": chosen once the page's list has it, unless the player chooses a
 * row by hand first. */
export const expected = signal<ReadonlyMap<string, string>>(new Map());

export function expectChoice(space: string, page: string, key: string): void {
  const next = new Map(expected.value);
  next.set(`${space}/${page}`, key);
  expected.value = next;
}

export function forgetExpected(space: string, page: string): void {
  const id = `${space}/${page}`;
  if (expected.value.has(id)) {
    const next = new Map(expected.value);
    next.delete(id);
    expected.value = next;
  }
}

/** The player's own choice of a row: a save's choice still waiting for its row goes (D218). */
export function chooseByHand(space: string, page: string, key: string): void {
  forgetExpected(space, page);
  choose(space, page, key);
}

/** The row a master list shows chosen: a save's choice once listed, else the chosen row if still listed, else the
 * first (D215, D218); null for an empty list. */
export function rowToShow(keys: readonly string[], held: string | undefined, wanted: string | undefined): string | null {
  if (wanted !== undefined && keys.includes(wanted)) {
    return wanted;
  }
  if (held !== undefined && keys.includes(held)) {
    return held;
  }
  return keys[0] ?? null;
}

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

/** Each filter bar's choices (G43, D250), by "space/topic" (a master list's by "space/master/topic"), for the session
 * only: never sent to the space, put in the address or stored (proposal 0012); on a table read a page at a time the
 * space filters, so its choices go with each page asked for (D254). */
export const filters = signal<ReadonlyMap<string, FilterState>>(new Map());

export function setFilter(key: string, state: FilterState): void {
  const next = new Map(filters.value);
  next.set(key, state);
  filters.value = next;
}

/** The scope in view on each space's scoped pages (G45, D254), as the player chose it for the session; a new session
 * starts at the space's `current`. Never sent but as a page's `scope`, put in the address or stored. */
export const scopes = signal<ReadonlyMap<string, string>>(new Map());

export function chooseScope(space: string, id: string): void {
  const next = new Map(scopes.value);
  next.set(space, id);
  scopes.value = next;
}
