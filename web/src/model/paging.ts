// SPDX-License-Identifier: MIT
// paging.ts: a space's scope and the rows read a page at a time (G45, D254). Pure. The scope's choices as its topic
// publishes them, and the one in view; the args a page is asked for with (the block's own, each filter's value by its
// column, the sort and its direction, the search and the scope), as grace-view's page_args builds them; a page's
// answer read as the view draws it; and the demo space's stand-in for a space's paged command, which the fixture
// transport and the tests' fake host share.
import type { Choice, FilterBar, Json, JsonObject, PageSource } from "../protocol.ts";
import type { FilterBarSpec, FilterState } from "./filter.ts";
import { visible, words } from "./filter.ts";

export interface ScopeChoice { id: string; label: string; readOnly: boolean }

const SCOPE_MAX = 32;   // a scope's choices, as grace-view reads them
export const PAGE_ROWS_MAX = 200;  // a page's rows (§4 of proposal 0014)

function object(value: Json | undefined): JsonObject | null {
  return typeof value === "object" && value !== null && !Array.isArray(value) ? value : null;
}

/** The scope's body: its topic's, or the member its field names. */
export function scopeBody(body: JsonObject | undefined, field: string | undefined): JsonObject | null {
  if (body === undefined) {
    return null;
  }
  return field !== undefined ? object(body[field]) : body;
}

/** Its choices, each an id (no slash: it names a member) with its words, at most 32, in the space's order. */
export function scopeChoices(body: JsonObject | null): ScopeChoice[] {
  const list = body !== null && Array.isArray(body.choices) ? body.choices : [];
  const out: ScopeChoice[] = [];
  for (const item of list) {
    const c = object(item);
    const id = c !== null ? words(c, "id") : "";
    if (c === null || id === "" || new TextEncoder().encode(id).length > 64 || id.includes("/")) {
      continue;  // an id names a family's member: no slash
    }
    const label = words(c, "label") !== "" ? words(c, "label") : id;
    out.push({ id, label, readOnly: c.read_only === true });
    if (out.length === SCOPE_MAX) {
      break;
    }
  }
  return out;
}

/** The scope in view: the player's choice if the space still lists it, else its `current`, else its first. */
export function scopeId(choices: readonly ScopeChoice[], chosen: string | undefined, body: JsonObject | null): string | null {
  if (chosen !== undefined && choices.some((c) => c.id === chosen)) {
    return chosen;
  }
  const current = body?.current;
  if (typeof current === "string" && choices.some((c) => c.id === current)) {
    return current;
  }
  return choices[0]?.id ?? null;
}

/** A filter's own choices on a paged table (D254): its list, or a state topic's (strings, or {value, label}). */
export function ownChoices(fixed: Choice[] | undefined, published: Json | undefined): { value: string; label: string }[] {
  if (fixed !== undefined) {
    return fixed.map((c) => ({ value: c.value, label: c.label !== "" ? c.label : c.value }));
  }
  const out: { value: string; label: string }[] = [];
  for (const v of Array.isArray(published) ? published : []) {
    if (out.length === SCOPE_MAX) {
      break;
    }
    if (typeof v === "string" && v !== "") {
      out.push({ value: v, label: v });
    } else {
      const o = object(v);
      const value = o !== null ? words(o, "value") : "";
      if (o !== null && value !== "") {
        const label = words(o, "label");
        out.push({ value, label: label !== "" ? label : value });
      }
    }
  }
  return out;
}

/** The args a page is asked for with, the cursor aside (Show more adds it); its JSON is the page's signature. */
export function pageArgs(source: PageSource, bar: FilterBar | undefined, state: FilterState, search: string,
                         scope: string | null): JsonObject {
  const args: JsonObject = { ...(source.args ?? {}) };
  if (bar !== undefined) {
    const filters: JsonObject = {};
    (bar.filters ?? []).forEach((f, i) => {
      const v = state.chosen[i] ?? null;
      if (v !== null) {
        filters[f.column] = v;
      }
    });
    if (Object.keys(filters).length > 0) {
      args.filters = filters;
    }
    const sorts = bar.sort ?? [];
    if (sorts.length > 0) {
      const by = sorts[Math.min(state.sort, sorts.length - 1)]!;
      const descending = (by.direction === "descending") !== state.reversed;
      args.sort = { key: by.key, direction: descending ? "descending" : "ascending" };
    }
    if (search.trim() !== "") {
      args.search = search.trim();
    }
  }
  if (scope !== null) {
    args.scope = scope;
  }
  return args;
}

export interface PageBody { columns: JsonObject[]; rows: JsonObject[]; next: string | null; total: number | null }

/** A page as the space answered it: its columns, at most 200 object rows, its cursor and its count if it gave them. */
export function readPage(body: JsonObject | undefined): PageBody {
  const columns = Array.isArray(body?.columns) ? body.columns.flatMap((c) => (object(c) ? [object(c)!] : [])) : [];
  const rows = Array.isArray(body?.rows) ? body.rows.flatMap((r) => (object(r) ? [object(r)!] : [])) : [];
  const next = typeof body?.next === "string" && body.next !== "" ? body.next : null;
  // A count, whole and at most 2^53, as the game takes it: anything else is no count.
  const total = typeof body?.total === "number" && Number.isSafeInteger(body.total) && body.total >= 0 ? body.total : null;
  return { columns, rows: rows.slice(0, PAGE_ROWS_MAX), next, total };
}

/** A paged command's rows as the demo's fixtures hold them (fixtures/demo/paged.json). */
export interface PagedFixture { page_size: number; columns: JsonObject[]; rows: JsonObject[] }

/** The demo space's answer to a page: its rows of the scope asked for, filtered by each column's value, searched in
 * every column, sorted as asked and cut a page at a time; the cursor is the next row's place, opaque to the view. */
export function answerPage(fixture: PagedFixture, args: JsonObject): JsonObject {
  const rows = fixture.rows.filter((r) => typeof args.scope !== "string" || r.scope === args.scope);
  const filters = object(args.filters) ?? {};
  const columns = Object.keys(filters);
  const sort = object(args.sort);
  const spec: FilterBarSpec = {
    filters: columns.map((column) => ({ column, label: column })),
    ...(sort !== null && typeof sort.key === "string"
      ? { sort: [{ key: sort.key, label: sort.key, direction: sort.direction === "descending" ? "descending" : "ascending" }] }
      : {}),
    ...(typeof args.search === "string"
      ? { search: { columns: fixture.columns.flatMap((c) => (typeof c.id === "string" ? [c.id] : [])), label: "" } }
      : {}),
  };
  const state: FilterState = {
    chosen: columns.map((c) => (typeof filters[c] === "string" ? filters[c] as string : null)),
    sort: 0, reversed: false, search: typeof args.search === "string" ? args.search : "",
  };
  const order = visible(rows, spec, state);
  const cursor = typeof args.cursor === "string" && /^o[0-9]{1,6}$/.test(args.cursor) ? Number(args.cursor.slice(1)) : 0;
  const size = Math.max(1, Math.min(PAGE_ROWS_MAX, fixture.page_size));
  const page = order.slice(cursor, cursor + size).map((i) => rows[i]!);
  const out: JsonObject = { columns: fixture.columns, rows: page, total: order.length };
  if (cursor + size < order.length) {
    out.next = `o${cursor + size}`;
  }
  return out;
}
