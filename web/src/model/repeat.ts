// SPDX-License-Identifier: MIT
// repeat.ts: what a held key's repeat does in the browser view (DESIGN §5.9, D270), as grace-view's repeat_acts does in
// game: it moves along and steps a slider, and nothing more. A switch, a choice, a tab, a tree's category, a list's row
// into its detail, a button's Enter or Space, Esc, a page or a tab turned, search opened: once a press. A key capture
// takes no repeat. In a text field every key but Enter and Esc repeats, as in any field; elsewhere a held arrow still
// scrolls the page, as the browser does.

/** The focused element, as far as a repeat goes. */
export interface RepeatTarget {
  role: string | null;  // its role attribute
  select: boolean;      // a <select>: its value moves with the arrows
  field: boolean;       // a text field: an <input> that takes text, a <textarea>, or editable content
  capture: boolean;     // a key capture waiting for its key
}

// Roles whose ← → act on the element itself: turn a switch, choose a tab or a radio, open or leave a tree's category,
// go from a list's row into its detail.
const SIDEWAYS_ACTS = new Set(["switch", "tab", "radio", "treeitem", "option"]);

// Keys whose repeat does nothing of the view's own: the browser's Tab order, and the modifiers.
const PASSES = new Set(["Tab", "Shift", "Control", "Alt", "Meta"]);

/** Whether a held key's repeat may act on `at`; false: the repeat is dropped. */
export function repeatActs(key: string, at: RepeatTarget): boolean {
  if (at.capture || key === "Enter" || key === "Escape") {
    return false;
  }
  if (at.field) {
    return true;  // the caret, Backspace, Delete and the characters typed
  }
  if (key === "ArrowUp" || key === "ArrowDown") {
    return at.role !== "radio" && !at.select;
  }
  if (key === "ArrowLeft" || key === "ArrowRight") {
    return !SIDEWAYS_ACTS.has(at.role ?? "") && !at.select;  // a slider steps; elsewhere the page scrolls
  }
  return PASSES.has(key);
}

/** The repeat's view of a key event's target. */
export function repeatTarget(target: EventTarget | null): RepeatTarget {
  if (typeof HTMLElement === "undefined" || !(target instanceof HTMLElement)) {
    return { role: null, select: false, field: false, capture: false };
  }
  const field = target.isContentEditable || target instanceof HTMLTextAreaElement ||
                (target instanceof HTMLInputElement && !["checkbox", "radio", "button", "range"].includes(target.type));
  return {
    role: target.getAttribute("role"),
    select: target instanceof HTMLSelectElement,
    field,
    capture: target.closest("[data-capturing], [data-capture='key']") !== null,
  };
}
