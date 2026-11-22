// SPDX-License-Identifier: MIT
// fixture-space.ts: one space played from its fixtures (DESIGN §9.2's fixtures; D160): its page data, its settings
// description and values, and its topics from fixtures/<id>/topics.json. Commands are checked as a space's one
// validating entry point checks them (§6.6, §7.3; invariant 3): a known command, a known key, a value of its type within
// its bounds, nothing locked, a confirmation for a page or all; a secret is kept only as "set" (invariant 5). Pure: the
// fixture transport and the tests' fake host share it.
import type {
  ErrorCode, InheritedValue, Json, JsonObject, Rows, Setting, SettingsBody, SettingsDescription, SpaceInfo, SpacePages,
  State,
} from "../protocol.ts";
import { PROTOCOL } from "../protocol.ts";
import { sameJson, triValues } from "../model/format.ts";
import type { Answer } from "./transport.ts";
import { PAGES_TOPIC, SETTINGS_TOPIC } from "./transport.ts";

export interface FixtureSpaceData {
  pages: SpacePages;
  settings: SettingsDescription;
  /** fixtures/<id>/topics.json: a state topic's body (an object), a stream's rows (a list). */
  topics?: { [topic: string]: Json };
  /** What each inherit tri-state on "inherit" resolves to (the settings body's `inherited`, D163). */
  inherited?: InheritedValue[];
  version?: string;
  increment?: string;
}

/** What a command did: its answer, and the state frames the space publishes after it. */
export interface Outcome {
  answer: Answer;
  publish: State[];
}

const TEXT_MAX = 256;    // EST: a text setting's bytes
const SECRET_MAX = 4096; // EST: a secret's bytes
const KEY_NAME = /^[A-Za-z0-9]{1,32}$/;
// Commands a browser may never send: they belong to the game (§6.6).
const IN_GAME_ONLY = new Set(["menu.close"]);

function refuse(error: ErrorCode): Outcome {
  return { answer: { ok: false, error }, publish: [] };
}

function bytes(text: string): number {
  return new TextEncoder().encode(text).length;
}

function rowsOf(value: Json | undefined): JsonObject[] {
  if (!Array.isArray(value)) {
    return [];
  }
  return value.filter((row): row is JsonObject => typeof row === "object" && row !== null && !Array.isArray(row));
}

export class FixtureSpace {
  readonly id: string;
  readonly data: FixtureSpaceData;
  private readonly byKey = new Map<string, Setting>();
  private readonly values = new Map<string, Json>();
  private rev = 1;

  constructor(data: FixtureSpaceData) {
    this.data = data;
    this.id = data.pages.space;
    for (const s of data.settings.settings) {
      this.byKey.set(s.key, s);
      this.values.set(s.key, s.default);
    }
  }

  info(): SpaceInfo {
    return {
      id: this.id,
      name: this.data.pages.name,
      version: this.data.version ?? "0.2.0",
      increment: this.data.increment ?? "0.2",
      settings_schema: this.data.settings.schema,
    };
  }

  /** The value the space holds now (a secret's as "set" or ""). */
  value(key: string): Json | undefined {
    return this.values.get(key);
  }

  settingsBody(): SettingsBody {
    const values: JsonObject = {};
    const changed: string[] = [];
    for (const s of this.data.settings.settings) {
      const v = this.values.get(s.key) ?? s.default;
      values[s.key] = v;
      if (!sameJson(v, s.default)) {
        changed.push(s.key);
      }
    }
    const inherited = (this.data.inherited ?? []).filter((one) => {
      const s = this.byKey.get(one.key);
      return s !== undefined && s.type === "tri_state" && s.third === "inherit" && values[one.key] === "inherit";
    });
    const body: SettingsBody = { schema: this.data.settings.schema, description: this.data.settings.settings, values,
                                 changed };
    if (inherited.length > 0) {
      body.inherited = inherited;
    }
    return body;
  }

  private stateFrame(topic: string, body: JsonObject): State {
    return { v: PROTOCOL.major, t: "state", space: this.id, topic, rev: this.rev, body };
  }

  /** A subscription's first frame: the state, or the stream's rows from `since`; null when the space has published
   * nothing on it yet (absence is silent); an error for a topic the space does not declare. */
  subscribe(topic: string, since?: number): State | Rows | null | { error: ErrorCode } {
    if (topic === PAGES_TOPIC) {
      return this.stateFrame(topic, this.data.pages as unknown as JsonObject);
    }
    if (topic === SETTINGS_TOPIC) {
      return this.stateFrame(topic, this.settingsBody() as unknown as JsonObject);
    }
    const slash = topic.lastIndexOf("/");
    const family = slash > 0 ? this.data.pages.topics.find((t) => t.id === topic.slice(0, slash)) : undefined;
    if (family?.family === true && family.shape === "state" && /^[a-z0-9._-]+$/.test(topic.slice(slash + 1))) {
      // A family's member (D215): its fixture by its own id, as a state.
      const fixture = this.data.topics?.[topic];
      return typeof fixture === "object" && fixture !== null && !Array.isArray(fixture)
        ? this.stateFrame(topic, fixture) : null;
    }
    const declared = this.data.pages.topics.find((t) => t.id === topic);
    if (!declared || declared.family === true) {
      return { error: "unknown_topic" };  // a family itself is never watched: one of its members is
    }
    const fixture = this.data.topics?.[topic];
    if (fixture === undefined) {
      return null;
    }
    if (declared.shape === "state") {
      return typeof fixture === "object" && fixture !== null && !Array.isArray(fixture)
        ? this.stateFrame(topic, fixture) : null;
    }
    const rows = rowsOf(fixture).filter((row) => since === undefined || (typeof row.seq === "number" && row.seq >= since));
    if (rows.length === 0) {
      return null;
    }
    const first = rows[0]?.seq;
    return { v: PROTOCOL.major, t: "rows", space: this.id, topic, from_seq: typeof first === "number" ? first : 0, rows,
             dropped: 0 };
  }

