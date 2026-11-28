// SPDX-License-Identifier: MIT
// rail.tsx: the rail (DESIGN §3.7, §3.8, §4.2.2; D79): the space's name as the wordmark, its build line and the
// collapse button; the space switcher from two spaces; the space's categories as an accordion tree with their pages; and
// at the foot, under a rule, the space's own foot categories (D248), then GraceUI's own space. Under 1100 px of menu width it collapses to one mark a category, each opening a
// flyout of its pages. An ARIA tree with one tab stop: ↑ ↓ move, → opens a category or enters it, ← closes it or goes
// back to it, Enter opens (a category its first page, as grace-view does).
import { useEffect, useRef, useState } from "preact/hooks";
import type { Category, Page } from "../protocol.ts";
import { pageById, railOrder } from "../model/layout.ts";
import type { SpaceView } from "../model/session.ts";
import { useSession } from "./context.ts";
import { CategoryIcon, ChevronSide } from "./icons.tsx";
import { focusHeading } from "./navigate.ts";
import { domId, expanded, railCollapsed, railManual } from "./view-state.ts";

interface Item {
  key: string;
  kind: "category" | "page";
  category: Category;
  page?: Page;
  level: 1 | 2;
  posinset: number;
  setsize: number;
}

const lastPage = new Map<string, string>();  // the page each space last showed, for the switcher (the session only)

function categoryPages(view: SpaceView, category: Category): Page[] {
  const pages = view.pages.value;
  return pages ? category.pages.flatMap((id) => {
    const p = pageById(pages, id);
    return p ? [p] : [];
  }) : [];
}

/** Which of a space's categories a tree or a column of marks holds: those of the rail's foot (D248), the rest, or all. */
type Part = "body" | "foot" | "all";

function inPart(category: Category, part: Part): boolean {
  return part === "all" || (category.foot === true) === (part === "foot");
}

/** Whether a space has categories at the rail's foot (D248). */
function hasFoot(view: SpaceView): boolean {
  return view.pages.value?.categories.some((c) => c.foot === true) ?? false;
}

function RailTree({ view, label, part = "all" }: { view: SpaceView; label: string; part?: Part }) {
  const session = useSession();
  const tree = useRef<HTMLDivElement>(null);
  const [active, setActive] = useState<string | null>(null);
  const pages = view.pages.value;
  if (!pages) {
    return null;
  }
  const space = view.info.id;
  const route = session.route.value;
  const open = expanded.value;
  const items: Item[] = [];
  // Once any of the space's categories has an icon, every category row keeps its place, so that the names line up (D277).
  const withIcons = pages.categories.some((category) => category.icon !== undefined);
  const categories = pages.categories.filter((category) => inPart(category, part));
  categories.forEach((category, c) => {
    items.push({ key: `category:${category.id}`, kind: "category", category, level: 1, posinset: c + 1,
                 setsize: categories.length });
    if (open === `${space}/${category.id}`) {
      const list = categoryPages(view, category);
      list.forEach((page, p) => items.push({ key: `page:${page.id}`, kind: "page", category, page, level: 2,
                                             posinset: p + 1, setsize: list.length }));
    }
  });
  const current = route && route.space === space ? `page:${route.page}` : null;
  // The tab stop: the item last focused, the page shown, its category, or the first, among this tree's own items.
  const routeCategory = route && route.space === space ? `category:${pageById(pages, route.page)?.category ?? ""}` : null;
  const stop = items.some((i) => i.key === active) ? active
    : items.some((i) => i.key === current) ? current
    : items.some((i) => i.key === routeCategory) ? routeCategory : items[0]?.key ?? null;
  const focusKey = (key: string | undefined) => {
    if (key === undefined) {
      return;
    }
    setActive(key);
    const el = tree.current?.querySelector<HTMLElement>(`[data-rail="${CSS.escape(key)}"]`);
    el?.focus();
  };
  const activate = (item: Item, byKeyboard: boolean) => {
    if (item.kind === "category") {
      const first = categoryPages(view, item.category)[0];
      expanded.value = `${space}/${item.category.id}`;
      if (first) {
        session.open(space, first.id);  // focus stays on the rail (grace-view's accordion)
      }
      setActive(item.key);
      return;
    }
    if (item.page && session.open(space, item.page.id)) {
      setActive(item.key);
      if (byKeyboard) {
        focusHeading();
      }
    }
  };
  return (
    <div ref={tree} role="tree" class="tree" aria-label={label}>
      {items.map((item, i) => {
        const isCurrentPage = item.key === current;
        const isCurrentCategory = item.kind === "category" && route?.space === space &&
          pageById(pages, route.page)?.category === item.category.id;
        const isOpen = open === `${space}/${item.category.id}`;
        const classes = [item.kind === "category" ? "tree__category" : "tree__page"];
        if (isCurrentPage || isCurrentCategory) {
          classes.push("is-current");
        }
        if (item.kind === "category" && isOpen) {
          classes.push("is-open");
        }
        if (item.page?.tag) {
          classes.push("is-later");
        }
        return (
          <div role="treeitem" key={item.key} data-rail={item.key} class={classes.join(" ")} aria-level={item.level}
               aria-posinset={item.posinset} aria-setsize={item.setsize} tabIndex={item.key === stop ? 0 : -1}
               aria-expanded={item.kind === "category" ? isOpen : undefined}
               aria-selected={item.kind === "page" ? isCurrentPage : undefined}
               aria-current={isCurrentPage ? "page" : undefined}
               onFocus={() => setActive(item.key)}
               onClick={() => activate(item, false)}
               onKeyDown={(e) => {
                 let handled = true;
                 if (e.key === "ArrowDown") {
                   focusKey(items[i + 1]?.key);
                 } else if (e.key === "ArrowUp") {
                   focusKey(items[i - 1]?.key);
                 } else if (e.key === "Home") {
                   focusKey(items[0]?.key);
                 } else if (e.key === "End") {
                   focusKey(items[items.length - 1]?.key);
                 } else if (e.key === "ArrowRight") {
                   if (item.kind === "category") {
                     if (!isOpen) {
                       expanded.value = `${space}/${item.category.id}`;
                     } else {
                       focusKey(items[i + 1]?.kind === "page" ? items[i + 1]?.key : undefined);
                     }
                   }
                 } else if (e.key === "ArrowLeft") {
                   if (item.kind === "page") {
                     focusKey(`category:${item.category.id}`);
                   } else if (isOpen) {
                     expanded.value = null;
                   }
                 } else if (e.key === "Enter" || e.key === " ") {
                   activate(item, true);
                 } else {
                   handled = false;
                 }
                 if (handled) {
                   e.preventDefault();
                 }
               }}>
            {item.kind === "category" && withIcons && (
              <span class="tree__icon">{item.category.icon && <CategoryIcon icon={item.category.icon} />}</span>
            )}
            <span class="tree__label">{item.kind === "category" ? item.category.name : item.page?.name}</span>
            {item.page?.tag && <span class="tag" title="Arrives in a later version">
              {item.page.tag === "wip" ? "WIP" : "Later"}
            </span>}
          </div>
        );
      })}
    </div>
  );
}

