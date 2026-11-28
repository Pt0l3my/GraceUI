// SPDX-License-Identifier: MIT
// session.ts: what the browser view holds of the spaces it shows, as signals (G3), and the one way it changes anything:
// a command to the space that owns the page, whose answer decides (DESIGN §5.7, §6.6, §6.7; invariant 3). An edit shows
// as pending ("Saving…") until its answer; a refusal clears it, puts its reason under the row and asks for focus there,
// as grace-view's refused() does. A space's page data and settings come from its `pages` and `settings` topics; a
// component's topic is watched only while a page shows it (§6.5). Nothing is kept in browser storage (PV3).
import { batch, signal } from "@preact/signals";
import type { Signal } from "@preact/signals";
import type {
  ErrorCode, Json, JsonObject, PageAction, Rows, Setting, SettingsBody, SpaceInfo, SpacePages, State, Welcome,
} from "../protocol.ts";
import { formatValue, isNumeric } from "./format.ts";
import { pageById, placeOf, railOrder, sectionsOf, tabCount, withShown, withinIncrement } from "./layout.ts";
import type { Answer, Ended, Transport, TransportEvents } from "../transport/transport.ts";
import { PAGES_TOPIC, SETTINGS_TOPIC } from "../transport/transport.ts";

/** GraceUI's own space, whose pages sit at the rail's foot (D79). */
export const GRACEUI_SPACE = "graceui";

export type TopicData =
  | { kind: "state"; rev: number; body: JsonObject }
  | { kind: "rows"; rows: JsonObject[]; dropped: number; /** the seq of the row after the last one held */ next: number };

export interface SpaceView {
  info: SpaceInfo;
  pages: Signal<SpacePages | null>;
  /** Its page data as it came, before its increment and its settings (D258) leave pages out. */
  raw?: SpacePages;
  settings: Signal<SettingsBody | null>;
  settingsRev: number;
}

export interface Edit {
  /** The value the row shows while it waits. */
  shown: Json;
  waiting: boolean;
  /** An edit made while another was on its way: sent when the first is answered (the newest wins). */
  next?: { kind: "set" | "reset"; value: Json };
  revAtSend: number;
}

export interface Route {
  space: string;
  page: string;
  tab: number;
}

export type Connection = "connecting" | "open" | "lost" | "ended";

export interface SessionOptions {
  /** A transport for each connection; called again to reconnect after the connection is lost. */
  transport: () => Transport;
  /** Loads the page again for a new cookie (§6.7's unauthorised); the browser's, once, by default. */
  reload?: () => void;
  /** Delay before reconnecting, in ms; none to stay disconnected. */
  reconnectDelay?: (attempt: number) => number | null;
}

export interface FocusRequest {
  space: string;
  key: string;
  seq: number;
}

function editKey(space: string, key: string): string {
  return space + "\u0000" + key;
}

/** A refusal in words, as §6.7's table gives it; never the value or a payload. */
export function refusalWords(answer: Answer, setting?: Setting): string {
  if (answer.ok) {
    return "";
  }
  if (answer.local !== undefined) {
    return answer.local;
  }
  if (answer.detail !== undefined && answer.detail.length > 0) {
    return answer.detail;  // the space's own words for it (protocol 1.1)
  }
  const code: ErrorCode = answer.error;
  switch (code) {
    case "unknown_topic":
    case "unknown_command":
    case "unknown_key":
      return "Not available in this version.";
    case "invalid_value":
      if (setting && isNumeric(setting.type) && setting.min !== undefined && setting.max !== undefined) {
        return `Choose a value from ${formatValue(setting, setting.min)} to ${formatValue(setting, setting.max)}.`;
      }
      return "That value is not allowed here.";
    case "locked":
      return "Locked in this version.";
    case "not_now":
      return "Not possible right now.";
    case "busy":
      return "Already running.";
    case "confirm_required":
      return "This needs a confirmation first.";
    case "unauthorised":
      return "This page's sign-in has ended: load the page again.";
    case "rate_limited":
      return "Too many changes at once: try again in a moment.";
    case "version":
      return "This page needs updating: reload it.";
    case "bad_message":
    case "internal":
      return "Something went wrong; the log has the details.";
  }
  return "Something went wrong; the log has the details.";
}

