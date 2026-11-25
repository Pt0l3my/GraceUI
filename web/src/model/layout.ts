// SPDX-License-Identifier: MIT
// layout.ts: a page's structure from its description alone (DESIGN §2.5, §9.3; invariant 12): its tabs, sections and
// rows in grace-view's order (page_view.cpp's rebuild), the rail's order across categories for Page Up and Page Down, and
// where a setting lives. The browser draws exactly this structure, so both views show the same rows in the same order.
import type { Category, Json, Page, PageBlock, PageSection, SpacePages } from "../protocol.ts";

export interface RowRef {
  kind: "setting" | "shortcut";
  key: string;
}

export function sectionsOf(page: Page, tab: number): PageSection[] {
  if (page.tabs && page.tabs.length > 0) {
    return page.tabs[Math.min(tab, page.tabs.length - 1)]?.sections ?? [];
  }
  return page.sections ?? [];
}

export function rowsOfBlocks(blocks: PageBlock[] | undefined): RowRef[] {
  const rows: RowRef[] = [];
  for (const block of blocks ?? []) {
    if (block.kind === "component") {
      continue;
    }
    for (const key of block.keys ?? []) {
      rows.push({ kind: block.kind === "shortcuts" ? "shortcut" : "setting", key });
    }
  }
  return rows;
}

/** A section's rows: its blocks, then, when it is open, its Advanced disclosure's. */
export function rowsOfSection(section: PageSection, advancedOpen: boolean): RowRef[] {
  const rows = rowsOfBlocks(section.blocks);
  if (advancedOpen) {
    rows.push(...rowsOfBlocks(section.advanced));
  }
  return rows;
}

/** A tab's rows with every section and every Advanced disclosure open: the order the parity golden holds. */
export function rowsOfTab(page: Page, tab: number): RowRef[] {
  return sectionsOf(page, tab).flatMap((section) => rowsOfSection(section, true));
}

/** The golden's line for a page and tab: "<space> <page>[#tab]: setting:<key> shortcut:<key> ...". */
export function orderLine(space: string, page: Page, tab: number): string {
  const tabId = page.tabs && page.tabs.length > 0 ? "#" + (page.tabs[tab]?.id ?? "") : "";
  const rows = rowsOfTab(page, tab).map((r) => ` ${r.kind}:${r.key}`).join("");
  return `${space} ${page.id}${tabId}:${rows}`;
}

export function tabCount(page: Page): number {
  return page.tabs && page.tabs.length > 0 ? page.tabs.length : 1;
}

/** Pages in rail order, across categories (Page Up and Page Down, grace-view's order). */
export function railOrder(pages: SpacePages): Page[] {
  const out: Page[] = [];
  for (const category of pages.categories) {
    for (const id of category.pages) {
      const page = pages.pages.find((p) => p.id === id);
      if (page) {
        out.push(page);
      }
    }
  }
  return out;
}

function parseIncrement(text: string): [number, number] | null {
  const match = /^(\d{1,7})\.(\d{1,7})$/.exec(text);
  return match ? [Number(match[1]), Number(match[2])] : null;
}

/** The space's page data as its increment shows it (§4.2.2, D173, grace-view's set_increment): a page of a later
 * increment is absent, from its category and from related links; a setting whose home has gone is then neither found
 * nor shown as a shortcut. A category with no page left, or none at all, goes from the rail, as in game (D191). An
 * increment that does not parse leaves every page. */
export function withinIncrement(pages: SpacePages, increment: string): SpacePages {
  const running = parseIncrement(increment);
  const later = (page: Page) => {
    const ships = running && page.increment !== undefined ? parseIncrement(page.increment) : null;
    return running !== null && ships !== null &&
      (ships[0] > running[0] || (ships[0] === running[0] && ships[1] > running[1]));
  };
  const absent = new Set(pages.pages.filter(later).map((p) => p.id));
  const present = new Set(pages.pages.filter((p) => !absent.has(p.id)).map((p) => p.id));
  const categories = pages.categories
    .map((c) => ({ ...c, pages: c.pages.filter((id) => present.has(id)) }))
    .filter((c) => c.pages.length > 0);
  if (absent.size === 0 && categories.length === pages.categories.length &&
      categories.every((c, i) => c.pages.length === pages.categories[i]!.pages.length)) {
    return pages;
  }
  return {
    ...pages,
    categories,
    pages: pages.pages.filter((p) => !absent.has(p.id)).map((p) => ({ ...p, related: p.related.filter((id) => !absent.has(id)) })),
  };
}

/** The page data as its settings show it (D258): a page shown with a switch is absent while the switch is off, from
 * its category, from Related links and from search, as a page of a later increment is. On a paired device (D264) a page
 * on this PC alone is absent too; GraceUI's server leaves it out of a device's page data already. */
export function withShown(pages: SpacePages, values: { [key: string]: Json } | undefined, device = false): SpacePages {
  const absent = new Set(pages.pages.filter((p) => (p.shown_with !== undefined && values?.[p.shown_with] !== true) ||
                                                    (device && p.pc_only === true))
    .map((p) => p.id));
  if (absent.size === 0) {
    return pages;
  }
  return {
    ...pages,
    categories: pages.categories.map((c) => ({ ...c, pages: c.pages.filter((id) => !absent.has(id)) }))
      .filter((c) => c.pages.length > 0),
    pages: pages.pages.filter((p) => !absent.has(p.id)).map((p) => ({ ...p, related: p.related.filter((id) => !absent.has(id)) })),
  };
}

export function pageById(pages: SpacePages, id: string): Page | undefined {
  return pages.pages.find((p) => p.id === id);
}

export function categoryOf(pages: SpacePages, page: Page): Category | undefined {
  return pages.categories.find((c) => c.id === page.category);
}

/** Where a key's row sits on its home page: the tab, the section, and whether it is behind Advanced. */
export interface RowPlace {
  tab: number;
  section: string;
  advanced: boolean;
}

export function placeOf(page: Page, key: string): RowPlace | undefined {
  for (let tab = 0; tab < tabCount(page); ++tab) {
    for (const section of sectionsOf(page, tab)) {
      if (rowsOfBlocks(section.blocks).some((r) => r.kind === "setting" && r.key === key)) {
        return { tab, section: section.id, advanced: false };
      }
      if (rowsOfBlocks(section.advanced).some((r) => r.kind === "setting" && r.key === key)) {
        return { tab, section: section.id, advanced: true };
      }
    }
  }
  return undefined;
}

/** A Related link: a page of this space, or "<space>/<page>" of another. */
export function parseLink(space: string, link: string): { space: string; page: string } {
  const slash = link.indexOf("/");
  return slash > 0 ? { space: link.slice(0, slash), page: link.slice(slash + 1) } : { space, page: link };
}
