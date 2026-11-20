// SPDX-License-Identifier: MIT
// search.ts: global search, GraceUI's part (DESIGN §4.3): page names and descriptions, and every setting's label,
// synonyms, help and key, across every space the view shows, each result with its breadcrumb and its space. Only what a
// space published is indexed (invariants 1 and 2). A setting grace-view's search finds (label, synonym or key) is always
// found here too, and ranked first among settings (page_view.cpp's search).
import type { Setting, SpacePages } from "../protocol.ts";
import { categoryOf, pageById } from "./layout.ts";

export interface SearchSpace {
  id: string;
  name: string;
  pages: SpacePages;
  settings: Setting[];
}

export interface SearchResult {
  kind: "page" | "setting";
  space: string;
  spaceName: string;
  page: string;
  key?: string;
  label: string;
  /** Category › Page, and the setting's label for a setting. */
  breadcrumb: string[];
  /** 0 a page's name, 1 a setting's label, 2 its synonyms or key, 3 its help or a page's description. */
  rank: number;
}

export const MAX_RESULTS = 20;

function has(text: string | undefined, q: string): boolean {
  return text !== undefined && text.toLowerCase().includes(q);
}

export function search(spaces: SearchSpace[], query: string, limit = MAX_RESULTS): SearchResult[] {
  const q = query.trim().toLowerCase();
  if (q.length === 0) {
    return [];
  }
  const results: SearchResult[] = [];
  for (const space of spaces) {
    for (const page of space.pages.pages) {
      const category = categoryOf(space.pages, page);
      const crumbs = category ? [category.name, page.name] : [page.name];
      const rank = has(page.name, q) ? 0 : has(page.description, q) ? 3 : -1;
      if (rank >= 0) {
        results.push({ kind: "page", space: space.id, spaceName: space.name, page: page.id, label: page.name,
                       breadcrumb: crumbs, rank });
      }
    }
    for (const s of space.settings) {
      const home = pageById(space.pages, s.home);
      if (!home) {
        continue;  // a setting with no page in the view is not shown, so it is not found either
      }
      const rank = has(s.label, q) ? 1
        : (s.synonyms ?? []).some((w) => has(w, q)) || s.key.includes(q) ? 2
        : has(s.help, q) ? 3 : -1;
      if (rank < 0) {
        continue;
      }
      const category = categoryOf(space.pages, home);
      const crumbs = category ? [category.name, home.name, s.label] : [home.name, s.label];
      results.push({ kind: "setting", space: space.id, spaceName: space.name, page: home.id, key: s.key, label: s.label,
                     breadcrumb: crumbs, rank });
    }
  }
  // By rank, then by label as grace-view sorts its hits (byte order), then by space; stable otherwise.
  results.sort((a, b) => a.rank - b.rank || (a.label < b.label ? -1 : a.label > b.label ? 1 : 0) ||
                         (a.space < b.space ? -1 : a.space > b.space ? 1 : 0));
  return results.slice(0, limit);
}

/** grace-view's own hits for a query: settings whose label, synonyms or key hold it, sorted by label. */
export function gameViewHits(settings: Setting[], query: string): string[] {
  const q = query.toLowerCase();
  if (q.length === 0) {
    return [];
  }
  return settings
    .filter((s) => s.label.toLowerCase().includes(q) || s.key.includes(q) ||
                   (s.synonyms ?? []).some((w) => w.toLowerCase().includes(q)))
    .sort((a, b) => (a.label < b.label ? -1 : a.label > b.label ? 1 : 0))
    .map((s) => s.key);
}