/** A flow lands on the view only this soon after the page it starts on (D278), as the menu's opening in game. */
export const LAND_WITHIN_MS = 2000;

export class Session {
  readonly spaces = signal<SpaceView[]>([]);
  readonly connection = signal<Connection>("connecting");
  readonly ended = signal<Ended | null>(null);
  readonly welcome = signal<Welcome | null>(null);
  readonly route = signal<Route | null>(null);
  /** Counts the commands this view has sent: Live updates holds nothing for 3 s after one (D252). */
  readonly sent = signal(0);
  readonly edits = signal<ReadonlyMap<string, Edit>>(new Map());
  readonly refusals = signal<ReadonlyMap<string, string>>(new Map());
  readonly focusRequest = signal<FocusRequest | null>(null);
  /** The content space whose rail is shown: the route's, or the last content space opened while on GraceUI's foot. */
  readonly railSpace = signal<string | null>(null);
  /** A notice for the whole page (a reload needed, the page out of date). */
  readonly notice = signal<string | null>(null);
  /** Whether a flow whose state says start may still land the view on it (D278): from the page the view starts on,
   * for LAND_WITHIN_MS, until the player opens a page or a tab; once a page load. */
  readonly landing = signal(false);
  private landed = false;

  private readonly options: SessionOptions;
  private transport: Transport | null = null;
  private readonly topics = new Map<string, { data: Signal<TopicData | undefined>; users: number; sub: number }>();
  private readonly settingsSubs = new Map<string, number>();
  private focusSeq = 0;
  private attempt = 0;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private stopped = false;

  constructor(options: SessionOptions) {
    this.options = options;
  }

  // ---- The connection.

  start(): Promise<boolean> {
    this.stopped = false;
    this.connection.value = "connecting";
    const transport = this.options.transport();
    this.transport = transport;
    const events: TransportEvents = {
      welcome: (w) => this.onWelcome(transport, w),
      state: (s) => this.onState(s),
      rows: (r) => this.onRows(r),
      error: (code, space) => this.onError(code, space),
      ended: (e) => this.onEnded(transport, e),
    };
    return transport.connect(events);
  }

  stop(): void {
    this.stopped = true;
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    this.transport?.close();
  }

