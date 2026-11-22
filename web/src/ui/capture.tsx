// SPDX-License-Identifier: MIT
// capture.tsx: a row action that captures first (protocol 1.4, G37, D209): a key, with its modifiers, or a pad chord
// from two pickers, then sent as the action's "value". The browser has no pad in v1 (§5.9), so a chord is chosen, never
// held. Escape cancels, as in any modal.
import { useState } from "preact/hooks";
import { chordValue, keyText, PAD_BUTTONS } from "../model/keys.ts";
import { Modal } from "./modal.tsx";
import { domId } from "./view-state.ts";

export function CaptureDialog({ space, id, kind, title, onPick, onClose }: {
  space: string; id: string; kind: "key" | "chord" | null; title: string;
  onPick: (value: string | number) => void; onClose: () => void;
}) {
  const titleId = domId("capture-title", space, id);
  const [anchor, setAnchor] = useState(0);
  const [key, setKey] = useState(12);  // D-pad Up, then View: EldenNet's chord, the likeliest one
  const [words, setWords] = useState<string | null>(null);
  return (
    <Modal open={kind !== null} onClose={() => { setWords(null); onClose(); }} labelledBy={titleId} size="small">
      {kind === "key" && (
        <>
          <h2 id={titleId} class="modal__title">{title}</h2>
          <p class="prose">Press the key to use, with Ctrl, Shift or Alt if you like.</p>
          <button type="button" class="keycap keycap--wide" autofocus data-capture="key"
                  onKeyDown={(e) => {
                    if (e.key === "Escape" || e.key === "Tab") {
                      return;  // Escape cancels and Tab moves on, as in any modal
                    }
                    e.preventDefault();  // F5 and F11 come here, not to the browser, while this has focus (§5.9)
                    e.stopPropagation();
                    const picked = keyText(e);
                    if (picked !== undefined) {
                      onPick(picked);
                    } else if (!["Shift", "Control", "Alt", "Meta"].includes(e.key)) {
                      setWords("That key cannot be a hotkey.");
                    }
                  }}>
            Press a key…
          </button>
          {words !== null && <p class="row__note" role="status">{words}</p>}
          <p class="row__note">While this waits, F5 and F11 come here, not to the browser.</p>
          <div class="modal__actions">
            <button type="button" class="button button--secondary" onClick={onClose}>Cancel</button>
            <button type="button" class="button button--secondary" data-capture-none="" onClick={() => onPick("")}>
              No key
            </button>
          </div>
        </>
      )}
      {kind === "chord" && (
        <>
          <h2 id={titleId} class="modal__title">{title}</h2>
          <p class="prose">Choose the button held first, then the one pressed while it is held.</p>
          <label class="picker-row">
            <span>Held first</span>
            <select class="field" value={anchor} data-capture="anchor"
                    onChange={(e) => { setAnchor(Number((e.target as HTMLSelectElement).value)); setWords(null); }}>
              {PAD_BUTTONS.map((b, i) => <option key={b.bit} value={i}>{b.name}</option>)}
            </select>
          </label>
          <label class="picker-row">
            <span>Then pressed</span>
            <select class="field" value={key} data-capture="chord-key"
                    onChange={(e) => { setKey(Number((e.target as HTMLSelectElement).value)); setWords(null); }}>
              {PAD_BUTTONS.map((b, i) => <option key={b.bit} value={i}>{b.name}</option>)}
            </select>
          </label>
          {words !== null && <p class="row__note" role="status">{words}</p>}
          <div class="modal__actions">
            <button type="button" class="button button--secondary" onClick={onClose}>Cancel</button>
            <button type="button" class="button button--secondary" data-capture-none="" onClick={() => onPick(0)}>
              No pad chord
            </button>
            <button type="button" class="button button--primary" data-capture-use=""
                    onClick={() => {
                      if (anchor === key) {
                        setWords("Choose two different buttons.");
                        return;
                      }
                      onPick(chordValue(PAD_BUTTONS[anchor]!.bit, PAD_BUTTONS[key]!.bit));
                    }}>
              Use
            </button>
          </div>
        </>
      )}
    </Modal>
  );
}
