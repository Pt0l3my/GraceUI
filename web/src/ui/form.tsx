// SPDX-License-Identifier: MIT
// form.tsx: a small form (G34, D195): one to four text fields sent together as one command to the space, which checks
// them at its one entry point and answers in words (invariant 3). Its fields are cleared once the space accepts them;
// a refusal's words show under the form and the fields stay as typed. On a greyed page it does nothing (D188).
import { useContext, useState } from "preact/hooks";
import type { JsonObject, PageBlock } from "../protocol.ts";
import { refusalWords } from "../model/session.ts";
import { GreyedContext, useSession } from "./context.ts";
import { Warning } from "./icons.tsx";
import { domId } from "./view-state.ts";

export function FormBlock({ space, block }: { space: string; block: PageBlock }) {
  const session = useSession();
  const greyed = useContext(GreyedContext);
  const inert = greyed.words(greyed.page) !== null;
  const [values, setValues] = useState<{ [field: string]: string }>({});
  const [busy, setBusy] = useState(false);
  const [refusal, setRefusal] = useState<string | null>(null);
  const fields = block.fields ?? [];
  const command = block.command;
  if (command === undefined || fields.length === 0) {
    return null;
  }
  const submit = async (e: Event) => {
    e.preventDefault();
    if (busy || inert) {
      return;
    }
    setRefusal(null);
    setBusy(true);
    const args: JsonObject = {};
    for (const f of fields) {
      args[f.id] = values[f.id] ?? "";
    }
    const answer = await session.command(space, command, args);
    setBusy(false);
    if (answer.ok) {
      setValues({});
    } else {
      setRefusal(refusalWords(answer));
    }
  };
  return (
    <form class="form" onSubmit={(e) => void submit(e)} noValidate>
      {fields.map((f) => {
        const id = domId("form", space, command, f.id);
        return (
          <div class="row form__row" key={f.id}>
            <div class="row__text"><label class="row__label" for={id}>{f.label}</label></div>
            <div class="row__control">
              {f.prose === true
                ? (  // several lines (D251), as the editor's prose fields are
                  <textarea id={id} class="field field--prose" rows={4} value={values[f.id] ?? ""} maxLength={f.max}
                            spellcheck={false} disabled={inert} data-prose=""
                            onInput={(e) => {
                              const value = (e.currentTarget as HTMLTextAreaElement).value;
                              setValues((v) => ({ ...v, [f.id]: value }));
                            }} />
                )
                : (
                  <input id={id} type="text" class="field" value={values[f.id] ?? ""} maxLength={f.max}
                         spellcheck={false} autocomplete="off" disabled={inert}
                         onInput={(e) => {
                           const value = (e.currentTarget as HTMLInputElement).value;
                           setValues((v) => ({ ...v, [f.id]: value }));
                         }} />
                )}
            </div>
          </div>
        );
      })}
      <div class="form__actions">
        <button type="submit" class="button button--primary" aria-disabled={busy || inert || undefined}
                data-form={command}>{block.submit}</button>
      </div>
      {refusal !== null && <p class="row__refusal" role="alert"><Warning /> {refusal}</p>}
    </form>
  );
}
