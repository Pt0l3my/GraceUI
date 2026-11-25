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
import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import type { FakeHost } from "../support/fake-host.ts";
import { startHost } from "../support/fake-host.ts";
import type { PwBrowser, PwPage } from "../support/browser.ts";
import { DIST, launch, lowContrast, openFromRail, openPage, unfoldAll, violations } from "../support/browser.ts";
import { builtinSpaces, goldenLines, space } from "../support/fixtures.ts";
import type { FixtureSpaceData } from "../../src/transport/fixture-space.ts";

const built = existsSync(join(DIST, "index.html"));


function allPages(): { space: string; category: string; page: string; name: string; tabs: number }[] {
  // Every page in game too: a browser-only one (D258) has no line in grace-view's golden, and opens behind its gate.
  return builtinSpaces().flatMap((s) => s.pages.pages.filter((p) => p.browser_only !== true).map((p) => ({
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
      assert.match((await page.locator("main .empty__line").textContent()) ?? "", /Nothing to show\./);
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
        if (await page.locator(".master").count() > 0) {
          // A master page's detail, its tabs among it, once its first row is chosen (D215).
          await page.locator(".master__row[aria-selected=true]").waitFor();
        }
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
      await page.locator("main[data-page='demo.keepers']").waitFor();
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

  test("a table's switch shows what the space published and sends its command; a refusal shows its reason (D181)",
       async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "graceui", "graceui.mods");
      const tide = page.locator("[role=switch][aria-label='Notices: Tide Charts']");
      const camp = page.locator("[role=switch][aria-label='Notices: Quiet Camp']");
      assert.equal(await tide.getAttribute("aria-checked"), "true");
      assert.equal(await camp.getAttribute("aria-checked"), "false");
      assert.match((await camp.locator("xpath=..").textContent()) ?? "", /Quiet/);
      const sent = () => host.received.filter((f) => f.t === "cmd" && f.name === "mods.notices");
      await tide.click();
      for (let i = 0; i < 20 && sent().length === 0; ++i) {
        await page.waitForTimeout(50);
      }
      assert.equal(sent().length, 1);
      assert.deepEqual(sent()[0]?.args, { id: "tide-charts", on: false });
      await page.waitForTimeout(100);
      // Accepted, but the space published nothing new: the switch is never flipped by the view (invariant 1).
      assert.equal(await tide.getAttribute("aria-checked"), "true");
      assert.equal(await page.locator(".table__notice").count(), 0);
      host.refuse.set("mods.notices", "not_now");
      await camp.press("ArrowRight");
      await page.locator(".table__notice[role=alert]").waitFor();
      assert.match((await page.locator(".table__notice").textContent()) ?? "", /Not possible right now/);
      assert.deepEqual(sent()[1]?.args, { id: "quiet-camp", on: true });
    } finally {
      host.refuse.delete("mods.notices");
      await close();
    }
  });

  test("G37 in the browser: a row's key captured, a taken one asked about then moved, a chord from two pickers; the " +
       "menu's row takes no actions (D208, D209)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "graceui", "graceui.hotkeys");
      const rows = page.locator("main table.table tbody tr");
      assert.equal(await rows.nth(0).locator("[data-row-action]").count(), 0, "the menu's own row: no id, no actions");
      const sent = () => host.received.filter((f) => f.t === "cmd" && f.name === "hotkeys.key");
      // A free key: sent with the row's id, and nothing asked.
      await rows.nth(2).locator("[data-row-action='key']").click();
      const field = page.locator("dialog.modal[open] [data-capture='key']");
      await field.waitFor();
      assert.match((await page.locator("dialog.modal[open] .modal__title").textContent()) ?? "",
                   /Change key: Tide Charts, Show the tide table/);
      await field.press("F9");
      for (let i = 0; i < 20 && sent().length === 0; ++i) {
        await page.waitForTimeout(50);
      }
      assert.deepEqual(sent()[0]?.args, { id: "tide-charts/tide-table", value: "F9" });
      await page.waitForFunction(() => document.querySelector("dialog.modal[open]") === null);
      // A key another row has: the space's question, then the same again with its confirmation.
      await rows.nth(2).locator("[data-row-action='key']").click();
      await field.waitFor();
      await field.press("Control+T");  // its own key: nothing to ask
      await page.waitForFunction(() => document.querySelector("dialog.modal[open]") === null);
      await rows.nth(2).locator("[data-row-action='key']").click();
      await field.waitFor();
      await field.press("T");
      await page.locator("dialog.modal[open] [data-confirm]").waitFor();
      assert.match((await page.locator("dialog.modal[open] .prose").textContent()) ?? "",
                   /T is Demo space's Trim the wick\. Use it here/);
      await page.locator("dialog.modal[open] [data-confirm]").click();
      for (let i = 0; i < 20 && sent().length < 4; ++i) {
        await page.waitForTimeout(50);
      }
      assert.deepEqual(sent()[1]?.args, { id: "tide-charts/tide-table", value: "Ctrl+T" });
      assert.deepEqual(sent()[2]?.args, { id: "tide-charts/tide-table", value: "T" });
      assert.deepEqual(sent()[3]?.args, { id: "tide-charts/tide-table", value: "T", confirm: true });
      // A pad chord from two pickers; the same button twice is refused in words; no pad chord sends 0.
      const chords = () => host.received.filter((f) => f.t === "cmd" && f.name === "hotkeys.chord");
      await rows.nth(3).locator("[data-row-action='chord']").click();
      await page.locator("dialog.modal[open] [data-capture='anchor']").waitFor();
      await page.locator("dialog.modal[open] [data-capture='anchor']").selectOption("12");
      await page.locator("dialog.modal[open] [data-capture-use]").click();
      await page.locator("dialog.modal[open] .row__note[role=status]").waitFor();
      assert.equal(chords().length, 0);
      await page.locator("dialog.modal[open] [data-capture='anchor']").selectOption("1");
      await page.locator("dialog.modal[open] [data-capture-use]").click();
      for (let i = 0; i < 20 && chords().length === 0; ++i) {
        await page.waitForTimeout(50);
      }
      assert.deepEqual(chords()[0]?.args, { id: "quiet-camp/pitch-tent", value: 0x0002 * 0x10000 + 0x0020 });
      await rows.nth(3).locator("[data-row-action='chord']").click();
      await page.locator("dialog.modal[open] [data-capture-none]").click();
      for (let i = 0; i < 20 && chords().length < 2; ++i) {
        await page.waitForTimeout(50);
      }
      assert.deepEqual(chords()[1]?.args, { id: "quiet-camp/pitch-tent", value: 0 });
    } finally {
      await close();
    }
  });

  test("master and detail in the browser: the first row chosen, its member watched and drawn, ↓ the next, Enter into " +
       "the detail, no key in the address (D215)", async () => {
    const { page, close } = await fresh();
    const subs = () => host.received.filter((f) => f.t === "sub" && typeof f.topic === "string" &&
                                                   f.topic.startsWith("lantern.lamp/")).map((f) => f.topic);
    const unsubs = () => host.received.filter((f) => f.t === "unsub" && typeof f.topic === "string" &&
                                                     f.topic.startsWith("lantern.lamp/")).map((f) => f.topic);
    try {
      await openFromRail(page, "records", "demo.lamps");
      await page.locator(".master__row[aria-selected=true]").waitFor();
      assert.equal(await page.locator(".master__row").count(), 3);
      assert.equal(await page.locator(".master__row[aria-selected=true]").getAttribute("data-key"), "harbour-mouth");
      await page.locator(".tile__value", { hasText: "Tall" }).waitFor();
      assert.deepEqual(subs(), ["lantern.lamp/harbour-mouth"]);
      // ↓ chooses the next row: its member watched, the last one let go, its tiles drawn.
      await page.locator(".master__row[aria-selected=true]").focus();
      await page.keyboard.press("ArrowDown");
      await page.locator(".master__row[data-key='fish-steps'][aria-selected=true]").waitFor();
      await page.locator(".tile__value", { hasText: "Low" }).waitFor();
      for (let i = 0; i < 20 && unsubs().length === 0; ++i) {
        await page.waitForTimeout(50);
      }
      assert.deepEqual(subs(), ["lantern.lamp/harbour-mouth", "lantern.lamp/fish-steps"]);
      assert.deepEqual(unsubs(), ["lantern.lamp/harbour-mouth"]);
      // Enter goes into the detail; nothing of the row's key is in the address or the title.
      await page.keyboard.press("Enter");
      const inDetail = await page.evaluate(() => document.getElementById(document.activeElement?.closest(".detail")?.id ?? "") !== null);
      assert.equal(inDetail, true);
      const where = await page.evaluate(() => location.href + " " + document.title);
      assert.equal(where.includes("fish-steps"), false);
      // The Notes tab draws the same member's sentence.
      await page.getByRole("tab", { name: "Notes", exact: true }).click();
      await page.locator(".sentence", { hasText: "trimmed short on purpose" }).waitFor();
    } finally {
      await close();
    }
  });

  test("the editor in the browser: fields drawn, an edit Unsaved and kept per row, check's messages under their " +
       "fields, a save, a draft's Use this, prose one paragraph (D217)", async () => {
    const { page, close } = await fresh();
    const cmds = (name: string) => host.received.filter((f) => f.t === "cmd" && f.name === name);
    try {
      await openFromRail(page, "records", "demo.lamps");
      await page.locator(".master__row[aria-selected=true]").waitFor();
      await page.locator("[role=tab]", { hasText: "Your notes" }).click();
      const editor = page.locator(".editor[data-editor='lantern.note/harbour-mouth']");
      await editor.waitFor();
      const brief = editor.locator(".editor__field[data-field=brief] input");
      assert.equal(await brief.inputValue(), "Mouth lamp");
      assert.equal(await editor.locator(".editor__field[data-field='public.summary'] .editor__count").textContent(),
                   "19 / 35 words");
      assert.equal(await editor.locator(".editor__field[data-field=sayings] .editor__item").count(), 3);
      assert.equal(await editor.locator(".editor__field[data-field=sayings] .editor__locked").textContent(),
                   "Locked: Written by hand: change it in the file.");
      await editor.locator(".editor__field[data-field=oil] option", { hasText: "Olive oil" }).waitFor({ state: "attached" });
      assert.equal(await editor.locator(".editor__field[data-field=met] .editor__shown").textContent(),
                   "Harbour mouth, on the first evening");
      // An edit: Unsaved, its count live, held to its 24 characters.
      await brief.fill("Mouth lamp of the harbour wall, the far one");
      assert.equal(await brief.inputValue(), "Mouth lamp of the harbou");
      await editor.locator(".editor__head .editor__unsaved", { hasText: "Unsaved changes" }).waitFor();
      // Prose is one paragraph: Enter types nothing, a pasted line break is a space.
      const summary = editor.locator(".editor__field[data-field='public.summary'] textarea");
      await summary.focus();
      await page.keyboard.press("End");
      await page.keyboard.press("Enter");
      await page.keyboard.type(" Green.");
      assert.equal((await summary.inputValue()).includes("\n"), false);
      // The edit is kept per row: another lamp, then back.
      await page.locator(".master__row[data-key='fish-steps']").click();
      await page.locator(".editor[data-editor='lantern.note/fish-steps']").waitFor();
      await page.locator(".editor__why", { hasText: "The drafter is resting" }).waitFor();
      await page.locator(".master__row[data-key='harbour-mouth']").click();
      await editor.waitFor();
      assert.equal(await brief.inputValue(), "Mouth lamp of the harbou");
      // Check: the space's messages under their fields, in its words.
      await editor.locator("[data-editor-part=check]").click();
      await editor.locator(".editor__top, .editor__message").first().waitFor();
      assert.equal(cmds("note.check").length, 1);
      assert.equal((cmds("note.check")[0]?.args as { rev: number }).rev, 1);
      // Save: sent with its revision; the space publishes it again, and Unsaved goes.
      await editor.locator("[data-editor-part=save]").click();
      await editor.locator(".editor__state", { hasText: "Saved" }).waitFor();
      for (let i = 0; i < 20 && await editor.locator(".editor__head .editor__unsaved").count() > 0; ++i) {
        await page.waitForTimeout(50);
      }
      assert.equal(await editor.locator(".editor__head .editor__unsaved").count(), 0);
      assert.equal(await brief.inputValue(), "Mouth lamp of the harbou");
      const saved = cmds("note.save")[0]?.args as { rev: number; doc: { [k: string]: unknown } };
      assert.equal(saved.rev, 1);
      assert.equal("met" in saved.doc, false);
      // A draft: the notes sent, its proposal beside the field; Use this makes it an unsaved edit.
      await editor.locator(".editor__draft textarea").fill("tall and green");
      await editor.locator("[data-editor-part=draft]").click();
      await editor.locator("[data-proposal=brief]").waitFor();
      assert.equal(await editor.locator("[data-proposal=brief] .editor__proposal-words").textContent(), "tall and green");
      await editor.locator("[data-use=brief]").click();
      assert.equal(await brief.inputValue(), "tall and green");
      await editor.locator(".editor__head .editor__unsaved").waitFor();
      assert.equal(await editor.locator("[data-proposal=brief]").count(), 0);
      await editor.locator("[data-keep='public.summary']").click();
      assert.equal(await editor.locator(".editor__proposal").count(), 0);
      // A save whose answer names another row (D218): the list chooses it, and Save keeps focus once its note comes.
      host.bodies.set("note.save", { chosen: "old-crane" });
      await editor.locator("[data-editor-part=save]").click();
      await page.locator(".master__row[data-key='old-crane'][aria-selected=true]").waitFor();
      await page.locator(".editor[data-editor='lantern.note/old-crane']").waitFor();
      for (let i = 0; i < 20 && await page.evaluate(() => document.activeElement?.getAttribute("data-editor-part")) !== "save"; ++i) {
        await page.waitForTimeout(50);
      }
      assert.equal(await page.evaluate(() => document.activeElement?.closest(".editor")?.getAttribute("data-editor")),
                   "lantern.note/old-crane");
      host.bodies.delete("note.save");
    } finally {
      await close();
    }
  });

  test("proposal 0007 in the browser: a row's action asks first, a row's secret is sent once and kept nowhere, a " +
       "form sends its fields, a picker offers the space's choices (D192 to D195)", async () => {
    const { page, close } = await fresh();
    const named = (name: string) => () => host.received.filter((f) => f.t === "cmd" && f.name === name);
    const settle = async (got: () => unknown[], n: number) => {
      for (let i = 0; i < 40 && got().length < n; ++i) {
        await page.waitForTimeout(50);
      }
    };
    try {
      await openFromRail(page, "demo", "demo.keepers");
      // A row action: a destructive one opens the confirmation; Cancel sends nothing; its button sends the row's key.
      const dismiss = named("keepers.dismiss");
      const first = page.locator("[data-row-action='dismiss']").first();
      assert.equal(await first.getAttribute("aria-label"), "Dismiss: Orla Venn");
      await first.click();
      await page.locator("dialog[open]").waitFor();
      assert.match((await page.locator("dialog[open] .prose").textContent()) ?? "", /leaves the roster/);
      await page.locator("dialog[open] .button--secondary").click();
      await page.waitForTimeout(100);
      assert.equal(dismiss().length, 0);
      await first.click();
      await page.locator("dialog[open] [data-confirm='dismiss']").click();
      await settle(dismiss, 1);
      assert.deepEqual(dismiss()[0]?.args, { id: "k1", confirm: true });
      // A row secret: its status in words, the value sent once, the field cleared, nothing kept (invariant 5).
      const ward = named("keepers.ward");
      const cells = page.locator(".table__secret");
      assert.match((await cells.nth(0).textContent()) ?? "", /Set/);
      assert.match((await cells.nth(1).textContent()) ?? "", /Missing/);
      assert.match((await cells.nth(2).textContent()) ?? "", /Not needed/);  // a row that needs none (D197)
      assert.equal(await cells.nth(2).locator("input").count(), 0);
      const field = cells.nth(1).locator("input");
      await field.fill("an-invented-word");
      await field.press("Enter");
      await settle(ward, 1);
      assert.deepEqual(ward()[0]?.args, { id: "k2", value: "an-invented-word" });
      assert.equal(await field.inputValue(), "");
      assert.equal((await page.content()).includes("an-invented-word"), false);
      const stored = await page.evaluate(() => JSON.stringify({ ...localStorage }) + JSON.stringify({ ...sessionStorage }));
      assert.equal(stored.includes("an-invented-word"), false);
      // A form: its fields sent together, cleared once accepted; a refusal's words under it, the fields kept.
      const hire = named("keepers.hire");
      const inputs = page.locator(".form input");
      await inputs.nth(0).fill("Wren Halloway");
      await inputs.nth(1).fill("South jetty");
      // Its prose field (D251): several lines in a textarea, sent with the rest.
      await page.locator(".form textarea[data-prose]").fill("Knows the tide.\nSteady in wind.");
      await page.locator("[data-form='keepers.hire']").click();
      await settle(hire, 1);
      assert.deepEqual(hire()[0]?.args, { name: "Wren Halloway", post: "South jetty",
                                          notes: "Knows the tide.\nSteady in wind." });
      await page.waitForFunction(() => (document.querySelector<HTMLInputElement>(".form input")?.value ?? "x") === "");
      host.refuse.set("keepers.hire", "invalid_value");
      await inputs.nth(0).fill("Wren Halloway");
      await page.locator("[data-form='keepers.hire']").click();
      await page.locator(".form .row__refusal[role=alert]").waitFor();
      assert.equal(await inputs.nth(0).inputValue(), "Wren Halloway");
      // A picker: the space's choices, its value one of them; a choice sends settings.set.
      const picker = page.locator("select.picker");
      assert.deepEqual(await picker.locator("option").allTextContents(), ["Rapeseed oil", "Olive oil", "Tallow"]);
      assert.equal(await picker.inputValue(), "rapeseed");
      await picker.selectOption("olive");
      const set = () => host.received.filter((f) => f.t === "cmd" && f.name === "settings.set" &&
                                                (f.args as { key?: string } | undefined)?.key === "lantern.oil_kind");
      await settle(set, 1);
      assert.deepEqual(set()[0]?.args, { key: "lantern.oil_kind", value: "olive" });
      assert.deepEqual(await violations(page), []);
    } finally {
      host.refuse.delete("keepers.hire");
      await close();
    }
  });

  test("an upload in the browser: a roster file chosen or dropped is sent once with its ticket, its bytes and hash " +
       "checked, its name never sent; a wrong kind, the space's refusal and a paired device in words (G28)", async () => {
    const { page, problems, close } = await fresh();
    const frames = () => host.received.filter((f) => f.t === "upload");
    const posts = () => host.requests.filter((r) => r === "POST /upload");
    const settle = async (got: () => unknown[], n: number) => {
      for (let i = 0; i < 60 && got().length < n; ++i) {
        await page.waitForTimeout(50);
      }
    };
    const shows = (selector: string, words: string) => page.waitForFunction(
      (a: { selector: string; words: string }) => (document.querySelector(a.selector)?.textContent ?? "").includes(a.words),
      { selector, words }, { timeout: 5000 });
    const zoneOf = "[data-upload='keepers.import']";
    const roster = Buffer.from("[[keeper]]\nname = \"Maren Holt\"\n\n[[keeper]]\nname = \"Tobin Reed\"\n", "utf8");
    try {
      await openFromRail(page, "demo", "demo.keepers");
      const zone = page.locator(zoneOf);
      await zone.waitFor();
      assert.match((await zone.locator(".upload__hint").textContent()) ?? "", /Drop a \.toml or \.txt file here/);
      assert.equal(await zone.locator("button").textContent(), "Add a roster file");
      // Chosen with its button's input: one upload frame, its ticket, one POST, the space's answer in words.
      await zone.locator("input[type=file]").setInputFiles({ name: "harbour-roster.toml", mimeType: "text/plain",
                                                              buffer: roster });
      await shows(`${zoneOf} .upload__status`, "Accepted");
      assert.equal(await zone.locator(".upload__status").textContent(), "Accepted: harbour-roster.toml.");
      assert.equal(frames().length, 1);
      const sent = frames()[0];
      assert.equal(sent?.size, roster.length);
      assert.equal(sent?.sha256, createHash("sha256").update(roster).digest("hex"));
      assert.deepEqual(sent?.args, {});
      assert.deepEqual(host.uploads.map((u) => [u.name, u.bytes.equals(roster)]), [["keepers.import", true]]);
      assert.equal(posts().length, 1);
      assert.ok(!JSON.stringify(host.received).includes("harbour-roster"), "the file's name stays in the page");
      // A wrong kind is refused here, before anything is sent.
      await zone.locator("input[type=file]").setInputFiles({ name: "harbour.png", mimeType: "image/png",
                                                              buffer: Buffer.from([1, 2, 3]) });
      await shows(`${zoneOf} .row__refusal`, "Choose a .toml or .txt file.");
      assert.equal(frames().length, 1);
      // Dropped on the zone: the space's own refusal in its words (no keeper in it).
      await page.evaluate((selector: string) => {
        const target = document.querySelector(selector);
        const data = new DataTransfer();
        data.items.add(new File(["# nothing here\n"], "empty-roster.txt", { type: "text/plain" }));
        target?.dispatchEvent(new DragEvent("dragover", { dataTransfer: data, bubbles: true, cancelable: true }));
        target?.dispatchEvent(new DragEvent("drop", { dataTransfer: data, bubbles: true, cancelable: true }));
      }, zoneOf);
      await shows(`${zoneOf} .row__refusal`, "No keeper in that file.");
      assert.equal(frames().length, 2);
      assert.equal(host.uploads.length, 2);
      // Refused by the space before its ticket: nothing is posted.
      host.refuse.set("keepers.import", "not_now");
      await zone.locator("input[type=file]").setInputFiles({ name: "harbour-roster.toml", mimeType: "text/plain",
                                                              buffer: roster });
      await settle(frames, 3);
      await shows(`${zoneOf} .row__refusal`, "Not possible right now.");
      assert.equal(posts().length, 2);
      assert.equal(host.uploads.length, 2);
      assert.deepEqual(problems, []);
      assert.deepEqual(await violations(page), []);
    } finally {
      await close();
    }
    // A paired device is told where to add the file, and offered nothing to send.
    host = await startHost(DIST, builtinSpaces());
    host.setView("device");
    const device = await openPage(browser, host);
    try {
      await openFromRail(device.page, "demo", "demo.keepers");
      const zone = device.page.locator(zoneOf);
      await zone.waitFor();
      assert.match((await zone.textContent()) ?? "", /only on the PC the game runs on/);
      assert.equal(await zone.locator("input, button").count(), 0);
    } finally {
      await device.close();
      await host.close();
    }
  });

  test("proposal 0008 in the browser: a section's chip, live status and own actions, a destructive one asking " +
       "first (D202)", async () => {
    const { page, close } = await fresh();
    const named = (name: string) => () => host.received.filter((f) => f.t === "cmd" && f.name === name);
    const settle = async (got: () => unknown[], n: number) => {
      for (let i = 0; i < 40 && got().length < n; ++i) {
        await page.waitForTimeout(50);
      }
    };
    try {
      await openFromRail(page, "demo", "demo.keepers");
      const oil = page.locator("[data-section='oil']");
      assert.equal((await oil.locator(".card__chip").textContent())?.trim(), "store");
      await oil.locator(".card__status").waitFor();
      assert.match((await oil.locator(".card__status").textContent()) ?? "", /Low/);
      assert.equal(await oil.locator(".card__status .lamp__dot--ember").count(), 1);
      // Not destructive: at once, with no confirmation.
      const order = named("oil.order");
      await oil.locator("[data-section-action='order']").click();
      await settle(order, 1);
      assert.deepEqual(order()[0]?.args, {});
      // Destructive: its confirmation first; Cancel sends nothing; its button sends "confirm": true.
      const all = named("keepers.dismiss_all");
      const roster = page.locator("[data-section='roster']");
      await roster.locator("[data-section-action='dismiss_all']").click();
      await page.locator("dialog[open]").waitFor();
      assert.match((await page.locator("dialog[open] .prose").textContent()) ?? "", /Every keeper leaves/);
      await page.locator("dialog[open] .button--secondary").click();
      await page.waitForTimeout(100);
      assert.equal(all().length, 0);
      await roster.locator("[data-section-action='dismiss_all']").click();
      await page.locator("dialog[open] [data-confirm='dismiss_all']").click();
      await settle(all, 1);
      assert.deepEqual(all()[0]?.args, { confirm: true });
      assert.deepEqual(await violations(page), []);
    } finally {
      await close();
    }
  });

  test("proposal 0006 in the browser: a block's field and empty words, a newest-first log tail, a greyed page, a " +
       "shortcut's control (D185 to D189)", async () => {
    // The demo space with the five: its overview's tiles from a member, its sentence's own empty words; its log tail
    // newest first; a space.state greying Behaviour.
    const demo = space("demo");
    const pages = JSON.parse(JSON.stringify(demo.pages)) as typeof demo.pages;
    const overview = pages.pages.find((p) => p.id === "demo.overview");
    const blocks = overview?.sections?.[0]?.blocks ?? [];
    blocks[0] = { ...blocks[0], kind: "component", props: { field: "now" } } as (typeof blocks)[number];
    blocks[1] = { ...blocks[1], kind: "component", props: { empty: "All quiet by the lantern." } } as (typeof blocks)[number];
    const log = pages.pages.find((p) => p.id === "demo.log");
    const journal = log?.sections?.[1]?.blocks?.[0];
    if (journal) {
      journal.props = { newest_first: true };
    }
    pages.topics.push({ id: "space.state", shape: "state" });
    const status = (demo.topics ?? {})["lantern.status"] as { tiles: unknown; sentence?: string };
    const data = {
      ...demo,
      pages,
      topics: {
        ...demo.topics,
        "lantern.status": { now: { tiles: status.tiles } },
        "space.state": { greyed: { "demo.behaviour": "Light the lantern first." } },
      },
    };
    const { page, close } = await fresh([space("graceui"), data]);
    try {
      await openFromRail(page, "lanterns", "demo.overview");
      await page.locator(".tile").first().waitFor();
      assert.ok((await page.locator(".tile").count()) >= 1);  // the tiles from the member `now`
      assert.match((await page.locator(".empty__line").first().textContent()) ?? "", /All quiet by the lantern\./);
      // The shortcut: its home's control and its link; Behaviour being greyed, the control is inert.
      const shortcut = page.locator("[data-row='shortcut:lantern.brightness']");
      assert.equal(await shortcut.locator("[role=slider]").count(), 1);
      assert.equal(await shortcut.locator("[role=slider]").getAttribute("aria-disabled"), "true");
      assert.equal(await shortcut.locator(".lives-in").count(), 1);
      await openFromRail(page, "lanterns", "demo.behaviour");
      assert.match((await page.locator("[data-greyed]").textContent()) ?? "", /Light the lantern first\./);
      assert.equal(await page.locator("[data-row='setting:lantern.enabled'] [role=switch]").getAttribute("aria-disabled"),
                   "true");
      const sent = host.received.filter((f) => f.t === "cmd").length;
      await page.locator("[data-row='setting:lantern.enabled'] [role=switch]").click({ force: true });
      await page.waitForTimeout(150);
      assert.equal(host.received.filter((f) => f.t === "cmd").length, sent);  // nothing sent from a greyed page
      // The journal, a log tail, newest first.
      const rows = (demo.topics ?? {})["lantern.journal"] as { line: string }[];
      await openFromRail(page, "records", "demo.log");
      await page.locator("[data-section='journal'] .card__toggle").click();
      await page.locator("[data-section='journal'] .feed__line").first().waitFor();
      assert.equal(await page.locator("[data-section='journal'] .feed__line").first().textContent(),
                   rows[rows.length - 1]?.line);
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

  test("a slider's typed entry: Enter or its value opens a field; Enter sets, the bounds in words, Escape drops (D266)",
       async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "lanterns", "demo.behaviour");
      const cmds = () => host.received.filter((f) => f.t === "cmd").map((f) => f.args);
      const row = "[data-row='setting:lantern.brightness']";
      const track = `${row} [role=slider]`;
      const entry = `${row} [data-entry='lantern.brightness']`;
      const valued = (text: string) => page.waitForFunction(([sel, want]) =>
        document.querySelector(sel!)?.getAttribute("aria-valuetext") === want, [track, text]);
      // Enter on the track: the field with the value at its step's decimals, all of it chosen, focused.
      await page.locator(track).focus();
      await page.keyboard.press("Enter");
      await page.locator(entry).waitFor();
      assert.equal(await page.locator(entry).inputValue(), "0.80");
      assert.equal(await page.evaluate((sel) => {
        const el = document.querySelector(sel) as HTMLInputElement;
        return document.activeElement === el && el.selectionStart === 0 && el.selectionEnd === el.value.length;
      }, entry), true);
      // What is typed replaces it, snapped to its step; focus goes back to the track.
      await page.keyboard.type("0.333");
      await page.keyboard.press("Enter");
      await valued("0.35");
      assert.equal(await page.evaluate(() => document.activeElement?.getAttribute("role")), "slider");
      // A click on the value opens it too. Outside the bounds: nothing sent, the bounds under the row in words.
      await page.locator(`${row} [data-value='lantern.brightness']`).click();
      await page.locator(entry).waitFor();
      await page.keyboard.type("2");
      await page.keyboard.press("Enter");
      await page.locator(`${row} .row__refusal`).waitFor();
      assert.match(await page.locator(`${row} .row__refusal`).innerText(), /Type a number from 0\.00 to 1\.00\./);
      // A new try clears the words; Escape drops what was typed.
      await page.keyboard.press("Enter");
      await page.locator(entry).waitFor();
      assert.equal(await page.locator(`${row} .row__refusal`).count(), 0);
      await page.keyboard.type("0.5");
      await page.keyboard.press("Escape");
      await page.locator(entry).waitFor({ state: "detached" });
      // Leaving the field keeps what was typed, as a click elsewhere does in game.
      await page.keyboard.press("Enter");
      await page.locator(entry).waitFor();
      await page.keyboard.type("0.6");
      await page.locator("[data-row='setting:lantern.enabled'] .row__label").click();
      await valued("0.60");
      for (let i = 0; i < 20 && cmds().length < 2; ++i) {
        await page.waitForTimeout(50);
      }
      assert.deepEqual(cmds(), [
        { key: "lantern.brightness", value: 0.35 },
        { key: "lantern.brightness", value: 0.6 },
      ]);
    } finally {
      await close();
    }
  });

  test("a page of a later increment is absent from the rail (§4.2.2, D173)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "graceui", "graceui.menu");
      assert.equal(await page.locator("[data-rail='page:graceui.about']").count(), 1);
      assert.equal(await page.locator("[data-rail='page:graceui.hotkeys']").count(), 1);  // 0.2 since D206
      assert.equal(await page.locator("[data-rail='page:graceui.compatibility']").count(), 0);  // ships in 0.6
    } finally {
      await close();
    }
  });

  test("a document: its blocks, links to a guide and to a heading, a link to none as text, its list's search (D251)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "records", "demo.handbook");
      await page.locator("article.doc").waitFor();
      assert.equal(await page.locator(".doc__title").textContent(), "Lighting the lamp");
      assert.equal(await page.locator(".doc ol.doc__list li").count(), 3);
      assert.equal(await page.locator(".doc strong").textContent(), "edge");
      assert.match((await page.locator(".doc__note").textContent()) ?? "", /Never leave a lit lamp alone/);
      // A link to another guide chooses its row; a link to a heading of a guide, likewise, to that heading.
      await page.locator(".doc__link[data-doc='oil']").click();
      await page.waitForFunction(() => document.querySelector(".doc__title")?.textContent === "Oil");
      // Focus on the chosen guide's row, where the reader now is, as in game.
      assert.equal(await page.evaluate(() => document.activeElement?.getAttribute("data-key")), "oil");
      assert.equal(await page.locator(".doc table.doc__table tbody tr").count(), 3);
      assert.match((await page.locator(".doc__code").textContent()) ?? "", /12 jars\n/);
      await page.locator(".master__row[data-key='storms']").click();
      await page.waitForFunction(() => document.querySelector(".doc__title")?.textContent === "Keeping a lamp in a storm");
      // A guide the list does not hold: its link is plain words.
      assert.equal(await page.locator(".doc__link[data-doc='signals']").count(), 0);
      assert.match((await page.locator(".doc").textContent()) ?? "", /Signals/);
      await page.locator(".doc__link[data-doc='lighting'][data-heading='Trimming']").click();
      await page.waitForFunction(() => document.querySelector(".doc__title")?.textContent === "Lighting the lamp");
      // At its heading: brought into view.
      await page.waitForFunction(() => {
        const h = [...document.querySelectorAll(".doc__h2, .doc__h3")].find((e) => e.textContent === "Trimming");
        const r = h?.getBoundingClientRect();
        return r !== undefined && r.top >= 0 && r.top < window.innerHeight;
      });
      // The list's own search finds a guide by a heading, never by its body.
      await page.locator(".master .filter-bar__search").fill("shutter");
      assert.deepEqual(await page.locator(".master__row .master__name").allTextContents(), ["Keeping a lamp in a storm"]);
      await page.locator(".master .filter-bar__search").fill("seal");  // in a body only
      await page.locator(".master .filter-bar__none").waitFor();
      assert.deepEqual(await violations(page), []);
    } finally {
      await close();
    }
  });

  test("a report preview: each part's fields, a code field's lines, a left-out part dimmed, a switch sends (D251)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "records", "demo.log");
      await page.locator(".preview").waitFor();
      assert.equal(await page.locator(".preview__part").count(), 3);
      assert.equal(await page.locator(".preview__part[data-part='notes'] .preview__fields").count(), 0);  // left out
      assert.equal(await page.locator(".preview__part.is-excluded").count(), 1);
      assert.match((await page.locator(".preview__part[data-part='log'] pre").textContent()) ?? "", /19:44.*\n19:48/s);
      assert.equal(await page.locator(".preview__part[data-part='summary'] [role=switch]").count(), 0);  // not optional
      // A field's own line breaks kept, its words wrapped, as in game.
      assert.equal(await page.locator(".preview__part[data-part='summary'] .kv__value").first()
        .evaluate((e) => getComputedStyle(e).whiteSpace), "pre-line");
      const sent = () => host.received.filter((f) => f.t === "cmd" && f.name === "report.part");
      await page.locator(".preview__part[data-part='log'] [role=switch]").click();
      await page.waitForFunction(() => true);
      for (let i = 0; i < 40 && sent().length === 0; ++i) {
        await page.waitForTimeout(50);
      }
      assert.deepEqual(sent()[0]?.args, { id: "log", on: false });
      assert.deepEqual(await violations(page), []);
    } finally {
      await close();
    }
  });

  test("quick switches: a panel under the top bar's button; a setting's switch and choice; a session switch (D252)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "lanterns", "demo.keepers");
      const button = page.locator(".topbar .quick > button");
      assert.equal(await button.getAttribute("aria-expanded"), "false");
      await button.click();
      await page.locator("#quick-switches").waitFor();
      assert.deepEqual(await page.locator(".quick__label").allTextContents(),
                       ["Lantern lit", "Flame colour", "Hush the bell for this session"]);
      assert.deepEqual(await page.locator(".quick__home").allTextContents(),
                       ["Lives in Behaviour", "Lives in Behaviour", "Lives in Overview"]);
      const sent = (name: string) => host.received.filter((f) => f.t === "cmd" && f.name === name);
      const until = async (done: () => boolean) => {
        for (let i = 0; i < 40 && !done(); ++i) {
          await page.waitForTimeout(50);
        }
      };
      // The bell: its state as the space published it; a flip sends {on}, and the space's next state shows it.
      const hush = page.locator(".quick__row").nth(2).locator("[role=switch]");
      await page.waitForFunction(() =>
        document.querySelectorAll(".quick__row")[2]?.querySelector("[role=switch]")?.getAttribute("aria-disabled") === null);
      assert.equal(await hush.getAttribute("aria-checked"), "false");
      await hush.click();
      await page.waitForFunction(() =>
        document.querySelectorAll(".quick__row")[2]?.querySelector("[role=switch]")?.getAttribute("aria-checked") === "true");
      assert.deepEqual(sent("bell.hush")[0]?.args, { on: true });
      // A setting's switch and its choice, as their rows send them.
      const before = sent("settings.set").length;
      await page.locator(".quick__row").nth(0).locator("[role=switch]").click();
      await until(() => sent("settings.set").length > before);
      assert.deepEqual(sent("settings.set")[before]?.args, { key: "lantern.enabled", value: false });
      await page.locator(".quick__select").selectOption("white");
      await until(() => sent("settings.set").length > before + 1);
      assert.deepEqual(sent("settings.set")[before + 1]?.args, { key: "lantern.colour", value: "white" });
      assert.deepEqual(await violations(page), []);
      // Esc closes it, focus back on its button; "Lives in" opens the page it lives in.
      await page.keyboard.press("Escape");
      assert.equal(await page.locator("#quick-switches").count(), 0);
      assert.equal(await page.evaluate(() => document.activeElement?.getAttribute("aria-controls")), "quick-switches");
      await button.click();
      await page.locator(".quick__home").nth(2).click();
      await page.waitForFunction(() => document.querySelector("#page")?.getAttribute("data-page") === "demo.overview");
      assert.equal(await page.locator("#quick-switches").count(), 0);
    } finally {
      await close();
    }
  });

  test("live updates: on a live page, Paused holds its topics and counts them; Live draws them (D252)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "lanterns", "demo.overview");
      await page.locator(".sentence").waitFor();
      const first = await page.locator(".sentence").textContent();
      const control = page.locator(".topbar__control[data-live]");
      assert.equal(await control.textContent(), "Updates: Live");
      await control.click();
      assert.equal(await control.textContent(), "Updates: Every 5 s");
      await control.click();
      assert.equal(await control.getAttribute("data-live"), "paused");
      host.push("demo", "lantern.status", { sentence: "The wind rose.", hushed: false });
      await page.waitForFunction(() => document.querySelector(".topbar__control[data-live]")?.textContent ===
                                       "Updates: Paused \u00B7 1 new");
      assert.equal(await page.locator(".sentence").textContent(), first);  // held
      await control.click();  // Live again: what came is drawn
      await page.waitForFunction(() => document.querySelector(".sentence")?.textContent === "The wind rose.");
      assert.equal(await control.textContent(), "Updates: Live");
      assert.deepEqual(await violations(page), []);
      // A page that is not live has no such control.
      await openFromRail(page, "lanterns", "demo.keepers");
      assert.equal(await page.locator(".topbar__control[data-live]").count(), 0);
    } finally {
      await close();
    }
  });

  test("a table's filter bar: its selects, its sort and direction, its search, Clear filters (G43, D250)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "lanterns", "demo.keepers");
      // The roster's first cell is its drawer's mark (D256), its second the keeper's name.
      const names = async () => page.locator(".table tbody tr:not(.table__drawer) td:nth-child(2)").allTextContents();
      await page.locator(".filter-bar select[data-filter='post']").waitFor();
      // Time kept, longest first: the space's own order here. The Post filter offers All and the posts the rows hold.
      assert.deepEqual(await names(), ["Orla Venn", "Tamsin Reed", "Ada Marsh"]);
      assert.deepEqual(await page.locator("select[data-filter='post'] option").allTextContents(),
                       ["All", "North light", "Harbour wall", "Store room"]);
      await page.locator("select[data-filter='post']").selectOption("Harbour wall");
      assert.deepEqual(await names(), ["Tamsin Reed"]);
      assert.equal(await page.locator(".filter-bar__count").textContent(), "1 of 3 shown");
      await page.locator("select[data-filter='post']").selectOption("");
      assert.equal(await page.locator(".filter-bar__count").count(), 0);
      // By name, then reversed.
      await page.locator("select[data-sort]").selectOption("1");
      assert.deepEqual(await names(), ["Ada Marsh", "Orla Venn", "Tamsin Reed"]);
      await page.locator(".filter-bar__direction").click();
      assert.deepEqual(await names(), ["Tamsin Reed", "Orla Venn", "Ada Marsh"]);
      // The search: nothing matching says so, and Clear filters brings every row back. Nothing went to the space.
      await page.locator(".filter-bar__search").fill("zz");
      await page.locator(".filter-bar__none").waitFor();
      assert.equal(await page.locator(".table").count(), 0);
      await page.locator("[data-clear-filters]").click();
      assert.equal((await names()).length, 3);
      assert.equal(await page.locator(".filter-bar__search").inputValue(), "");
      // A row's action still names its own row, in whatever order the rows show.
      assert.equal(await page.locator("[data-row-action='dismiss']").first().getAttribute("aria-label"),
                   "Dismiss: Tamsin Reed");
    } finally {
      await close();
    }
  });

  test("a master list's filter bar: its rows along the wall, and its search (D250)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "records", "demo.lamps");
      await page.locator(".master .filter-bar").waitFor();
      const lamps = async () => page.locator(".master__row .master__name").allTextContents();
      assert.deepEqual(await lamps(), ["Harbour mouth", "Fish steps", "Old crane"]);
      await page.locator(".master .filter-bar__search").fill("crane");
      assert.deepEqual(await lamps(), ["Old crane"]);
      assert.equal(await page.locator(".master .filter-bar__count").textContent(), "1 of 3 shown");
    } finally {
      await close();
    }
  });

  test("a scoped page: the season in the top bar, a list read a page at a time, Show more, a read-only season (D254)", async () => {
    const { page, close } = await fresh();
    const pages = () => host.received.filter((f) => f.t === "cmd" && f.name === "lightings.page")
      .map((f) => f.args as { [key: string]: unknown }).filter((a) => a.view === "list");
    try {
      await openFromRail(page, "records", "demo.lightings");
      const scope = page.locator("select[data-scope]");
      await scope.waitFor();
      assert.equal(await page.locator(".topbar__scope .topbar__label").textContent(), "Season");
      assert.deepEqual(await scope.locator("option").allTextContents(), ["This winter", "Last autumn"]);
      assert.equal(await scope.inputValue(), "winter");  // the space's current
      await page.locator(".master__row[aria-selected=true]").waitFor();
      const rows = page.locator(".master__row");
      assert.equal(await rows.count(), 25);
      assert.equal(await page.locator(".master__row[aria-selected=true] .master__name").textContent(), "Night 60, at dusk");
      assert.equal(await page.locator(".master .paging__total").textContent(), "40 in all");
      assert.deepEqual(pages()[0], { view: "list", sort: { key: "at_ms", direction: "descending" }, scope: "winter" });
      // The detail: the chosen lighting's member, and the season's (a family on a scoped page draws the scope's).
      await page.locator("[data-section='lighting'] .kv__value", { hasText: "Night 60, at dusk" }).waitFor();
      await page.locator("[data-section='season'] .kv__value", { hasText: "40" }).waitFor();
      // Show more: the next page below, then no more.
      await page.locator(".master [data-paging='more']").click();
      await page.waitForFunction(() => document.querySelectorAll(".master__row").length === 40);
      assert.equal(await page.locator(".master [data-paging='more']").count(), 0);
      assert.equal(pages().at(-1)?.cursor, "o25");
      // A filter goes to the space with the first page asked again: the North light's lightings alone.
      assert.deepEqual(await page.locator(".master select[data-filter='lamp'] option").allTextContents(),
                       ["All", "North light", "Harbour wall", "Store room"]);
      await page.locator(".master select[data-filter='lamp']").selectOption("North light");
      await page.waitForFunction(() => document.querySelectorAll(".master__row").length === 14);
      assert.deepEqual(pages().at(-1)?.filters, { lamp: "North light" });
      assert.equal(await page.locator(".master .filter-bar__count").count(), 0);  // the space filtered: no "of" line
      await page.locator(".master select[data-filter='lamp']").selectOption("");
      // Last autumn: its own lightings and season, and the page read only.
      await scope.selectOption("autumn");
      await page.locator("[data-greyed]").waitFor();
      assert.equal(await page.locator("[data-greyed]").textContent(), "Last autumn is read only.");
      await page.waitForFunction(() => document.querySelector(".master__row[aria-selected=true] .master__name")
        ?.textContent === "Night 20, at dusk");
      assert.equal(pages().at(-1)?.scope, "autumn");
      await page.locator("[data-section='season'] .kv__value", { hasText: "20" }).waitFor();
      assert.deepEqual(await violations(page), []);
      // A page with no scope has no chooser.
      await openFromRail(page, "records", "demo.lamps");
      assert.equal(await page.locator("select[data-scope]").count(), 0);
    } finally {
      await close();
    }
  });

  test("a table read a page at a time: its filter's choices from a topic, New rows past its first page (D254)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "records", "demo.log");
      const history = page.locator("[data-section='history']");
      await history.locator(".table tbody tr").first().waitFor();
      assert.equal(await history.locator(".table tbody tr").count(), 25);
      assert.equal(await history.locator(".paging__total").textContent(), "60 in all");
      assert.deepEqual(await history.locator("select[data-filter='lamp'] option").allTextContents(),
                       ["All", "North light", "Harbour wall", "Store room"]);
      // New rows: showing only its first page, a change of its topic asks for it again; past it, it says so.
      await history.locator("[data-paging='more']").click();
      await page.waitForFunction(() => document.querySelectorAll("[data-section='history'] .table tbody tr").length === 50);
      host.push("demo", "lantern.status", { sentence: "A lamp was lit.", hushed: false,
                                            lamps: ["North light", "Harbour wall", "Store room"] });
      await history.locator("[data-paging='fresh']").waitFor();
      assert.equal(await history.locator(".table tbody tr").count(), 50);  // nothing moved until asked
      await history.locator("[data-paging='fresh']").click();
      await page.waitForFunction(() => document.querySelectorAll("[data-section='history'] .table tbody tr").length === 25);
      // A refusal's words stand in for the rows; when its topic moves the space is asked again (as in game).
      host.refuse.set("lightings.page", "not_now");
      await history.locator(".filter-bar__search").fill("Tam");
      await history.locator(".empty__line", { hasText: "Not possible right now." }).waitFor();
      host.refuse.delete("lightings.page");
      host.push("demo", "lantern.status", { sentence: "Another lamp was lit.", hushed: false,
                                            lamps: ["North light", "Harbour wall", "Store room"] });
      await page.waitForFunction(() => document.querySelectorAll("[data-section='history'] .table tbody tr").length > 0);
      // Its search goes to the space once the typing stops.
      await history.locator(".filter-bar__search").fill("Ada");
      await page.waitForFunction(() => [...document.querySelectorAll("[data-section='history'] .table tbody tr")]
        .every((tr) => tr.textContent?.includes("Ada Marsh")));
      const last = host.received.filter((f) => f.t === "cmd" && f.name === "lightings.page").at(-1);
      assert.equal((last?.args as { search?: string } | undefined)?.search, "Ada");
    } finally {
      await close();
    }
  });

  test("a header switch shows the space's state and sends {on}; a row's drawer opens under it (D256)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "lanterns", "demo.overview");
      const hush = page.locator(".page__actions [data-action='hush']");
      await hush.waitFor();
      assert.equal(await hush.getAttribute("role"), "switch");
      assert.equal(await hush.getAttribute("aria-checked"), "false");  // the fixture's hushed: false
      await hush.click();
      await page.waitForFunction(() => document.querySelector(".page__actions [data-action='hush']")
        ?.getAttribute("aria-checked") === "true");  // the space published its new state
      const sent = host.received.filter((f) => f.t === "cmd" && f.name === "bell.hush").at(-1);
      assert.deepEqual(sent?.args, { on: true });
      // The keepers' roster: Notes is a drawer column, off the row until its drawer opens.
      await openFromRail(page, "lanterns", "demo.keepers");
      await page.locator("[data-drawer='k1']").waitFor();
      assert.equal(await page.locator(".table th", { hasText: "Notes" }).count(), 0);
      assert.equal(await page.locator(".drawer").count(), 0);
      await page.locator("[data-drawer='k1']").click();
      assert.equal(await page.locator("[data-drawer='k1']").getAttribute("aria-expanded"), "true");
      assert.match((await page.locator(".drawer__value").first().textContent()) ?? "", /Keeps the north light/);
      await page.locator("[data-drawer='k2']").click();  // one drawer open a table
      assert.equal(await page.locator(".drawer").count(), 1);
      assert.match((await page.locator(".drawer__value").first().textContent()) ?? "", /learning the tides/);
      assert.deepEqual(await violations(page), []);
    } finally {
      await close();
    }
  });

  test("a stream's filter bar: its level filter and its search over the rows it holds, newest first (D257)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "records", "demo.log");
      const log = page.locator("[data-section='log']");
      await log.locator(".feed__row").first().waitFor();
      const lines = async () => log.locator(".feed__line").allTextContents();
      assert.equal((await lines())[0], "The flame guttered in a draught, then steadied.");  // newest first
      assert.deepEqual(await log.locator("select[data-filter='level'] option").allTextContents(), ["All", "info", "warning"]);
      await log.locator("select[data-filter='level']").selectOption("warning");
      assert.deepEqual(await lines(), ["The flame guttered in a draught, then steadied."]);
      await log.locator("select[data-filter='level']").selectOption("");
      await log.locator(".filter-bar__search").fill("wick");
      assert.deepEqual(await lines(), ["The wick was trimmed."]);
      await log.locator(".filter-bar__search").fill("zz");
      await log.locator(".filter-bar__none").waitFor();
      assert.equal(await log.locator(".feed__row").count(), 0);
    } finally {
      await close();
    }
  });

  test("a filter's own choices on a stream's bar, in their order and words; no drawer on an empty row; a stream's bar " +
       "before its rows (D262)", async () => {
    const demo = space("demo");
    const pages = JSON.parse(JSON.stringify(demo.pages)) as typeof demo.pages;
    const tail = pages.pages.find((p) => p.id === "demo.log")?.sections?.[0]?.blocks?.[0];
    const level = tail?.filter_bar?.filters?.[0];
    assert.ok(level);
    level.choices = [{ value: "warning", label: "Warnings" }, { value: "info", label: "Notes" }];
    const rows = [
      { seq: 1, time: "19:02", line: "The lantern was lit.", level: "info" },
      { seq: 2, time: "19:16", line: "The glass cracked.", level: "error" },
      { seq: 3, time: "19:40", line: "The flame guttered in a draught.", level: "warning" },
    ];
    const { page, close } = await fresh([space("graceui"), { ...demo, pages, topics: { ...demo.topics, "lantern.log": rows } }]);
    try {
      await openFromRail(page, "records", "demo.log");
      const log = page.locator("[data-section='log']");
      await log.locator(".feed__row").first().waitFor();
      const lines = async () => log.locator(".feed__line").allTextContents();
      assert.deepEqual(await log.locator("select[data-filter='level'] option").allTextContents(), ["All", "Warnings", "Notes"]);
      assert.equal((await lines()).length, 3);  // All: the unlisted level's row too
      await log.locator("select[data-filter='level']").selectOption("info");
      assert.deepEqual(await lines(), ["The lantern was lit."]);
      // The keepers: Ada Marsh's notes are empty, so her row has no drawer, its mark's cell kept.
      await openFromRail(page, "lanterns", "demo.keepers");
      await page.locator("[data-drawer='k1']").waitFor();
      assert.equal(await page.locator("[data-drawer='k3']").count(), 0);
      assert.equal(await page.locator("tbody .table__mark").count(), 3);
      assert.deepEqual(await violations(page), []);
    } finally {
      await close();
    }
    // No rows yet: the stream's bar is there all the same, over its empty words.
    const { page: bare, close: closeBare } = await fresh([space("graceui"), { ...demo, topics: { ...demo.topics, "lantern.log": [] } }]);
    try {
      await openFromRail(bare, "records", "demo.log");
      const log = bare.locator("[data-section='log']");
      await log.locator("select[data-filter='level']").waitFor();
      assert.equal(await log.locator(".feed__row").count(), 0);
      assert.deepEqual(await log.locator("select[data-filter='level'] option").allTextContents(), ["All"]);
    } finally {
      await closeBare();
    }
  });

  test("a report's Copy and Save: the space's text on the clipboard, a download of it, nothing sent; greyed while " +
       "blocked; neither on a paired device (D263)", async () => {
    const demo = space("demo");
    const share = (demo.topics ?? {})["lantern.report"] as { share: { text: string; file: string } };
    const { page, requests, close } = await fresh();
    try {
      await page.context().grantPermissions(["clipboard-read", "clipboard-write"]);
      await openFromRail(page, "records", "demo.log");
      const bar = page.locator("[data-section='report'] [data-share]");
      await bar.waitFor();
      const copy = bar.locator("[data-share-part='copy']");
      const save = bar.locator("[data-share-part='save']");
      assert.equal(await copy.isDisabled(), false);
      await copy.click();
      await page.waitForFunction(() =>
        document.querySelector("[data-share-part='copy']")?.textContent === "Copied");
      assert.equal(await page.evaluate(() => navigator.clipboard.readText()), share.share.text);
      const before = requests.length;
      const [download] = await Promise.all([page.waitForEvent("download"), save.click()]);
      assert.equal(download.suggestedFilename(), share.share.file);
      const path = await download.path();
      assert.equal(readFileSync(path, "utf8"), share.share.text);
      assert.equal(host.received.filter((f) => f.t === "cmd" && f.name === "report.save").length, 0);  // no request
      assert.deepEqual(requests.slice(before).filter((u) => !u.startsWith("blob:")), []);
      assert.deepEqual(await violations(page), []);
      await page.waitForFunction(() =>
        document.querySelector("[data-share-part='copy']")?.textContent === "Copy");  // its two seconds over
    } finally {
      await close();
    }
    // Blocked: greyed, with the space's words.
    const blocked = { ...demo, topics: { ...demo.topics,
      "lantern.report": { ...(demo.topics ?? {})["lantern.report"] as object,
                          share: { blocked: "Remove the path in Your notes before sharing." } } } };
    const { page: greyedPage, close: closeGreyed } = await fresh([space("graceui"), blocked]);
    try {
      await openFromRail(greyedPage, "records", "demo.log");
      const bar = greyedPage.locator("[data-section='report'] [data-share]");
      await bar.waitFor();
      assert.equal(await bar.locator("[data-share-part='copy']").isDisabled(), true);
      assert.equal(await bar.locator("[data-share-part='save']").isDisabled(), true);
      assert.equal(await bar.locator(".preview__share-words").textContent(), "Remove the path in Your notes before sharing.");
      assert.deepEqual(await lowContrast(greyedPage), []);
    } finally {
      await closeGreyed();
    }
    // A paired device: the preview's parts, and neither button.
    host = await startHost(DIST, builtinSpaces());
    host.setView("device");
    const device = await openPage(browser, host);
    try {
      await openFromRail(device.page, "records", "demo.log");
      await device.page.locator("[data-section='report'] [data-part='summary']").waitFor();
      assert.equal(await device.page.locator("[data-share]").count(), 0);
    } finally {
      await device.close();
      await host.close();
    }
  });

  test("a page on this PC alone: absent from a paired device's view, present on this PC (D264)", async () => {
    host = await startHost(DIST, builtinSpaces());
    host.setView("device");
    const device = await openPage(browser, host);
    try {
      await openFromRail(device.page, "records", "demo.log");  // its category open, its pages listed
      if ((await device.page.locator("[data-rail='page:demo.log']").count()) === 0) {
        await device.page.locator("[data-rail='category:records']").first().click();
      }
      await device.page.locator("[data-rail='page:demo.log']").first().waitFor({ state: "attached" });
      assert.equal(await device.page.locator("[data-rail='page:demo.workbench']").count(), 0);
      assert.equal(await device.page.locator(".related a, .related button", { hasText: "Workbench" }).count(), 0);
    } finally {
      await device.close();
      await host.close();
    }
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "records", "demo.workbench");
      await page.locator("[data-gate='open']").waitFor();
    } finally {
      await close();
    }
  });

  test("a gated page: its words once a session before anything shows, its ribbon, and present only with its setting (D258)", async () => {
    const { page, close } = await fresh();
    try {
      await openFromRail(page, "records", "demo.workbench");
      await page.locator("[data-gate='open']").waitFor();
      assert.match((await page.locator(".gate").textContent()) ?? "", /raw readings, unworded/);
      assert.equal(await page.locator(".tiles, .kv, [data-ribbon]").count(), 0);  // nothing of it until opened
      assert.equal(host.received.filter((f) => f.t === "sub" && f.topic === "lantern.status").length > 0, true);  // (the lamp's)
      await page.locator("[data-gate='open']").click();
      await page.locator("[data-ribbon]").waitFor();
      assert.equal(await page.locator("[data-ribbon]").textContent(), "Raw readings");
      await page.locator(".kv__value", { hasText: "412 °C" }).waitFor();
      // Once a session: away and back, no gate.
      await openFromRail(page, "records", "demo.log");
      await openFromRail(page, "records", "demo.workbench");
      assert.equal(await page.locator("[data-gate]").count(), 0);
      assert.deepEqual(await violations(page), []);
      // Its setting off: the page goes from the rail, and the view leaves it.
      await openFromRail(page, "lanterns", "demo.behaviour");
      await page.locator("[data-row='setting:lantern.enabled'] [role=switch]").click();
      await page.waitForFunction(() => document.querySelector("[data-rail='page:demo.workbench']") === null);
    } finally {
      await close();
    }
  });

  test("a space's foot categories sit at the rail's foot, under its rule, above GraceUI's own (D248)", async () => {
    const { page, close } = await fresh();
    try {
      await page.locator(".rail__foot [data-rail='category:records']").waitFor();
      assert.equal(await page.locator(".rail__body [data-rail='category:records']").count(), 0);
      assert.equal(await page.locator(".rail__body [data-rail='category:lanterns']").count(), 1);
      // The space's foot first, then GraceUI's tree, each a tree of its own with one tab stop.
      const labels = await page.locator(".rail__foot [role='tree']").evaluateAll(
        (trees) => trees.map((t) => t.getAttribute("aria-label")));
      assert.deepEqual(labels, ["Demo space, at the foot", "GraceUI"]);
      assert.equal(await page.locator(".rail__foot [role='tree'] [tabindex='0']").count(), 2);
      const body = await page.locator(".rail__body").boundingBox();
      const foot = await page.locator(".rail__foot [data-rail='category:records']").boundingBox();
      assert.ok(body && foot && foot.y >= body.y + body.height);
      await page.locator(".rail__foot [data-rail='category:records']").click();
      await page.locator("main[data-page='demo.log']").waitFor();
      assert.equal(await page.locator(".rail__foot [data-rail='page:demo.lamps']").count(), 1);
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
      assert.equal(await page.locator(".flyout .flyout__page").count(), 6);  // Log, Festival, Lamps, Lightings, Handbook, Workbench
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
