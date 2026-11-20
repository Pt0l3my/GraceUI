// SPDX-License-Identifier: MIT
// topbar.tsx: the top bar (DESIGN §4.2.3, §4.3): global search at the left; at the right the status lamp, only for a
// space that publishes a `status` topic (otherwise nothing, invariant 2), and the theme switch, a shortcut of
// ui.menu.theme. There is no Close in the browser.
import { useEffect, useState } from "preact/hooks";
import type { Signal } from "@preact/signals";
import { formatValue, nextValue } from "../model/format.ts";
import { pageById } from "../model/layout.ts";
import { search } from "../model/search.ts";
import type { SearchResult, SearchSpace } from "../model/search.ts";
import type { TopicData } from "../model/session.ts";
import { GRACEUI_SPACE } from "../model/session.ts";
import { useSession } from "./context.ts";
import { Cross, Palette, SearchGlass } from "./icons.tsx";
import { focusHeading, revealSetting } from "./navigate.ts";

function Search() {
  const session = useSession();
  const [query, setQuery] = useState("");
  const [open, setOpen] = useState(false);
  const [active, setActive] = useState(0);
  const spaces: SearchSpace[] = session.spaces.value.flatMap((view) => {
    const pages = view.pages.value;
    const settings = view.settings.value;
    return pages ? [{ id: view.info.id, name: view.info.name, pages, settings: settings?.description ?? [] }] : [];
  });
  const results = search(spaces, query);
  const showing = open && query.trim().length > 0;
  const badged = spaces.length > 1;
  const go = (result: SearchResult | undefined) => {
    if (!result) {
      return;
    }
    setOpen(false);
    if (result.kind === "setting" && result.key !== undefined) {
      revealSetting(session, result.space, result.key);
    } else if (session.open(result.space, result.page)) {
      focusHeading();
    }
  };
  const count = results.length === 0 ? "No results" : results.length === 1 ? "1 result" : `${results.length} results`;
  return (
    <div class="search" role="search">
      <SearchGlass />
      <label class="visually-hidden" for="search">Search every page and setting</label>
      <input id="search" class="search__field" type="text" role="combobox" autocomplete="off" spellcheck={false}
             placeholder="Search pages and settings" value={query} aria-expanded={showing && results.length > 0}
             aria-controls="search-results" aria-autocomplete="list"
             aria-activedescendant={showing && results[active] ? `search-result-${active}` : undefined}
             onInput={(e) => {
               setQuery((e.currentTarget as HTMLInputElement).value);
               setActive(0);
               setOpen(true);
             }}
             onFocus={() => setOpen(true)}
             onBlur={() => setOpen(false)}
             onKeyDown={(e) => {
               if (e.key === "ArrowDown" || e.key === "ArrowUp") {
                 e.preventDefault();
                 setOpen(true);
                 const n = Math.max(results.length, 1);
                 setActive((active + (e.key === "ArrowDown" ? 1 : n - 1)) % n);
               } else if (e.key === "Enter") {
                 e.preventDefault();
                 go(results[active]);
               } else if (e.key === "Escape") {
                 e.preventDefault();
                 if (showing) {
                   setOpen(false);
                 } else {
                   setQuery("");
                   (e.currentTarget as HTMLInputElement).blur();
                   focusHeading();
                 }
               }
             }} />
      {query.length > 0 && (
        <button type="button" class="search__clear icon-button" aria-label="Clear the search"
                onClick={() => {
                  setQuery("");
                  document.getElementById("search")?.focus();
                }}>
          <Cross />
        </button>
      )}
      <span class="search__count" aria-live="polite">{showing ? count : ""}</span>
      {showing && results.length > 0 && (
        <ul id="search-results" class="search__results" role="listbox" aria-label="Search results">
          {results.map((result, i) => (
            <li id={`search-result-${i}`} key={`${result.space}/${result.kind}/${result.key ?? result.page}`}
                role="option" aria-selected={i === active} class={i === active ? "result is-active" : "result"}
                data-result={result.key ?? result.page}
                onMouseDown={(e) => e.preventDefault()}
                onClick={() => go(result)}>
              <span class="result__label">{result.label}</span>
              <span class="result__crumbs">{result.breadcrumb.join(" › ")}</span>
              {badged && <span class="badge result__space">{result.spaceName}</span>}
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

const TONES = new Set(["moss", "ember", "glint", "rot"]);

function LampFor({ space }: { space: string }) {
  const session = useSession();
  const [data, setData] = useState<Signal<TopicData | undefined> | null>(null);
  useEffect(() => {
    const handle = session.watch(space, "status");
    setData(handle.data);
    return () => handle.release();
  }, [space]);
  const value = data?.value;
  if (value?.kind !== "state" || typeof value.body.word !== "string") {
    return null;  // nothing published: no lamp
  }
  const tone = typeof value.body.tone === "string" && TONES.has(value.body.tone) ? value.body.tone : "glint";
  const reason = typeof value.body.reason === "string" ? value.body.reason : undefined;
  const target = typeof value.body.page === "string" ? value.body.page : undefined;
  const pages = session.space(space)?.pages.value;
  const opens = target && pages && pageById(pages, target) ? target : undefined;
  const inner = (
    <>
      <span class={`lamp__dot lamp__dot--${tone}`} aria-hidden="true" />
      <span class="lamp__word">{value.body.word}</span>
      {reason !== undefined && <span class="visually-hidden">: {reason}</span>}
    </>
  );
  return opens ? (
    <button type="button" class="lamp" title={reason} onClick={() => session.open(space, opens) && focusHeading()}>
      {inner}
    </button>
  ) : <span class="lamp" title={reason} role="status">{inner}</span>;
}

function Lamp() {
  const session = useSession();
  const space = session.railSpace.value;
  const declared = space ? session.space(space)?.pages.value?.topics.some((t) => t.id === "status" && t.shape === "state")
    : false;
  return space && declared ? <LampFor space={space} /> : null;
}

const THEME_KEY = "ui.menu.theme";

function ThemeSwitch() {
  const session = useSession();
  const s = session.setting(GRACEUI_SPACE, THEME_KEY);
  if (!s || s.type !== "choice" || (s.choices ?? []).length === 0) {
    return null;
  }
  const shown = session.shown(GRACEUI_SPACE, THEME_KEY);
  const label = formatValue(s, shown);
  const pages = session.space(GRACEUI_SPACE)?.pages.value;
  const home = pages ? pageById(pages, s.home)?.name : undefined;
  return (
    <button type="button" class="topbar__control" data-shortcut={THEME_KEY}
            aria-label={`Theme: ${label}. Press for the next${home ? `; its home is ${home}` : ""}.`}
            title={home ? `Lives in ${home}` : undefined}
            onClick={() => {
              const next = nextValue(s, shown, 1);
              if (next !== undefined) {
                session.set(GRACEUI_SPACE, THEME_KEY, next);
              }
            }}>
      <Palette />
      <span class="topbar__value">{label}</span>
    </button>
  );
}

export function TopBar() {
  return (
    <header class="topbar">
      <Search />
      <div class="topbar__right">
        <Lamp />
        <ThemeSwitch />
      </div>
    </header>
  );
}
