// SPDX-License-Identifier: MIT
// fixtures.ts: the built-in spaces' fixtures for the tests (fixtures/<id>/, which tools/pages/build.py writes), read
// from disk, and the golden files grace-view's tests write (src/grace-view/tests/golden/).
import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import type { Json, SettingsDescription, SpacePages } from "../../src/protocol.ts";
import type { FixtureSpaceData } from "../../src/transport/fixture-space.ts";
import { FIXTURE_INHERITED } from "../../src/dev/extras.ts";

export const WEB = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
export const ROOT = join(WEB, "..");
export const GOLDEN = join(ROOT, "src", "grace-view", "tests", "golden");

function json(path: string): unknown {
  return JSON.parse(readFileSync(path, "utf8"));
}

export function space(id: string): FixtureSpaceData {
  const dir = join(ROOT, "fixtures", id);
  const data: FixtureSpaceData = {
    pages: json(join(dir, "pages.json")) as SpacePages,
    settings: json(join(dir, "settings.json")) as SettingsDescription,
  };
  try {
    data.topics = json(join(dir, "topics.json")) as { [topic: string]: Json };
  } catch {
    // a space with no topic fixtures
  }
  if (existsSync(join(dir, "paged.json"))) {
    // The rows its page commands answer, and the family members they name (D254).
    const paged = json(join(dir, "paged.json")) as { commands: FixtureSpaceData["paged"]; members?: { [topic: string]: Json } };
    data.paged = paged.commands;
    data.topics = { ...(data.topics ?? {}), ...(paged.members ?? {}) };
  }
  const inherited = FIXTURE_INHERITED[id];
  if (inherited) {
    data.inherited = inherited;
  }
  return data;
}

export function builtinSpaces(): FixtureSpaceData[] {
  return [space("graceui"), space("demo")];
}

/** The golden's lines that are not comments. */
export function goldenLines(name: string): string[] {
  return readFileSync(join(GOLDEN, name), "utf8").split("\n").filter((line) => line.length > 0 && !line.startsWith("#"));
}
