// SPDX-License-Identifier: MIT
// filter.ts: a table's filter bar (G43, D250): which of its rows show, and in what order, from the rows its space
// published. Pure. grace-view's filter.cpp does the same, and both run fixtures/filter-cases.json, so the two views
// never differ: a cell's words as the table draws them; text compared by code point with ASCII letters folded; two
// numbers as numbers; a row with no value for the sort key last in either direction; equal rows in the space's order.
import type { Json, JsonObject } from "../protocol.ts";

export type Direction = "ascending" | "descending";

/** A filter: its column, its label, and on any bar its own choices, words for the values (D254, D262). */
export interface FilterSpec { column: string; label: string; choices?: { value: string; label: string }[] }
export interface SortSpec { key: string; label: string; direction?: Direction }
export interface SearchSpec { columns: string[]; label: string }
export interface FilterBarSpec { filters?: FilterSpec[]; sort?: SortSpec[]; search?: SearchSpec }

/** The player's choices on one bar, for the session: each filter's value (null for All), the sort option, whether
 *  its direction is reversed, and the search's words. */
export interface FilterState { chosen: (string | null)[]; sort: number; reversed: boolean; search: string }

export const FRESH: FilterState = { chosen: [], sort: 0, reversed: false, search: "" };

/** A cell's words, as the table draws them: a string as it is, a number as JSON writes it, anything else nothing. */
export function words(row: JsonObject, column: string): string {
  const v = row[column];
  if (typeof v === "string") {
    return v;
  }
  return typeof v === "number" && Number.isFinite(v) ? JSON.stringify(v) : "";
}

/** ASCII letters to lower case; everything else as it is. */
export function fold(text: string): string {
  return text.replace(/[A-Z]/g, (c) => String.fromCharCode(c.charCodeAt(0) + 32));
}

/** Text by code point (as UTF-8's bytes order it), so that the browser and the game agree. */
export function compareText(a: string, b: string): number {
  const x = [...a];
  const y = [...b];
  for (let i = 0; i < Math.min(x.length, y.length); ++i) {
    const p = x[i]!.codePointAt(0)!;
    const q = y[i]!.codePointAt(0)!;
    if (p !== q) {
      return p < q ? -1 : 1;
    }
  }
  return x.length === y.length ? 0 : x.length < y.length ? -1 : 1;
}

/** A filter's choices: the column's values its rows hold, each once, in the order the rows give them (invariant 2:
 *  a value no row has is never offered). An empty cell is no choice. */
export function choices(rows: readonly Json[], column: string): string[] {
  const seen = new Set<string>();
  const out: string[] = [];
  for (const row of rows) {
    if (typeof row !== "object" || row === null || Array.isArray(row)) {
      continue;
    }
    const w = words(row, column);
    if (w !== "" && !seen.has(w)) {
      seen.add(w);
      out.push(w);
    }
  }
  return out;
}

/** A sort key's value: a number, its words, or none (absent, empty or neither). */
function keyOf(row: JsonObject, key: string): number | string | null {
  const v = row[key];
  if (typeof v === "number" && Number.isFinite(v)) {
    return v;
  }
  return typeof v === "string" && v !== "" ? v : null;
}

function compareKeys(a: number | string, b: number | string): number {
  if (typeof a === "number" && typeof b === "number") {
    return a === b ? 0 : a < b ? -1 : 1;
  }
  const x = typeof a === "number" ? JSON.stringify(a) : a;
  const y = typeof b === "number" ? JSON.stringify(b) : b;
  return compareText(fold(x), fold(y));
}

/** Whether any filter or the search holds (the count line shows only then; a sort alone hides nothing). */
export function narrowed(spec: FilterBarSpec, state: FilterState): boolean {
  return (spec.filters ?? []).some((_, i) => (state.chosen[i] ?? null) !== null) ||
         (spec.search !== undefined && state.search.trim() !== "");
}

/** The rows that show, as indices into `rows`, in the order they show. */
export function visible(rows: readonly Json[], spec: FilterBarSpec, state: FilterState): number[] {
  const needle = spec.search !== undefined ? fold(state.search.trim()) : "";
  const out: number[] = [];
  rows.forEach((row, index) => {
    if (typeof row !== "object" || row === null || Array.isArray(row)) {
      return;
    }
    const kept = (spec.filters ?? []).every((f, i) => {
      const want = state.chosen[i] ?? null;
      return want === null || words(row, f.column) === want;
    }) && (needle === "" || (spec.search?.columns ?? []).some((c) => fold(words(row, c)).includes(needle)));
    if (kept) {
      out.push(index);
    }
  });
  const option = (spec.sort ?? [])[state.sort];
  if (option === undefined) {
    return out;
  }
  const descending = (option.direction === "descending") !== state.reversed;
  const key = (i: number) => keyOf(rows[i] as JsonObject, option.key);
  return out.sort((i, j) => {
    const a = key(i);
    const b = key(j);
    if (a === null || b === null) {
      return a === b ? i - j : a === null ? 1 : -1;  // no value: last, whichever the direction
    }
    const c = compareKeys(a, b);
    return c !== 0 ? (descending ? -c : c) : i - j;
  });
}
