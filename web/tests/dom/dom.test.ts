// SPDX-License-Identifier: MIT
// The built browser view in Chromium (DESIGN §9.1's web app layer, §9.3, §7.2, §7.4, §5.9): dist/ behind the tests'
// fake host, which sends grace-serve's CSP and checks the cookie, the ticket, Host and Origin. Every page of both
// built-in spaces renders with no console error and no CSP violation, its readable text at 4.5:1 or better in both
// themes (invariant 13), its rows in grace-view's order (the parity golden); nothing is requested outside the origin
// (invariant 6); the keyboard map works; a destructive action needs its confirmation; a secret never reaches the DOM
// or storage (invariant 5); a refusal puts its reason under the row and focus back on it; the rail collapses under
// 1100 px; the space switcher and the status lamp appear only when there is something to show (invariant 2).
import { after, before, describe, test } from "node:test";
import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { join } from "node:path";
import type { FakeHost } from "../support/fake-host.ts";
import { startHost } from "../support/fake-host.ts";
import type { PwBrowser, PwPage } from "../support/browser.ts";
import { DIST, launch, openFromRail, openPage, unfoldAll, violations } from "../support/browser.ts";
import { builtinSpaces, goldenLines, space } from "../support/fixtures.ts";
import type { FixtureSpaceData } from "../../src/transport/fixture-space.ts";

const built = existsSync(join(DIST, "index.html"));

/** Every visible text below 4.5:1 against the grounds composited under it (WCAG 2.x). */
async function lowContrast(page: PwPage): Promise<string[]> {
  return page.evaluate(() => {
    const parse = (c: string): number[] | null => {
      const m = /rgba?\(([^)]+)\)/.exec(c);
      if (!m || m[1] === undefined) {
        return null;
      }
      const p = m[1].split(/[\s,/]+/).filter((x) => x.length > 0).map((x) => (x.endsWith("%") ? parseFloat(x) / 100 : Number(x)));
      return [p[0] ?? 0, p[1] ?? 0, p[2] ?? 0, p[3] ?? 1];
    };
    const lum = (rgb: number[]) => {
      const ch = (v: number) => {
        const s = v / 255;
        return s <= 0.04045 ? s / 12.92 : Math.pow((s + 0.055) / 1.055, 2.4);
      };
      return 0.2126 * ch(rgb[0] ?? 0) + 0.7152 * ch(rgb[1] ?? 0) + 0.0722 * ch(rgb[2] ?? 0);
    };
    const over = (top: number[], under: number[]) => {
      const a = top[3] ?? 1;
      return [0, 1, 2].map((i) => (top[i] ?? 0) * a + (under[i] ?? 0) * (1 - a));
    };
    const failures: string[] = [];
    const all = document.querySelectorAll<HTMLElement>("body *");
    for (const el of all) {
      const own = [...el.childNodes].some((n) => n.nodeType === 3 && (n.textContent ?? "").trim().length > 0);
      if (!own || el.closest(".visually-hidden") || el.getClientRects().length === 0) {
        continue;
      }
      const style = getComputedStyle(el);
      if (style.visibility === "hidden" || style.display === "none") {
        continue;
      }
      const chain: HTMLElement[] = [];
      for (let at: HTMLElement | null = el; at; at = at.parentElement) {
        chain.push(at);
      }
      let ground = parse(getComputedStyle(document.documentElement).backgroundColor) ?? [0, 0, 0, 1];
      for (const at of chain.reverse()) {
        const bg = parse(getComputedStyle(at).backgroundColor);
        if (bg && (bg[3] ?? 0) > 0) {
          ground = [...over(bg, ground), 1];
        }
      }
      const ink = parse(style.color);
      if (!ink) {
        continue;
      }
      const fg = over(ink, ground);
      const a = lum(fg);
      const b = lum(ground);
      const ratio = (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
      if (ratio < 4.5) {
        failures.push(`${ratio.toFixed(2)}:1 "${(el.textContent ?? "").trim().slice(0, 40)}" (${el.className})`);
      }
    }
    return failures;
  });
}

