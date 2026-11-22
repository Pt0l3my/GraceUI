// SPDX-License-Identifier: MIT
// editor.ts: the editor block's model in the browser (D217, G38, proposal 0010 §3.3 and §7), as grace-view's editor.h
// has it in game: the document a space publishes on the block's topic, read tolerantly (a malformed field is left out,
// never guessed at), and the player's unsaved edits over it, kept in memory for the session by the document's topic,
// never in browser storage (G38 (2), PV3). A draft lives while its page is open.
import { signal } from "@preact/signals";
import type { Json, JsonObject } from "../protocol.ts";

export type FieldKind = "line" | "prose" | "list" | "choice" | "pick" | "number" | "switch" | "shown";
const KINDS: readonly FieldKind[] = ["line", "prose", "list", "choice", "pick", "number", "switch", "shown"];

export interface Choice {
  value: string;
  label: string;
}

export interface EditorField {
  id: string;
  label: string;
  kind: FieldKind;
  help: string;
  /** as published: a string, a list, a number, a bool, or null ("not set here") */ value: Json;
  /** read-only beside the field, never sent */ reference: string[];
  referenceLabel: string;
  maxWords: number;
  maxChars: number;
  maxItems: number;
  min?: number;
  max?: number;
  choices: Choice[];
  choicesFrom: string;
  /** words: shown, never edited, sent back as published */ locked: string;
  nullable: boolean;
}

export interface EditorDocument {
  target: string;
  rev: number;
  stateWord: string;
  stateTone: string;
  stateLine: string;
  draftBlocked: string;
  draftHelp: string;
  fields: EditorField[];
}

export interface EditorMessage {
  field: string;
  item?: number;
  error: boolean;
  words: string;
}

export interface EditorState {
  baseRev: number;
  edits: Readonly<Record<string, Json>>;
  messages: readonly EditorMessage[];
  top: string;
  draft: Readonly<Record<string, Json>>;
  notice: string;
  notes: string;
  conflict: boolean;
}

export const FRESH: EditorState = {
  baseRev: 0, edits: {}, messages: [], top: "", draft: {}, notice: "", notes: "", conflict: false,
};

