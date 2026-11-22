// SPDX-License-Identifier: MIT
// keys.ts: key capture in the browser (DESIGN §5.9): a key's KeyboardEvent.code mapped to the framework's key name.
// Letters and digits drop their "Key" and "Digit" prefixes ("F9", "A", "7"); every other key keeps the code's own name
// ("PageUp", "Numpad1", "Backquote"). Escape cancels a capture and is never a key, as GraceUI's hotkeys refuse it
// (grace-input's reserved keys), and so are the modifiers on their own.

const REFUSED = new Set([
  "Escape", "ShiftLeft", "ShiftRight", "ControlLeft", "ControlRight", "AltLeft", "AltRight", "MetaLeft", "MetaRight",
  "OSLeft", "OSRight", "ContextMenu", "Fn", "FnLock", "CapsLock", "NumLock", "ScrollLock",
]);

// The codes whose names GraceUI shortens to fit a key's name (1 to 12 letters and digits, D206).
const SHORTER: { [code: string]: string } = {
  NumpadMultiply: "NumpadStar", NumpadSubtract: "NumpadMinus", NumpadAdd: "NumpadPlus", NumpadDecimal: "NumpadDot",
  NumpadDivide: "NumpadSlash", IntlBackslash: "Oem102",
};

/** The framework's name for a KeyboardEvent.code, or undefined for a key that cannot be bound. */
export function keyName(code: string): string | undefined {
  if (code.length === 0 || REFUSED.has(code) || !/^[A-Za-z0-9]+$/.test(code)) {
    return undefined;
  }
  if (SHORTER[code] !== undefined) {
    return SHORTER[code];
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

/** A hotkey's key with its modifiers, as GraceUI writes it ("Ctrl+Shift+K"), or undefined for a key that cannot be bound
 * (G37, D209). */
export function keyText(e: { code: string; ctrlKey: boolean; shiftKey: boolean; altKey: boolean }): string | undefined {
  const name = keyName(e.code);
  if (name === undefined) {
    return undefined;
  }
  return `${e.ctrlKey ? "Ctrl+" : ""}${e.shiftKey ? "Shift+" : ""}${e.altKey ? "Alt+" : ""}${name}`;
}

/** XInput's fourteen buttons as a pad chord's pickers offer them (D209), with their bits. */
export const PAD_BUTTONS: readonly { bit: number; name: string }[] = [
  { bit: 0x0001, name: "D-pad Up" }, { bit: 0x0002, name: "D-pad Down" }, { bit: 0x0004, name: "D-pad Left" },
  { bit: 0x0008, name: "D-pad Right" }, { bit: 0x1000, name: "A" }, { bit: 0x2000, name: "B" }, { bit: 0x4000, name: "X" },
  { bit: 0x8000, name: "Y" }, { bit: 0x0100, name: "LB" }, { bit: 0x0200, name: "RB" }, { bit: 0x0040, name: "LS" },
  { bit: 0x0080, name: "RS" }, { bit: 0x0020, name: "View" }, { bit: 0x0010, name: "Menu" },
];

/** A chord as register_hotkey takes it: the button held first in the high 16 bits, the one pressed then in the low. */
export function chordValue(anchor: number, key: number): number {
  return ((anchor & 0xffff) * 0x10000) + (key & 0xffff);
}

