// SPDX-License-Identifier: MIT
// check-space.ts: a space's pages in the real browser view, over a space folder (D265). The folder holds what
// grace-dev takes: pages.json and settings.json, and optionally topics.json and paged.json. The check opens GraceUI's
// built browser view (npm run build first) over GraceUI's fake host with the folder's fixtures. For this PC's browser
// and a paired device, and for each switch a page is shown with, it prints what the view shows: the rail, and each
// page as its text reads (its gate first, then the page behind it), in both themes for the contrast check; and each
// flow (D278) as the player meets it, a load whose state says start landing on it, step by step. It fails,
// naming each, on a console error, a CSP violation, a request outside the page's origin or a text under 4.5:1. The
// printed dump is a consumer's golden: --golden <file> compares with it and --update writes it. Nothing it prints names
// the folder or any path (PV3).
//
//   npm run -s check:space -- <space folder> [--golden <file>] [--update]
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import process from "node:process";
import type { JsonObject, SettingsDescription, SpacePages } from "../src/protocol.ts";
import type { FixtureSpaceData, PagedFixture } from "../src/transport/fixture-space.ts";
import type { PwBrowser, PwPage } from "../tests/support/browser.ts";
import type { FakeHost } from "../tests/support/fake-host.ts";
import { DIST, launch, lowContrast, openPage, violations } from "../tests/support/browser.ts";
import { startHost } from "../tests/support/fake-host.ts";
import { space as builtin } from "../tests/support/fixtures.ts";

interface Options {
  folder: string;
  golden?: string;
  update: boolean;
}

function options(argv: string[]): Options {
  const out: Options = { folder: "", update: false };
  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i]!;
    if (arg === "--golden") {
      out.golden = argv[++i];
    } else if (arg === "--update") {
      out.update = true;
    } else if (out.folder === "") {
      out.folder = arg;
    } else {
      throw new Error(`unknown argument: ${arg}`);
    }
  }
  if (out.folder === "") {
    throw new Error("usage: check:space -- <space folder> [--golden <file>] [--update]");
  }
  return out;
}

function readJson(folder: string, name: string, needed: boolean): unknown {
  const file = join(folder, name);
  if (!existsSync(file)) {
    if (needed) {
      throw new Error(`the space folder has no ${name}`);
    }
    return undefined;
  }
  try {
    return JSON.parse(readFileSync(file, "utf8")) as unknown;
  } catch (error) {
    throw new Error(`${name} is not JSON: ${(error as Error).message}`);
  }
}

function load(folder: string): FixtureSpaceData {
  const data: FixtureSpaceData = {
    pages: readJson(folder, "pages.json", true) as SpacePages,
    settings: readJson(folder, "settings.json", true) as SettingsDescription,
  };
  const topics = readJson(folder, "topics.json", false);
  if (topics !== undefined) {
    data.topics = topics as FixtureSpaceData["topics"];
  }
  const paged = readJson(folder, "paged.json", false);
  if (paged !== undefined) {
    data.paged = paged as { [command: string]: PagedFixture };
  }
  return data;
}

/** The space's data with one switch's default turned the other way: the view as the player who flips it sees it. */
function flipped(data: FixtureSpaceData, key: string): FixtureSpaceData {
  const settings = JSON.parse(JSON.stringify(data.settings)) as SettingsDescription;
  for (const s of settings.settings) {
    if (s.key === key) {
      s.default = s.default !== true;
    }
  }
  return { ...data, settings };
}

const squash = (words: string) => words.replace(/\s+/g, " ").trim();

/** What the rail lists of the space: each category's pages, in order, each category opened to list them. */
async function rail(page: PwPage, pages: SpacePages): Promise<string[]> {
  const out: string[] = [];
  for (const category of pages.categories) {
    const button = page.locator(`[data-rail="category:${category.id}"]`);
    if ((await button.count()) === 0) {
      out.push(`rail: ${category.name}: absent`);
      continue;
    }
    if ((await page.locator(`[data-rail^="page:"]`).count()) === 0 ||
        (await page.locator(category.pages.map((id) => `[data-rail="page:${id}"]`).join(", ")).count()) === 0) {
      await button.first().click();
    }
    const listed: string[] = [];
    for (const id of category.pages) {
      if ((await page.locator(`[data-rail="page:${id}"]`).count()) > 0) {
        listed.push(id);
      }
    }
    out.push(`rail: ${category.name}: ${listed.length > 0 ? listed.join(", ") : "nothing"}`);
  }
  return out;
}

/** Until the page has sent the host nothing for 1.2 s (at most 15 s): its subscriptions go at the protocol's rate, a
 * batch a second (D203), so a page that watches many topics sends them over several seconds, and the fake host answers
 * each at once. Past this, every topic the page asked for has its first body. */
