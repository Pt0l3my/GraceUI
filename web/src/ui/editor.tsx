// SPDX-License-Identifier: MIT
// editor.tsx: the editor block in the browser (D217, proposal 0010 §3.3 and §7): a document the space publishes on the
// block's topic, its fields edited in place and kept in memory as unsaved edits for the session (G38 (2)), and the
// space's commands (check, save, reset, restore, draft), each checked at its one entry point (invariant 3). The space's
// messages show under their fields in its own words; a stale save shows the conflict and Reload; a draft shows beside
// each field it fills. Prose is one paragraph: a line break typed or pasted is a space (G38 (3)). Every string is text,
// never markup (D81).
import { useContext, useEffect, useState } from "preact/hooks";
import type { Json, JsonObject, PageBlock } from "../protocol.ts";
import type { Answer } from "../transport/transport.ts";
import { refusalWords } from "../model/session.ts";
import type { Choice, EditorDocument, EditorField, EditorMessage, EditorState } from "../model/editor.ts";
import {
  cleared, countChars, countWords, dirty, docOf, draftOf, editorKey, listItem, listJson, messagesOf, oneParagraph,
  parseDocument, setValue, stateOf, update, valueOf,
} from "../model/editor.ts";
import { GreyedContext, MasterContext, ScopeContext, topicFor, useSession } from "./context.ts";
import { useTopic } from "./components.tsx";
import { ConfirmDialog } from "./modal.tsx";
import type { Confirmable } from "./modal.tsx";
import { Warning } from "./icons.tsx";
import { domId, expectChoice } from "./view-state.ts";

type Part = "save" | "check" | "reset" | "restore" | "draft";

const RESET: Confirmable = {
  id: "reset", label: "Reset all", tone: "destructive",
  confirm: "Every field goes back to the space's own, and your saved changes to this one go. Unsaved edits go too.",
};
const RELOAD: Confirmable = {
  id: "reload", label: "Reload", tone: "destructive",
  confirm: "Your unsaved edits go, and the document is shown as the space has it now.",
};

function choicesIn(body: JsonObject | undefined): Choice[] {
  const list = Array.isArray(body?.choices) ? body.choices : [];
  return list.filter((c): c is JsonObject => typeof c === "object" && c !== null && !Array.isArray(c))
    .filter((c) => typeof c.value === "string" && c.value !== "").slice(0, 256)
    .map((c) => ({ value: c.value as string, label: typeof c.label === "string" ? c.label : c.value as string }));
}

/** A field's choices: its own, or a topic's (watched while the field is shown). */
function useChoices(space: string, field: EditorField): Choice[] {
  const data = useTopic(space, field.choices.length === 0 && field.choicesFrom !== "" ? field.choicesFrom : undefined);
  return field.choices.length > 0 ? field.choices : choicesIn(data?.kind === "state" ? data.body : undefined);
}

function Messages({ messages, field, item }: { messages: readonly EditorMessage[]; field: string; item?: number }) {
  const mine = messages.filter((m) => m.field === field && m.item === item);
  if (mine.length === 0) {
    return null;
  }
  return (
    <ul class="editor__messages">
      {mine.map((m, i) => (
        <li key={i} class={m.error ? "editor__message editor__message--error" : "editor__message editor__message--warning"}>
          {m.error ? <Warning /> : null} {m.error ? "Error: " : "Warning: "}{m.words}
        </li>
      ))}
    </ul>
  );
}

function valueWords(field: EditorField, value: Json, choices: Choice[]): string {
  const label = (v: string) => choices.find((c) => c.value === v)?.label || v;
  if (value === null) {
    return "Not set here";
  }
  if (typeof value === "string") {
    return field.kind === "choice" || field.kind === "pick" ? label(value) : value;
  }
  if (typeof value === "boolean") {
    return value ? "On" : "Off";
  }
  if (typeof value === "number") {
    return String(Math.round(value));
  }
  if (Array.isArray(value)) {
    return value.map((item) => {
      const li = listItem(item);
      return li.choice !== "" ? `${label(li.choice)}: ${li.text}` : li.text;
    }).join("; ");
  }
  return "";
}

function Count({ field, value }: { field: EditorField; value: Json }) {
  if (field.kind === "list") {
    return field.maxItems > 0
      ? <span class="editor__count">{Array.isArray(value) ? value.length : 0} / {field.maxItems}</span> : null;
  }
  if (field.kind !== "line" && field.kind !== "prose") {
    return null;
  }
  const text = typeof value === "string" ? value : "";
  if (field.maxWords > 0) {
    const n = countWords(text);
    return (
      <span class={n > field.maxWords ? "editor__count editor__count--over" : "editor__count"} aria-live="polite">
        {n} / {field.maxWords} words
      </span>
    );
  }
  return field.maxChars > 0 ? <span class="editor__count">{countChars(text)} / {field.maxChars}</span> : null;
}

