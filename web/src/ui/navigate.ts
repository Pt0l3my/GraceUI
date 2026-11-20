// SPDX-License-Identifier: MIT
// navigate.ts: moving the view and its focus (DESIGN §4.3, §5.7, §5.9): a setting's home opened with its row revealed,
// focused and tinted; the page's heading or its active tab focused after the page has drawn.
import type { Session } from "../model/session.ts";
import { pageById, sectionsOf } from "../model/layout.ts";
import { found, openSection, sectionId, toggleAdvanced } from "./view-state.ts";

/** Opens a setting's home on its tab, its section open and its Advanced disclosure too, the row focused and tinted. */
export function revealSetting(session: Session, space: string, key: string): boolean {
  const place = session.openSetting(space, key);
  const route = session.route.value;
  const pages = session.space(space)?.pages.value;
  const page = route && pages ? pageById(pages, route.page) : undefined;
  if (!place || !route || !page) {
    return false;
  }
  const section = sectionsOf(page, route.tab).find((s) => s.id === place.section);
  if (section) {
    openSection(space, page.id, section);
  }
  if (place.advanced) {
    toggleAdvanced(sectionId(space, page.id, place.section), true);
  }
  found.value = space + "\u0000" + key;
  return true;
}

// After the page has drawn, unless the player has moved focus meanwhile (a key pressed straight after Page Down).
function afterDraw(fn: () => void): void {
  const from = document.activeElement;
  const run = () => {
    if (document.activeElement === from || document.activeElement === document.body ||
        (document.activeElement !== null && !document.activeElement.isConnected)) {
      fn();
    }
  };
  if (typeof requestAnimationFrame === "function") {
    requestAnimationFrame(run);
  } else {
    setTimeout(run, 0);
  }
}

export function focusHeading(): void {
  afterDraw(() => document.getElementById("page-title")?.focus());
}

export function focusActiveTab(): void {
  afterDraw(() => document.querySelector<HTMLElement>("[role=tab][aria-selected=true]")?.focus());
}

export function focusSearch(): void {
  const field = document.getElementById("search") as HTMLInputElement | null;
  field?.focus();
  field?.select();
}
