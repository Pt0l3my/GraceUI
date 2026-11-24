// SPDX-License-Identifier: MIT
// filter-bar.tsx: a table's or a master list's filter bar (G43, D250; §3.8's 40 px strip): a select a filter, with
// "All" and the values its rows hold; the sort's options and its direction; and a search field where the space names
// one. Done here on the rows already published: nothing is sent to the space, stored or put in the address.
import type { JSX } from "preact";
import type { Json } from "../protocol.ts";
import { FRESH, choices, narrowed, visible } from "../model/filter.ts";
import type { FilterBarSpec, FilterState } from "../model/filter.ts";
import { filters, setFilter } from "./view-state.ts";

/** A bar's choices for the session, and the rows that show, as indices into `rows` in the order they show. */
export function useFilterBar(key: string, spec: FilterBarSpec | undefined, rows: readonly Json[]):
    { state: FilterState; order: number[]; set: (state: FilterState) => void } {
  const state = filters.value.get(key) ?? FRESH;
  const order = spec ? visible(rows, spec, state) : rows.map((_, i) => i);
  return { state, order, set: (next) => setFilter(key, next) };
}

/** The bar itself, its count line while a filter or the search holds, and nothing when no row is published. */
export function FilterBar({ spec, rows, state, shown, set, label }: {
  spec: FilterBarSpec; rows: readonly Json[]; state: FilterState; shown: number; set: (state: FilterState) => void;
  label: string;
}): JSX.Element {
  const sorts = spec.sort ?? [];
  const option = sorts[state.sort];
  const descending = option !== undefined && (option.direction === "descending") !== state.reversed;
  const total = rows.filter((r) => typeof r === "object" && r !== null && !Array.isArray(r)).length;
  const choose = (i: number, value: string) => {
    const chosen = [...state.chosen];
    while (chosen.length <= i) {
      chosen.push(null);
    }
    chosen[i] = value === "" ? null : value;
    set({ ...state, chosen });
  };
  return (
    <>
      <div class="filter-bar" role="group" aria-label={label !== "" ? `Filter and sort: ${label}` : "Filter and sort"}>
        {(spec.filters ?? []).map((f, i) => {
          const values = choices(rows, f.column);
          const now = state.chosen[i] ?? null;
          if (now !== null && !values.includes(now)) {
            values.push(now);  // the player's own choice, kept though no row has it now
          }
          return (
            <label class="filter-bar__part" key={`f${i}`}>
              <span class="filter-bar__label">{f.label}</span>
              <select class="field filter-bar__select" value={now ?? ""} data-filter={f.column}
                      onChange={(e) => choose(i, (e.currentTarget as HTMLSelectElement).value)}>
                <option value="">All</option>
                {values.map((v) => <option value={v} key={v}>{v}</option>)}
              </select>
            </label>
          );
        })}
        {sorts.length > 0 && (
          <span class="filter-bar__part">
            <label class="filter-bar__part">
              <span class="filter-bar__label">Sort</span>
              <select class="field filter-bar__select" value={String(state.sort)} data-sort=""
                      onChange={(e) => set({ ...state, sort: Number((e.currentTarget as HTMLSelectElement).value),
                                             reversed: false })}>
                {sorts.map((s, i) => <option value={String(i)} key={s.key}>{s.label}</option>)}
              </select>
            </label>
            <button type="button" class="button button--small button--secondary filter-bar__direction"
                    data-direction={descending ? "descending" : "ascending"}
                    aria-label={descending ? "Descending: reverse it" : "Ascending: reverse it"}
                    onClick={() => set({ ...state, reversed: !state.reversed })}>
              {descending ? "↓" : "↑"}
            </button>
          </span>
        )}
        {spec.search && (
          <input type="search" class="field filter-bar__search" value={state.search} placeholder={spec.search.label}
                 aria-label={spec.search.label} spellcheck={false} autocomplete="off" maxLength={64}
                 onInput={(e) => set({ ...state, search: (e.currentTarget as HTMLInputElement).value })} />
        )}
      </div>
      {narrowed(spec, state) && shown > 0 && <p class="filter-bar__count" aria-live="polite">{shown} of {total} shown</p>}
      {narrowed(spec, state) && shown === 0 && (
        <p class="filter-bar__none" aria-live="polite">
          Nothing matches these filters.{" "}
          <button type="button" class="button button--small button--secondary" data-clear-filters=""
                  onClick={() => set({ ...state, chosen: [], search: "" })}>Clear filters</button>
        </p>
      )}
    </>
  );
}
