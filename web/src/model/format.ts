// SPDX-License-Identifier: MIT
// format.ts: a setting's value as both views write it, and each control's next value (DESIGN §6.1, §9.3; D162). Every
// rule here is grace-view's (src/grace-view/page_view.cpp: decimals_of, format, change), so the browser shows "1.05×",
// "2.0 s", "On" and a choice's label exactly as the game does, and sends the same command for the same press.
import type { Json, Setting, SettingType } from "../protocol.ts";

export function isNumeric(type: SettingType): boolean {
  return type === "integer" || type === "number" || type === "duration_s";
}

/** Decimals a step shows: 0.05 two, 0.5 one, 1 none (at most four). */
export function decimalsOf(step: number): number {
  let d = 0;
  while (d < 4) {
    const x = step * Math.pow(10, d);
    if (Math.abs(x - roundHalfAway(x)) <= 1e-9) {
      break;
    }
    ++d;
  }
  return d;
}

/** The decimals of a number setting's grid (typed.h's grid_decimals): the finer of its step's and its minimum's, so
 * that a value rounded to them stays on the grid the settings store checks (D266). */
export function gridDecimals(s: Setting): number {
  const step = decimalsOf(s.step ?? 1);
  return s.min !== undefined ? Math.max(step, decimalsOf(s.min)) : step;
}

/** std::round: halves away from zero. */
export function roundHalfAway(x: number): number {
  return x < 0 ? -Math.round(-x) : Math.round(x);
}

// The exact binary value of a finite double: m × 2^e, m a non-negative integer.
function exactParts(x: number): { m: bigint; e: number } {
  const view = new DataView(new ArrayBuffer(8));
  view.setFloat64(0, Math.abs(x));
  const bits = view.getBigUint64(0);
  const exponent = Number((bits >> 52n) & 0x7ffn);
  const fraction = bits & ((1n << 52n) - 1n);
  return exponent === 0 ? { m: fraction, e: -1074 } : { m: fraction | (1n << 52n), e: exponent - 1075 };
}

/**
 * printf("%.*f", d, x) as glibc writes it: the exact value rounded to d decimals, an exact tie to even (JavaScript's
 * toFixed takes a tie up, so ties are settled here), and negative zero keeping its sign.
 */
export function fixed(x: number, d: number): string {
  if (!Number.isFinite(x)) {
    return Number.isNaN(x) ? "nan" : x > 0 ? "inf" : "-inf";
  }
  const negative = x < 0 || Object.is(x, -0);
  const { m, e } = exactParts(x);
  const scale = 10n ** BigInt(d);
  let q: bigint;
  if (e >= 0) {
    q = (m << BigInt(e)) * scale;
  } else {
    const num = m * scale;
    const shift = BigInt(-e);
    q = num >> shift;
    const rest = num - (q << shift);
    const half = 1n << (shift - 1n);
    if (rest > half || (rest === half && (q & 1n) === 1n)) {
      q += 1n;
    }
  }
  let digits = q.toString();
  if (d > 0) {
    digits = digits.padStart(d + 1, "0");
    digits = digits.slice(0, digits.length - d) + "." + digits.slice(digits.length - d);
  }
  return (negative ? "-" : "") + digits;
}

const TRI_VALUES = ["on", "off"] as const;

/** A tri-state's three values, in grace-view's order: on, off, and its third (auto, or inherit, D163). */
export function triValues(s: Setting): [string, string, string] {
  return [TRI_VALUES[0], TRI_VALUES[1], s.third === "inherit" ? "inherit" : "auto"];
}

export function triLabels(s: Setting): [string, string, string] {
  return ["On", "Off", s.third === "inherit" ? "Inherit" : "Auto"];
}

/** A value as the views write it ("2.5 s", "1.05×", "On", a choice's label, "Set"). */
export function formatValue(s: Setting, v: Json | undefined): string {
  switch (s.type) {
    case "bool":
      return v === true ? "On" : "Off";
    case "integer":
    case "number":
    case "duration_s": {
      const n = typeof v === "number" ? v : 0;
      let out = fixed(n, gridDecimals(s));
      if (s.unit !== undefined) {
        const first = s.unit.charCodeAt(0);
        const word = s.unit.length > 0 && first >= 97 && first <= 122;  // a unit that is a word takes a space
        out += (word ? " " : "") + s.unit;
      }
      return out;
    }
    case "choice":
      for (const c of s.choices ?? []) {
        if (typeof v === "string" && c.value === v) {
          return c.label;
        }
      }
      return "";
    case "tri_state": {
      const values = triValues(s);
      const labels = triLabels(s);
      for (let i = 0; i < 3; ++i) {
        if (v === values[i]) {
          return labels[i] ?? "";
        }
      }
      return "";
    }
    case "secret":
      return typeof v === "string" && v.length > 0 ? "Set" : "Missing";  // §4.2.4's words
    case "key_name":
    case "text":
      return typeof v === "string" ? v : "";
  }
  return "";
}

/**
 * The value a press gives (grace-view's change): a switch flips; a number moves by its step (ten with Shift), held to
 * its bounds and rounded to the step's decimals, whole for an integer; a choice or a tri-state moves to the next value,
 * wrapping. Undefined for a key, a text or a secret, which are typed.
 */
export function nextValue(s: Setting, v: Json | undefined, direction: 1 | -1, shift = false): Json | undefined {
  switch (s.type) {
    case "bool":
      return !(v === true);
    case "integer":
    case "number":
    case "duration_s": {
      const step = (s.step ?? 1) * (shift ? 10 : 1);
      let n = (typeof v === "number" ? v : 0) + direction * step;
      n = Math.min(Math.max(n, s.min ?? n), s.max ?? n);
      const scale = Math.pow(10, gridDecimals(s));  // on the store's grid (D266)
      n = roundHalfAway(n * scale) / scale;
      return s.type === "integer" ? roundHalfAway(n) : n;
    }
    case "choice": {
      const choices = s.choices ?? [];
      if (choices.length === 0) {
        return undefined;
      }
      let at = 0;
      choices.forEach((c, i) => {
        if (typeof v === "string" && c.value === v) {
          at = i;
        }
      });
      const n = choices.length;
      return choices[(at + (direction > 0 ? 1 : n - 1)) % n]?.value;
    }
    case "tri_state": {
      const values = triValues(s);
      let at = 2;
      values.forEach((value, i) => {
        if (v === value) {
          at = i;
        }
      });
      return values[(at + (direction > 0 ? 1 : 2)) % 3];
    }
    default:
      return undefined;
  }
}

/** JSON equality, for "changed from its default" (grace-view compares the JSON values). */
export function sameJson(a: Json | undefined, b: Json | undefined): boolean {
  if (a === b) {
    return true;
  }
  if (a === undefined || b === undefined || a === null || b === null || typeof a !== "object" || typeof b !== "object") {
    return false;
  }
  if (Array.isArray(a) !== Array.isArray(b)) {
    return false;
  }
  if (Array.isArray(a) && Array.isArray(b)) {
    return a.length === b.length && a.every((x, i) => sameJson(x, b[i]));
  }
  const ao = a as { [key: string]: Json };
  const bo = b as { [key: string]: Json };
  const keys = Object.keys(ao);
  return keys.length === Object.keys(bo).length && keys.every((k) => k in bo && sameJson(ao[k], bo[k]));
}
