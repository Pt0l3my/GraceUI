// SPDX-License-Identifier: MIT
// fixtures.ts: the dev page's spaces (npm run dev): GraceUI's own space and the demo space, from the fixtures the page
// lint builds (fixtures/<id>/), played by the fixture transport. Only the dev server loads this; the release bundle
// has none of it (its scan checks).
import type { Json, SettingsDescription, SpacePages } from "../protocol.ts";
import type { FixtureSpaceData } from "../transport/fixture-space.ts";
import { FixtureTransport } from "../transport/fixture.ts";
import type { Transport } from "../transport/transport.ts";
import { FIXTURE_INHERITED } from "./extras.ts";
import graceuiPages from "../../../fixtures/graceui/pages.json";
import graceuiSettings from "../../../fixtures/graceui/settings.json";
import graceuiTopics from "../../../fixtures/graceui/topics.json";
import demoPages from "../../../fixtures/demo/pages.json";
import demoSettings from "../../../fixtures/demo/settings.json";
import demoTopics from "../../../fixtures/demo/topics.json";

export function fixtureSpaces(): FixtureSpaceData[] {
  return [
    { pages: graceuiPages as unknown as SpacePages, settings: graceuiSettings as unknown as SettingsDescription,
      topics: graceuiTopics as unknown as { [topic: string]: Json } },
    { pages: demoPages as unknown as SpacePages, settings: demoSettings as unknown as SettingsDescription,
      topics: demoTopics as unknown as { [topic: string]: Json }, inherited: FIXTURE_INHERITED.demo ?? [] },
  ];
}

export function devTransport(): Transport {
  return new FixtureTransport(fixtureSpaces(), { latency: 60 });
}
