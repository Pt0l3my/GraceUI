// SPDX-License-Identifier: MIT
// browser.ts: Chromium through the Playwright already installed on the machine (never a dependency of the web app:
// it is found the way Node finds a global module), a page that records every request, console message, page error and
// CSP violation, and the helpers the DOM tests share.
import { createRequire } from "node:module";
import { join } from "node:path";
import type { FakeHost } from "./fake-host.ts";
import { WEB } from "./fixtures.ts";

export const DIST = join(WEB, "dist");

// Playwright's own types are not installed with the app: the tests use the little they need through these.
export interface PwLocator {
  click(options?: object): Promise<void>;
  press(key: string): Promise<void>;
  fill(text: string): Promise<void>;
  focus(): Promise<void>;
  count(): Promise<number>;
  first(): PwLocator;
  nth(i: number): PwLocator;
  textContent(): Promise<string | null>;
  getAttribute(name: string): Promise<string | null>;
  isVisible(): Promise<boolean>;
  waitFor(options?: object): Promise<void>;
  evaluateAll<T>(fn: (elements: Element[]) => T): Promise<T>;
  locator(selector: string): PwLocator;
  setInputFiles(files: { name: string; mimeType: string; buffer: Buffer }): Promise<void>;
}
export interface PwPage {
  goto(url: string): Promise<unknown>;
  locator(selector: string): PwLocator;
  keyboard: { press(key: string): Promise<void>; type(text: string): Promise<void> };
  evaluate<T>(fn: () => T | Promise<T>): Promise<T>;
  evaluate<T, A>(fn: (arg: A) => T | Promise<T>, arg: A): Promise<T>;
  waitForFunction(fn: () => unknown, arg?: unknown, options?: object): Promise<unknown>;
  waitForFunction<A>(fn: (arg: A) => unknown, arg: A, options?: object): Promise<unknown>;
  waitForTimeout(ms: number): Promise<void>;
  setViewportSize(size: { width: number; height: number }): Promise<void>;
  screenshot(options: object): Promise<unknown>;
  on(event: string, handler: (arg: never) => void): void;
  addInitScript(fn: () => void): Promise<void>;
  content(): Promise<string>;
}
export interface PwBrowser {
  newContext(options?: object): Promise<{ newPage(): Promise<PwPage>; close(): Promise<void> }>;
  close(): Promise<void>;
}

export async function launch(): Promise<PwBrowser> {
  const require = createRequire(import.meta.url);
  const { chromium } = require("playwright") as { chromium: { launch(options?: object): Promise<PwBrowser> } };
  return chromium.launch({ headless: true });
}

export interface Recorded {
  page: PwPage;
  requests: string[];
  problems: string[];
  close(): Promise<void>;
}

/** A fresh context (its own cookies) and page, recording everything the page asks for and everything it complains of. */
export async function openPage(browser: PwBrowser, host: FakeHost,
                               viewport = { width: 1920, height: 1080 }, scale = 1): Promise<Recorded> {
  const context = await browser.newContext({ viewport, deviceScaleFactor: scale, bypassCSP: false });
  const page = await context.newPage();
  const requests: string[] = [];
  const problems: string[] = [];
  page.on("request", (request: { url(): string }) => requests.push(request.url()));
  page.on("websocket", (socket: { url(): string }) => requests.push(socket.url()));
  page.on("console", (message: { type(): string; text(): string }) => {
    if (message.type() === "error" || message.type() === "warning") {
      problems.push(`console ${message.type()}: ${message.text()}`);
    }
  });
  page.on("pageerror", (error: Error) => problems.push(`page error: ${error.message}`));
  // CSP violations, as the page itself sees them (the init script is the browser's, not the page's, so the CSP
  // does not apply to it).
  await page.addInitScript(() => {
    document.addEventListener("securitypolicyviolation", (e) => {
      const list = ((window as unknown as { __violations?: string[] }).__violations ??= []);
      list.push(`${e.violatedDirective} ${e.blockedURI}`);
    });
  });
  await page.goto(host.origin + "/");
  await page.locator("main.page h1").waitFor();
  return { page, requests, problems, close: () => context.close() };
}

export async function violations(page: PwPage): Promise<string[]> {
  return page.evaluate(() => (window as unknown as { __violations?: string[] }).__violations ?? []);
}

/** Opens a page through the rail as a player would: its category, then the page. */
export async function openFromRail(page: PwPage, category: string, pageId: string): Promise<void> {
  const item = page.locator(`[data-rail="page:${pageId}"]`);
  if ((await item.count()) === 0) {
    await page.locator(`[data-rail="category:${category}"]`).first().click();
  }
  await page.locator(`[data-rail="page:${pageId}"]`).first().click();
  await page.locator(`main.page[data-page="${pageId}"]`).waitFor();
}

/** Opens every folded section and every Advanced disclosure of the page as it is drawn now. */
export async function unfoldAll(page: PwPage): Promise<void> {
  for (const selector of [".card__toggle[aria-expanded=false]", ".advanced__toggle[aria-expanded=false]"]) {
    for (let guard = 0; guard < 20 && (await page.locator(selector).count()) > 0; ++guard) {
      await page.locator(selector).first().click();
    }
  }
}
