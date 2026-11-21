// SPDX-License-Identifier: MIT
// components.tsx: the page components a block binds to a topic (DESIGN §2.5's vocabulary, §4.2.4): stat tiles, a
// computed sentence, a live feed, a log tail, a key-value list, a table, a check list and a statement. Each draws only
// what its topic's body holds, as text: a space formats its own words (§6.1), and no string is ever markup (D81). A topic
// with nothing published yet shows an empty state, and nothing it might hold (invariant 2). Until protocol.toml defines
// the components' bodies, each reads the shape the demo space's fixtures give (D166).
import { useContext, useEffect, useRef, useState } from "preact/hooks";
import type { JSX } from "preact";
import type { Signal } from "@preact/signals";
import type { Json, JsonObject, PageBlock, RowAction, RowSecret } from "../protocol.ts";
import type { TopicData } from "../model/session.ts";
import { refusalWords } from "../model/session.ts";
import { GreyedContext, useSession } from "./context.ts";
import { Warning } from "./icons.tsx";
import { ConfirmDialog } from "./modal.tsx";

function text(value: Json | undefined): string | null {
  return typeof value === "string" ? value : typeof value === "number" ? String(value) : null;
}

function objects(value: Json | undefined): JsonObject[] {
  return Array.isArray(value)
    ? value.filter((v): v is JsonObject => typeof v === "object" && v !== null && !Array.isArray(v)) : [];
}

// The empty state: the space's own words once its topic has published an empty body (D186), else the framework's.
function Empty({ own }: { own?: string | null }) {
  if (own) {
    return <div class="empty"><p class="empty__line">{own}</p></div>;
  }
  return (
    <div class="empty">
      <p class="empty__line">Nothing to show yet.</p>
      <p class="empty__hint">It appears here as soon as there is something to show.</p>
    </div>
  );
}

export function useTopic(space: string, topic: string | undefined): TopicData | undefined {
  const session = useSession();
  const [watched, setWatched] = useState<Signal<TopicData | undefined> | null>(null);
  useEffect(() => {
    if (topic === undefined) {
      return undefined;
    }
    const handle = session.watch(space, topic);
    setWatched(handle.data);
    return () => handle.release();
  }, [space, topic]);
  return watched?.value;
}

function StatTiles({ body, empty }: { body: JsonObject; empty: JSX.Element }) {
  const tiles = objects(body.tiles).filter((t) => text(t.label) !== null && text(t.value) !== null);
  if (tiles.length === 0) {
    return empty;
  }
  return (
    <dl class="tiles">
      {tiles.map((t, i) => (
        <div class="tile" key={i}>
          <dt class="tile__caption">{text(t.label)}</dt>
          <dd class="tile__value">
            {text(t.value)}
            {text(t.unit) !== null && <span class="tile__unit"> {text(t.unit)}</span>}
          </dd>
        </div>
      ))}
    </dl>
  );
}

function Feed({ rows, newestFirst, live, empty }: {
  rows: JsonObject[]; newestFirst: boolean; live: boolean; empty: JSX.Element;
}) {
  const lines = rows.filter((r) => text(r.line) !== null);
  if (lines.length === 0) {
    return empty;
  }
  const ordered = newestFirst ? [...lines].reverse() : lines;
  const list = (
    <ol class={live ? "feed feed--live" : "feed"}>
      {ordered.map((r) => (
        <li class="feed__row" key={text(r.seq) ?? text(r.line)}>
          {text(r.time) !== null && <span class="feed__time">{text(r.time)}</span>}
          <span class="feed__line">{text(r.line)}</span>
        </li>
      ))}
    </ol>
  );
  return live ? <div role="log" aria-live="polite">{list}</div> : list;
}

function KeyValues({ body, empty }: { body: JsonObject; empty: JSX.Element }) {
  const items = objects(body.items).filter((t) => text(t.label) !== null && text(t.value) !== null);
  if (items.length === 0) {
    return empty;
  }
  return (
    <dl class="kv">
      {items.map((t, i) => (
        <div class="kv__row" key={i}>
          <dt class="kv__label">{text(t.label)}</dt>
          <dd class="kv__value">{text(t.value)}</dd>
        </div>
      ))}
    </dl>
  );
}

interface SwitchColumn {
  command: string;
  key: string;
  on: string;
  off: string;
}

// A column whose cells are a switch (D181): its command, the row's field that names the row, and its two words.
function switchOf(column: JsonObject): SwitchColumn | null {
  const sw = column.switch;
  if (typeof sw !== "object" || sw === null || Array.isArray(sw)) {
    return null;
  }
  const { command, key, on, off } = sw;
  return typeof command === "string" && typeof key === "string" && typeof on === "string" && typeof off === "string" &&
    command !== "" && key !== "" ? { command, key, on, off } : null;
}

function named(value: Json | undefined): value is string | number {
  return typeof value === "string" || typeof value === "number";
}

