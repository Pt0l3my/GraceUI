// SPDX-License-Identifier: MIT
// upload.tsx: an upload block (G28, D226): a drop zone and a button that send one file with a space's file command, by
// the upload route on this PC alone (DESIGN §7.2 item 18). The file's name is shown here and never sent; the space's
// answer shows under the zone, a refusal's words as a form's do. On a paired device, or wherever the page has no secure
// context, it says where to add the file instead; on a greyed page it does nothing (D188).
import { useContext, useRef, useState } from "preact/hooks";
import type { PageBlock } from "../protocol.ts";
import { LIMITS } from "../protocol.ts";
import { refusalWords } from "../model/session.ts";
import { GreyedContext, useSession } from "./context.ts";
import { Warning } from "./icons.tsx";
import { domId } from "./view-state.ts";

type Status =
  | { kind: "idle" }
  | { kind: "sending"; name: string }
  | { kind: "sent"; name: string }
  | { kind: "refused"; words: string };

/** The extensions an upload block's `accept` lists (".toml, .txt"), lower case; none: any file. */
export function acceptedExtensions(accept: string | undefined): string[] {
  return (accept ?? "").split(",").map((e) => e.trim().toLowerCase()).filter((e) => e.startsWith("."));
}

/** A file's name as the zone shows it: at most 60 characters, never sent. */
function shown(name: string): string {
  return name.length <= 60 ? name : `${name.slice(0, 57)}…`;
}

function extensionWords(extensions: string[]): string {
  if (extensions.length === 1) {
    return `a ${extensions[0]} file`;
  }
  return `a ${extensions.slice(0, -1).join(", ")} or ${extensions[extensions.length - 1]} file`;
}

export function UploadBlock({ space, block }: { space: string; block: PageBlock }) {
  const session = useSession();
  const greyed = useContext(GreyedContext);
  const inert = greyed.words(greyed.page) !== null;
  const input = useRef<HTMLInputElement>(null);
  const [over, setOver] = useState(false);
  const [status, setStatus] = useState<Status>({ kind: "idle" });
  const command = block.command;
  if (command === undefined) {
    return null;
  }
  // Only this PC's own browser may send a file (§7.2 item 18): a paired device, as welcome names it, is told where to
  // go; so is any page without a secure context, which has no SHA-256 of its own to send (D114).
  const device = session.welcome.value?.view === "device" || globalThis.isSecureContext === false;
  if (device) {
    return (
      <div class="upload upload--off" data-upload={command}>
        <p class="upload__off">{block.submit}: only on the PC the game runs on. Open the browser view there to add a
          file.</p>
      </div>
    );
  }
  const extensions = acceptedExtensions(block.accept);
  const busy = status.kind === "sending";
  const send = async (files: FileList | null) => {
    const file = files?.[0];
    if (busy || inert || files === null || file === undefined) {
      return;
    }
    if (files.length > 1) {
      setStatus({ kind: "refused", words: "One file at a time." });
      return;
    }
    if (extensions.length > 0 && !extensions.some((e) => file.name.toLowerCase().endsWith(e))) {
      setStatus({ kind: "refused", words: `Choose ${extensionWords(extensions)}.` });
      return;
    }
    if (file.size > LIMITS.uploadMax) {
      setStatus({ kind: "refused", words: "The file is larger than 32 MiB." });
      return;
    }
    const name = shown(file.name);
    setStatus({ kind: "sending", name });
    const answer = await session.upload(space, command, {}, file);
    setStatus(answer.ok ? { kind: "sent", name } : { kind: "refused", words: refusalWords(answer) });
  };
  const id = domId("upload", space, command);
  return (
    <div class={`upload${over ? " upload--over" : ""}`} data-upload={command} aria-disabled={inert || undefined}
         onDragOver={(e) => {
           e.preventDefault();
           if (e.dataTransfer) {
             e.dataTransfer.dropEffect = inert || busy ? "none" : "copy";
           }
           setOver(!inert && !busy);
         }}
         onDragLeave={() => setOver(false)}
         onDrop={(e) => {
           e.preventDefault();
           setOver(false);
           void send(e.dataTransfer?.files ?? null);
         }}>
      <p class="upload__hint">
        {extensions.length > 0 ? `Drop ${extensionWords(extensions)} here, or choose one.` : "Drop a file here, or choose one."}
      </p>
      <input id={id} ref={input} type="file" class="visually-hidden" tabIndex={-1} aria-hidden="true"
             accept={extensions.length > 0 ? extensions.join(",") : undefined} disabled={inert}
             onChange={(e) => {
               const element = e.currentTarget as HTMLInputElement;
               void send(element.files).finally(() => {
                 element.value = "";  // the same file may be chosen again
               });
             }} />
      <button type="button" class="button button--primary" aria-disabled={busy || inert || undefined}
              onClick={() => {
                if (!busy && !inert) {
                  input.current?.click();
                }
              }}>{block.submit}</button>
      <p class="upload__status" role="status">
        {status.kind === "sending" ? `Sending ${status.name}…` : status.kind === "sent" ? `Accepted: ${status.name}.` : ""}
      </p>
      {status.kind === "refused" && <p class="row__refusal" role="alert"><Warning /> {status.words}</p>}
    </div>
  );
}