function allPages(): { space: string; category: string; page: string; name: string; tabs: number }[] {
  return builtinSpaces().flatMap((s) => s.pages.pages.map((p) => ({
    space: s.pages.space, category: p.category, page: p.id, name: p.name, tabs: p.tabs?.length ?? 0,
  })));
}

/** Both spaces as an increment would show them in which every page has shipped (0.6), for the tests that visit every
 * page; the default, 0.2, leaves the later ones out (§4.2.2, D173). */
function everyPage(): FixtureSpaceData[] {
  return builtinSpaces().map((s) => ({ ...s, increment: "0.6" }));
}

describe("the browser view, built", { skip: built ? false : "build dist/ first (npm run build)" }, () => {
  let browser: PwBrowser;
  let host: FakeHost;

  before(async () => {
    browser = await launch();
  });
  after(async () => {
    await browser?.close();
  });

  async function fresh(spaces: FixtureSpaceData[] = builtinSpaces(), viewport?: { width: number; height: number }) {
    host = await startHost(DIST, spaces);
    const opened = await openPage(browser, host, viewport);
    return { ...opened, close: async () => {
      await opened.close();
      await host.close();
    } };
  }

  test("every page of both spaces renders, in both themes, with no error, no CSP violation and every text 4.5:1", async () => {
    const { page, problems, requests, close } = await fresh(everyPage());
    try {
      // The check itself finds a text too faint: ink.off, which the theme keeps for what is disabled, never for text.
      await page.evaluate(() => {
        const probe = document.createElement("span");
        probe.id = "contrast-probe";
        probe.textContent = "probe";
        probe.style.setProperty("color", "var(--ink-off)");
        document.querySelector("main")?.append(probe);
      });
      assert.equal((await lowContrast(page)).length, 1, "the contrast check sees a faint text");
      await page.evaluate(() => document.getElementById("contrast-probe")?.remove());
      assert.equal(await page.locator(".lamp").count(), 0, "no status topic: no lamp");
      assert.equal(await page.locator(".switcher").count(), 0, "one content space: no switcher");
      for (const theme of ["grace", "clear"]) {
        if (theme === "clear") {
          await page.locator("[data-shortcut='ui.menu.theme']").click();
          await page.waitForFunction(() => document.documentElement.dataset.theme === "clear");
        }
        for (const p of allPages()) {
          await openFromRail(page, p.category, p.page);
          assert.equal((await page.locator("#page-title").textContent())?.trim(), p.name);
          for (let tab = 0; tab < Math.max(p.tabs, 1); ++tab) {
            if (p.tabs > 0) {
              await page.locator("[role=tab]").nth(tab).click();
            }
            await unfoldAll(page);
            assert.deepEqual(await lowContrast(page), [], `${theme} ${p.page} tab ${tab}`);
          }
        }
      }
      assert.deepEqual(problems, []);
      assert.deepEqual(await violations(page), []);
      const origin = host.origin;
      const outside = requests.filter((url) => !url.startsWith(origin + "/") && !url.startsWith(origin.replace("http:", "ws:") + "/") &&
                                                !url.startsWith("data:"));
      assert.deepEqual(outside, [], "nothing is requested outside the origin");
      assert.ok(requests.some((url) => url.endsWith("/api/ws")), "the socket is on the origin");
    } finally {
      await close();
    }
  });

  test("components draw their topics: tiles, a sentence, a live feed newest first, a log tail; nothing published, an empty state",
       async () => {
    const spaces = builtinSpaces();
    delete spaces[0]?.topics?.["graceui.about"];  // a topic its space has published nothing on
    const { page, close } = await fresh(spaces);
    try {
      await page.locator(".tile").first().waitFor();
      assert.deepEqual(await page.locator(".tile__value").evaluateAll((els) => els.map((e) => e.textContent)),
                       ["Steady", "62%", "14 min"]);
      assert.match((await page.locator(".sentence").textContent()) ?? "", /burns steady amber/);
      await openFromRail(page, "records", "demo.log");
      await page.locator("[role=log] .feed__row").first().waitFor();
      assert.deepEqual(await page.locator("[role=log] .feed__line").evaluateAll((els) => els.map((e) => e.textContent)),
                       ["The flame guttered in a draught, then steadied.", "The wick was trimmed.", "The lantern was lit."]);
      assert.equal(await page.locator("[data-section='journal'] .feed__row").count(), 0, "the journal starts folded");
      await unfoldAll(page);
      await page.locator("[data-section='journal'] .feed__row").first().waitFor();
      assert.equal(await page.locator("[data-section='journal'] .feed__row").count(), 2);
      await openFromRail(page, "graceui", "graceui.about");
      assert.match((await page.locator("main .empty__line").textContent()) ?? "", /Nothing to show yet/);
      await openFromRail(page, "records", "demo.festival");
      assert.match((await page.locator(".statement").textContent()) ?? "", /later version strings small lights/);
      assert.equal((await page.locator("#page-title + .tag").textContent())?.trim().startsWith("Later"), true);
    } finally {
      await close();
    }
  });

  test("each page's rows in grace-view's order (the parity golden, §9.3)", async () => {
    const { page, close } = await fresh(everyPage());
    try {
      const golden = goldenLines("rows-order.txt");
      const ours: string[] = [];
      for (const p of allPages()) {
        await openFromRail(page, p.category, p.page);
        const tabs = await page.locator("[role=tab]").count();
        for (let tab = 0; tab < Math.max(tabs, 1); ++tab) {
          if (tabs > 0) {
            await page.locator("[role=tab]").nth(tab).click();
          }
          await unfoldAll(page);
          const tabId = tabs > 0 ? "#" + ((await page.locator("[role=tab]").nth(tab).getAttribute("id"))?.split("-").pop() ?? "") : "";
          const rows = await page.locator("main [data-row]").evaluateAll((els) => els.map((e) => e.getAttribute("data-row")));
          ours.push(`${p.space} ${p.page}${tabId}:${rows.map((r) => ` ${r}`).join("")}`);
        }
      }
      assert.deepEqual(ours.map((l) => l.trimEnd()), golden.map((l) => l.trimEnd()));
    } finally {
      await close();
    }
  });

  test("the keyboard map: / and Ctrl+F, Q and E, Page Up and Page Down, Esc; Tab from the rail to the top bar to the page", async () => {
    const { page, close } = await fresh();
    try {
      const active = () => page.evaluate(() => {
        const el = document.activeElement;
        return el ? (el.id || el.getAttribute("data-rail") || el.getAttribute("role") || el.tagName) : "";
      });
      await page.locator("#page-title").focus();
      await page.keyboard.press("/");
      assert.equal(await active(), "search");
      await page.keyboard.press("Escape");
      await page.locator("[data-rail='page:demo.overview']").focus();
      await page.keyboard.press("Control+f");
      assert.equal(await active(), "search");
      // The rail as a tree: ↓ to the next page, Enter opens it.
      await page.locator("[data-rail='page:demo.overview']").focus();
      await page.keyboard.press("ArrowDown");
      assert.equal(await active(), "page:demo.behaviour");
      await page.keyboard.press("Enter");
      await page.locator("main[data-page='demo.behaviour']").waitFor();
      await page.waitForFunction(() => document.activeElement?.id === "page-title");
      await page.keyboard.press("e");
      await page.waitForFunction(() => document.activeElement?.getAttribute("role") === "tab");
      assert.equal((await page.locator("[role=tab][aria-selected=true]").textContent())?.trim(), "Timing");
      await page.keyboard.press("q");
      assert.equal((await page.locator("[role=tab][aria-selected=true]").textContent())?.trim(), "Light");
      await page.keyboard.press("PageDown");
      await page.locator("main[data-page='demo.log']").waitFor();
      await page.keyboard.press("PageUp");
      await page.locator("main[data-page='demo.behaviour']").waitFor();
      // The help dialog: opened from its mark, closed by Esc, focus back on the mark.
      await page.locator(".help-mark").focus();
      await page.keyboard.press("Enter");
      await page.locator("dialog[open]").waitFor();
      assert.equal(await page.locator("dialog[open] .steps li").count(), 3);
      await page.keyboard.press("Escape");
      await page.waitForFunction(() => document.querySelector("dialog[open]") === null);
      assert.equal(await page.evaluate(() => document.activeElement?.classList.contains("help-mark")), true);
      // Tab order follows the visual order, from the page's start: the rail, then the top bar, then the page.
      await page.goto(host.origin + "/");
      await page.locator("main.page h1").waitFor();
      const regions: string[] = [];
      for (let i = 0; i < 14; ++i) {
        await page.keyboard.press("Tab");
        regions.push(await page.evaluate(() => {
          const el = document.activeElement;
          return el?.closest(".rail") ? "rail" : el?.closest(".topbar") ? "top" : el?.closest("main") ? "page" : "other";
        }));
      }
      const order = regions.filter((r, i) => i === 0 || r !== regions[i - 1]);
      assert.deepEqual(order.slice(0, 3), ["rail", "top", "page"], regions.join(" "));
    } finally {
      await close();
    }
  });

  test("search: a result opens its row's home, on its tab, behind Advanced if it must, focused and tinted", async () => {
    const { page, close } = await fresh();
    try {
      await page.keyboard.press("/");
      await page.keyboard.type("sundial");
      await page.locator("#search-results [role=option]").first().waitFor();
      assert.match((await page.locator("#search-results .result__crumbs").first().textContent()) ?? "",
                   /Lanterns › Behaviour › Only at night/);
      await page.keyboard.press("Enter");
      await page.locator("main[data-page='demo.behaviour']").waitFor();
      assert.equal((await page.locator("[role=tab][aria-selected=true]").textContent())?.trim(), "Timing");
      await page.waitForFunction(() => document.activeElement?.closest("[data-row='setting:lantern.night_only']") !== null);
      assert.equal(await page.locator("[data-row='setting:lantern.night_only'].row--found").count(), 1);
      // A shortcut opens its setting's home with the row focused.
      await openFromRail(page, "lanterns", "demo.overview");
      await page.locator("[data-row='shortcut:lantern.brightness'] .lives-in").click();
      await page.locator("main[data-page='demo.behaviour']").waitFor();
      await page.waitForFunction(() => document.activeElement?.closest("[data-row='setting:lantern.brightness']") !== null);
    } finally {
      await close();
    }
  });

  test("a destructive action needs its confirmation; Cancel sends nothing", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "graceui", "graceui.browser");
      const sent = () => host.received.filter((f) => f.t === "cmd" && f.name === "dashboard.disconnect");
      await page.locator("[data-action='disconnect']").click();
      await page.locator("dialog[open]").waitFor();
      assert.match((await page.locator("dialog[open] .prose").textContent()) ?? "", /must load the page again/);
      await page.waitForTimeout(100);
      assert.equal(sent().length, 0);
      await page.locator("dialog[open] .button--secondary").click();
      await page.waitForTimeout(100);
      assert.equal(sent().length, 0);
      await page.locator("[data-action='disconnect']").click();
      await page.locator("dialog[open] [data-confirm='disconnect']").click();
      await page.waitForFunction(() => document.querySelector("dialog[open]") === null);
      for (let i = 0; i < 20 && sent().length === 0; ++i) {
        await page.waitForTimeout(50);
      }
      assert.equal(sent().length, 1);
      assert.deepEqual(sent()[0]?.args, { confirm: true });  // its confirmation, which the server requires (D173)
    } finally {
      await close();
    }
  });

  test("a secret's value never reaches the DOM or storage; the row shows only Set", async () => {
    const { page, close } = await fresh();
    const secret = "Zq7-wick-tallow-99";
    try {
      await openFromRail(page, "lanterns", "demo.behaviour");
      await page.locator("[role=tab]").nth(2).click();
      const row = page.locator("[data-row='setting:lantern.oil_token']");
      assert.equal(await page.locator("input[type=password]").count(), 0);
      assert.equal(await page.locator("form").count(), 0);
      await row.locator("input").fill(secret);
      await row.locator("input").press("Enter");
      await page.waitForFunction(() => document.querySelector("[data-row='setting:lantern.oil_token'] .badge--set") !== null);
      assert.equal((await row.locator(".badge").textContent())?.trim(), "Set");
      const leaks = await page.evaluate(async (s: string) => {
        const found: string[] = [];
        if (document.documentElement.outerHTML.includes(s)) found.push("markup");
        for (const input of document.querySelectorAll("input")) {
          if (input.value.includes(s)) found.push("an input's value");
        }
        if (localStorage.length > 0 || sessionStorage.length > 0) found.push("web storage");
        if (document.cookie.length > 0) found.push("a readable cookie");
        if ((await indexedDB.databases()).length > 0) found.push("IndexedDB");
        if (typeof caches !== "undefined" && (await caches.keys()).length > 0) found.push("caches");
        if (performance.getEntries().some((e) => e.name.includes(s))) found.push("a URL");
        return found;
      }, secret);
      assert.deepEqual(leaks, []);
      assert.equal(host.spaces.get("demo")?.value("lantern.oil_token"), "set", "the space keeps only that it is set");
    } finally {
      await close();
    }
  });

  test("a refusal: its reason under the row, the value unchanged, focus back on the control", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "lanterns", "demo.behaviour");
      host.refuse.set("settings.set", "not_now");
      const lit = page.locator("[data-row='setting:lantern.enabled'] [role=switch]");
      await lit.click();
      await page.locator("[data-row='setting:lantern.enabled'] .row__refusal").waitFor();
      assert.match((await page.locator("[data-row='setting:lantern.enabled'] .row__refusal").textContent()) ?? "",
                   /Not possible right now/);
      assert.equal(await lit.getAttribute("aria-checked"), "true");
      assert.equal(await page.evaluate(() => document.activeElement?.getAttribute("role")), "switch");
      host.refuse.clear();
      await page.locator("[role=tab]").nth(2).click();
      const keeper = page.locator("[data-row='setting:lantern.keeper'] input");
      await keeper.fill("x".repeat(300));
      await keeper.press("Enter");
      await page.locator("[data-row='setting:lantern.keeper'] .row__refusal").waitFor();
      assert.match((await page.locator("[data-row='setting:lantern.keeper'] .row__refusal").textContent()) ?? "",
                   /not allowed here/);
      assert.equal(await page.evaluate(() => document.activeElement?.closest("[data-row]")?.getAttribute("data-row")),
                   "setting:lantern.keeper");
      assert.equal(host.spaces.get("demo")?.value("lantern.keeper"), "The north keeper");
    } finally {
      await close();
    }
  });

  test("controls send grace-view's commands: a switch, a slider's arrows, a segmented choice, a key captured", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "lanterns", "demo.behaviour");
      const cmds = () => host.received.filter((f) => f.t === "cmd").map((f) => f.args);
      await page.locator("[data-row='setting:lantern.brightness'] [role=slider]").focus();
      await page.keyboard.press("ArrowRight");
      await page.waitForFunction(() => document.querySelector("[data-row='setting:lantern.brightness'] [role=slider]")
        ?.getAttribute("aria-valuetext") === "0.85");
      await page.locator("[data-row='setting:lantern.colour'] [role=radio]").first().focus();
      await page.keyboard.press("ArrowLeft");
      await page.waitForFunction(() => document.querySelector("[data-row='setting:lantern.colour'] [aria-checked=true]")
        ?.textContent === "Blue");
      await page.locator("[data-row='setting:lantern.enabled'] [role=switch]").click();
      await page.waitForFunction(() => document.querySelector("[data-row='setting:lantern.enabled'] [role=switch]")
        ?.getAttribute("aria-checked") === "false");
      await page.locator("[role=tab]").nth(2).click();
      const cap = page.locator("[data-row='setting:lantern.trim_key'] .keycap");
      await cap.click();
      await page.keyboard.press("F5");  // taken by the capture, not the browser
      await page.waitForFunction(() => document.querySelector("[data-row='setting:lantern.trim_key'] .keycap")
        ?.textContent?.trim() === "F5");
      for (let i = 0; i < 20 && cmds().length < 4; ++i) {
        await page.waitForTimeout(50);
      }
      assert.deepEqual(cmds(), [
        { key: "lantern.brightness", value: 0.85 },
        { key: "lantern.colour", value: "blue" },
        { key: "lantern.enabled", value: false },
        { key: "lantern.trim_key", value: "F5" },
      ]);
      // The changed row gains its lozenge and its reset, which sends settings.reset.
      await page.locator("[role=tab]").nth(0).click();
      await page.locator("[data-row='setting:lantern.enabled'] .reset").click();
      await page.waitForFunction(() => document.querySelector("[data-row='setting:lantern.enabled'] [role=switch]")
        ?.getAttribute("aria-checked") === "true");
      assert.deepEqual(host.received.filter((f) => f.t === "cmd").at(-1),
                       { v: 1, t: "cmd", space: "demo", id: 5, name: "settings.reset",
                         args: { scope: "key", target: "lantern.enabled" } });
    } finally {
      await close();
    }
  });

  test("a page of a later increment is absent from the rail (§4.2.2, D173)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "graceui", "graceui.menu");
      assert.equal(await page.locator("[data-rail='page:graceui.about']").count(), 1);
      assert.equal(await page.locator("[data-rail='page:graceui.hotkeys']").count(), 0);  // ships in 0.6
      assert.equal(await page.locator("[data-rail='page:graceui.compatibility']").count(), 0);
    } finally {
      await close();
    }
  });

  test("under 1100 px the rail collapses to its marks, each opening a flyout of its pages", async () => {
    const { page, close } = await fresh(builtinSpaces(), { width: 1000, height: 760 });
    try {
      assert.equal(await page.locator(".rail[data-collapsed]").count(), 1);
      assert.equal(await page.locator(".mark").count(), 3);
      await page.locator("[data-rail='category:records']").click();
      await page.locator(".flyout").waitFor();
      assert.equal(await page.locator(".flyout .flyout__page").count(), 2);
      await page.waitForFunction(() => document.activeElement?.classList.contains("flyout__page") === true);
      await page.keyboard.press("Escape");
      await page.waitForFunction(() => document.querySelector(".flyout") === null);
      assert.equal(await page.evaluate(() => document.activeElement?.getAttribute("data-rail")), "category:records");
      await page.keyboard.press("Enter");
      await page.locator(".flyout [data-rail='page:demo.log']").click();
      await page.locator("main[data-page='demo.log']").waitFor();
    } finally {
      await close();
    }
  });

  test("from two content spaces a switcher heads the rail; a space's status topic lights the lamp", async () => {
    const second = space("demo");
    second.pages = { ...second.pages, space: "demo-b", name: "Second demo",
                     topics: [...second.pages.topics, { id: "status", shape: "state" }] };
    second.topics = { ...second.topics, status: { word: "Ready", tone: "moss", reason: "The lantern is lit." } };
    const { page, problems, close } = await fresh([...builtinSpaces(), second]);
    try {
      assert.equal(await page.locator(".switcher [role=radio]").count(), 2);
      assert.equal(await page.locator(".lamp").count(), 0, "the demo space publishes no status");
      await page.locator("[data-space-switch='demo-b']").click();
      await page.locator("main[data-space='demo-b']").waitFor();
      assert.equal((await page.locator(".wordmark").textContent())?.trim(), "Second demo");
      await page.locator(".lamp").waitFor();
      assert.match((await page.locator(".lamp").textContent()) ?? "", /Ready/);
      await page.locator("#page-title").focus();
      await page.keyboard.press("Control+PageDown");
      await page.locator("main[data-space='demo']").waitFor();
      await page.keyboard.press("/");
      await page.keyboard.type("brightness");
      await page.locator("#search-results .result__space").first().waitFor();
      assert.ok((await page.locator("#search-results .result__space").count()) >= 2, "each result badged with its space");
      assert.deepEqual(problems, []);
    } finally {
      await close();
    }
  });
});