// A row secret's cell (D194, D197): its status as the space published it ("set", "refused", "none" for a row that needs
// none, or empty), a field typed into and sent once, then cleared at once; never bound to state, kept or shown again
// (invariant 5).
function SecretCell({ space, secret, row, status, label, inert, onRefusal }: {
  space: string; secret: RowSecret; row: JsonObject; status: string | null; label: string; inert: boolean;
  onRefusal: (words: string | null) => void;
}) {
  const session = useSession();
  const field = useRef<HTMLInputElement>(null);
  const set = status === "set";
  useEffect(() => () => {
    if (field.current) {
      field.current.value = "";
    }
  }, []);
  const save = async () => {
    const el = field.current;
    const name = row[secret.key];
    if (!el || inert || !named(name)) {
      return;
    }
    const value = el.value;
    el.value = "";  // sent once, kept nowhere (invariant 5)
    onRefusal(null);
    const answer = await session.command(space, secret.command, { [secret.key]: name, value });
    if (!answer.ok) {
      onRefusal(refusalWords(answer));
    }
  };
  if (status === "none") {
    return <span class="secret"><span class="badge">Not needed</span></span>;  // no key for this row (D197)
  }
  const refused = status === "refused";  // sent, and refused by whom it was for (D197)
  return (
    <span class="secret">
      <span class={set ? "badge badge--set" : refused ? "badge badge--refused" : "badge"}>
        {set ? "Set" : refused ? "Refused" : "Missing"}
      </span>
      <input ref={field} type="text" class="field field--secret field--small" autocomplete="off" spellcheck={false}
             autocapitalize="off" aria-label={label} disabled={inert} data-control=""
             placeholder={set || refused ? "Type a new one to replace it" : "Type it here"}
             onKeyDown={(e) => {
               if (e.key === "Enter") {
                 e.preventDefault();
                 void save();
               }
             }} />
      <button type="button" class="button button--secondary button--small" disabled={inert}
              onClick={() => void save()}>Save</button>
    </span>
  );
}

// A table (D166); a column with a switch (D181) shows each row's state as the space published it, never flipped here
// (invariant 1): a press sends the space's command, and the space's next body shows the change. Its block may add a
// button on each row (D192), a destructive one asking first as a header action does, and a row secret (D194).
function Table({ space, block, body, empty }: { space: string; block: PageBlock; body: JsonObject; empty: JSX.Element }) {
  const session = useSession();
  const [busy, setBusy] = useState<string | null>(null);
  const [refusal, setRefusal] = useState<string | null>(null);
  const [asking, setAsking] = useState<{ action: RowAction; row: JsonObject } | null>(null);
  const greyed = useContext(GreyedContext);
  const inert = greyed.words(greyed.page) !== null;  // a greyed page's switches do nothing (D188)
  const columns = objects(body.columns).filter((c) => text(c.id) !== null && text(c.label) !== null);
  const rows = objects(body.rows);
  if (columns.length === 0 || rows.length === 0) {
    return empty;
  }
  const first = columns.find((c) => switchOf(c) === null && c.numeric !== true);
  const actions = block.row_actions ?? [];
  const secret = block.row_secret ?? null;
  const act = async (a: RowAction, row: JsonObject, confirmed: boolean) => {
    const name = row[a.key];
    if (!named(name) || busy !== null || inert) {
      return;
    }
    if ((a.tone === "destructive" || a.confirm !== undefined) && !confirmed) {
      setAsking({ action: a, row });  // its second press (§7.3, D173, D192)
      return;
    }
    setRefusal(null);
    setBusy(String(name));
    const args: JsonObject = { [a.key]: name };
    if (a.tone === "destructive") {
      args.confirm = true;
    }
    const answer = await session.command(space, a.command, args);
    setBusy(null);
    if (!answer.ok) {
      setRefusal(refusalWords(answer));
    }
  };
  const flip = async (sw: SwitchColumn, row: JsonObject, on: boolean) => {
    const name = row[sw.key];
    if ((typeof name !== "string" && typeof name !== "number") || busy !== null || inert) {
      return;
    }
    setRefusal(null);
    setBusy(String(name));
    const answer = await session.command(space, sw.command, { [sw.key]: name, on: !on });
    setBusy(null);
    if (!answer.ok) {
      setRefusal(refusalWords(answer));
    }
  };
  return (
    <>
      <table class="table">
        <thead>
          <tr>
            {columns.map((c) => <th scope="col" key={text(c.id)} class={c.numeric === true ? "num" : undefined}>{text(c.label)}</th>)}
            {actions.length > 0 && <th scope="col" class="table__actions"><span class="visually-hidden">Actions</span></th>}
          </tr>
        </thead>
        <tbody>
          {rows.map((r, i) => (
            <tr key={i}>
              {columns.map((c) => {
                const id = text(c.id) ?? "";
                const sw = switchOf(c);
                if (secret !== null && id === secret.column) {
                  const rowName = first ? text(r[text(first.id) ?? ""]) : null;
                  const label = `${text(c.label) ?? ""}${rowName !== null ? `: ${rowName}` : ""}`;
                  return (
                    <td key={id} class="table__secret">
                      <SecretCell space={space} secret={secret} row={r} status={text(r[id])} label={label} inert={inert}
                                  onRefusal={setRefusal} />
                    </td>
                  );
                }
                if (sw === null) {
                  return <td key={id} class={c.numeric === true ? "num" : undefined}>{text(r[id])}</td>;
                }
                const word = text(r[id]);
                const on = word === sw.on;
                const rowName = first ? text(r[text(first.id) ?? ""]) : null;
                const name = r[sw.key];
                const named = typeof name === "string" || typeof name === "number";
                return (
                  <td key={id} class="table__switch">
                    {word !== null && named && (
                      <span class="switch-wrap">
                        <button type="button" role="switch" class="switch" aria-checked={on}
                                aria-label={rowName !== null ? `${text(c.label)}: ${rowName}` : text(c.label) ?? undefined}
                                aria-disabled={busy !== null || inert || undefined} data-control=""
                                onClick={() => void flip(sw, r, on)}
                                onKeyDown={(e) => {
                                  if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
                                    e.preventDefault();
                                    void flip(sw, r, on);
                                  }
                                }}>
                          <span class="switch__knob" />
                        </button>
                        <span class="switch__word" aria-hidden="true">{on ? sw.on : sw.off}</span>
                      </span>
                    )}
                  </td>
                );
              })}
              {actions.length > 0 && (
                <td class="table__actions">
                  {actions.map((a) => {
                    const rowName = first ? text(r[text(first.id) ?? ""]) : null;
                    return (
                      <button type="button" key={a.id}
                              class={`button button--small button--${a.tone === "destructive" ? "danger" : a.tone}`}
                              aria-label={rowName !== null ? `${a.label}: ${rowName}` : a.label}
                              aria-disabled={busy !== null || inert || undefined} data-row-action={a.id}
                              onClick={() => void act(a, r, false)}>
                        {a.label}
                      </button>
                    );
                  })}
                </td>
              )}
            </tr>
          ))}
        </tbody>
      </table>
      {refusal !== null && <p class="table__notice" role="alert"><Warning /> {refusal}</p>}
      <ConfirmDialog space={space} action={asking?.action ?? null} onClose={() => setAsking(null)}
                     onConfirm={(a) => {
                       const row = asking?.row;
                       if (row) {
                         void act(a, row, true);
                       }
                     }} />
    </>
  );
}

