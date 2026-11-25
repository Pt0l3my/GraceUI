// SPDX-License-Identifier: MIT
// typed.ts: a number typed into a slider's field (DESIGN §3.8, D266), as grace-view's typed.cpp reads it: the text the
// field opens with, and what a typed text becomes, a value the settings store will take or the words that refuse it.
// Both follow fixtures/typed-cases.json (web/tests/unit/typed.test.ts and grace-view's typed_tests.cpp).
import type { Json, Setting } from "../protocol.ts";
import { decimalsOf, fixed, formatValue, roundHalfAway } from "./format.ts";

/** The most a slider's field holds (typed.h's kTypedNumberMax). */
export const TYPED_NUMBER_MAX = 32;

export type Typed =
  | { ok: true; value: Json }
  | { ok: false; words: string };  // words empty: nothing typed, nothing said

/** The text a slider's field opens with: the value at its step's decimals, without its unit ("1.25"). */
export function plainNumber(s: Setting, n: number): string {
  return fixed(n, decimalsOf(s.step ?? 1));
}

function trimmed(text: string): string {
  let a = 0;
  let b = text.length;
  while (a < b && text[a] === " ") {
    ++a;
  }
  while (b > a && text[b - 1] === " ") {
    --b;
  }
  return text.slice(a, b);
}

function boundsWords(s: Setting): string {
  if (s.min !== undefined && s.max !== undefined) {
    return `Type a number from ${formatValue(s, s.min)} to ${formatValue(s, s.max)}.`;
  }
  if (s.min !== undefined) {
    return `Type a number of ${formatValue(s, s.min)} or more.`;
  }
  if (s.max !== undefined) {
    return `Type a number of ${formatValue(s, s.max)} or less.`;
  }
  return "Type a number.";
}

const NUMBER = /^[+-]?(?:[0-9]+(?:[.,][0-9]*)?|[.,][0-9]+)$/;

/** What a typed text becomes for an integer, a number or a duration: typed.h's typed_value, rule for rule. */
export function typedValue(s: Setting, raw: string): Typed {
  let text = trimmed(raw);
  if (s.unit !== undefined && s.unit !== "" && text.endsWith(s.unit)) {
    text = trimmed(text.slice(0, text.length - s.unit.length));
  } else if (s.unit === "×" && (text.endsWith("x") || text.endsWith("X"))) {
    text = trimmed(text.slice(0, -1));  // "1.3x": the sign typed as the letter
  }
  if (text === "") {
    return { ok: false, words: "" };
  }
  if (text.length > TYPED_NUMBER_MAX || !NUMBER.test(text)) {
    return { ok: false, words: boundsWords(s) };
  }
  const x = Number(text.replace(",", ".").replace(/^\+/, ""));
  if (!Number.isFinite(x)) {
    return { ok: false, words: boundsWords(s) };
  }
  if ((s.min !== undefined && x < s.min) || (s.max !== undefined && x > s.max)) {
    return { ok: false, words: boundsWords(s) };
  }
  let n = x;
  if (s.step !== undefined && s.step > 0) {
    // The nearest step on the grid the settings store checks (from the minimum, or 0), within the bounds.
    const step = s.step;
    const from = s.min ?? 0;
    let k = roundHalfAway((x - from) / step);
    if (s.max !== undefined) {
      k = Math.min(k, Math.floor((s.max - from) / step + 1e-9));
    }
    if (s.min !== undefined) {
      k = Math.max(k, 0);
    }
    n = from + k * step;
    const scale = Math.pow(10, decimalsOf(step));
    n = roundHalfAway(n * scale) / scale;
  }
  if (n === 0) {
    n = 0;  // never a negative zero
  }
  return { ok: true, value: s.type === "integer" ? roundHalfAway(n) : n };
}
