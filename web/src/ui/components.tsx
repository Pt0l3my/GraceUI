// SPDX-License-Identifier: MIT
// components.tsx: the page components a block binds to a topic (DESIGN §2.5's vocabulary, §4.2.4): stat tiles, a
// computed sentence, a live feed, a log tail, a key-value list, a table, a check list and a statement. Each draws only
// what its topic's body holds, as text: a space formats its own words (§6.1), and no string is ever markup (D81). A topic
// with nothing published yet shows an empty state, and nothing it might hold (invariant 2). Until protocol.toml defines
// the components' bodies, each reads the shape the demo space's fixtures give (D166).
import { useEffect, useState } from "preact/hooks";
import type { Signal } from "@preact/signals";
import type { Json, JsonObject, PageBlock } from "../protocol.ts";
import type { TopicData } from "../model/session.ts";
import { refusalWords } from "../model/session.ts";
import { useSession } from "./context.ts";
import { Warning } from "./icons.tsx";

function text(value: Json | undefined): string | null {
  return typeof value === "string" ? value : typeof value === "number" ? String(value) : null;
}

function objects(value: Json | undefined): JsonObject[] {
  return Array.isArray(value)
    ? value.filter((v): v is JsonObject => typeof v === "object" && v !== null && !Array.isArray(v)) : [];
}

function Empty() {
  return (
    <div class="empty">
      <p class="empty__line">Nothing to show yet.</p>
      <p class="empty__hint">It appears here as soon as there is something to show.</p>
    </div>
  );
}

function useTopic(space: string, topic: string | undefined): TopicData | undefined {
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

function StatTiles({ body }: { body: JsonObject }) {
  const tiles = objects(body.tiles).filter((t) => text(t.label) !== null && text(t.value) !== null);
  if (tiles.length === 0) {
    return <Empty />;
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

function Feed({ rows, newestFirst, live }: { rows: JsonObject[]; newestFirst: boolean; live: boolean }) {
  const lines = rows.filter((r) => text(r.line) !== null);
  if (lines.length === 0) {
    return <Empty />;
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

function KeyValues({ body }: { body: JsonObject }) {
  const items = objects(body.items).filter((t) => text(t.label) !== null && text(t.value) !== null);
  if (items.length === 0) {
    return <Empty />;
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

// A table (D166); a column with a switch (D181) shows each row's state as the space published it, never flipped here
// (invariant 1): a press sends the space's command, and the space's next body shows the change.
function Table({ space, body }: { space: string; body: JsonObject }) {
  const session = useSession();
  const [busy, setBusy] = useState<string | null>(null);
  const [refusal, setRefusal] = useState<string | null>(null);
  const columns = objects(body.columns).filter((c) => text(c.id) !== null && text(c.label) !== null);
  const rows = objects(body.rows);
  if (columns.length === 0 || rows.length === 0) {
    return <Empty />;
  }
  const first = columns.find((c) => switchOf(c) === null && c.numeric !== true);
  const flip = async (sw: SwitchColumn, row: JsonObject, on: boolean) => {
    const name = row[sw.key];
    if ((typeof name !== "string" && typeof name !== "number") || busy !== null) {
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
          <tr>{columns.map((c) => <th scope="col" key={text(c.id)} class={c.numeric === true ? "num" : undefined}>{text(c.label)}</th>)}</tr>
        </thead>
        <tbody>
          {rows.map((r, i) => (
            <tr key={i}>
              {columns.map((c) => {
                const id = text(c.id) ?? "";
                const sw = switchOf(c);
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
                                aria-disabled={busy !== null || undefined} data-control=""
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
            </tr>
          ))}
        </tbody>
      </table>
      {refusal !== null && <p class="table__notice" role="alert"><Warning /> {refusal}</p>}
    </>
  );
}

const CHECK_WORDS: { [state: string]: string } = { pass: "Passed", warn: "Warning", fail: "Failed", wait: "Waiting" };

function CheckList({ body }: { body: JsonObject }) {
  const checks = objects(body.checks).filter((c) => text(c.label) !== null);
  if (checks.length === 0) {
    return <Empty />;
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
  switch (block.component) {
    case "statement": {
      const line = text(block.props?.text);
      return line !== null ? <p class="statement">{line}</p> : null;
    }
    case "stat_tiles":
      return data?.kind === "state" ? <StatTiles body={data.body} /> : <Empty />;
    case "computed_sentence": {
      const line = data?.kind === "state" ? text(data.body.sentence) : null;
      return line !== null ? <p class="sentence">{line}</p> : <Empty />;
    }
    case "live_feed":
      return <Feed rows={data?.kind === "rows" ? data.rows : []} newestFirst live />;
    case "log_tail":
      return <Feed rows={data?.kind === "rows" ? data.rows : []} newestFirst={false} live={false} />;
    case "key_value_list":
      return data?.kind === "state" ? <KeyValues body={data.body} /> : <Empty />;
    case "table":
      return data?.kind === "state" ? <Table space={space} body={data.body} /> : <Empty />;
    case "check_list":
      return data?.kind === "state" ? <CheckList body={data.body} /> : <Empty />;
    default:
      return <p class="empty__line">Not shown in this version of the browser view.</p>;
  }
}
