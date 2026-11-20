// SPDX-License-Identifier: MIT
// extras.ts: what the fixtures say beyond topics.json, for the fixture transport: what each inherit tri-state on
// "inherit" resolves to (D163), in the same invented words as grace-view's test of it.
import type { InheritedValue } from "../protocol.ts";

export const FIXTURE_INHERITED: { [space: string]: InheritedValue[] } = {
  demo: [{ key: "lantern.shield", on: true, source: "The lighthouse", page: "demo.overview" }],
};