function isObject(v: Json | undefined): v is JsonObject {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

function textOf(o: JsonObject, name: string, max = 1024): string {
  const v = o[name];
  return typeof v === "string" && new TextEncoder().encode(v).length <= max ? v : "";
}

function sizeOf(o: JsonObject, name: string): number {
  const v = o[name];
  return typeof v === "number" && Number.isInteger(v) && v >= 0 ? Math.min(v, 100000) : 0;
}

function isItem(item: Json): boolean {
  return typeof item === "string" || (isObject(item) && typeof item.text === "string");
}

/** Whether a value suits its field's kind (null only where the field may be left unset). */
export function fits(field: EditorField, value: Json | undefined): boolean {
  if (value === null) {
    return field.nullable || field.kind === "shown";
  }
  switch (field.kind) {
    case "line":
    case "prose":
    case "choice":
    case "pick":
    case "shown":
      return typeof value === "string";
    case "list":
      return Array.isArray(value) && value.every(isItem);
    case "number":
      return typeof value === "number" && Number.isFinite(value);
    case "switch":
      return typeof value === "boolean";
  }
  return false;
}

/** The document of a topic's body; null if it is none (no fields array, or no target). */
export function parseDocument(body: JsonObject | undefined): EditorDocument | null {
  if (!body || !Array.isArray(body.fields)) {
    return null;
  }
  const target = textOf(body, "target", 256);
  if (target === "") {
    return null;
  }
  const state = isObject(body.state) ? body.state : {};
  const doc: EditorDocument = {
    target,
    rev: typeof body.rev === "number" && Number.isInteger(body.rev) && body.rev >= 0 ? body.rev : 0,
    stateWord: textOf(state, "word", 64),
    stateTone: textOf(state, "tone", 16),
    stateLine: textOf(state, "line", 256),
    draftBlocked: textOf(body, "draft_blocked", 256),
    draftHelp: textOf(body, "draft_help", 512),
    fields: [],
  };
  for (const item of body.fields) {
    if (!isObject(item) || doc.fields.length >= 64) {
      continue;
    }
    const id = textOf(item, "id", 64);
    const label = textOf(item, "label", 128);
    const kind = textOf(item, "kind", 16) as FieldKind;
    if (id === "" || label === "" || !KINDS.includes(kind) || doc.fields.some((f) => f.id === id)) {
      continue;  // no id, label or known kind, or a second of one id: left out, never guessed at
    }
    const reference = typeof item.reference === "string" ? [item.reference]
      : Array.isArray(item.reference) && item.reference.every((r) => typeof r === "string") ? item.reference as string[]
      : [];
    const field: EditorField = {
      id, label, kind,
      help: textOf(item, "help", 512),
      value: "value" in item ? (item.value as Json) : null,
      reference,
      referenceLabel: textOf(item, "reference_label", 64),
      maxWords: sizeOf(item, "max_words"),
      maxChars: sizeOf(item, "max_chars"),
      maxItems: sizeOf(item, "max_items"),
      min: typeof item.min === "number" ? item.min : undefined,
      max: typeof item.max === "number" ? item.max : undefined,
      choices: (Array.isArray(item.choices) ? item.choices : []).filter(isObject)
        .map((c) => ({ value: textOf(c, "value", 200), label: textOf(c, "label", 128) }))
        .filter((c) => c.value !== ""),
      choicesFrom: textOf(item, "choices_from", 64),
      locked: textOf(item, "locked", 256),
      nullable: item.nullable === true,
    };
    if (!fits(field, field.value)) {
      continue;  // a value of another kind's: the field is left out
    }
    doc.fields.push(field);
  }
  return doc;
}

export function countWords(text: string): number {
  return text.split(" ").filter((w) => w !== "").length;
}

export function countChars(text: string): number {
  return [...text].length;
}

/** One paragraph: line breaks and other control characters as spaces (D217's prose; G38 (3)). */
export function oneParagraph(text: string): string {
  // eslint-disable-next-line no-control-regex
  return text.replace(/[\u0000-\u001f\u007f]/g, " ");
}

export interface ListItem {
  text: string;
  choice: string;
  key: Json;
  locked: string;
}

export function listItem(item: Json): ListItem {
  if (typeof item === "string") {
    return { text: item, choice: "", key: null, locked: "" };
  }
  if (isObject(item)) {
    return { text: textOf(item, "text", 4096), choice: textOf(item, "choice", 200),
             key: "key" in item ? (item.key as Json) : null, locked: textOf(item, "locked", 256) };
  }
  return { text: "", choice: "", key: null, locked: "" };
}

export function listJson(item: ListItem, withChoice: boolean): Json {
  if (!withChoice && item.key === null && item.locked === "") {
    return item.text;  // a plain item stays a string
  }
  const out: JsonObject = { text: item.text };
  if (withChoice) {
    out.choice = item.choice;
  }
  if (item.key !== null) {
    out.key = item.key;
  }
  if (item.locked !== "") {
    out.locked = item.locked;
  }
  return out;
}

function same(a: Json | undefined, b: Json | undefined): boolean {
  return JSON.stringify(a) === JSON.stringify(b);
}

/** A field's value as the view shows it: the player's edit, or as published. */
export function valueOf(state: EditorState, field: EditorField): Json {
  return field.id in state.edits ? state.edits[field.id] as Json : field.value;
}

export function dirty(state: EditorState): boolean {
  return Object.keys(state.edits).length > 0;
}

/** The document as check and save send it: {id: value} for every field but a shown one. */
export function docOf(state: EditorState, doc: EditorDocument): JsonObject {
  const out: JsonObject = {};
  for (const field of doc.fields) {
    if (field.kind !== "shown") {
      out[field.id] = valueOf(state, field);
    }
  }
  return out;
}

export function messagesOf(body: JsonObject | undefined): EditorMessage[] {
  const out: EditorMessage[] = [];
  for (const m of Array.isArray(body?.messages) ? body.messages : []) {
    if (!isObject(m) || out.length >= 64) {
      continue;
    }
    const words = textOf(m, "words", 256);
    if (words === "") {
      continue;
    }
    const item = typeof m.item === "number" && Number.isInteger(m.item) && m.item >= 0 ? m.item : undefined;
    out.push({ field: textOf(m, "field", 64), item, error: textOf(m, "level", 16) !== "warning", words });
  }
  return out;
}

export function draftOf(doc: EditorDocument, body: JsonObject | undefined): Record<string, Json> {
  const out: Record<string, Json> = {};
  const draft = body?.draft;
  if (!isObject(draft)) {
    return out;
  }
  for (const [id, value] of Object.entries(draft)) {
    const field = doc.fields.find((f) => f.id === id);
    if (field && field.kind !== "shown" && field.locked === "" && value !== null && fits(field, value)) {
      out[id] = value;  // only fields the document has, of their own kinds
    }
  }
  return out;
}

// ---- The store: every editor's state, by "space\u0000topic", for the session.

export const editors = signal<ReadonlyMap<string, EditorState>>(new Map());

export function editorKey(space: string, topic: string): string {
  return space + "\u0000" + topic;
}

export function stateOf(key: string): EditorState {
  return editors.value.get(key) ?? FRESH;
}

export function update(key: string, change: (state: EditorState) => EditorState): void {
  const next = new Map(editors.value);
  next.set(key, change(stateOf(key)));
  editors.value = next;
}

/** Sets a field's value: an edit, or none if it is the published value again; the base revision follows. */
export function setValue(key: string, doc: EditorDocument, field: EditorField, value: Json): void {
  if (field.locked !== "" || field.kind === "shown") {
    return;
  }
  update(key, (st) => {
    const edits: Record<string, Json> = { ...st.edits };
    if (same(value, field.value)) {
      delete edits[field.id];
    } else {
      edits[field.id] = value;
    }
    const any = Object.keys(edits).length > 0;
    return { ...st, edits, baseRev: any ? (st.baseRev !== 0 ? st.baseRev : doc.rev) : 0 };
  });
}

/** Everything let go, as after a save, a reset or a reload; the draft's notes stay. */
export function cleared(st: EditorState): EditorState {
  return { ...FRESH, notes: st.notes };
}

/** Whether any editor holds unsaved edits (the browser's own prompt before the tab closes). */
export function anyDirty(): boolean {
  for (const st of editors.value.values()) {
    if (dirty(st)) {
      return true;
    }
  }
  return false;
}

/** A page left: its drafts go (proposal 0010 §3.3); the edits stay for the session. */
export function dropDrafts(): void {
  let changed = false;
  const next = new Map(editors.value);
  for (const [key, st] of next) {
    if (Object.keys(st.draft).length > 0 || st.notice !== "") {
      next.set(key, { ...st, draft: {}, notice: "" });
      changed = true;
    }
  }
  if (changed) {
    editors.value = next;
  }
}