/** Characters cut to a field's cap, by code point. */
function capped(text: string, max: number): string {
  if (max <= 0) {
    return text;
  }
  const chars = [...text];
  return chars.length > max ? chars.slice(0, max).join("") : text;
}

function FieldView({ space, editor, doc, field, st, inert }: {
  space: string; editor: string; doc: EditorDocument; field: EditorField; st: EditorState; inert: boolean;
}) {
  const choices = useChoices(space, field);
  const value = valueOf(st, field);
  const id = domId("editor", editor, field.id);
  const locked = field.locked !== "";
  const off = inert || locked;
  const set = (v: Json) => setValue(editor, doc, field, v);
  const edited = field.id in st.edits;
  const proposal = st.draft[field.id];
  let control;
  switch (field.kind) {
    case "shown":
      control = <p class="editor__shown" id={id}>{valueWords(field, value, choices)}</p>;
      break;
    case "line":
      control = (
        <input id={id} type="text" class="field" value={typeof value === "string" ? value : ""} spellcheck={false}
               autocomplete="off" disabled={off} placeholder={value === null ? "Not set here" : undefined}
               onInput={(e) => set(capped(oneParagraph((e.currentTarget as HTMLInputElement).value), field.maxChars))} />
      );
      break;
    case "prose":
      control = (
        <textarea id={id} class="field field--prose" rows={3} value={typeof value === "string" ? value : ""}
                  spellcheck={false} disabled={off} placeholder={value === null ? "Not set here" : undefined}
                  onKeyDown={(e) => {
                    if (e.key === "Enter") {
                      e.preventDefault();  // one paragraph (G38 (3))
                    }
                  }}
                  onInput={(e) => {
                    const el = e.currentTarget as HTMLTextAreaElement;
                    const next = capped(oneParagraph(el.value), field.maxChars);
                    if (next !== el.value) {
                      el.value = next;
                    }
                    set(next);
                  }} />
      );
      break;
    case "choice":
    case "pick":
      control = (
        <select id={id} class="field field--small" disabled={off}
                value={typeof value === "string" ? value : ""}
                onChange={(e) => {
                  const v = (e.currentTarget as HTMLSelectElement).value;
                  set(v === "" ? null : v);
                }}>
          {(field.nullable || value === null) && <option value="">Not set here</option>}
          {choices.map((c) => <option key={c.value} value={c.value}>{c.label || c.value}</option>)}
        </select>
      );
      break;
    case "number":
      control = (
        <input id={id} type="number" class="field field--small" disabled={off} min={field.min} max={field.max} step={1}
               value={typeof value === "number" ? value : ""}
               onInput={(e) => {
                 const n = Number((e.currentTarget as HTMLInputElement).value);
                 if (Number.isFinite(n) && (e.currentTarget as HTMLInputElement).value !== "") {
                   set(Math.round(Math.min(field.max ?? n, Math.max(field.min ?? n, n))));
                 }
               }} />
      );
      break;
    case "switch":
      control = (
        <button id={id} type="button" role="switch" class="switch" aria-checked={value === true} disabled={off}
                onClick={() => set(!(value === true))}>
          <span class="switch__thumb" aria-hidden="true" />
          <span class="visually-hidden">{value === true ? "On" : "Off"}</span>
        </button>
      );
      break;
    case "list": {
      const items = Array.isArray(value) ? value : [];
      const withChoice = field.choices.length > 0 || field.choicesFrom !== "";
      const put = (next: Json[]) => set(next);
      control = (
        <div class="editor__list" id={id} role="group" aria-label={field.label}>
          {items.map((raw, i) => {
            const item = listItem(raw);
            const itemOff = off || item.locked !== "";
            const itemId = domId("editor", editor, field.id, String(i));
            return (
              <div class="editor__item" key={i} data-item={i}>
                {withChoice && (
                  <select class="field field--small" disabled={itemOff} value={item.choice}
                          aria-label={`${field.label}, item ${i + 1}: when`}
                          onChange={(e) => {
                            const next = [...items];
                            next[i] = listJson({ ...item, choice: (e.currentTarget as HTMLSelectElement).value }, true);
                            put(next);
                          }}>
                    {choices.map((c) => <option key={c.value} value={c.value}>{c.label || c.value}</option>)}
                  </select>
                )}
                <input id={itemId} type="text" class="field" value={item.text} disabled={itemOff} spellcheck={false}
                       autocomplete="off" aria-label={`${field.label}, item ${i + 1}`}
                       onInput={(e) => {
                         const next = [...items];
                         next[i] = listJson({ ...item, text: capped(oneParagraph((e.currentTarget as HTMLInputElement).value), field.maxChars) }, withChoice);
                         put(next);
                       }} />
                <button type="button" class="button button--quiet button--small" disabled={off || i === 0}
                        aria-label={`Move item ${i + 1} up`}
                        onClick={() => {
                          const next = [...items];
                          [next[i - 1], next[i]] = [next[i] as Json, next[i - 1] as Json];
                          put(next);
                        }}>↑</button>
                <button type="button" class="button button--quiet button--small" disabled={off || i === items.length - 1}
                        aria-label={`Move item ${i + 1} down`}
                        onClick={() => {
                          const next = [...items];
                          [next[i + 1], next[i]] = [next[i] as Json, next[i + 1] as Json];
                          put(next);
                        }}>↓</button>
                <button type="button" class="button button--quiet button--small" disabled={itemOff}
                        aria-label={`Remove item ${i + 1}`} data-remove={i}
                        onClick={() => put(items.filter((_, k) => k !== i))}>Remove</button>
                {item.locked !== "" && <p class="editor__locked">Locked: {item.locked}</p>}
                <Messages messages={st.messages} field={field.id} item={i} />
              </div>
            );
          })}
          {items.length === 0 && <p class="editor__none">No items.</p>}
          {!off && (field.maxItems === 0 || items.length < field.maxItems) && (
            <button type="button" class="button button--quiet button--small" data-add={field.id}
                    onClick={() => put([...items, listJson({ text: "", choice: withChoice ? (choices[0]?.value ?? "") : "",
                                                             key: null, locked: "" }, withChoice)])}>
              + Add
            </button>
          )}
        </div>
      );
      break;
    }
  }
  return (
    <div class="editor__field" data-field={field.id}>
      <div class="editor__label-line">
        {field.kind === "list" || field.kind === "shown" || field.kind === "switch"
          ? <span class="editor__label" id={`${id}-label`}>{field.label}</span>
          : <label class="editor__label" for={id}>{field.label}</label>}
        {edited && <span class="editor__unsaved">Unsaved</span>}
        <Count field={field} value={value} />
      </div>
      {field.help !== "" && <p class="editor__help">{field.help}</p>}
      {control}
      {locked && <p class="editor__locked">Locked: {field.locked}</p>}
      {field.reference.length > 0 && (
        <p class="editor__reference">{field.referenceLabel || "For reference"}: {field.reference.join("; ")}</p>
      )}
      <Messages messages={st.messages} field={field.id} />
      {proposal !== undefined && (
        <div class="editor__proposal" data-proposal={field.id}>
          <p class="editor__proposal-title">Draft</p>
          <p class="editor__proposal-words">{valueWords(field, proposal, choices)}</p>
          <div class="editor__proposal-actions">
            <button type="button" class="button button--primary button--small" data-use={field.id}
                    onClick={() => {
                      setValue(editor, doc, field, proposal);
                      update(editor, (s) => {
                        const draft = { ...s.draft };
                        delete draft[field.id];
                        return { ...s, draft };
                      });
                    }}>Use this</button>
            <button type="button" class="button button--secondary button--small" data-keep={field.id}
                    onClick={() => update(editor, (s) => {
                      const draft = { ...s.draft };
                      delete draft[field.id];
                      return { ...s, draft };
                    })}>Keep mine</button>
          </div>
        </div>
      )}
    </div>
  );
}