const CHECK_WORDS: { [state: string]: string } = { pass: "Passed", warn: "Warning", fail: "Failed", wait: "Waiting" };

function CheckList({ body, empty }: { body: JsonObject; empty: JSX.Element }) {
  const checks = objects(body.checks).filter((c) => text(c.label) !== null);
  if (checks.length === 0) {
    return empty;
  }
  return (
    <ul class="checks">
      {checks.map((c, i) => {
        const state = text(c.state) ?? "wait";
        return (
          <li class={`check check--${CHECK_WORDS[state] ? state : "wait"}`} key={i}>
            <span class="check__word">{CHECK_WORDS[state] ?? "Waiting"}</span>
            <span class="check__label">{text(c.label)}</span>
            {text(c.detail) !== null && <span class="check__detail">{text(c.detail)}</span>}
          </li>
        );
      })}
    </ul>
  );
}

/** A component block: its topic watched while it is shown, its body drawn by its kind. */
export function ComponentBlock({ space, block }: { space: string; block: PageBlock }) {
  const data = useTopic(space, block.component === "statement" ? undefined : block.topic);
  const props = block.props ?? {};
  // D185: one topic feeding several blocks, each drawing the member its props.field names.
  const field = typeof props.field === "string" ? props.field : undefined;
  const whole = data?.kind === "state" ? data.body : undefined;
  const member = whole !== undefined && field !== undefined ? whole[field] : whole;
  const body = typeof member === "object" && member !== null && !Array.isArray(member) ? member as JsonObject : undefined;
  // D186: the space's own words for an empty block, once its topic has spoken.
  const own = whole !== undefined && typeof props.empty === "string" ? props.empty : null;
  const empty = <Empty own={own} />;
  switch (block.component) {
    case "statement": {
      const line = text(props.text);
      return line !== null ? <p class="statement">{line}</p> : null;
    }
    case "stat_tiles":
      return body ? <StatTiles body={body} empty={empty} /> : empty;
    case "computed_sentence": {
      const line = body ? text(body.sentence) : null;
      return line !== null ? <p class="sentence">{line}</p> : empty;
    }
    case "live_feed":
      return <Feed rows={data?.kind === "rows" ? data.rows : []} newestFirst live empty={empty} />;
    case "log_tail":  // D187: newest first if the page says so
      return <Feed rows={data?.kind === "rows" ? data.rows : []} newestFirst={props.newest_first === true} live={false}
                   empty={data?.kind === "rows" ? <Empty own={typeof props.empty === "string" ? props.empty : null} /> : empty} />;
    case "key_value_list":
      return body ? <KeyValues body={body} empty={empty} /> : empty;
    case "table":
      return body ? <Table space={space} block={block} body={body} empty={empty} /> : empty;
    case "check_list":
      return body ? <CheckList body={body} empty={empty} /> : empty;
    default:
      return <p class="empty__line">Not shown in this version of the browser view.</p>;
  }
}