async function settled(page: PwPage, host: FakeHost): Promise<void> {
  let count = host.received.length;
  let quiet = 0;
  for (let waited = 0; quiet < 1200 && waited < 15000; waited += 100) {
    await page.waitForTimeout(100);
    quiet = host.received.length === count ? quiet + 100 : 0;
    count = host.received.length;
  }
}

/** A page's text as the view shows it: its gate's words first, if it has one, then the page behind it. */
async function pageText(page: PwPage, host: FakeHost, category: string, id: string): Promise<string[]> {
  const item = page.locator(`[data-rail="page:${id}"]`);
  if ((await item.count()) === 0) {
    await page.locator(`[data-rail="category:${category}"]`).first().click();
  }
  if ((await page.locator(`[data-rail="page:${id}"]`).count()) === 0) {
    return [`page ${id}: absent`];
  }
  await page.locator(`[data-rail="page:${id}"]`).first().click();
  const out: string[] = [];
  const gate = page.locator("[data-gate='open']");
  try {
    await page.locator(`main.page[data-page="${id}"], [data-gate='open']`).first().waitFor({ timeout: 5000 });
  } catch {
    return [`page ${id}: did not open`];
  }
  if ((await gate.count()) > 0) {
    const words = ((await page.locator(".gate").first().innerText()) ?? "").split("\n").map(squash).filter((l) => l !== "");
    out.push(`page ${id}: gate: ${words.join(" | ")}`);
    await gate.first().click();
    await page.locator(`main.page[data-page="${id}"]`).waitFor({ timeout: 5000 });
  }
  out.push(`page ${id}:`, ...await textOf(page, host, id));
  return out;
}

/** Every topic's first body (the page's subscriptions all sent and answered), then the page's text once it has held
 * still for 400 ms (at most 10 s), however loaded the machine, so that the dump never catches a page half drawn. */
async function textOf(page: PwPage, host: FakeHost, id: string): Promise<string[]> {
  await settled(page, host);
  const main = page.locator(`main.page[data-page="${id}"]`).first();
  let text = (await main.innerText()) ?? "";
  for (let still = 0, waited = 0; still < 4 && waited < 10000; waited += 100) {
    await page.waitForTimeout(100);
    const now = (await main.innerText()) ?? "";
    still = now === text ? still + 1 : 0;
    text = now;
  }
  return text.split("\n").map(squash).filter((l) => l !== "").map((line) => `  ${line}`);
}

/** A space's flows (D278), each as the player meets it: a load whose state says start lands on it; then each of its
 * steps as the space would publish it, its text and, in both themes, its contrast. */
async function flows(browser: PwBrowser, data: FixtureSpaceData, problems: string[]): Promise<string[]> {
  const out: string[] = [];
  for (const flow of data.pages.pages) {
    if (flow.flow === undefined) {
      continue;
    }
    const topic = flow.flow.topic;
    const state = { ...((data.topics?.[topic] ?? {}) as JsonObject) };
    const asking: FixtureSpaceData = { ...data, topics: { ...data.topics, [topic]: { ...state, start: true } } };
    const host = await startHost(DIST, [builtin("graceui"), asking]);
    const opened = await openPage(browser, host);
    try {
      const { page } = opened;
      try {
        await page.locator(`main.page[data-page="${flow.id}"]`).waitFor({ timeout: 10000 });
      } catch {
        out.push(`page ${flow.id}: did not land`);
        problems.push(`flow ${flow.id}: a load whose state says start does not land on it`);
        continue;
      }
      await settled(page, host);
      for (const theme of ["grace", "clear"]) {
        if (theme === "clear") {
          const toggle = page.locator("[data-shortcut='ui.menu.theme']");
          if ((await toggle.count()) === 0) {
            break;
          }
          await toggle.first().click();
          await page.waitForFunction(() => document.documentElement.dataset.theme === "clear");
        }
        for (const step of flow.tabs ?? []) {
          host.push(data.pages.space, topic, { ...state, step: step.id });
          try {
            await page.locator(`.flow-step.is-current[data-step="${step.id}"]`).waitFor({ timeout: 5000 });
          } catch {
            problems.push(`flow ${flow.id}: step ${step.id} is not shown when its state names it`);
            continue;
          }
          const lines = await textOf(page, host, flow.id);
          if (theme === "grace") {
            out.push(`page ${flow.id}#${step.id}:`, ...lines);
          }
          for (const faint of await lowContrast(page)) {
            problems.push(`flow ${flow.id}, ${theme} theme, step ${step.id}: under 4.5:1: ${faint}`);
          }
        }
      }
      for (const v of await violations(page)) {
        problems.push(`flow ${flow.id}: CSP violation: ${v}`);
      }
      for (const p of opened.problems) {
        problems.push(`flow ${flow.id}: ${p}`);
      }
    } finally {
      await opened.close();
      await host.close();
    }
  }
  return out.length > 0 ? ["== this PC's browser, its flows", ...out] : out;
}

