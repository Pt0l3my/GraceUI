// SPDX-License-Identifier: MIT
// topbar.tsx: the top bar (DESIGN §4.2.3, §4.3): global search at the left; at the right the status lamp, only for a
// space that publishes a `status` topic (otherwise nothing, invariant 2), and the theme switch, a shortcut of
// ui.menu.theme. There is no Close in the browser.
import { useEffect, useRef, useState } from "preact/hooks";
import type { Signal } from "@preact/signals";
import { formatValue, nextValue } from "../model/format.ts";
import { pageById } from "../model/layout.ts";
import { search } from "../model/search.ts";
import type { SearchResult, SearchSpace } from "../model/search.ts";
import type { TopicData } from "../model/session.ts";
import { GRACEUI_SPACE } from "../model/session.ts";
import { useSession } from "./context.ts";
import { Clock, Cross, Palette, Power, SearchGlass } from "./icons.tsx";
import type { Json, QuickSwitch, SpacePages } from "../protocol.ts";
import { useTopic } from "./components.tsx";
import { LIVE_WORDS, heldNew, liveMode, nextLiveMode, setLiveMode } from "./live.ts";
import { focusHeading, revealSetting } from "./navigate.ts";
import { useScope } from "./paged.tsx";
import { chooseScope } from "./view-state.ts";

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

/** Live updates (G45, D252), on a page that says it is live: a press steps Live, Every 5 s, Paused, as Enter does in
 * game; while it holds, its words count what came meanwhile. */
function LiveUpdates() {
  const session = useSession();
  const route = session.route.value;
  const pages = route ? session.space(route.space)?.pages.value : undefined;
  const page = route && pages ? pageById(pages, route.page) : undefined;
  if (page?.live !== true) {
    return null;
  }
  const mode = liveMode.value;
  const count = heldNew.value;
  const words = LIVE_WORDS[mode] + (mode !== "live" && count > 0 ? ` \u00B7 ${count} new` : "");
  return (
    <button type="button" class="topbar__control" data-live={mode}
            aria-label={`Live updates: ${words}. Press for the next.`}
            onClick={() => setLiveMode(nextLiveMode(mode))}>
      <Clock />
      <span class="topbar__value">Updates: {words}</span>
    </button>
  );
}

/** A space's scope (G45, D254; EldenNet's Journey), on its scoped pages once its topic lists a choice: its label and
 * a select of its choices in the space's words. A choice is for the session; a new one starts at the space's current. */
function ScopeChooser() {
  const session = useSession();
  const route = session.route.value;
  const pages = route ? session.space(route.space)?.pages.value : undefined;
  const page = route && pages ? pageById(pages, route.page) : undefined;
  const scope = useScope(route?.space ?? "", pages);
  if (!route || page?.scoped !== true || scope === null || scope.choices.length === 0 || scope.id === null) {
    return null;
  }
  return (
    <label class="topbar__control topbar__scope">
      <span class="topbar__label">{scope.label}</span>
      <select class="topbar__select" value={scope.id} data-scope=""
              onChange={(e) => chooseScope(route.space, (e.currentTarget as HTMLSelectElement).value)}>
        {scope.choices.map((c) => <option value={c.id} key={c.id}>{c.label}</option>)}
      </select>
    </label>
  );
}

/** One quick switch's row: a setting's switch or choice as its row has it, or a session switch the space owns, which
 * sends its command with {on} and shows the state the space publishes (invariant 1); where it lives, as a link. */
