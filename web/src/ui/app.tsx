// SPDX-License-Identifier: MIT
// app.tsx: the frame (DESIGN §3.7, §4.2): the rail, the top bar and the page, in that order for Tab (§5.7); the
// browser's keyboard map (§5.9): / and Ctrl+F for search, Q and E for tabs, Page Up and Page Down for pages, Ctrl+Page Up
// and Page Down for spaces, Esc for modals, a held key's repeats only to move along or step a slider (D270); GraceUI's
// own settings applied to the page (its theme, motion, type and scale, §3.13, §3.15); and the connection's state,
// stale while it is lost (§3.9).
import { useEffect } from "preact/hooks";
import { useSignalEffect } from "@preact/signals";
import type { Session } from "../model/session.ts";
import { GRACEUI_SPACE } from "../model/session.ts";
import { pageById } from "../model/layout.ts";
import { SessionContext } from "./context.ts";
import { focusActiveTab, focusHeading, focusSearch } from "./navigate.ts";
import { PageView } from "./page.tsx";
import { FlowLanding } from "./flow.tsx";
import { Rail, openSpace } from "./rail.tsx";
import { TopBar } from "./topbar.tsx";
import { expanded, menuWidth } from "./view-state.ts";
import { noteSent } from "./live.ts";
import { repeatActs, repeatRegion, repeatTarget } from "../model/repeat.ts";

const THEMES = new Set(["grace", "clear"]);

function editable(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) {
    return false;
  }
  if (target.isContentEditable || target.closest("[data-capturing]")) {
    return true;
  }
  if (target instanceof HTMLTextAreaElement || target instanceof HTMLSelectElement) {
    return true;
  }
  return target instanceof HTMLInputElement && !["checkbox", "radio", "button", "range"].includes(target.type);
}

function scaleOf(session: Session): number {
  const v = session.stored(GRACEUI_SPACE, "ui.scale");
  return typeof v === "number" && Number.isFinite(v) ? Math.min(Math.max(v, 0.75), 1.5) : 1;
}

function Banner({ session }: { session: Session }) {
  const connection = session.connection.value;
  const ended = session.ended.value;
  const notice = session.notice.value;
  if (notice !== null) {
    return <div class="banner banner--ember" role="alert">{notice}</div>;
  }
  if (connection === "lost") {
    return (
      <div class="banner banner--glint" role="status">
        <span>Reconnecting</span>
        <button type="button" class="button button--quiet button--small" onClick={() => session.retry()}>Retry</button>
      </div>
    );
  }
  if (connection === "ended" && ended && ended.reason !== "lost") {
    const words = ended.reason === "superseded" ? "Another tab took this one's place. Load this page to use it here."
      : ended.reason === "shutdown" ? "The browser view has closed. Load this page again once it is back."
      : ended.reason === "version" ? "This page needs updating: reload it."
      : ended.reason === "unauthorised" ? "This page's sign-in has ended: load the page again."
      : "Something went wrong; the log has the details.";
    return <div class="banner banner--glint" role="status">{words}</div>;
  }
  if (connection === "connecting" && session.spaces.value.length === 0) {
    return <div class="banner banner--quiet" role="status">Connecting…</div>;
  }
  return null;
}