async function view(browser: PwBrowser, data: FixtureSpaceData, as: "browser" | "device", label: string,
                    problems: string[]): Promise<string[]> {
  const host = await startHost(DIST, [builtin("graceui"), data]);
  host.setView(as);
  const opened = await openPage(browser, host);
  const out: string[] = [`== ${label}`];
  try {
    const { page } = opened;
    await page.locator("[data-rail]").first().waitFor({ timeout: 10000 });
    await settled(page, host);  // the settings, which a page shown with a switch waits for (D258)
    out.push(...await rail(page, data.pages));
    for (const theme of ["grace", "clear"]) {
      if (theme === "clear") {
        const toggle = page.locator("[data-shortcut='ui.menu.theme']");
        if ((await toggle.count()) === 0) {
          break;
        }
        await toggle.first().click();
        await page.waitForFunction(() => document.documentElement.dataset.theme === "clear");
      }
      for (const category of data.pages.categories) {
        for (const id of category.pages) {
          const lines = await pageText(page, host, category.id, id);
          if (theme === "grace") {
            out.push(...lines);
          }
          for (const faint of await lowContrast(page)) {
            problems.push(`${label}, ${theme} theme, page ${id}: under 4.5:1: ${faint}`);
          }
        }
      }
    }
    for (const v of await violations(page)) {
      problems.push(`${label}: CSP violation: ${v}`);
    }
    for (const p of opened.problems) {
      problems.push(`${label}: ${p}`);
    }
    const origin = host.origin;
    for (const url of opened.requests) {
      if (!url.startsWith(origin + "/") && !url.startsWith(origin.replace("http:", "ws:") + "/") &&
          !url.startsWith("data:") && !url.startsWith("blob:")) {
        problems.push(`${label}: a request outside the page's origin: ${url}`);
      }
    }
  } finally {
    await opened.close();
    await host.close();
  }
  return out;
}

async function main(): Promise<number> {
  const opts = options(process.argv.slice(2));
  if (!existsSync(join(DIST, "index.html"))) {
    process.stderr.write("check:space: build the browser view first (npm run build)\n");
    return 2;
  }
  const data = load(resolve(opts.folder));
  const id = data.pages.space;
  const switches = [...new Set(data.pages.pages.map((p) => p.shown_with).filter((k): k is string => k !== undefined))];
  const browser = await launch();
  const problems: string[] = [];
  const dump: string[] = [`# ${id}: the browser view, by GraceUI's space check (D265)`];
  try {
    dump.push(...await view(browser, data, "browser", "this PC's browser", problems));
    dump.push(...await view(browser, data, "device", "a paired device", problems));
    dump.push(...await flows(browser, data, problems));
    for (const key of switches) {
      const setting = data.settings.settings.find((s) => s.key === key);
      const now = setting?.default === true ? "off" : "on";
      dump.push(...await view(browser, flipped(data, key), "browser", `this PC's browser, ${key} ${now}`, problems));
    }
  } finally {
    await browser.close();
  }
  const text = dump.join("\n") + "\n";
  let failed = problems.length > 0;
  if (opts.golden !== undefined) {
    if (opts.update) {
      writeFileSync(opts.golden, text);
    } else if (!existsSync(opts.golden) || readFileSync(opts.golden, "utf8") !== text) {
      problems.push("the dump differs from its golden (run with --update to accept it)");
      failed = true;
    }
  }
  process.stdout.write(text);
  for (const problem of problems) {
    process.stderr.write(`check:space: ${problem}\n`);
  }
  process.stderr.write(`check:space: ${id}: ${failed ? `${problems.length} problem(s)` : "passes"}\n`);
  return failed ? 1 : 0;
}

// Exits once both streams have handed on what was written: through a pipe Node writes asynchronously, 64 KiB at a time,
// and process.exit at once would cut a larger dump short.
function finish(code: number): void {
  process.exitCode = code;
  process.stdout.on("error", () => process.exit(code));  // a reader that stopped early (| head): the dump was taken
  process.stdout.write("", () => process.stderr.write("", () => process.exit(code)));
}

main().then(finish, (error: unknown) => {
  process.stderr.write(`check:space: ${(error as Error).message}\n`);
  finish(2);
});
