// SPDX-License-Identifier: MIT
// paged.tsx: a space's scope and its rows read a page at a time in the browser (G45, D254), as grace-view has them in
// game. The scope: its topic's choices and the one in view, the player's choice for the session or the space's
// `current`. A paged source: its first page asked for with the block's args, the bar's choices (the search 300 ms
// after the typing stops), the sort and the scope; a page that comes late is dropped; Show more adds the next page
// below; a change of its `changes` topic asks for the first page again while only it shows, or says "New rows" past
// it; a refusal's words stand in for the rows. Nothing a page answered is stored or searched.
import { useEffect, useRef, useState } from "preact/hooks";
import type { JSX } from "preact";
import type { FilterBar, Json, JsonObject, PageSource, SpacePages } from "../protocol.ts";
import type { FilterState } from "../model/filter.ts";
import type { PageBody, ScopeChoice } from "../model/paging.ts";
import { ownChoices, pageArgs, readPage, scopeBody, scopeChoices, scopeId } from "../model/paging.ts";
import type { TopicData } from "../model/session.ts";
import { refusalWords } from "../model/session.ts";
import { useSession } from "./context.ts";
import { useLiveTopic, useTopic } from "./components.tsx";
import { scopes } from "./view-state.ts";

export interface ScopeInView {
  label: string;
  /** Its topic has come: a scoped page asks for its pages only then (with no choices, it sends none). */
  come: boolean;
  choices: ScopeChoice[];
  id: string | null;
  choice: ScopeChoice | null;
}

/** A space's scope, or null if it declares none. */
export function useScope(space: string, pages: SpacePages | null | undefined): ScopeInView | null {
  const spec = pages?.scope;
  const data = useTopic(space, spec?.topic);
  void scopes.value;  // drawn again when the player chooses
  if (spec === undefined) {
    return null;
  }
  const body = scopeBody(data?.kind === "state" ? data.body : undefined, spec.field);
  const choices = scopeChoices(body);
  const id = scopeId(choices, scopes.value.get(space), body);
  return { label: spec.label, come: data !== undefined, choices, id, choice: choices.find((c) => c.id === id) ?? null };
}

/** The key a paged table's view state goes by, as grace-view names it: "page.<command>", and ".<view>" if its args
 * name one. */
export function pagedKey(source: PageSource): string {
  const view = source.args?.view;
  return `page.${source.command}${typeof view === "string" && view !== "" ? `.${view}` : ""}`;
}

/** Each filter's own choices on a paged bar (null for a filter whose choices are its rows'), the topics they come
 * from watched while the bar shows. */
export function useOwnChoices(space: string, bar: FilterBar | undefined): ({ value: string; label: string }[] | null)[] {
  const session = useSession();
  const topics = [...new Set((bar?.filters ?? []).flatMap((f) => (f.choices_from ? [f.choices_from.topic] : [])))];
  const [watched, setWatched] = useState<Map<string, { value: TopicData | undefined }>>(new Map());
  const joined = `${space}\u0000${topics.join("\u0000")}`;
  useEffect(() => {
    const handles = topics.map((t) => [t, session.watch(space, t)] as const);
    setWatched(new Map(handles.map(([t, h]) => [t, h.data])));
    return () => handles.forEach(([, h]) => h.release());
  }, [joined]);
  return (bar?.filters ?? []).map((f) => {
    if (f.choices !== undefined) {
      return ownChoices(f.choices, undefined);
    }
    if (f.choices_from === undefined) {
      return null;
    }
    const data = watched.get(f.choices_from.topic)?.value;
    const body: JsonObject | undefined = data?.kind === "state" ? data.body : undefined;
    const list: Json | undefined = body !== undefined && f.choices_from.field !== undefined ? body[f.choices_from.field] : body;
    return ownChoices(undefined, list);
  });
}

export interface Paged {
  /** The rows the space answered, null until a page has come for what is asked now. */
  body: PageBody | null;
  loading: boolean;
  refused: string | null;
  /** New rows came past the first page: "New rows · Show". */
  fresh: boolean;
  more: () => void;
  showNew: () => void;
}

interface Held { key: string; body: PageBody | null; loading: boolean; refused: string | null; fresh: boolean; pages: number }