export function App({ session }: { session: Session }) {
  // GraceUI's own settings, as the space holds them, on the page (never a pending value).
  useSignalEffect(() => {
    const root = document.documentElement;
    const theme = session.stored(GRACEUI_SPACE, "ui.menu.theme");
    root.dataset.theme = typeof theme === "string" && THEMES.has(theme) ? theme : "grace";
    root.toggleAttribute("data-reduce-motion", session.stored(GRACEUI_SPACE, "ui.menu.reduce_motion") === true);
    root.toggleAttribute("data-plain-text", session.stored(GRACEUI_SPACE, "ui.menu.plain_text") === true);
    const scale = scaleOf(session);
    root.style.setProperty("--ui-scale", String(scale));  // CSSOM, which the CSP allows
    menuWidth.value = window.innerWidth / scale;
  });
  // The rail's accordion follows the page opened.
  useSignalEffect(() => {
    const route = session.route.value;
    const pages = route ? session.space(route.space)?.pages.value : undefined;
    const page = route && pages ? pageById(pages, route.page) : undefined;
    if (route && page) {
      expanded.value = `${route.space}/${page.category}`;
      const space = session.space(route.space)?.info.name ?? "";
      document.title = `${page.name} · ${space}`;
    }
  });
  useSignalEffect(() => {
    if (session.sent.value > 0) {
      noteSent();  // a command this view sent: Live updates draws what it held, and holds nothing while it is answered
    }
  });
  useEffect(() => {
    const resize = () => {
      menuWidth.value = window.innerWidth / scaleOf(session);
    };
    const keys = (e: KeyboardEvent) => {
      if (e.defaultPrevented) {
        return;
      }
      const ctrl = e.ctrlKey || e.metaKey;
      if (ctrl && !e.altKey && !e.shiftKey && e.key.toLowerCase() === "f") {
        e.preventDefault();
        focusSearch();
        return;
      }
      if (document.querySelector("dialog[open]")) {
        return;  // a modal holds the keys; Esc closes it
      }
      if (ctrl && !e.altKey && (e.key === "PageUp" || e.key === "PageDown")) {
        const spaces = session.contentSpaces();
        const at = spaces.findIndex((s) => s.info.id === session.railSpace.value);
        const next = spaces[(at + (e.key === "PageDown" ? 1 : spaces.length - 1)) % spaces.length];
        if (spaces.length >= 2 && next) {
          e.preventDefault();
          openSpace(session, next);
          focusHeading();
        }
        return;
      }
      if (ctrl || e.altKey || editable(e.target)) {
        return;
      }
      if (e.key === "/") {
        e.preventDefault();
        focusSearch();
      } else if ((e.key === "q" || e.key === "Q" || e.key === "e" || e.key === "E") && !e.shiftKey) {
        const route = session.route.value;
        const pages = route ? session.space(route.space)?.pages.value : undefined;
        const target = route && pages ? pageById(pages, route.page) : undefined;
        const tabs = target && !target.flow ? target.tabs ?? [] : [];  // a flow's step is its space's (D278)
        const next = route ? route.tab + (e.key.toLowerCase() === "e" ? 1 : -1) : -1;
        if (tabs.length >= 2 && next >= 0 && next < tabs.length) {
          e.preventDefault();
          session.setTab(next);
          focusActiveTab();
        }
      } else if ((e.key === "PageUp" || e.key === "PageDown") && !e.shiftKey) {
        e.preventDefault();
        if (session.step(e.key === "PageDown" ? 1 : -1)) {
          focusHeading();
        }
      }
    };
    // A held key (D270): before any element's own keys, a repeat that would turn, choose, open or press is dropped,
    // and so is one whose press took focus out of the list it was in, or that a press of the mouse has come since.
    let pressed: Element | null = null;
    const held = (e: KeyboardEvent) => {
      if (!e.repeat) {
        pressed = repeatRegion(e.target);
        return;
      }
      if (!repeatActs(e.key, repeatTarget(e.target)) || pressed === null || repeatRegion(e.target) !== pressed) {
        e.preventDefault();
        e.stopPropagation();
      }
    };
    const pointed = () => {
      pressed = null;
    };
    window.addEventListener("resize", resize);
    document.addEventListener("keydown", held, true);
    document.addEventListener("pointerdown", pointed, true);
    document.addEventListener("keydown", keys);
    return () => {
      window.removeEventListener("resize", resize);
      document.removeEventListener("keydown", held, true);
      document.removeEventListener("pointerdown", pointed, true);
      document.removeEventListener("keydown", keys);
    };
  }, [session]);
  const stale = session.connection.value === "lost";
  return (
    <SessionContext.Provider value={session}>
      <div class="frame" data-stale={stale ? "" : undefined}>
        <Rail />
        <div class="frame__main">
          <TopBar />
          <Banner session={session} />
          <PageView />
        </div>
      </div>
      <FlowLanding />
    </SessionContext.Provider>
  );
}
