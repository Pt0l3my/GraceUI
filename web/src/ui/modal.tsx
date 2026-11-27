// SPDX-License-Identifier: MIT
// modal.tsx: a modal of the native kind (§3.8, §5.9), and the confirmation an action that asks first shows: a header
// action's (§7.3, D173), a section's own (D202) or a table's row action's (D192), whose button may wait (D274).
import { useEffect, useLayoutEffect, useRef, useState } from "preact/hooks";
import type { ComponentChildren } from "preact";
import { confirmWait } from "../model/confirm.ts";
import { domId } from "./view-state.ts";

/** A modal of the native kind: focus held inside, Escape closes it, focus back where it was (§3.8, §5.9). */
export function Modal({ open, onClose, labelledBy, size, children }: {
  open: boolean; onClose: () => void; labelledBy: string; size: "small" | "medium"; children: ComponentChildren;
}) {
  const dialog = useRef<HTMLDialogElement>(null);
  const opener = useRef<Element | null>(null);
  useLayoutEffect(() => {
    const d = dialog.current;
    if (!d) {
      return;
    }
    if (open && !d.open) {
      opener.current = document.activeElement;
      d.showModal();
    } else if (!open && d.open) {
      d.close();
    }
  }, [open]);
  return (
    <dialog ref={dialog} class={`modal modal--${size}`} aria-labelledby={labelledBy}
            onClose={() => {
              onClose();
              const back = opener.current as HTMLElement | null;
              opener.current = null;
              back?.focus?.();
            }}>
      {open && children}
    </dialog>
  );
}

/** What asks to be confirmed: a header action (§7.3, D173) or a table's row action (D192). */
export interface Confirmable {
  id: string;
  label: string;
  tone: "primary" | "secondary" | "destructive";
  confirm?: string;
  confirm_after_ms?: number;  // 1.21 (D274): its button counts a press only this long after the dialog opens
}

/** The action's own button in its confirmation: greyed until its wait is over, its words there at once (D274). */
function ConfirmButton<T extends Confirmable>({ action, onPress }: { action: T; onPress: () => void }) {
  const wait = confirmWait(action);
  const [ready, setReady] = useState(wait === 0);
  useEffect(() => {
    if (wait === 0) {
      return undefined;
    }
    const timer = setTimeout(() => setReady(true), wait);
    return () => clearTimeout(timer);
  }, [wait]);
  return (
    <button type="button" class={`button button--${action.tone === "destructive" ? "danger" : action.tone}`}
            data-confirm={action.id} disabled={!ready}
            onClick={() => {
              if (ready) {
                onPress();
              }
            }}>
      {action.label}
    </button>
  );
}

/** The second press of an action that asks first: its words, Cancel focused, and the action's own button. */
export function ConfirmDialog<T extends Confirmable>({ space, action, onClose, onConfirm }: {
  space: string; action: T | null; onClose: () => void; onConfirm: (action: T) => void;
}) {
  const titleId = domId("confirm-title", space, action?.id ?? "none");
  return (
    <Modal open={action !== null} onClose={onClose} labelledBy={titleId} size="small">
      {action && (
        <>
          <h2 id={titleId} class="modal__title">{action.label}</h2>
          <p class="prose">{action.confirm ?? "This cannot be undone."}</p>
          <div class="modal__actions">
            <button type="button" class="button button--secondary" autofocus onClick={onClose}>Cancel</button>
            <ConfirmButton key={action.id} action={action} onPress={() => {
              onClose();
              onConfirm(action);
            }} />
          </div>
        </>
      )}
    </Modal>
  );
}
