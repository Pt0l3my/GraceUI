// SPDX-License-Identifier: MIT
// keys.ts: key capture in the browser (DESIGN §5.9): a key's KeyboardEvent.code mapped to the framework's key name.
// Letters and digits drop their "Key" and "Digit" prefixes ("F9", "A", "7"); every other key keeps the code's own name
// ("PageUp", "Numpad1", "Backquote"). Escape cancels a capture and is never a key, as GraceUI's hotkeys refuse it
// (grace-input's reserved keys), and so are the modifiers on their own.

const REFUSED = new Set([
  "Escape", "ShiftLeft", "ShiftRight", "ControlLeft", "ControlRight", "AltLeft", "AltRight", "MetaLeft", "MetaRight",
  "OSLeft", "OSRight", "ContextMenu", "Fn", "FnLock", "CapsLock", "NumLock", "ScrollLock",
]);

/** The framework's name for a KeyboardEvent.code, or undefined for a key that cannot be bound. */
export function keyName(code: string): string | undefined {
  if (code.length === 0 || REFUSED.has(code) || !/^[A-Za-z0-9]+$/.test(code)) {
    return undefined;
  }
  if (/^Key[A-Z]$/.test(code)) {
    return code.slice(3);
  }
  if (/^Digit[0-9]$/.test(code)) {
    return code.slice(5);
  }
  return code;
}

/** Keys a browser keeps for itself, which a capture takes only while its field holds focus (§5.9). */
export const BROWSER_KEYS = new Set(["F5", "F11"]);

/** Tab leaves a capture without binding anything, so the keyboard is never trapped in the field. */
export const LEAVES_CAPTURE = "Tab";