  private changedState(): State[] {
    this.rev += 1;
    return [this.stateFrame(SETTINGS_TOPIC, this.settingsBody() as unknown as JsonObject)];
  }

  /** Checks a value against its setting's description, as the space's entry point does. */
  valid(s: Setting, v: Json): boolean {
    switch (s.type) {
      case "bool":
        return typeof v === "boolean";
      case "integer":
        return typeof v === "number" && Number.isInteger(v) && v >= (s.min ?? -Infinity) && v <= (s.max ?? Infinity);
      case "number":
      case "duration_s":
        return typeof v === "number" && Number.isFinite(v) && v >= (s.min ?? -Infinity) && v <= (s.max ?? Infinity);
      case "choice":
        return typeof v === "string" && (s.choices ?? []).some((c) => c.value === v);
      case "tri_state":
        return typeof v === "string" && triValues(s).includes(v);
      case "key_name":
        return typeof v === "string" && KEY_NAME.test(v);
      case "text":
        return typeof v === "string" && bytes(v) <= TEXT_MAX;
      case "secret":
        return typeof v === "string" && bytes(v) <= SECRET_MAX;
    }
    return false;
  }

  handle(name: string, args: JsonObject): Outcome {
    if (!this.data.pages.commands.includes(name)) {
      return refuse("unknown_command");
    }
    if (IN_GAME_ONLY.has(name)) {
      return refuse("not_now");
    }
    const destructive = this.data.pages.pages.some((p) => (p.actions ?? []).some(
      (a) => a.command === name && a.tone === "destructive"));
    if (destructive && args.confirm !== true) {
      return refuse("confirm_required");  // as the server refuses it (§7.3, D173)
    }
    if (name === "settings.set") {
      const key = args.key;
      if (typeof key !== "string" || !("value" in args)) {
        return refuse("bad_message");
      }
      const s = this.byKey.get(key);
      if (!s) {
        return refuse("unknown_key");
      }
      if (s.locked || s.availability === "later") {
        return refuse("locked");
      }
      const value = args.value as Json;
      if (!this.valid(s, value)) {
        return refuse("invalid_value");
      }
      // A secret is never kept, echoed or published: only whether one is set (§7.4, invariant 5).
      this.values.set(key, s.type === "secret" ? (value === "" ? "" : "set") : value);
      return { answer: { ok: true }, publish: this.changedState() };
    }
    if (name === "settings.reset") {
      const scope = args.scope;
      const target = args.target;
      let keys: Setting[];
      if (scope === "key") {
        const s = typeof target === "string" ? this.byKey.get(target) : undefined;
        if (!s) {
          return refuse("unknown_key");
        }
        if (s.locked) {
          return refuse("locked");
        }
        keys = [s];
      } else if (scope === "page" || scope === "all") {
        if (args.confirm !== true) {
          return refuse("confirm_required");
        }
        if (scope === "page" && !this.data.pages.pages.some((p) => p.id === target)) {
          return refuse("invalid_value");
        }
        keys = this.data.settings.settings.filter((s) => !s.locked && (scope === "all" || s.home === target));
      } else {
        return refuse("bad_message");
      }
      for (const s of keys) {
        this.values.set(s.key, s.default);
      }
      return { answer: { ok: true }, publish: this.changedState() };
    }
    if (name === "hotkeys.key" && typeof args.value === "string" && args.value !== "" && args.confirm !== true) {
      // As GraceUI's host asks before a move (D208): a key another row of the Hotkeys page's list has.
      const listed = this.data.topics?.["graceui.hotkeys"];
      const rows = typeof listed === "object" && listed !== null && !Array.isArray(listed) && Array.isArray(listed.rows)
        ? listed.rows : [];
      for (const row of rows) {
        if (typeof row === "object" && row !== null && !Array.isArray(row) && row.key === args.value &&
            row.id !== args.id && typeof row.id === "string" && row.id !== "") {
          return {
            answer: { ok: false, error: "confirm_required",
                      detail: `${args.value} is ${String(row.mod)}'s ${String(row.action)}. Use it here, and leave that one with no key?` },
            publish: [],
          };
        }
      }
    }
    return { answer: { ok: true }, publish: [] };  // the space's own commands: accepted, with nothing to show here
  }
}
