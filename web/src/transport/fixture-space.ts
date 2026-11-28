// SPDX-License-Identifier: MIT
// fixture-space.ts: one space played from its fixtures (DESIGN §9.2's fixtures; D160): its page data, its settings
// description and values, and its topics from fixtures/<id>/topics.json. Commands are checked as a space's one
// validating entry point checks them (§6.6, §7.3; invariant 3): a known command, a known key, a value of its type within
// its bounds, nothing locked, a confirmation for a page or all; a secret is kept only as "set" (invariant 5). Pure: the
// fixture transport and the tests' fake host share it.
import type {
  ErrorCode, InheritedValue, Json, JsonObject, Page, PageBlock, Rows, Setting, SettingsBody, SettingsDescription, SpaceInfo,
  SpacePages, State,
} from "../protocol.ts";
import { PROTOCOL } from "../protocol.ts";
import { sameJson, triValues } from "../model/format.ts";
import { countChars, countWords, fits, listItem, parseDocument } from "../model/editor.ts";
import type { EditorField } from "../model/editor.ts";
import type { Answer } from "./transport.ts";
import { answerPage } from "../model/paging.ts";
import type { PagedFixture } from "../model/paging.ts";
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
  /** fixtures/<id>/paged.json: the rows a page command answers a page at a time (D254), by command. */
  paged?: { [command: string]: PagedFixture };
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
  // The editor blocks' documents as the space holds them now, and each one's version before its last save (D217).
  private readonly docs = new Map<string, JsonObject>();
  private readonly previous = new Map<string, JsonObject>();

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
      const fixture = this.docs.get(topic) ?? this.data.topics?.[topic];
      return typeof fixture === "object" && fixture !== null && !Array.isArray(fixture)
        ? this.stateFrame(topic, fixture) : null;
    }
    const declared = this.data.pages.topics.find((t) => t.id === topic);
    if (!declared || declared.family === true) {
      return { error: "unknown_topic" };  // a family itself is never watched: one of its members is
    }
    const fixture = this.docs.get(topic) ?? this.data.topics?.[topic];
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
    if ((this.data.pages.file_commands ?? []).includes(name)) {
      // As the server answers a file command sent without its file (G28, D226).
      return { answer: { ok: false, error: "invalid_value", detail: "This takes a file: add it with its button." },
               publish: [] };
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
    const paged = this.data.paged?.[name];
    if (paged !== undefined) {
      return { answer: { ok: true, body: answerPage(paged, args) }, publish: [] };  // a page of its rows (D254)
    }
    const editor = this.editorFor(name);
    if (editor) {
      return this.editorCommand(editor.block, editor.part, args);
    }
    const quick = (this.data.pages.quick ?? []).find((q) => q.command === name && q.state !== undefined);
    if (quick?.state) {
      // A session switch (D252): the space publishes its state as asked, as the demo space does.
      if (typeof args.on !== "boolean") {
        return refuse("bad_message");
      }
      const fixture = this.docs.get(quick.state.topic) ?? this.data.topics?.[quick.state.topic];
      const body = typeof fixture === "object" && fixture !== null && !Array.isArray(fixture) ? fixture : {};
      const next: JsonObject = { ...body, [quick.state.field]: args.on };
      this.docs.set(quick.state.topic, next);
      this.rev += 1;
      return { answer: { ok: true }, publish: [this.stateFrame(quick.state.topic, next)] };
    }
    const flow = this.data.pages.pages.find((p) => p.flow?.command === name && (p.tabs ?? []).length > 0);
    if (flow !== undefined) {
      return this.flowCommand(flow, args);
    }
    return { answer: { ok: true }, publish: [] };  // the space's own commands: accepted, with nothing to show here
  }

  /** A flow's command (D278), as grace-dev answers it: from the step the view showed, next to the step after it (that
   * step done), skip to it (left undone), back to the step before, done on the last step (that step done, start
   * false); its blocked words gone. Unlike grace-dev it takes the view's step as it is, for the tests publish steps of
   * their own. */
  private flowCommand(page: Page, args: JsonObject): Outcome {
    const steps = (page.tabs ?? []).map((t) => t.id);
    const at = typeof args.step === "string" ? steps.indexOf(args.step) : -1;
    if (at < 0 || page.flow === undefined) {
      return { answer: { ok: false, error: "invalid_value", detail: "That step is not one of this flow's." }, publish: [] };
    }
    const topic = page.flow.topic;
    const fixture = this.docs.get(topic) ?? this.data.topics?.[topic];
    const body: JsonObject = typeof fixture === "object" && fixture !== null && !Array.isArray(fixture) ? { ...fixture } : {};
    const done = new Set(Array.isArray(body.done) ? body.done.filter((d): d is string => typeof d === "string") : []);
    const last = at + 1 === steps.length;
    const here = steps[at]!;
    if ((args.go === "next" || args.go === "skip") && !last) {
      if (args.go === "next") {
        done.add(here);
      }
      body.step = steps[at + 1]!;
    } else if (args.go === "back" && at > 0) {
      body.step = steps[at - 1]!;
    } else if (args.go === "done" && last) {
      done.add(here);
      body.start = false;
    } else {
      return { answer: { ok: false, error: "not_now", detail: "Not possible on this step." }, publish: [] };
    }
    body.done = [...done];
    delete body.blocked;
    this.docs.set(topic, body);
    this.rev += 1;
    return { answer: { ok: true }, publish: [this.stateFrame(topic, body)] };
  }

  /** A file command with its file (G28), as the demo space answers it: a roster file's entries counted, one for each
   * line that starts `name =`; nothing is kept. */
  handleFile(name: string, _args: JsonObject, text: string): Outcome {
    if (!this.data.pages.commands.includes(name) || !(this.data.pages.file_commands ?? []).includes(name)) {
      return refuse("unknown_command");
    }
    const added = text.split("\n").filter((line) => /^\s*name\s*=/.test(line)).length;
    if (added === 0) {
      return { answer: { ok: false, error: "invalid_value", detail: "No keeper in that file." }, publish: [] };
    }
    return { answer: { ok: true, body: { added } }, publish: [] };
  }

  // ---- The editor blocks' commands, as a space would answer them (D217): its own checks, a save over a stale revision
  // refused as conflict, a reset to its fixture, a restore of the version before the last save, and a draft of its
  // own words from the notes. The demo's stand-in for a space's one entry point; a real space checks far more.

  private editorFor(name: string): { block: PageBlock; part: string } | null {
    for (const page of this.data.pages.pages) {
      const sections = [...(page.sections ?? []), ...(page.tabs ?? []).flatMap((t) => t.sections)];
      for (const section of sections) {
        for (const block of [...section.blocks, ...(section.advanced ?? [])]) {
          const ed = block.kind === "editor" ? block.editor : undefined;
          if (!ed) {
            continue;
          }
          for (const part of ["save", "check", "reset", "restore", "draft"] as const) {
            if (ed[part] === name) {
              return { block, part };
            }
          }
        }
      }
    }
    return null;
  }

  private editorCommand(block: PageBlock, part: string, args: JsonObject): Outcome {
    const target = args.target;
    const family = this.data.pages.topics.find((t) => t.id === block.topic)?.family === true;
    if (typeof target !== "string" || block.topic === undefined || (family && !/^[a-z0-9._-]{1,48}$/.test(target))) {
      return refuse("invalid_value");
    }
    const topic = family ? `${block.topic}/${target}` : block.topic;
    const fixture = this.data.topics?.[topic];
    const original = typeof fixture === "object" && fixture !== null && !Array.isArray(fixture) ? fixture : undefined;
    const now = this.docs.get(topic) ?? original;
    const doc = parseDocument(now);
    if (!now || !doc || doc.target !== target) {
      return { answer: { ok: false, error: "invalid_value", detail: "No such document." }, publish: [] };
    }
    const publish = (body: JsonObject, word: string): Outcome => {
      const next: JsonObject = { ...body, rev: doc.rev + 1, state: { word, tone: "moss" } };
      this.docs.set(topic, next);
      this.rev += 1;
      return { answer: { ok: true }, publish: [this.stateFrame(topic, next)] };
    };
    switch (part) {
      case "check":
      case "save": {
        const sent = args.doc;
        if (typeof sent !== "object" || sent === null || Array.isArray(sent) || typeof args.rev !== "number") {
          return refuse("bad_message");
        }
        const messages = this.checkDocument(doc, sent);
        if (part === "check") {
          return { answer: { ok: true, body: { messages } }, publish: [] };
        }
        if (args.rev !== doc.rev) {
          return { answer: { ok: false, error: "conflict" }, publish: [] };
        }
        if (messages.some((m) => (m as JsonObject).level === "error")) {
          return { answer: { ok: false, error: "invalid_value", body: { messages } }, publish: [] };
        }
        this.previous.set(topic, now);
        const fields = (now.fields as JsonObject[]).map((f) => {
          const id = f.id as string;
          const field = doc.fields.find((one) => one.id === id);
          return field && field.kind !== "shown" && field.locked === "" && id in sent ? { ...f, value: sent[id] as Json } : f;
        });
        return publish({ ...now, fields }, "Saved");
      }
      case "reset":
        if (args.all !== true || args.confirm !== true) {
          return refuse("confirm_required");
        }
        if (!original) {
          return refuse("not_now");
        }
        this.previous.set(topic, now);
        return publish({ ...original }, "Reset");
      case "restore": {
        const before = this.previous.get(topic);
        if (!before) {
          return { answer: { ok: false, error: "not_now", detail: "There is no earlier version to restore." }, publish: [] };
        }
        this.previous.set(topic, now);
        return publish({ ...before }, "Restored");
      }
      case "draft": {
        if (doc.draftBlocked !== "") {
          return { answer: { ok: false, error: "not_now", detail: doc.draftBlocked }, publish: [] };
        }
        const notes = typeof args.notes === "string" ? args.notes : "";
        if (countChars(notes) > 400) {
          return refuse("invalid_value");
        }
        const words = notes.split(" ").filter((w) => w !== "");
        const draft: JsonObject = {};
        for (const field of doc.fields) {
          if (field.locked !== "") {
            continue;
          }
          const cut = (text: string) => (field.maxChars > 0 ? [...text].slice(0, field.maxChars).join("") : text);
          if (field.kind === "line") {
            draft[field.id] = cut(words.length > 0 ? words.slice(0, 3).join(" ") : "A drafted name");
          } else if (field.kind === "prose") {
            draft[field.id] = cut(words.length > 0 ? `Drafted from your notes: ${words.join(" ")}.` : "A plain account, drafted with no notes.");
          }
        }
        return { answer: { ok: true, body: { draft, notice: "" } }, publish: [] };
      }
    }
    return refuse("unknown_command");
  }

  /** The demo's own checks: each field's kind and limits, a locked field or item unchanged. */
  private checkDocument(doc: ReturnType<typeof parseDocument> & object, sent: JsonObject): Json[] {
    const out: Json[] = [];
    const say = (field: string, level: string, words: string, item?: number) => {
      out.push(item === undefined ? { field, level, words } : { field, item, level, words });
    };
    const limits = (field: EditorField, text: string, item?: number) => {
      if (field.maxChars > 0 && countChars(text) > field.maxChars) {
        say(field.id, "error", `At most ${field.maxChars} characters.`, item);
      }
      if (field.maxWords > 0 && countWords(text) > field.maxWords) {
        say(field.id, "warning", `Over ${field.maxWords} words: it may be cut short.`, item);
      }
    };
    for (const field of doc.fields) {
      if (field.kind === "shown") {
        continue;
      }
      const value = field.id in sent ? sent[field.id] as Json : field.value;
      if (!fits(field, value)) {
        say(field.id, "error", "Not a value this field takes.");
        continue;
      }
      if (field.locked !== "" && JSON.stringify(value) !== JSON.stringify(field.value)) {
        say(field.id, "error", "Locked: it cannot be changed here.");
        continue;
      }
      if (typeof value === "string") {
        limits(field, value);
        if (field.kind === "line" && value.trim() === "" && !field.nullable) {
          say(field.id, "error", "Needs a few words.");
        }
      }
      if (Array.isArray(value)) {
        if (field.maxItems > 0 && value.length > field.maxItems) {
          say(field.id, "error", `At most ${field.maxItems} items.`);
        }
        value.forEach((raw, i) => {
          const item = listItem(raw);
          limits(field, item.text, i);
          if (item.text.trim() === "") {
            say(field.id, "error", "An empty item.", i);
          }
        });
        const kept = (Array.isArray(field.value) ? field.value : []).map(listItem).filter((it) => it.locked !== "");
        for (const locked of kept) {
          if (!value.map(listItem).some((it) => it.locked === locked.locked && it.text === locked.text &&
                                                 JSON.stringify(it.key) === JSON.stringify(locked.key))) {
            say(field.id, "error", "A locked item was changed or removed.");
          }
        }
      }
      if (typeof value === "number" && ((field.min !== undefined && value < field.min) ||
                                        (field.max !== undefined && value > field.max))) {
        say(field.id, "error", `From ${field.min ?? "any"} to ${field.max ?? "any"}.`);
      }
    }
    if (JSON.stringify(sent).length > 8192) {
      out.push({ level: "warning", words: "The document is long: over 8 KB." });
    }
    return out;
  }
}