function RailMarks({ view, label, part = "all" }: { view: SpaceView; label: string; part?: Part }) {
  const session = useSession();
  const [flyout, setFlyout] = useState<string | null>(null);
  const holder = useRef<HTMLUListElement>(null);
  const pages = view.pages.value;
  useEffect(() => {
    if (flyout === null) {
      return undefined;
    }
    holder.current?.querySelector<HTMLElement>(".flyout button")?.focus();
    const away = (e: PointerEvent) => {
      if (!holder.current?.contains(e.target as Node)) {
        setFlyout(null);
      }
    };
    document.addEventListener("pointerdown", away);
    return () => document.removeEventListener("pointerdown", away);
  }, [flyout]);
  if (!pages) {
    return null;
  }
  const space = view.info.id;
  const route = session.route.value;
  const close = (category: string) => {
    setFlyout(null);
    holder.current?.querySelector<HTMLElement>(`[data-rail="${CSS.escape(`category:${category}`)}"]`)?.focus();
  };
  return (
    <ul ref={holder} class="marks" aria-label={label}>
      {pages.categories.filter((category) => inPart(category, part)).map((category) => {
        const list = categoryPages(view, category);
        const isCurrent = route?.space === space && pageById(pages, route.page)?.category === category.id;
        const flyId = domId("flyout", space, category.id);
        return (
          <li key={category.id} class="marks__item">
            <button type="button" class={isCurrent ? "mark is-current" : "mark"} data-rail={`category:${category.id}`}
                    aria-label={category.name} aria-haspopup="true" aria-expanded={flyout === category.id}
                    aria-controls={flyout === category.id ? flyId : undefined}
                    onClick={() => setFlyout(flyout === category.id ? null : category.id)}
                    onKeyDown={(e) => {
                      if (e.key === "Escape" && flyout === category.id) {
                        e.preventDefault();
                        setFlyout(null);
                      }
                    }}>
              {category.icon
                ? <span class="mark__icon" aria-hidden="true"><CategoryIcon icon={category.icon} /></span>
                : <span class="mark__glyph" aria-hidden="true">{category.name.slice(0, 1)}</span>}
              <span class="mark__name" aria-hidden="true">{category.name}</span>
            </button>
            {flyout === category.id && (
              <div class="flyout" id={flyId} role="group" aria-label={category.name}
                   onKeyDown={(e) => {
                     const buttons = [...(e.currentTarget as HTMLElement).querySelectorAll<HTMLElement>("button")];
                     const at = buttons.indexOf(document.activeElement as HTMLElement);
                     if (e.key === "Escape") {
                       e.preventDefault();
                       e.stopPropagation();
                       close(category.id);
                     } else if (e.key === "ArrowDown" || e.key === "ArrowUp") {
                       e.preventDefault();
                       buttons[(at + (e.key === "ArrowDown" ? 1 : buttons.length - 1)) % buttons.length]?.focus();
                     }
                   }}>
                <p class="flyout__title eyebrow">{category.name}</p>
                <ul>
                  {list.map((page) => (
                    <li key={page.id}>
                      <button type="button" class="flyout__page" data-rail={`page:${page.id}`}
                              aria-current={route?.space === space && route.page === page.id ? "page" : undefined}
                              onClick={() => {
                                setFlyout(null);
                                if (session.open(space, page.id)) {
                                  focusHeading();
                                }
                              }}>
                        {page.name}
                        {page.tag && <span class="tag">{page.tag === "wip" ? "WIP" : "Later"}</span>}
                      </button>
                    </li>
                  ))}
                </ul>
              </div>
            )}
          </li>
        );
      })}
    </ul>
  );
}