/** An editor block: its document, its commands, the player's edits over it. */
export function EditorBlockView({ space, block }: { space: string; block: PageBlock }) {
  const session = useSession();
  const greyed = useContext(GreyedContext);
  const inert = greyed.words(greyed.page) !== null;
  const master = useContext(MasterContext);
  const topic = topicFor(master, block.topic, useContext(ScopeContext));
  const data = useTopic(space, topic);
  const [running, setRunning] = useState<ReadonlySet<Part>>(new Set());
  const [confirming, setConfirming] = useState<Confirmable | null>(null);
  const [refocus, setRefocus] = useState<string | null>(null);  // the topic whose document takes Save's focus back
  const ed = block.editor;
  const key = editorKey(space, topic ?? "");
  const st = stateOf(key);
  const doc = parseDocument(data?.kind === "state" ? data.body : undefined);
  // The browser's own prompt before the tab closes while this editor holds unsaved edits (proposal 0010 §3.3).
  const unsaved = dirty(st);
  useEffect(() => {
    if (!unsaved) {
      return undefined;
    }
    const ask = (e: BeforeUnloadEvent) => {
      e.preventDefault();
      e.returnValue = "";
    };
    globalThis.addEventListener("beforeunload", ask);
    return () => globalThis.removeEventListener("beforeunload", ask);
  }, [unsaved]);
  const ready = doc !== null;
  useEffect(() => {
    if (refocus !== null && refocus === topic && ready) {
      setRefocus(null);
      document.querySelector<HTMLElement>(`.editor[data-editor="${CSS.escape(topic ?? "")}"] [data-editor-part=save]`)?.focus();
    }
  }, [refocus, ready, topic]);
  if (!ed || topic === undefined) {
    return null;
  }
  if (!doc) {
    return (
      <div class="empty">
        <p class="empty__line">{data !== undefined && typeof block.props?.empty === "string" ? block.props.empty : "Nothing to show."}</p>
      </div>
    );
  }
  const send = async (part: Part, name: string | undefined, args: JsonObject): Promise<Answer | null> => {
    if (name === undefined || inert || running.has(part)) {
      return null;
    }
    setRunning((r) => new Set([...r, part]));
    const answer = await session.command(space, name, args);
    setRunning((r) => {
      const next = new Set(r);
      next.delete(part);
      return next;
    });
    return answer;
  };
  const rev = () => (dirty(st) && st.baseRev !== 0 ? st.baseRev : doc.rev);
  const check = async () => {
    const answer = await send("check", ed.check, { target: doc.target, rev: rev(), doc: docOf(st, doc) });
    if (answer) {
      const messages = messagesOf(answer.body);
      update(key, (s) => ({
        ...s, messages,
        top: !answer.ok ? refusalWords(answer) : messages.length === 0 ? "No problems found." : "",
      }));
    }
  };
  const save = async () => {
    const answer = await send("save", ed.save, { target: doc.target, rev: rev(), doc: docOf(st, doc) });
    if (!answer) {
      return;
    }
    if (answer.ok) {
      update(key, cleared);  // the space publishes the document again, with its new revision
      // The row its page's list should choose next (D218), once listed; Save keeps focus across the change.
      const next = answer.body?.chosen;
      if (master !== null && typeof next === "string" && /^[a-z0-9._-]+$/.test(next) &&
          master.family.length + 1 + next.length <= 64 && next !== master.key) {
        setRefocus(block.topic !== undefined && master.families.includes(block.topic) ? `${block.topic}/${next}` : null);
        expectChoice(space, greyed.page, next);
      }
    } else if (answer.error === "conflict") {
      update(key, (s) => ({ ...s, conflict: true, top: answer.detail || "Changed elsewhere since you opened it." }));
    } else {
      const messages = messagesOf(answer.body);
      update(key, (s) => ({
        ...s, messages,
        top: answer.detail || (messages.length > 0 ? "Not saved: see the messages below." : refusalWords(answer)),
      }));
    }
  };
  const reset = async () => {
    const answer = await send("reset", ed.reset, { target: doc.target, all: true, confirm: true });
    if (answer) {
      update(key, (s) => (answer.ok ? cleared(s) : { ...s, top: refusalWords(answer) }));
    }
  };
  const restore = async () => {
    const answer = await send("restore", ed.restore, { target: doc.target });
    if (answer) {
      update(key, (s) => (answer.ok ? cleared(s) : { ...s, top: refusalWords(answer) }));
    }
  };
  const draft = async () => {
    if (doc.draftBlocked !== "") {
      return;
    }
    const answer = await send("draft", ed.draft, { target: doc.target, notes: st.notes });
    if (answer) {
      update(key, (s) => {
        if (!answer.ok) {
          return { ...s, top: refusalWords(answer) };
        }
        const proposals = draftOf(doc, answer.body);
        const notice = typeof answer.body?.notice === "string" ? answer.body.notice.slice(0, 256) : "";
        return { ...s, draft: proposals, notice,
                 top: Object.keys(proposals).length === 0 && notice === "" ? "The draft proposed nothing." : "" };
      });
    }
  };
  const words = (part: Part, label: string) => (running.has(part) ? "Working…" : label);
  const errors = st.messages.some((m) => m.error);
  const fileMessages = st.messages.filter((m) => m.field === "" || !doc.fields.some((f) => f.id === m.field));
  const notesMax = ed.notes_max ?? 400;
  const notesId = domId("editor", key, "notes");
  return (
    <div class="editor" data-editor={topic}>
      {st.conflict && (
        <div class="editor__conflict" role="alert">
          <p>{st.top || "Changed elsewhere since you opened it."}</p>
          <button type="button" class="button button--secondary button--small" data-editor-part="reload"
                  onClick={() => (dirty(st) ? setConfirming(RELOAD) : update(key, cleared))}>Reload</button>
        </div>
      )}
      <div class="editor__head">
        {doc.stateWord !== "" && (
          <span class="editor__state">
            <span class={`lamp__dot lamp__dot--${doc.stateTone || "glint"}`} aria-hidden="true" /> {doc.stateWord}
          </span>
        )}
        {doc.stateLine !== "" && <span class="editor__state-line">{doc.stateLine}</span>}
        {unsaved && <span class="editor__unsaved" role="status">Unsaved changes</span>}
      </div>
      <div class="editor__actions">
        <button type="button" class="button button--primary" data-editor-part="save" aria-disabled={inert || undefined}
                aria-busy={running.has("save") || undefined} onClick={() => void save()}>{words("save", "Save")}</button>
        {ed.check && (
          <button type="button" class="button button--secondary" data-editor-part="check"
                  aria-disabled={inert || undefined} onClick={() => void check()}>{words("check", "Check")}</button>
        )}
        {ed.restore && (
          <button type="button" class="button button--secondary" data-editor-part="restore"
                  aria-disabled={inert || undefined}
                  onClick={() => void restore()}>{words("restore", "Restore the previous version")}</button>
        )}
        {ed.reset && (
          <button type="button" class="button button--danger" data-editor-part="reset" aria-disabled={inert || undefined}
                  onClick={() => (inert ? undefined : setConfirming(RESET))}>{words("reset", "Reset all")}</button>
        )}
      </div>
      {!st.conflict && st.top !== "" && (
        <p class={errors ? "editor__top editor__top--error" : "editor__top"} role="status">{st.top}</p>
      )}
      {fileMessages.length > 0 && (
        <ul class="editor__messages">
          {fileMessages.map((m, i) => (
            <li key={i} class={m.error ? "editor__message editor__message--error" : "editor__message editor__message--warning"}>
              {m.error ? <Warning /> : null} {m.error ? "Error: " : "Warning: "}{m.words}
            </li>
          ))}
        </ul>
      )}
      {ed.draft && (
        <div class="editor__draft">
          <div class="editor__label-line">
            <label class="editor__label" for={notesId}>{ed.notes_label ?? "Notes for the draft"}</label>
            <span class="editor__count">{countChars(st.notes)} / {notesMax}</span>
          </div>
          <textarea id={notesId} class="field field--prose" rows={2} value={st.notes} spellcheck={false}
                    disabled={inert}
                    onKeyDown={(e) => {
                      if (e.key === "Enter") {
                        e.preventDefault();
                      }
                    }}
                    onInput={(e) => {
                      const el = e.currentTarget as HTMLTextAreaElement;
                      const next = capped(oneParagraph(el.value), notesMax);
                      if (next !== el.value) {
                        el.value = next;
                      }
                      update(key, (s) => ({ ...s, notes: next }));
                    }} />
          <div class="editor__draft-line">
            <button type="button" class="button button--secondary" data-editor-part="draft"
                    aria-disabled={inert || doc.draftBlocked !== "" || undefined}
                    aria-busy={running.has("draft") || undefined}
                    onClick={() => void draft()}>{words("draft", "Draft")}</button>
            {(doc.draftBlocked || doc.draftHelp) && (
              <span class={doc.draftBlocked ? "editor__why" : "editor__help"}>{doc.draftBlocked || doc.draftHelp}</span>
            )}
            {Object.keys(st.draft).length > 0 && (
              <button type="button" class="button button--secondary button--small" data-editor-part="use_all"
                      onClick={() => {
                        for (const field of doc.fields) {
                          const proposal = st.draft[field.id];
                          if (proposal !== undefined) {
                            setValue(key, doc, field, proposal);
                          }
                        }
                        update(key, (s) => ({ ...s, draft: {} }));
                      }}>Use all</button>
            )}
          </div>
          {st.notice !== "" && <p class="editor__notice" role="status">{st.notice}</p>}
        </div>
      )}
      {doc.fields.map((field) => (
        <FieldView key={field.id} space={space} editor={key} doc={doc} field={field} st={st} inert={inert} />
      ))}
      <ConfirmDialog space={space} action={confirming} onClose={() => setConfirming(null)}
                     onConfirm={(action) => {
                       if (action.id === "reset") {
                         void reset();
                       } else {
                         update(key, cleared);
                       }
                     }} />
    </div>
  );
}
