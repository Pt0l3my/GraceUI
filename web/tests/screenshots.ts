// SPDX-License-Identifier: MIT
// screenshots.ts: the owner's review shots, not a test: GraceUI › Menu and the demo's Behaviour page at 1920 × 1080
// and device scale 2, from the built dist/ behind the tests' fake host. Run after the build:
//   node tests/screenshots.ts <folder>
import { mkdirSync } from "node:fs";
import { join } from "node:path";
import { DIST, launch, openFromRail, openPage } from "./support/browser.ts";
import { startHost } from "./support/fake-host.ts";
import { builtinSpaces } from "./support/fixtures.ts";

const out = process.argv[2];
if (!out) {
  console.error("usage: node tests/screenshots.ts <folder>");
  process.exit(2);
}
mkdirSync(out, { recursive: true });
const host = await startHost(DIST, builtinSpaces());
const browser = await launch();
try {
  const { page, close } = await openPage(browser, host, { width: 1920, height: 1080 }, 2);
  await openFromRail(page, "graceui", "graceui.menu");
  await page.waitForTimeout(300);
  await page.screenshot({ path: join(out, "graceui-menu-1920x1080@2x.png") });
  await openFromRail(page, "lanterns", "demo.behaviour");
  await page.waitForTimeout(300);
  await page.screenshot({ path: join(out, "demo-behaviour-1920x1080@2x.png") });
  await close();
} finally {
  await browser.close();
  await host.close();
}
