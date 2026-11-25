// SPDX-License-Identifier: MIT
// components.tsx: the page components a block binds to a topic (DESIGN §2.5's vocabulary, §4.2.4): stat tiles, a
// computed sentence, a live feed, a log tail, a key-value list, a table, a check list and a statement. Each draws only
// what its topic's body holds, as text: a space formats its own words (§6.1), and no string is ever markup (D81). A topic
// with nothing published yet shows an empty state, and nothing it might hold (invariant 2). Until protocol.toml defines
// the components' bodies, each reads the shape the demo space's fixtures give (D166).
import { useContext, useEffect, useRef, useState } from "preact/hooks";
import { Fragment } from "preact";
import type { JSX } from "preact";
import type { Signal } from "@preact/signals";
import type { Json, JsonObject, PageBlock, RowAction, RowSecret } from "../protocol.ts";
import type { TopicData } from "../model/session.ts";
import { refusalWords } from "../model/session.ts";
import { GreyedContext, MasterContext, ScopeContext, topicFor, useSession } from "./context.ts";
import { Chevron, Warning } from "./icons.tsx";
import { CaptureDialog } from "./capture.tsx";
import { ConfirmDialog } from "./modal.tsx";
import { FilterBar, useFilterBar } from "./filter-bar.tsx";
import { LivePage, countHeld, holdsNow, liveMode, released } from "./live.ts";
import type { Paged } from "./paged.tsx";
import { PagingFoot, PagingTop, pagedKey, pagedWords, useOwnChoices, usePaged } from "./paged.tsx";
import { domId, filters } from "./view-state.ts";
import { FRESH } from "../model/filter.ts";

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
      <p class="empty__line">Nothing to show.</p>
      <p class="empty__hint">It appears here as soon as there is something to show.</p>
    </div>
  );
}

export function useTopic(space: string, topic: string | undefined): TopicData | undefined {
  const session = useSession();
  const [watched, setWatched] = useState<{ id: string; data: Signal<TopicData | undefined> } | null>(null);
  useEffect(() => {
    if (topic === undefined) {
      return undefined;
    }
    const handle = session.watch(space, topic);
    setWatched({ id: `${space}\u0000${topic}`, data: handle.data });
    return () => handle.release();
  }, [space, topic]);
  // Only the topic asked for: never the last one's body for a render after it changes (a master's new row, D215).
  return watched !== null && watched.id === `${space}\u0000${topic ?? ""}` ? watched.data.value : undefined;
}

/** A topic's data as a live page draws it (D252): held while Live updates holds, the newest otherwise. A topic's first
 * body is always drawn; `live`, if given, says whether the page is live where no LivePage context reaches. */