function Switcher({ spaces, current }: { spaces: SpaceView[]; current: string }) {
  const session = useSession();
  return (
    <div class="switcher" role="radiogroup" aria-label="Space">
      {spaces.map((view) => (
        <button type="button" role="radio" key={view.info.id} class="switcher__part" data-space-switch={view.info.id}
                aria-checked={view.info.id === current} tabIndex={view.info.id === current ? 0 : -1}
                onClick={() => openSpace(session, view)}
                onKeyDown={(e) => {
                  if (e.key === "ArrowRight" || e.key === "ArrowLeft" || e.key === "ArrowDown" || e.key === "ArrowUp") {
                    e.preventDefault();
                    const at = spaces.indexOf(view);
                    const forward = e.key === "ArrowRight" || e.key === "ArrowDown";
                    const next = spaces[(at + (forward ? 1 : spaces.length - 1)) % spaces.length];
                    if (next) {
                      openSpace(session, next);
                      document.querySelector<HTMLElement>(`[data-space-switch="${CSS.escape(next.info.id)}"]`)?.focus();
                    }
                  }
                }}>
          {view.info.name}
        </button>
      ))}
    </div>
  );
}

/** Opens a space at the page it last showed, or its first (the switcher, and Ctrl+Page Up and Page Down, D79). */
export function openSpace(session: ReturnType<typeof useSession>, view: SpaceView): void {
  const pages = view.pages.value;
  if (!pages) {
    return;
  }
  const remembered = lastPage.get(view.info.id);
  const target = remembered && pageById(pages, remembered) ? remembered : railOrder(pages)[0]?.id;
  if (target) {
    session.open(view.info.id, target);
  }
}

export function Rail() {
  const session = useSession();
  const collapsed = railCollapsed();
  const content = session.contentSpaces();
  const railId = session.railSpace.value;
  const main = content.find((s) => s.info.id === railId) ?? content[0];
  const foot = session.footSpace();
  const route = session.route.value;
  if (route) {
    lastPage.set(route.space, route.page);
  }
  if (!main) {
    return <nav class="rail" aria-label="Menu" />;
  }
  return (
    <nav class="rail" aria-label="Menu" data-collapsed={collapsed ? "" : undefined}>
      <div class="rail__head">
        <p class="wordmark" title={main.info.name}>
          {collapsed ? <span aria-hidden="true">{main.info.name.slice(0, 1)}</span> : main.info.name}
          {collapsed && <span class="visually-hidden">{main.info.name}</span>}
        </p>
        <button type="button" class="rail__collapse" aria-expanded={!collapsed}
                aria-label={collapsed ? "Show the rail's names" : "Collapse the rail to its marks"}
                onClick={() => {
                  railManual.value = !collapsed;
                }}>
          <ChevronSide />
        </button>
      </div>
      {!collapsed && <p class="rail__build">v{main.info.version}</p>}
      {content.length >= 2 && !collapsed && <Switcher spaces={content} current={main.info.id} />}
      <div class="rail__body">
        {collapsed ? <RailMarks view={main} label={main.info.name} part="body" />
          : <RailTree view={main} label={main.info.name} part="body" />}
      </div>
      {(foot || hasFoot(main)) && (
        <div class="rail__foot">
          {hasFoot(main) && (collapsed
            ? <RailMarks view={main} label={`${main.info.name}, at the foot`} part="foot" />
            : <RailTree view={main} label={`${main.info.name}, at the foot`} part="foot" />)}
          {foot && (collapsed ? <RailMarks view={foot} label={foot.info.name} />
            : <RailTree view={foot} label={foot.info.name} />)}
        </div>
      )}
    </nav>
  );
}