function QuickRow({ space, q, pages, done }: { space: string; q: QuickSwitch; pages: SpacePages; done: () => void }) {
  const session = useSession();
  const state = useTopic(space, q.key === undefined ? q.state?.topic : undefined);
  const id = `quick-${q.key ?? q.id ?? ""}`.replace(/[^a-z0-9_-]/gi, "-");
  const s = q.key !== undefined ? session.setting(space, q.key) : undefined;
  if (q.key !== undefined && (!s || (s.type !== "bool" && s.type !== "choice"))) {
    return null;
  }
  const homeId = s ? s.home : q.home ?? "";
  const home = pageById(pages, homeId);
  const label = s ? s.label : q.label ?? "";
  const lives = home && (
    <button type="button" class="quick__home" onClick={() => {
      if (session.open(space, home.id)) {
        done();
        focusHeading();
      }
    }}>
      Lives in {home.name}
    </button>
  );
  let control;
  if (s && s.type === "choice") {
    const shown = session.shown(space, s.key);
    control = (
      <select class="field quick__select" aria-labelledby={id} value={typeof shown === "string" ? shown : ""}
              onChange={(e) => session.set(space, s.key, e.currentTarget.value)}>
        {(s.choices ?? []).map((c) => <option key={c.value} value={c.value}>{c.label}</option>)}
      </select>
    );
  } else {
    let on = false;
    let known = true;
    if (s) {
      on = session.shown(space, s.key) === true;
    } else {
      const body = state?.kind === "state" ? state.body : undefined;
      const value: Json | undefined = body && q.state ? body[q.state.field] : undefined;
      known = typeof value === "boolean";
      on = value === true;
    }
    control = (
      <span class="switch-wrap">
        {!known && <span class="switch__word">Not known yet</span>}
        <button type="button" role="switch" class="switch" aria-checked={on} aria-labelledby={id}
                aria-disabled={known ? undefined : "true"}
                onClick={() => {
                  if (s) {
                    session.set(space, s.key, !on);
                  } else if (known && q.command !== undefined) {
                    void session.command(space, q.command, { on: !on });  // the space's next state shows it
                  }
                }}>
          <span class="switch__knob" />
        </button>
      </span>
    );
  }
  return (
    <li class="quick__row">
      <span class="quick__words">
        <span class="quick__label" id={id}>{label}</span>
        {lives}
      </span>
      {control}
    </li>
  );
}

/** Quick switches (G45, D252), where the space in view has some: a panel under its button. Esc, the button again or a
 * press outside closes it, focus going back to the button. */
function QuickSwitches() {
  const session = useSession();
  const space = session.railSpace.value;
  const pages = space ? session.space(space)?.pages.value : undefined;
  const quick = pages?.quick ?? [];
  const [open, setOpen] = useState(false);
  const button = useRef<HTMLButtonElement>(null);
  const panel = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) {
      return undefined;
    }
    panel.current?.querySelector<HTMLElement>("button, select")?.focus();
    const away = (e: MouseEvent) => {
      const at = e.target as Node | null;
      if (!panel.current?.contains(at) && !button.current?.contains(at)) {
        setOpen(false);
      }
    };
    document.addEventListener("mousedown", away);
    return () => document.removeEventListener("mousedown", away);
  }, [open]);
  if (!space || !pages || quick.length === 0) {
    return null;
  }
  const close = () => {
    setOpen(false);
    button.current?.focus();
  };
  return (
    <div class="quick">
      <button ref={button} type="button" class="topbar__control" aria-expanded={open} aria-controls="quick-switches"
              onClick={() => setOpen(!open)}>
        <Power />
        <span class="topbar__value">Quick switches</span>
      </button>
      {open && (
        <div ref={panel} id="quick-switches" class="quick__panel" role="dialog" aria-label="Quick switches"
             onKeyDown={(e) => {
               if (e.key === "Escape") {
                 e.preventDefault();
                 e.stopPropagation();
                 close();
               }
             }}>
          <ul class="quick__list">
            {quick.map((q) => <QuickRow key={q.key ?? q.id ?? ""} space={space} q={q} pages={pages}
                                        done={() => setOpen(false)} />)}
          </ul>
        </div>
      )}
    </div>
  );
}

export function TopBar() {
  return (
    <header class="topbar">
      <Search />
      <div class="topbar__right">
        <Lamp />
        <LiveUpdates />
        <ScopeChooser />
        <ThemeSwitch />
        <QuickSwitches />
      </div>
    </header>
  );
}
