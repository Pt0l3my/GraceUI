// SPDX-License-Identifier: MIT
// document.ts: a document block's body (G44, D251), checked and put in one shape: its title and its blocks (headings,
// paragraphs, lists, notes, code, plain tables), each paragraph a list of spans (plain, strong, code, or a link to
// another document of the family, perhaps to one of its headings; never a URL). Pure. grace-view's document.cpp does
// the same, and both run fixtures/document-cases.json. A block that breaks a rule is left out; a document over 64 KiB
// is not drawn at all.
import type { Json, JsonObject } from "../protocol.ts";

export type SpanStyle = "plain" | "strong" | "code" | "link";
export interface Span { text: string; style: SpanStyle; doc?: string; heading?: string }
export type DocBlock =
  | { kind: "heading"; level: 2 | 3; text: string }
  | { kind: "paragraph"; spans: Span[] }
  | { kind: "list"; ordered: boolean; items: Span[][] }
  | { kind: "note"; spans: Span[] }
  | { kind: "code"; text: string }
  | { kind: "table"; columns: { id: string; label: string }[]; rows: string[][] };
export interface DocumentView { title: string; blocks: DocBlock[]; error?: "too_large" | "not_a_document" }

export const MAX_BYTES = 64 * 1024;
export const MAX_BLOCKS = 400;
export const MAX_TEXT = 2000;       // a paragraph's, a list item's or a note's characters
export const MAX_HEADING = 120;
export const MAX_CODE = 16 * 1024;  // bytes
export const MAX_ITEMS = 100;
export const MAX_COLUMNS = 8;
export const MAX_ROWS = 200;

const DOC_ID = /^[a-z0-9._-]{1,64}$/;
const encoder = new TextEncoder();

function chars(text: string): number {
  return [...text].length;
}

function isObject(v: Json | undefined): v is JsonObject {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

/** A paragraph's spans: a plain string is one plain span; a span with no text, or with more than one style, is left
 *  out; a whole paragraph past MAX_TEXT characters, or with no span left, is none. */
export function spansOf(v: Json | undefined): Span[] | null {
  const list: Json[] = typeof v === "string" ? [{ text: v }] : Array.isArray(v) ? v : [];
  const out: Span[] = [];
  let total = 0;
  for (const s of list) {
    if (!isObject(s) || typeof s.text !== "string" || s.text === "") {
      continue;
    }
    const strong = s.strong === true;
    const code = s.code === true;
    const link = typeof s.doc === "string";
    if (Number(strong) + Number(code) + Number(link) > 1) {
      continue;
    }
    if (link && !DOC_ID.test(s.doc as string)) {
      continue;
    }
    const span: Span = { text: s.text, style: strong ? "strong" : code ? "code" : link ? "link" : "plain" };
    if (link) {
      span.doc = s.doc as string;
      if (typeof s.heading === "string" && s.heading !== "" && chars(s.heading) <= MAX_HEADING) {
        span.heading = s.heading;
      }
    }
    total += chars(s.text);
    out.push(span);
  }
  return out.length > 0 && total <= MAX_TEXT ? out : null;
}

function cellWords(v: Json | undefined): string {
  return typeof v === "string" ? v : typeof v === "number" && Number.isFinite(v) ? JSON.stringify(v) : "";
}

function blockOf(b: Json): DocBlock | null {
  if (!isObject(b)) {
    return null;
  }
  const kinds = ["heading", "paragraph", "list", "note", "code", "table"].filter((k) => k in b);
  if (kinds.length !== 1) {
    return null;
  }
  switch (kinds[0]) {
    case "heading": {
      const level = b.level === undefined ? 2 : b.level;
      return typeof b.heading === "string" && b.heading !== "" && chars(b.heading) <= MAX_HEADING &&
             (level === 2 || level === 3) ? { kind: "heading", level, text: b.heading } : null;
    }
    case "paragraph":
    case "note": {
      const spans = spansOf(b[kinds[0]]);
      return spans ? { kind: kinds[0] as "paragraph" | "note", spans } : null;
    }
    case "list": {
      if (!Array.isArray(b.list) || b.list.length === 0 || b.list.length > MAX_ITEMS ||
          (b.ordered !== undefined && typeof b.ordered !== "boolean")) {
        return null;
      }
      const items: Span[][] = [];
      for (const item of b.list) {
        const spans = spansOf(item);
        if (!spans) {
          return null;
        }
        items.push(spans);
      }
      return { kind: "list", ordered: b.ordered === true, items };
    }
    case "code":
      return typeof b.code === "string" && b.code !== "" && encoder.encode(b.code).length <= MAX_CODE
        ? { kind: "code", text: b.code } : null;
    case "table": {
      const t = b.table;
      if (!isObject(t) || !Array.isArray(t.columns) || !Array.isArray(t.rows) || t.columns.length === 0 ||
          t.columns.length > MAX_COLUMNS || t.rows.length > MAX_ROWS) {
        return null;
      }
      const columns: { id: string; label: string }[] = [];
      for (const c of t.columns) {
        if (!isObject(c) || typeof c.id !== "string" || typeof c.label !== "string") {
          return null;
        }
        columns.push({ id: c.id, label: c.label });
      }
      const rows = t.rows.filter(isObject).map((r) => columns.map((c) => cellWords(r[c.id])));
      return { kind: "table", columns, rows };
    }
    default:
      return null;
  }
}

/** The body as the views draw it. */
export function parseDocument(body: Json | undefined): DocumentView {
  if (!isObject(body) || !Array.isArray(body.blocks)) {
    return { title: "", blocks: [], error: "not_a_document" };
  }
  if (encoder.encode(JSON.stringify(body)).length > MAX_BYTES) {
    return { title: "", blocks: [], error: "too_large" };
  }
  const title = typeof body.title === "string" && chars(body.title) <= MAX_HEADING ? body.title : "";
  const blocks: DocBlock[] = [];
  for (const b of body.blocks.slice(0, MAX_BLOCKS)) {
    const block = blockOf(b);
    if (block) {
      blocks.push(block);
    }
  }
  return { title, blocks };
}