export function useLiveTopic(space: string, topic: string | undefined, live?: boolean): TopicData | undefined {
  const data = useTopic(space, topic);
  const onLivePage = useContext(LivePage);
  const tick = released.value;
  void liveMode.value;  // drawn again when the mode changes
  const key = `${space}\u0000${topic ?? ""}`;
  const shown = useRef<{ data: TopicData | undefined; tick: number; key: string }>({ data: undefined, tick: -1, key: "" });
  const holding = (live ?? onLivePage) && holdsNow() && data !== undefined && shown.current.data !== undefined &&
    shown.current.key === key && shown.current.tick === tick;
  if (!holding) {
    shown.current = { data, tick, key };
  }
  useEffect(() => {
    if (holding && data !== undefined && data !== shown.current.data) {
      countHeld(key, data);
    }
  });
  return shown.current.data;
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

function Feed({ rows, newestFirst, live, empty, barKey, spec }: {
  rows: JsonObject[]; newestFirst: boolean; live: boolean; empty: JSX.Element; barKey?: string; spec?: PageBlock["filter_bar"];
}) {
  const lines = rows.filter((r) => text(r.line) !== null);
  // Its filter bar (D257): filters and a search over the rows it holds, in time's order (no sort).
  const bar = useFilterBar(barKey ?? "", spec, lines);
  const shown = bar.order.map((i) => lines[i]!);
  const ordered = newestFirst ? [...shown].reverse() : shown;
  const strip = spec
    ? <FilterBar spec={spec} rows={lines} state={bar.state} shown={shown.length} set={bar.set} label="" />
    : null;
  if (lines.length === 0) {
    return strip !== null ? <>{strip}{empty}</> : empty;  // the bar with no rows yet too, as in game (D262)
  }
  if (shown.length === 0) {
    return strip;
  }
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
  return <>{strip}{live ? <div role="log" aria-live="polite">{list}</div> : list}</>;
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
// A table read a page at a time (G45, D254): its rows the space's answers, filtered, sorted and searched by the
// space; its refusal's words, or "Loading…", in place of rows until it has some.
function PagedTable({ space, block, own }: { space: string; block: PageBlock; own: string | null }) {
  const source = block.page!;
  const scope = useContext(ScopeContext);
  const live = useContext(LivePage);
  const key = `${space}/${pagedKey(source)}`;
  const state = filters.value.get(key) ?? FRESH;
  const paged = usePaged(space, key, source, block.filter_bar, state, scope?.id ?? null, scope === null || scope.ready,
                         live);
  const choices = useOwnChoices(space, block.filter_bar);
  const body: JsonObject = { columns: paged.body?.columns ?? [], rows: paged.body?.rows ?? [] };
  const waiting = paged.refused !== null || paged.loading || paged.body === null;
  const empty = <Empty own={waiting ? pagedWords(paged, "") : own} />;
  return <Table space={space} block={block} body={body} empty={empty} paged={paged} own={choices} barKey={key} />;
}

function Table({ space, block, body, empty, paged, own, barKey }: {
  space: string; block: PageBlock; body: JsonObject; empty: JSX.Element;
  paged?: Paged; own?: ({ value: string; label: string }[] | null)[]; barKey?: string;
}) {
  const session = useSession();
  const [busy, setBusy] = useState<string | null>(null);
  const [refusal, setRefusal] = useState<string | null>(null);
  // A row action asking first: a destructive one's second press, or the space's question before a move (D209).
  const [asking, setAsking] = useState<{ action: RowAction; row: JsonObject; value?: string | number } | null>(null);
  const [capturing, setCapturing] = useState<{ action: RowAction; row: JsonObject } | null>(null);
  const greyed = useContext(GreyedContext);
  const inert = greyed.words(greyed.page) !== null;  // a greyed page's switches do nothing (D188)
  const labelled = objects(body.columns).filter((c) => text(c.id) !== null && text(c.label) !== null);
  // A column marked `drawer` shows only in its row's drawer (D256), opened by the row's mark, one a table.
  const columns = labelled.filter((c) => c.drawer !== true);
  const drawerColumns = labelled.filter((c) => c.drawer === true);
  const [openRow, setOpenRow] = useState<string | null>(null);
  const rows = objects(body.rows);
  // Its filter bar (G43, D250): the rows that show, in the order they show, by their index in the space's rows.
  const spec = block.filter_bar;
  const field = typeof block.props?.field === "string" ? `#${block.props.field}` : "";
  const bar = useFilterBar(barKey ?? `${space}/${block.topic ?? ""}${field}`, spec, rows, paged !== undefined);
  if (columns.length === 0 || rows.length === 0) {
    if (paged === undefined) {
      return empty;
    }
    return (  // its bar stays, for the space filters: a choice that leaves no row can be changed back
      <>
        {spec && <FilterBar spec={spec} rows={rows} state={bar.state} shown={0} set={bar.set} label="" own={own} />}
        <PagingTop paged={paged} />
        {empty}
      </>
    );
  }
  const first = columns.find((c) => switchOf(c) === null && c.numeric !== true);
  const actions = block.row_actions ?? [];
  const secret = block.row_secret ?? null;
  const act = async (a: RowAction, row: JsonObject, confirmed: boolean, value?: string | number) => {
    const name = row[a.key];
    if (!named(name) || busy !== null || inert) {
      return;
    }
    if (a.capture !== undefined && value === undefined) {
      setRefusal(null);
      setCapturing({ action: a, row });  // a key or a pad chord first (D209)
      return;
    }
    if ((a.tone === "destructive" || a.confirm !== undefined) && !confirmed) {
      setAsking({ action: a, row });  // its second press (§7.3, D173, D192)
      return;
    }
    setRefusal(null);
    setBusy(String(name));
    const args: JsonObject = { [a.key]: name };
    if (value !== undefined) {
      args.value = value;
    }
    if (a.tone === "destructive" || (value !== undefined && confirmed)) {
      args.confirm = true;
    }
    const answer = await session.command(space, a.command, args);
    setBusy(null);
    if (!answer.ok && answer.error === "confirm_required" && value !== undefined && !confirmed) {
      // The space asks first (another hotkey has it, D208): its words, then the same again with "confirm".
      setAsking({ action: { ...a, confirm: answer.detail ?? "Use it here?" }, row, value });
    } else if (!answer.ok) {
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
      {spec && <FilterBar spec={spec} rows={rows} state={bar.state} shown={bar.order.length} set={bar.set} label=""
                          own={own} />}
      {paged && <PagingTop paged={paged} />}
      {bar.order.length > 0 && <table class="table">
        <thead>
          <tr>
            {drawerColumns.length > 0 && <th scope="col" class="table__mark"><span class="visually-hidden">Details</span></th>}
            {columns.map((c) => <th scope="col" key={text(c.id)} class={c.numeric === true ? "num" : undefined}>{text(c.label)}</th>)}
            {actions.length > 0 && <th scope="col" class="table__actions"><span class="visually-hidden">Actions</span></th>}
          </tr>
        </thead>
        <tbody>
          {bar.order.map((i) => rows[i]!).map((r, n) => {
            const rowKey = typeof r.id === "string" && r.id !== "" && !r.id.includes("/") ? r.id : `#${bar.order[n]}`;
            // A row whose drawer columns are all empty has no drawer: its mark's cell stays, empty (D262).
            const drawn = drawerColumns.some((c) => { const v = text(r[text(c.id) ?? ""]); return v !== null && v !== ""; });
            const open = drawn && openRow === rowKey;
            const drawerId = domId("drawer", space, block.topic ?? "", rowKey);
            const rowName = first ? text(r[text(first.id) ?? ""]) : null;
            return (
            <Fragment key={rowKey}>
            <tr>
              {drawerColumns.length > 0 && (
                <td class="table__mark">
                  {drawn && (
                    <button type="button" class="icon-button table__drawer-toggle" aria-expanded={open}
                            aria-controls={open ? drawerId : undefined} data-drawer={rowKey}
                            aria-label={rowName !== null ? `Details: ${rowName}` : "Details"}
                            onClick={() => setOpenRow(open ? null : rowKey)}>
                      <Chevron open={open} />
                    </button>
                  )}
                </td>
              )}
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
                  {actions.filter((a) => named(r[a.key]) && r[a.key] !== "").map((a) => {  // no key, no actions (D209)
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
            {open && (
              <tr class="table__drawer" id={drawerId}>
                <td colSpan={columns.length + 1 + (actions.length > 0 ? 1 : 0)}>
                  <dl class="drawer">
                    {drawerColumns.filter((c) => text(r[text(c.id) ?? ""]) !== null && text(r[text(c.id) ?? ""]) !== "")
                      .map((c) => (
                        <div class="drawer__part" key={text(c.id)}>
                          <dt class="drawer__label">{text(c.label)}</dt>
                          <dd class="drawer__value">{text(r[text(c.id) ?? ""])}</dd>
                        </div>
                      ))}
                  </dl>
                </td>
              </tr>
            )}
            </Fragment>
            );
          })}
        </tbody>
      </table>}
      {paged && <PagingFoot paged={paged} />}
      {refusal !== null && <p class="table__notice" role="alert"><Warning /> {refusal}</p>}
      <ConfirmDialog space={space} action={asking?.action ?? null} onClose={() => setAsking(null)}
                     onConfirm={(a) => {
                       const row = asking?.row;
                       if (row) {
                         void act(a, row, true, asking?.value);
                       }
                     }} />
      <CaptureDialog space={space} id={capturing?.action.id ?? "none"} kind={capturing?.action.capture ?? null}
                     title={capturing ? captureTitle(capturing.action, capturing.row, columns) : ""}
                     onClose={() => setCapturing(null)}
                     onPick={(value) => {
                       const held = capturing;
                       setCapturing(null);
                       if (held) {
                         void act(held.action, held.row, false, value);
                       }
                     }} />
    </>
  );
}

// "Change key: Tide Charts, Show the tide table": the action's label and the row's first two columns, as in game (D209).
function captureTitle(a: RowAction, row: JsonObject, columns: JsonObject[]): string {
  const words: string[] = [];
  for (const c of columns) {
    const cell = text(row[text(c.id) ?? ""]);
    if (cell !== null && cell !== "" && switchOf(c) === null && c.numeric !== true && words.length < 2) {
      words.push(cell);
    }
  }
  return words.length > 0 ? `${a.label}: ${words.join(", ")}` : a.label;
}

// A report's preview (G44, D251): each part with every field visible, a code field in the mono face with its lines; an
// optional part's switch sends the block's command with the part and its new state, and the space's next body shows
// it (invariant 1); an excluded part is dimmed, its fields hidden.
function ReportPreview({ space, block, body, empty }: { space: string; block: PageBlock; body: JsonObject;
                                                        empty: JSX.Element }) {
  const session = useSession();
  const [busy, setBusy] = useState<string | null>(null);
  const [refusal, setRefusal] = useState<string | null>(null);
  const greyed = useContext(GreyedContext);
  const inert = greyed.words(greyed.page) !== null;
  const parts = objects(body.parts).filter((p) => text(p.id) !== null && text(p.label) !== null);
  if (parts.length === 0) {
    return empty;
  }
  const flip = async (id: string, on: boolean) => {
    if (block.command === undefined || busy !== null || inert) {
      return;
    }
    setRefusal(null);
    setBusy(id);
    const answer = await session.command(space, block.command, { id, on: !on });
    setBusy(null);
    if (!answer.ok) {
      setRefusal(refusalWords(answer));
    }
  };
  return (
    <div class="preview">
      {parts.map((p) => {
        const id = text(p.id) ?? "";
        const label = text(p.label) ?? "";
        const optional = p.optional === true;
        const included = !optional || p.included !== false;
        return (
          <section class={included ? "preview__part" : "preview__part is-excluded"} key={id} data-part={id}>
            <header class="preview__head">
              <h3 class="preview__label">{label}</h3>
              {optional && block.command !== undefined && (
                <span class="switch-wrap">
                  <button type="button" role="switch" class="switch" aria-checked={included} aria-label={label}
                          aria-disabled={busy !== null || inert || undefined} data-control=""
                          onClick={() => void flip(id, included)}>
                    <span class="switch__knob" />
                  </button>
                  <span class="switch__word" aria-hidden="true">{included ? "Included" : "Left out"}</span>
                </span>
              )}
            </header>
            {included && (
              <dl class="kv preview__fields">
                {objects(p.fields).filter((f) => text(f.label) !== null).map((f, i) => (
                  <div class="kv__row" key={i}>
                    <dt class="kv__label">{text(f.label)}</dt>
                    <dd class="kv__value">
                      {f.code === true ? <pre class="doc__code"><code>{text(f.value) ?? ""}</code></pre> : text(f.value)}
                    </dd>
                  </div>
                ))}
              </dl>
            )}
          </section>
        );
      })}
      {refusal !== null && <p class="table__notice" role="alert"><Warning /> {refusal}</p>}
    </div>
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
  // A block bound to the page's master family draws the chosen row's member (D215).
  const master = useContext(MasterContext);
  const topic = topicFor(master, block.topic, useContext(ScopeContext));  // since D217, any family the page's blocks name
  const data = useLiveTopic(space, block.component === "statement" ? undefined : topic);
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
      return <Feed rows={data?.kind === "rows" ? data.rows : []} newestFirst live empty={empty}
                   barKey={`${space}/${topic ?? ""}`} spec={block.filter_bar} />;
    case "log_tail":  // D187: newest first if the page says so
      return <Feed rows={data?.kind === "rows" ? data.rows : []} newestFirst={props.newest_first === true} live={false}
                   empty={data?.kind === "rows" ? <Empty own={typeof props.empty === "string" ? props.empty : null} /> : empty}
                   barKey={`${space}/${topic ?? ""}`} spec={block.filter_bar} />;
    case "key_value_list":
      return body ? <KeyValues body={body} empty={empty} /> : empty;
    case "table":
      if (block.page !== undefined) {
        return <PagedTable space={space} block={block} own={typeof props.empty === "string" ? props.empty : null} />;
      }
      return body ? <Table space={space} block={block} body={body} empty={empty} /> : empty;
    case "check_list":
      return body ? <CheckList body={body} empty={empty} /> : empty;
    case "report_preview":
      return body ? <ReportPreview space={space} block={block} body={body} empty={empty} /> : empty;
    default:
      return <p class="empty__line">Not shown in this version of the browser view.</p>;
  }
}