/** A source's pages. `key` names what is asked (its page and its block): another key starts again. */
export function usePaged(space: string, key: string, source: PageSource | undefined, bar: FilterBar | undefined,
                         state: FilterState, scope: string | null, ready: boolean, live?: boolean): Paged {
  const session = useSession();
  const [held, setHeld] = useState<Held>({ key, body: null, loading: false, refused: null, fresh: false, pages: 0 });
  const serial = useRef(0);
  const signature = useRef("");
  // The search goes 300 ms after the typing stops (proposal 0014 §4).
  const [search, setSearch] = useState(state.search);
  useEffect(() => {
    if (search === state.search) {
      return undefined;
    }
    const wait = setTimeout(() => setSearch(state.search), 300);
    return () => clearTimeout(wait);
  }, [state.search]);
  const args = source !== undefined ? pageArgs(source, bar, state, search, scope) : null;
  const sig = `${key}\u0000${JSON.stringify(args)}`;
  const ask = (with_: JsonObject, append: boolean, keep: boolean) => {
    if (source === undefined) {
      return;
    }
    const n = ++serial.current;
    setHeld((h) => ({ ...h, key, loading: true, ...(keep || append ? {} : { body: null, refused: null, pages: 0 }) }));
    void session.command(space, source.command, with_).then((answer) => {
      if (n !== serial.current) {
        return;  // a later page was asked for: this one is dropped
      }
      if (!answer.ok) {
        setHeld((h) => ({ ...h, loading: false, refused: refusalWords(answer) || "Not now." }));
        return;
      }
      const page = readPage(answer.body);
      setHeld((h) => ({
        ...h, loading: false, refused: null,
        body: append && h.body !== null
          ? { ...page, columns: page.columns.length > 0 ? page.columns : h.body.columns, rows: [...h.body.rows, ...page.rows] }
          : page,
        pages: append ? h.pages + 1 : 1,
        fresh: append ? h.fresh : false,
      }));
    });
  };
  useEffect(() => {
    if (args === null || !ready) {
      return;
    }
    const changed = signature.current !== "" && signature.current !== sig;  // another query: its rows are not these
    signature.current = sig;
    ask(args, false, !changed);
  }, [sig, ready]);
  useEffect(() => () => {
    serial.current += 1;  // leaving the page forgets its pages
  }, []);
  // New rows: its changes topic, held while Live updates holds (D252).
  const changes = useLiveTopic(space, source?.changes, live);
  const last = useRef<{ key: string; data: TopicData | undefined }>({ key: "", data: undefined });
  useEffect(() => {
    const before = last.current;
    last.current = { key, data: changes };
    if (changes === undefined || before.key !== key || before.data === undefined || before.data === changes ||
        held.key !== key || (held.body === null && held.refused === null) || args === null) {
      return;  // its first body, or nothing come yet: the first page is asked for already
    }
    if (held.pages <= 1) {  // its first page alone, or a refusal: asked for again, as in game
      ask(args, false, true);
    } else {
      setHeld((h) => ({ ...h, fresh: true }));
    }
  }, [changes, key]);
  const current = held.key === key;
  return {
    body: current ? held.body : null,
    loading: current ? held.loading : ready && args !== null,
    refused: current ? held.refused : null,
    fresh: current && held.fresh,
    more: () => {
      if (current && held.body?.next && !held.loading && args !== null) {
        ask({ ...args, cursor: held.body.next }, true, true);
      }
    },
    showNew: () => {
      if (args !== null) {
        setHeld((h) => ({ ...h, fresh: false }));
        ask(args, false, true);
      }
    },
  };
}

/** "New rows · Show" at a paged list's top, while new rows came past its first page. */
export function PagingTop({ paged }: { paged: Paged }): JSX.Element | null {
  if (!paged.fresh) {
    return null;
  }
  return (
    <p class="paging paging--top" role="status">
      New rows{" · "}
      <button type="button" class="button button--small button--secondary" data-paging="fresh" onClick={paged.showNew}>
        Show
      </button>
    </p>
  );
}

/** Its foot: "Loading…" while a page is asked for, Show more while the space has a next page, and how many in all if
 * it said. */
export function PagingFoot({ paged }: { paged: Paged }): JSX.Element | null {
  const body = paged.body;
  if (body === null || body.rows.length === 0) {
    return null;
  }
  const total = body.total !== null ? `${body.total.toLocaleString("en-GB")} in all` : null;
  return (
    <p class="paging paging--foot">
      {paged.loading
        ? <span class="paging__loading" role="status">Loading{"…"}</span>
        : body.next !== null && (
          <button type="button" class="button button--small button--secondary" data-paging="more" onClick={paged.more}>
            Show more
          </button>
        )}
      {!paged.loading && paged.refused !== null && (
        <span class="paging__refused" role="alert">{paged.refused}</span>
      )}
      {total !== null && <span class="paging__total">{total}</span>}
    </p>
  );
}

/** What a paged list says in place of rows: the space's refusal, "Loading…", or its own empty words. */
export function pagedWords(paged: Paged, empty: string): string {
  return paged.refused ?? (paged.loading || paged.body === null ? "Loading…" : empty);
}