  /** Retry at once, from the stale banner. */
  retry(): void {
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.connection.value === "lost") {
      void this.start();
    }
  }

  private onWelcome(transport: Transport, welcome: Welcome): void {
    if (transport !== this.transport) {
      return;
    }
    this.attempt = 0;
    const known = new Map(this.spaces.value.map((s) => [s.info.id, s]));
    const views: SpaceView[] = welcome.spaces.map((info) => {
      const old = known.get(info.id);
      if (old && old.info.settings_schema.hash === info.settings_schema.hash &&
          old.info.settings_schema.number === info.settings_schema.number) {
        return { ...old, info };
      }
      return { info, pages: signal<SpacePages | null>(null), settings: signal<SettingsBody | null>(null), settingsRev: 0 };
    });
    batch(() => {
      this.welcome.value = welcome;
      this.spaces.value = views;
      this.connection.value = "open";
      this.ended.value = null;
    });
    this.settingsSubs.clear();
    for (const view of views) {
      transport.subscribe(view.info.id, PAGES_TOPIC);
      this.settingsSubs.set(view.info.id, transport.subscribe(view.info.id, SETTINGS_TOPIC));
    }
    // Topics pages were watching before the connection was lost are watched again.
    for (const [id, topic] of this.topics) {
      const [space, name] = id.split("\u0000") as [string, string];
      if (views.some((v) => v.info.id === space)) {
        topic.sub = transport.subscribe(space, name);
      }
    }
  }

  private onEnded(transport: Transport, ended: Ended): void {
    if (transport !== this.transport) {
      return;
    }
    batch(() => {
      this.ended.value = ended;
      this.connection.value = ended.reason === "lost" ? "lost" : "ended";
      // Edits on their way were answered as lost by the transport; nothing else waits.
    });
    if (ended.reason === "unauthorised") {
      this.reloadOnce();
      return;
    }
    if (ended.reason === "version") {
      this.notice.value = "This page needs updating: reload it.";
      return;
    }
    if (ended.reason === "lost" && !this.stopped) {
      const delay = (this.options.reconnectDelay ?? ((n: number) => Math.min(1000 * 2 ** n, 10000)))(this.attempt++);
      if (delay !== null) {
        this.reconnectTimer = setTimeout(() => {
          this.reconnectTimer = null;
          void this.start();
        }, delay);
      }
    }
  }

  private reloadOnce(): void {
    if (this.options.reload) {
      this.options.reload();
      return;
    }
    // Once per load: a page that was itself reloaded asks the player instead, so it never loops (and keeps nothing in
    // browser storage to know it, PV3).
    const navigation = globalThis.performance?.getEntriesByType?.("navigation")[0] as { type?: string } | undefined;
    if (navigation?.type === "reload") {
      this.notice.value = "This page's sign-in has ended: load the page again.";
    } else {
      globalThis.location.reload();
    }
  }

  private onError(code: ErrorCode, space?: string): void {
    if (code === "unauthorised") {
      this.reloadOnce();
    } else if (code === "version") {
      this.notice.value = "This page needs updating: reload it.";
    }
    void space;  // a topic a space does not have is simply not shown (invariant 2)
  }

  private view(space: string): SpaceView | undefined {
    return this.spaces.value.find((s) => s.info.id === space);
  }

  private onState(state: State): void {
    const view = this.view(state.space);
    if (!view) {
      return;
    }
    if (state.topic === PAGES_TOPIC) {
      view.raw = state.body as unknown as SpacePages;
      this.showPages(view);
      this.chooseStart();
      return;
    }
    if (state.topic === SETTINGS_TOPIC) {
      view.settingsRev = state.rev;
      batch(() => {
        view.settings.value = state.body as unknown as SettingsBody;
        this.showPages(view);  // a page shown with a setting comes and goes with it (D258)
        // Confirmed edits give way to the space's own values.
        const edits = new Map(this.edits.value);
        let changed = false;
        for (const [k, edit] of edits) {
          if (k.startsWith(state.space + "\u0000") && !edit.waiting) {
            edits.delete(k);
            changed = true;
          }
        }
        if (changed) {
          this.edits.value = edits;
        }
      });
      return;
    }
    const topic = this.topics.get(editKey(state.space, state.topic));
    if (topic) {
      topic.data.value = { kind: "state", rev: state.rev, body: state.body };
    }
  }

  private onRows(rows: Rows): void {
    const topic = this.topics.get(editKey(rows.space, rows.topic));
    if (!topic) {
      return;
    }
    const before = topic.data.value?.kind === "rows" ? topic.data.value : undefined;
    // A batch past the row after the last one held is a gap: the stream starts over there, and an empty one clears it
    // (protocol 1.1, §6.3). Rows held already are never shown twice.
    const held = before !== undefined && rows.from_seq <= before.next ? before.rows : [];
    const skip = before !== undefined && rows.from_seq < before.next ? before.next - rows.from_seq : 0;
    const ring = this.view(rows.space)?.pages.value?.topics.find((t) => t.id === rows.topic)?.ring ?? 500;
    const all = [...held, ...rows.rows.slice(skip)];
    const next = Math.max(rows.from_seq + rows.rows.length, before !== undefined && rows.from_seq <= before.next ? before.next : 0);
    topic.data.value = { kind: "rows", rows: all.slice(Math.max(0, all.length - ring)), dropped: rows.dropped, next };
  }

  // ---- What the view reads.

  space(id: string): SpaceView | undefined {
    return this.view(id);
  }

  /** The spaces whose rails can head the menu: every space but GraceUI's, or GraceUI's alone (D79). */
  contentSpaces(): SpaceView[] {
    const all = this.spaces.value;
    const content = all.filter((s) => s.info.id !== GRACEUI_SPACE);
    return content.length > 0 ? content : all;
  }

  /** GraceUI's own space, at the rail's foot, when another space heads the rail. */
  footSpace(): SpaceView | undefined {
    const all = this.spaces.value;
    return all.some((s) => s.info.id !== GRACEUI_SPACE) ? all.find((s) => s.info.id === GRACEUI_SPACE) : undefined;
  }

  setting(space: string, key: string): Setting | undefined {
    return this.view(space)?.settings.value?.description.find((s) => s.key === key);
  }

  /** The space's value for a key (its default if it has published none), before any edit. */
  stored(space: string, key: string): Json | undefined {
    const body = this.view(space)?.settings.value;
    if (body && key in body.values) {
      return body.values[key];
    }
    return this.setting(space, key)?.default;
  }

  edit(space: string, key: string): Edit | undefined {
    return this.edits.value.get(editKey(space, key));
  }

  /** What the row shows: its edit's value while one waits, or the space's. */
  shown(space: string, key: string): Json | undefined {
    const edit = this.edit(space, key);
    return edit ? edit.shown : this.stored(space, key);
  }

  refusal(space: string, key: string): string | undefined {
    return this.refusals.value.get(editKey(space, key));
  }

  /** A topic's data while a component shows it: subscribed on the first use, unsubscribed after the last. */
  watch(space: string, topic: string): { data: Signal<TopicData | undefined>; release: () => void } {
    const id = editKey(space, topic);
    let entry = this.topics.get(id);
    if (!entry) {
      entry = { data: signal<TopicData | undefined>(undefined), users: 0, sub: 0 };
      this.topics.set(id, entry);
      if (this.transport && this.connection.value === "open") {
        entry.sub = this.transport.subscribe(space, topic);
      }
    }
    entry.users += 1;
    const held = entry;
    let released = false;
    return {
      data: held.data,
      release: () => {
        if (released) {
          return;
        }
        released = true;
        held.users -= 1;
        if (held.users === 0) {
          this.topics.delete(id);
          if (this.transport && this.connection.value === "open") {
            this.transport.unsubscribe(space, held.sub, topic);
          }
        }
      },
    };
  }

  // ---- Where the view is.

  /** A space's pages as its increment (§4.2.2, D173) and its settings (D258) show them; the open page, if it has gone,
   * gives way to the space's first. */
  private showPages(view: SpaceView): void {
    if (view.raw === undefined) {
      return;
    }
    const next = withShown(withinIncrement(view.raw, view.info.increment), view.settings.value?.values,
                           this.welcome.value?.view === "device");
    const before = view.pages.value;
    if (before !== null && JSON.stringify(before) === JSON.stringify(next)) {
      return;  // the same pages: nothing to draw again
    }
    view.pages.value = next;
    const route = this.route.value;
    if (route && route.space === view.info.id && !pageById(next, route.page)) {
      const first = railOrder(next)[0];
      if (first) {
        this.open(view.info.id, first.id);
      }
    }
  }

  private chooseStart(): void {
    if (this.route.value) {
      return;
    }
    for (const view of this.contentSpaces()) {
      const pages = view.pages.value;
      const first = pages ? railOrder(pages)[0] : undefined;
      if (pages && first) {
        this.show(view.info.id, first.id);
        if (!this.landed) {
          this.landing.value = true;  // a flow that asks may still land on it (D278)
          setTimeout(() => {
            this.landing.value = false;
          }, LAND_WITHIN_MS);
        }
        return;
      }
    }
  }

  /** Lands the view on a flow whose state says start (D278), if the view may still: false otherwise. */
  land(space: string, page: string): boolean {
    if (!this.landing.value || this.landed) {
      return false;
    }
    this.landed = true;
    this.landing.value = false;
    return this.show(space, page);
  }

  /** Opens a page of a space (its first tab, or `tab`); false for a page the space does not have. The player's: no
   * flow lands on the view after it (D278). */
  open(space: string, page: string, tab = 0): boolean {
    this.landing.value = false;
    return this.show(space, page, tab);
  }

  private show(space: string, page: string, tab = 0): boolean {
    const pages = this.view(space)?.pages.value;
    const target = pages ? pageById(pages, page) : undefined;
    if (!target) {
      return false;
    }
    batch(() => {
      this.route.value = { space, page, tab: Math.max(0, Math.min(tab, tabCount(target) - 1)) };
      if (space !== GRACEUI_SPACE || this.contentSpaces().some((s) => s.info.id === GRACEUI_SPACE)) {
        this.railSpace.value = space;
      } else if (this.railSpace.value === null) {
        this.railSpace.value = this.contentSpaces()[0]?.info.id ?? null;
      }
    });
    return true;
  }

  setTab(tab: number): void {
    this.landing.value = false;
    const route = this.route.value;
    if (route) {
      this.route.value = { ...route, tab };
    }
  }

  /** Page Up and Page Down: the previous or next page in the open space's rail order (grace-view's). */
  step(direction: 1 | -1): boolean {
    const route = this.route.value;
    const pages = route ? this.view(route.space)?.pages.value : undefined;
    if (!route || !pages) {
      return false;
    }
    const order = railOrder(pages);
    const at = order.findIndex((p) => p.id === route.page);
    if (at < 0) {
      return false;  // a flow, outside the rail's order (D278): its own buttons move it
    }
    const next = order[at + direction];
    return next !== undefined && this.open(route.space, next.id);
  }

  /** Opens a setting's home page on the tab that holds it, and asks for focus on its row (a shortcut, a search hit). */
  openSetting(space: string, key: string): { section: string; advanced: boolean } | undefined {
    const s = this.setting(space, key);
    const pages = this.view(space)?.pages.value;
    const home = s && pages ? pageById(pages, s.home) : undefined;
    const place = home ? placeOf(home, key) : undefined;
    if (!home || !place) {
      return undefined;
    }
    this.open(space, home.id, place.tab);
    this.askFocus(space, key);
    return { section: place.section, advanced: place.advanced };
  }

  askFocus(space: string, key: string): void {
    this.focusRequest.value = { space, key, seq: ++this.focusSeq };
  }

  /** The sections of the route's page and tab. */
  sections() {
    const route = this.route.value;
    const pages = route ? this.view(route.space)?.pages.value : undefined;
    const page = route && pages ? pageById(pages, route.page) : undefined;
    return page && route ? sectionsOf(page, route.tab) : [];
  }

  // ---- Commands: every change is one, and only its answer changes what the space holds.

  private setEdit(space: string, key: string, edit: Edit | undefined): void {
    const edits = new Map(this.edits.value);
    if (edit) {
      edits.set(editKey(space, key), edit);
    } else {
      edits.delete(editKey(space, key));
    }
    this.edits.value = edits;
  }

  /** The view's own refusal of what was typed (D266: a slider's bounds in words), under the row as a space's would
   * be; undefined clears it. Nothing is sent. */
  refuseHere(space: string, key: string, reason: string | undefined): void {
    this.setRefusal(space, key, reason);
  }

  private setRefusal(space: string, key: string, reason: string | undefined): void {
    const refusals = new Map(this.refusals.value);
    if (reason === undefined) {
      refusals.delete(editKey(space, key));
    } else {
      refusals.set(editKey(space, key), reason);
    }
    this.refusals.value = refusals;
  }

  /** settings.set for a key: its row shows the value as pending until the space answers. */
  set(space: string, key: string, value: Json): void {
    this.change(space, key, "set", value);
  }

  /** settings.reset for one key (grace-view's R). */
  reset(space: string, key: string): void {
    const s = this.setting(space, key);
    if (s) {
      this.change(space, key, "reset", s.default);
    }
  }

  private change(space: string, key: string, kind: "set" | "reset", value: Json): void {
    const s = this.setting(space, key);
    if (!s || s.locked) {
      return;  // a locked setting does not change (grace-view's set)
    }
    // A secret's value is sent once and never shown: its row shows only "Set" while it waits (invariant 5).
    const shown: Json = s.type === "secret" ? (kind === "reset" || value === "" ? "" : "set") : value;
    const edit = this.edit(space, key);
    this.setRefusal(space, key, undefined);
    if (edit?.waiting) {
      this.setEdit(space, key, { ...edit, shown, next: { kind, value } });
      return;
    }
    this.setEdit(space, key, { shown, waiting: true, revAtSend: this.view(space)?.settingsRev ?? 0 });
    void this.transmit(space, key, kind, value, false);
  }

  private async transmit(space: string, key: string, kind: "set" | "reset", value: Json, retried: boolean): Promise<void> {
    const transport = this.transport;
    if (!transport) {
      return;
    }
    const args: JsonObject = kind === "set" ? { key, value } : { scope: "key", target: key };
    this.sent.value += 1;
    const answer = await transport.command(space, kind === "set" ? "settings.set" : "settings.reset", args);
    const edit = this.edit(space, key);
    if (!edit) {
      return;
    }
    if (!answer.ok && answer.error === "rate_limited" && !retried) {
      setTimeout(() => void this.transmit(space, key, kind, value, true), 1000);  // retried once after 1 s (§6.7)
      return;
    }
    if (edit.next) {
      const next = edit.next;
      this.setEdit(space, key, { shown: edit.shown, waiting: true, revAtSend: this.view(space)?.settingsRev ?? 0 });
      void this.transmit(space, key, next.kind, next.value, false);
      return;
    }
    if (answer.ok) {
      if ((this.view(space)?.settingsRev ?? 0) > edit.revAtSend) {
        this.setEdit(space, key, undefined);  // the space's state already holds it
      } else {
        this.setEdit(space, key, { ...edit, waiting: false });  // shown until the space's next state
      }
      return;
    }
    batch(() => {
      this.setEdit(space, key, undefined);
      this.setRefusal(space, key, refusalWords(answer, this.setting(space, key)));
      this.askFocus(space, key);
    });
    if (answer.error === "unauthorised") {
      this.reloadOnce();
    }
  }

  /** A command a page's component sends, such as a table's switch (D181): the space's own command and arguments. */
  async command(space: string, name: string, args: JsonObject): Promise<Answer> {
    if (!this.transport) {
      return { ok: false, error: "not_now", local: "Not connected to the game." };
    }
    this.sent.value += 1;
    return this.transport.command(space, name, args, this.slow(space, name));
  }

  /** Sends a file with one of a space's file commands (G28): its answer, as for a command. */
  async upload(space: string, name: string, args: JsonObject, file: Blob): Promise<Answer> {
    if (!this.transport) {
      return { ok: false, error: "not_now", local: "Not connected to the game." };
    }
    this.sent.value += 1;
    return this.transport.upload(space, name, args, file);
  }

  /** Whether a space names a command slow (D216): its answer is waited for up to 95 s. */
  slow(space: string, name: string): boolean {
    return (this.space(space)?.pages.value?.slow_commands ?? []).includes(name);
  }

  /** A header action's command (its confirmation, if it has one, already given). */
  async action(space: string, action: PageAction): Promise<Answer> {
    if (!this.transport) {
      return { ok: false, error: "not_now", local: "Not connected to the game." };
    }
    // A destructive action's command carries its confirmation, given by its second press (§7.3, D173).
    this.sent.value += 1;
    return this.transport.command(space, action.command, action.tone === "destructive" ? { confirm: true } : {},
                                  this.slow(space, action.command));
  }
}
