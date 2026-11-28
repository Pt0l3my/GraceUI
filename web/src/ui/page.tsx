// SPDX-License-Identifier: MIT
// page.tsx: one page of a space (DESIGN §4.2.4): its header with the help mark and its dialog (purpose, steps,
// Related), the header actions (a destructive one behind its confirmation), the tabs, the section cards with their
// Advanced disclosures, the blocks (settings rows, shortcut rows, components) and the Related card. Everything drawn is
// in the page's description (invariant 12); nothing absent is hinted at (invariant 2).
import { useContext, useEffect, useState } from "preact/hooks";
import type { Json, Page, PageAction, PageBlock, PageSection } from "../protocol.ts";
import { pageById, parseLink, sectionsOf } from "../model/layout.ts";
import { refusalWords } from "../model/session.ts";
import type { TopicData } from "../model/session.ts";
import { GreyedContext, MasterContext, ScopeContext, useSession } from "./context.ts";
import { ComponentBlock, useLiveTopic, useTopic } from "./components.tsx";
import { LivePage, release } from "./live.ts";
import { Arrow, Chevron, Cross, HelpMark, Lozenge, Warning } from "./icons.tsx";
import { focusHeading } from "./navigate.ts";
import { SettingRow } from "./row.tsx";
import {
  advancedOpen, choose, chooseByHand, chosen, domId, expected, forgetExpected, isCollapsed, rowToShow, sectionId,
  toggleAdvanced, toggleSection,
} from "./view-state.ts";
import type { PageMaster } from "../protocol.ts";
import { ConfirmDialog, Modal } from "./modal.tsx";
import { FormBlock } from "./form.tsx";
import { EditorBlockView } from "./editor.tsx";
import { UploadBlock } from "./upload.tsx";
import { FilterBar, useFilterBar } from "./filter-bar.tsx";
import type { Paged } from "./paged.tsx";
import { PagingFoot, PagingTop, pagedWords, useOwnChoices, usePaged, useScope } from "./paged.tsx";
import { filters, gatesOpened, openGate } from "./view-state.ts";
import { railOrder } from "../model/layout.ts";
import { FRESH } from "../model/filter.ts";
import { DocumentBlock } from "./document.tsx";
import { FlowButtons, FlowSteps } from "./flow.tsx";
import { flowStep } from "../model/flow.ts";
import { dropDrafts } from "../model/editor.ts";

// The framework topic a space's greyed pages come on (D188): {"greyed": {page: words}, "inactive": words}.
const SPACE_STATE = "space.state";

function wordsOf(value: Json | undefined): string | null {
  return typeof value === "string" && value.length > 0 && value.length <= 200 ? value : null;
}

/** A page's words if its space has greyed it, else null: named in `greyed`, or every page with `inactive`. */
export function greyedWords(body: { [key: string]: Json } | undefined, page: string): string | null {
  if (body === undefined) {
    return null;
  }
  const greyed = body.greyed;
  if (typeof greyed === "object" && greyed !== null && !Array.isArray(greyed)) {
    const own = wordsOf(greyed[page]);
    if (own !== null) {
      return own;
    }
  }
  return wordsOf(body.inactive);
}

/** A header switch (D256): its label and a switch showing the state the space publishes ("Not known yet" until it has
 * come); a press sends its command with {on} flipped, and nothing changes until the space publishes (invariant 1). */
function HeaderSwitch({ space, action, inert, onRefusal }: {
  space: string; action: PageAction; inert: boolean; onRefusal: (words: string) => void;
}) {
  const session = useSession();
  const data = useTopic(space, action.state?.topic);
  const raw = data?.kind === "state" && action.state ? data.body[action.state.field] : undefined;
  const on = typeof raw === "boolean" ? raw : null;
  const flip = async () => {
    if (on === null || inert) {
      return;
    }
    const answer = await session.command(space, action.command, { on: !on });
    if (!answer.ok) {
      onRefusal(refusalWords(answer));
    }
  };
  return (
    <button type="button" role="switch" class="button button--secondary header-switch" data-action={action.id}
            aria-checked={on ?? false} aria-disabled={inert || on === null || undefined}
            onClick={() => void flip()}>
      <span>{action.label}</span>
      {on === null
        ? <span class="header-switch__unknown">Not known yet</span>
        : <span class="switch header-switch__switch" aria-hidden="true"><span class="switch__knob" /></span>}
    </button>
  );
}

function TagMark({ tag }: { tag: Page["tag"] }) {
  if (tag === undefined) {
    return null;
  }
  return (
    <span class="tag" title="Arrives in a later version">
      {tag === "wip" ? "WIP" : "Later"}
      <span class="visually-hidden"> (arrives in a later version)</span>
    </span>
  );
}

function Blocks({ space, blocks }: { space: string; blocks: PageBlock[] }) {
  return (
    <>
      {blocks.map((block, i) => block.kind === "component"
        ? <div class="block block--component" key={`c${i}`}><ComponentBlock space={space} block={block} /></div>
        : block.kind === "form"
        ? <div class="block block--form" key={`f${i}`}><FormBlock space={space} block={block} /></div>
        : block.kind === "editor"
        ? <div class="block block--editor" key={`e${i}`}><EditorBlockView space={space} block={block} /></div>
        : block.kind === "upload"
        ? <div class="block block--upload" key={`u${i}`}><UploadBlock space={space} block={block} /></div>
        : block.kind === "document"
        ? <div class="block block--document" key={`d${i}`}><DocumentBlock space={space} block={block} /></div>
        : (
          <div class="block block--rows" key={`r${i}`}>
            {(block.keys ?? []).map((key) => (
              <SettingRow key={key} space={space} settingKey={key} shortcut={block.kind === "shortcuts"} />
            ))}
          </div>
        ))}
    </>
  );
}

// A section's live status (D202): {word, tone} from its topic's body or a member of it; null while unpublished.
function sectionStatus(data: TopicData | undefined, field: string | undefined): { word: string; tone: string } | null {
  if (data?.kind !== "state") {
    return null;
  }
  const raw: Json | undefined = field !== undefined ? data.body[field] : data.body;
  if (typeof raw !== "object" || raw === null || Array.isArray(raw)) {
    return null;
  }
  const word = typeof raw.word === "string" && raw.word !== "" && raw.word.length <= 64 ? raw.word : null;
  if (word === null) {
    return null;
  }
  const tone = typeof raw.tone === "string" && ["moss", "ember", "glint", "rot"].includes(raw.tone) ? raw.tone : "glint";
  return { word, tone };
}

/** An action that opens a page once answered ok (D278): opened if the player is still on the page that sent it. */
function opened(session: ReturnType<typeof useSession>, space: string, from: string, action: PageAction): void {
  const now = session.route.value;
  if (action.opens !== undefined && now && now.space === space && now.page === from &&
      session.open(space, action.opens)) {
    focusHeading();
  }
}

function SectionCard({ space, page, section }: { space: string; page: Page; section: PageSection }) {
  const session = useSession();
  const greyed = useContext(GreyedContext);
  const inert = greyed.words(greyed.page) !== null;
  const [asking, setAsking] = useState<PageAction | null>(null);
  const [refusal, setRefusal] = useState<string | null>(null);
  const status = sectionStatus(useLiveTopic(space, section.status?.topic), section.status?.field);
  const actions = section.actions ?? [];
  const act = async (action: PageAction) => {
    setRefusal(null);
    const answer = await session.action(space, action);  // a destructive one carries its confirmation (D173)
    if (!answer.ok) {
      setRefusal(refusalWords(answer));
    } else {
      opened(session, space, page.id, action);
    }
  };
  const collapsed = isCollapsed(space, page.id, section);
  const headId = domId("sect-head", space, page.id, section.id);
  const bodyId = domId("sect", space, page.id, section.id);
  const advId = sectionId(space, page.id, section.id);
  const advBody = domId("adv", space, page.id, section.id);
  const advOpen = advancedOpen.value.has(advId);
  const hasAdvanced = (section.advanced ?? []).length > 0;
  return (
    <section class="card" data-section={section.id} aria-labelledby={headId}>
      <div class="card__headrow">
        <h2 class="card__head">
          <button type="button" id={headId} class="card__toggle" aria-expanded={!collapsed}
                  aria-controls={collapsed ? undefined : bodyId} onClick={() => toggleSection(space, page.id, section)}>
            <span class="card__name">
              <span class="eyebrow">{section.title}</span>
              {section.chip !== undefined && <span class="card__chip">{section.chip}</span>}
            </span>
            <Chevron open={!collapsed} />
          </button>
        </h2>
        {(actions.length > 0 || status !== null) && (
          <div class="card__extras">
            {actions.map((action) => (
              <button type="button" key={action.id} data-section-action={action.id}
                      class={`button button--small button--${action.tone === "destructive" ? "danger" : action.tone}`}
                      aria-disabled={inert || undefined}
                      onClick={() => (inert ? undefined
                        : action.confirm !== undefined || action.tone === "destructive"
                          ? setAsking(action) : void act(action))}>
                {action.label}
              </button>
            ))}
            {status !== null && (
              <span class="card__status" role="status">
                <span class={`lamp__dot lamp__dot--${status.tone}`} aria-hidden="true" />
                <span>{status.word}</span>
              </span>
            )}
          </div>
        )}
      </div>
      {refusal !== null && <p class="row__refusal" role="alert"><Warning /> {refusal}</p>}
      <ConfirmDialog space={space} action={asking} onClose={() => setAsking(null)} onConfirm={(a) => void act(a)} />
      {section.description !== undefined && <p class="card__desc">{section.description}</p>}
      {!collapsed && (
        <div class="card__body" id={bodyId}>
          <Blocks space={space} blocks={section.blocks} />
          {hasAdvanced && (
            <div class="advanced">
              <button type="button" class="advanced__toggle" aria-expanded={advOpen}
                      aria-controls={advOpen ? advBody : undefined} onClick={() => toggleAdvanced(advId)}>
                Advanced <Chevron open={advOpen} />
              </button>
              {advOpen && (
                <div id={advBody} class="advanced__body">
                  <Blocks space={space} blocks={section.advanced ?? []} />
                </div>
              )}
            </div>
          )}
        </div>
      )}
    </section>
  );
}

function RelatedCard({ space, page }: { space: string; page: Page }) {
  const session = useSession();
  const links = page.related.flatMap((link) => {
    const to = parseLink(space, link);
    const pages = session.space(to.space)?.pages.value;
    const target = pages ? pageById(pages, to.page) : undefined;
    return target ? [{ link, to, target }] : [];  // a page the view does not have is not offered (invariant 2)
  });
  if (links.length === 0) {
    return null;
  }
  const headId = domId("related", space, page.id);
  return (
    <section class="card card--related" aria-labelledby={headId}>
      <h2 id={headId} class="card__title eyebrow">Related</h2>
      <ul class="related">
        {links.map(({ link, to, target }) => (
          <li key={link}>
            <button type="button" class="related__row" data-related={link}
                    onClick={() => {
                      if (session.open(to.space, to.page)) {
                        focusHeading();
                      }
                    }}>
              <span class="related__name">{target.name}</span>
              <span class="related__desc">{target.description}</span>
              <Arrow />
            </button>
          </li>
        ))}
      </ul>
    </section>
  );
}

function HelpDialog({ space, page, open, onClose }: { space: string; page: Page; open: boolean; onClose: () => void }) {
  const session = useSession();
  const titleId = domId("help-title", space, page.id);
  const links = page.related.flatMap((link) => {
    const to = parseLink(space, link);
    const pages = session.space(to.space)?.pages.value;
    const target = pages ? pageById(pages, to.page) : undefined;
    return target ? [{ link, to, target }] : [];
  });
  return (
    <Modal open={open} onClose={onClose} labelledBy={titleId} size="medium">
      <div class="modal__head">
        <h2 id={titleId} class="modal__title">{page.name}</h2>
        <button type="button" class="icon-button" aria-label="Close the help" onClick={onClose}><Cross /></button>
      </div>
      <p class="prose">{page.help.purpose}</p>
      <h3 class="eyebrow modal__eyebrow">How to use it</h3>
      <ol class="steps">
        {page.help.steps.map((step, i) => <li key={i}>{step}</li>)}
      </ol>
      {links.length > 0 && (
        <>
          <h3 class="eyebrow modal__eyebrow">Related</h3>
          <ul class="modal__links">
            {links.map(({ link, to, target }) => (
              <li key={link}>
                <button type="button" class="link"
                        onClick={() => {
                          onClose();
                          if (session.open(to.space, to.page)) {
                            focusHeading();
                          }
                        }}>
                  {target.name}
                </button>
              </li>
            ))}
          </ul>
        </>
      )}
    </Modal>
  );
}

// A row's key as a member's (D215): the id characters but '/', the member's whole id at most 64 bytes.
function memberKey(family: string, key: string): boolean {
  return key.length > 0 && family.length + 1 + key.length <= 64 && /^[a-z0-9._-]+$/.test(key);
}

function cellText(row: { [key: string]: Json }, column: string | undefined): string {
  if (column === undefined) {
    return "";
  }
  const value = row[column];
  return typeof value === "string" ? value : typeof value === "number" ? String(value) : "";
}

/** The rows of a master list's body that name a member, each with its key, in the list's order. */
function masterRows(master: PageMaster, body: { [key: string]: Json } | undefined): { key: string; row: { [key: string]: Json } }[] {
  if (body === undefined || !Array.isArray(body.rows)) {
    return [];
  }
  const out: { key: string; row: { [key: string]: Json } }[] = [];
  for (const row of body.rows) {
    if (typeof row === "object" && row !== null && !Array.isArray(row)) {
      const key = cellText(row, master.key);
      if (memberKey(master.detail, key)) {
        out.push({ key, row });
      }
    }
  }
  return out;
}

/** A master list's filter bar's key: its topic's, or its page's for a list read a page at a time (D254). */
function masterBarKey(space: string, page: Page, master: PageMaster): string {
  return `${space}/master/${master.topic ?? `${page.id}.list`}`;
}

/** A page's master list (D215, D36): a listbox of its rows, each by its first column's words and its second's; ↑ ↓,
 * Home and End move the choice, → or Enter go into the detail. Read a page at a time (D254), the space's rows in
 * its order, with Show more at the foot. */
function MasterList({ space, page, master, body, current, paged }: {
  space: string; page: Page; master: PageMaster; body: { [key: string]: Json } | undefined; current: string | null;
  paged: Paged | null;
}) {
  // Its filter bar (G43, D250): the rows that show, in the order they show; the chosen row stays chosen if hidden.
  const all = masterRows(master, body);
  const bar = useFilterBar(masterBarKey(space, page, master), master.filter_bar, all.map((r) => r.row), paged !== null);
  const own = useOwnChoices(space, paged !== null ? master.filter_bar : undefined);
  const rows = bar.order.map((i) => all[i]!);
  const columns = body !== undefined && Array.isArray(body.columns) ? body.columns : [];
  const ids = columns.flatMap((c) => (typeof c === "object" && c !== null && !Array.isArray(c) && typeof c.id === "string"
    ? [c.id] : []));
  const optionId = (key: string) => domId("option", space, page.id, key);
  const move = (to: number) => {
    const target = rows[Math.max(0, Math.min(rows.length - 1, to))];
    if (target !== undefined) {
      chooseByHand(space, page.id, target.key);
      document.getElementById(optionId(target.key))?.focus();
    }
  };
  const at = rows.findIndex((r) => r.key === current);
  return (
    <nav class="master" aria-label={master.label}>
      <h2 class="master__label">{master.label}</h2>
      {master.filter_bar && (all.length > 0 || paged !== null) && (
        <FilterBar spec={master.filter_bar} rows={all.map((r) => r.row)} state={bar.state} shown={rows.length}
                   set={bar.set} label={master.label} own={paged !== null ? own : undefined} />
      )}
      {paged !== null && <PagingTop paged={paged} />}
      {all.length === 0
        ? <p class="master__empty">{paged !== null && (paged.refused !== null || paged.loading || paged.body === null)
                                      ? pagedWords(paged, "")
                                      : body !== undefined && master.empty !== undefined ? master.empty : "Nothing to show."}</p>
        : (
          rows.length > 0 && <ul class="master__list" role="listbox" aria-label={master.label}>
            {rows.map(({ key, row }, i) => (
              <li key={key} id={optionId(key)} role="option" class="master__row" aria-selected={key === current}
                  data-key={key} tabIndex={key === current || (at < 0 && i === 0) ? 0 : -1}
                  onClick={() => chooseByHand(space, page.id, key)}
                  onKeyDown={(e) => {
                    if (e.key === "ArrowDown" || e.key === "ArrowUp" || e.key === "Home" || e.key === "End") {
                      e.preventDefault();
                      move(e.key === "Home" ? 0 : e.key === "End" ? rows.length - 1 : i + (e.key === "ArrowDown" ? 1 : -1));
                    } else if (e.key === "ArrowRight" || e.key === "Enter") {
                      e.preventDefault();
                      chooseByHand(space, page.id, key);
                      const detail = document.getElementById(domId("detail", space, page.id));
                      detail?.querySelector<HTMLElement>("button, [href], input, select, [tabindex='0']")?.focus();
                    }
                  }}>
                <span class="master__name">{cellText(row, ids[0]) || key}</span>
                {ids[1] !== undefined && <span class="master__more">{cellText(row, ids[1])}</span>}
              </li>
            ))}
          </ul>
        )}
      {paged !== null && <PagingFoot paged={paged} />}
    </nav>
  );
}

export function PageView() {
  const session = useSession();
  const [helpOpen, setHelpOpen] = useState(false);
  const [confirming, setConfirming] = useState<PageAction | null>(null);
  const [notice, setNotice] = useState<{ page: string; text: string } | null>(null);
  const route = session.route.value;
  const pages = route ? session.space(route.space)?.pages.value : undefined;
  const page = route && pages ? pageById(pages, route.page) : undefined;
  // The space's greyed pages (D188), watched with every page of a space that declares space.state.
  const declaresState = (pages?.topics ?? []).some((t) => t.id === SPACE_STATE && t.shape === "state");
  const stateData = useTopic(route?.space ?? "", route && declaresState ? SPACE_STATE : undefined);
  // A scoped page (G45, D254): the scope in view; its families draw that scope's member.
  const scope = useScope(route?.space ?? "", pages);
  const scoped = page?.scoped === true && scope !== null;
  const scopeIn = scoped ? scope.id : null;
  const families = (pages?.topics ?? []).filter((t) => t.family === true).map((t) => t.id);
  // A master list (D215): its rows, and the chosen one (the first, as soon as the list has one); its topic a family's
  // member on a scoped page, or its rows read a page at a time.
  const master = page?.master;
  const masterTopic = master?.topic !== undefined && scoped && families.includes(master.topic)
    ? (scopeIn !== null ? `${master.topic}/${scopeIn}` : undefined) : master?.topic;
  const masterData = useLiveTopic(route?.space ?? "", masterTopic, page?.live === true);
  const barKey = route && page && master ? masterBarKey(route.space, page, master) : "";
  const masterPaged = usePaged(route?.space ?? "", `${route?.space ?? ""}/${page?.id ?? ""}.list`, master?.page,
                               master?.filter_bar, filters.value.get(barKey) ?? FRESH, scoped ? scopeIn : null,
                               !scoped || scope.come, page?.live === true);
  const masterBody: { [key: string]: Json } | undefined = master?.page !== undefined
    ? (masterPaged.body !== null ? { columns: masterPaged.body.columns, rows: masterPaged.body.rows } : undefined)
    : masterData?.kind === "state" ? masterData.body : undefined;
  const masterKeys = master ? masterRows(master, masterBody).map((r) => r.key) : [];
  // A flow (D278): its state, which says the step shown.
  const flowData = useTopic(route?.space ?? "", page?.flow?.topic);
  const held = route && page ? chosen.value.get(`${route.space}/${page.id}`) : undefined;
  const wanted = route && page ? expected.value.get(`${route.space}/${page.id}`) : undefined;
  const current = master ? rowToShow(masterKeys, held, wanted) : null;
  useEffect(() => dropDrafts(), [route?.space, page?.id]);  // a draft lives while its page is open (D217)
  useEffect(() => release(), [route?.space, page?.id]);  // another page: what was held is drawn (D252)
  useEffect(() => {
    if (route && page && master && current !== null) {
      if (current === wanted) {
        forgetExpected(route.space, page.id);  // a save's choice, its row listed now (D218)
      }
      if (held !== current) {
        choose(route.space, page.id, current);  // or the first row, or the first since the chosen one went (D215)
      }
    }
  }, [route?.space, page?.id, current, held, wanted]);
  if (!route || !page) {
    return null;
  }
  if (page.gate !== undefined && !gatesOpened.value.has(`${route.space}/${page.id}`)) {
    // Its gate (D258): the space's words, once a session, before anything of the page is drawn or watched.
    const back = pages ? railOrder(pages).find((p) => p.id !== page.id) : undefined;
    return (
      <main class="page page--gate" id="page" data-page={page.id} data-space={route.space} aria-labelledby="page-title">
        <header class="page__head">
          <h1 id="page-title" class="page__title" tabIndex={-1}>{page.name}</h1>
        </header>
        <div class="card gate" role="alertdialog" aria-labelledby="page-title" aria-describedby="gate-words">
          <p id="gate-words" class="prose">{page.gate.words}</p>
          <div class="gate__actions">
            <button type="button" class="button button--primary" data-gate="open"
                    onClick={() => openGate(route.space, page.id)}>
              {page.gate.confirm}
            </button>
            {back && (
              <button type="button" class="button button--secondary" data-gate="back"
                      onClick={() => {
                        if (session.open(route.space, back.id)) {
                          focusHeading();
                        }
                      }}>
                Go back
              </button>
            )}
          </div>
        </div>
      </main>
    );
  }
  const stateBody = stateData?.kind === "state" ? stateData.body : undefined;
  // An earlier scope the space marks read only greys the page as the space's own words would (D188, D254).
  const readOnly = scoped && scope.choice?.readOnly === true ? `${scope.choice.label} is read only.` : null;
  const greyed = { page: page.id,
                   words: (id: string) => greyedWords(stateBody, id) ?? (id === page.id ? readOnly : null) };
  const pageWords = greyed.words(page.id);
  const space = route.space;
  const flowBody = flowData?.kind === "state" ? flowData.body : undefined;
  const step = page.flow ? flowStep(page, flowBody) : route.tab;  // a flow's step is its space's (D278)
  const sections = sectionsOf(page, step);
  const tabs = page.flow ? [] : page.tabs ?? [];
  const panelId = domId("panel", space, page.id);
  const run = async (action: PageAction, confirmed: boolean) => {
    setNotice(null);
    const answer = await session.action(space, action);
    if (!answer.ok && answer.error === "confirm_required" && !confirmed) {
      setConfirming(action);
    } else if (!answer.ok) {
      setNotice({ page: page.id, text: refusalWords(answer) });
    } else {
      opened(session, space, page.id, action);
    }
  };
  return (
    <LivePage.Provider value={page.live === true}>
    <GreyedContext.Provider value={greyed}>
    <main class={pageWords !== null ? "page page--greyed" : "page"} id="page" data-page={page.id} data-space={space}
          aria-labelledby="page-title">
      <header class="page__head">
        {page.ribbon !== undefined && <p class="page__ribbon" role="note" data-ribbon="">{page.ribbon}</p>}
        <div class="page__line">
          <h1 id="page-title" class="page__title" tabIndex={-1}>{page.name}</h1>
          <TagMark tag={page.tag} />
          <button type="button" class="help-mark" aria-label={`Help: ${page.name}`} aria-haspopup="dialog"
                  onClick={() => setHelpOpen(true)}>
            <HelpMark />
          </button>
          {(page.actions ?? []).length > 0 && (
            <div class="page__actions">
              {(page.actions ?? []).map((action) => action.state !== undefined ? (
                <HeaderSwitch key={action.id} space={space} action={action} inert={pageWords !== null}
                              onRefusal={(text) => setNotice({ page: page.id, text })} />
              ) : (
                <button type="button" key={action.id} data-action={action.id}
                        class={`button button--${action.tone === "destructive" ? "danger" : action.tone}`}
                        aria-disabled={pageWords !== null || undefined}
                        onClick={() => (pageWords !== null ? undefined
                          : action.confirm !== undefined || action.tone === "destructive"
                            ? setConfirming(action) : void run(action, false))}>
                  {action.label}
                </button>
              ))}
            </div>
          )}
        </div>
        <p class="page__desc">{page.description}</p>
        <div class="lozenge-rule" aria-hidden="true"><Lozenge /></div>
        {pageWords !== null && <p class="page__greyed" role="status" data-greyed="">{pageWords}</p>}
        {notice && notice.page === page.id && (
          <p class="page__notice" role="alert"><Warning /> {notice.text}</p>
        )}
      </header>
      <div class={master ? "master-detail" : "single"}>
      {master && <MasterList space={space} page={page} master={master} body={masterBody} current={current}
                             paged={master.page !== undefined ? masterPaged : null} />}
      <div class="detail" id={domId("detail", space, page.id)}>
      {master && current === null && (
        <p class="detail__none" role="status">{master.none ?? "Choose one in the list."}</p>
      )}
      <MasterContext.Provider value={master ? {
        family: master.detail, member: current !== null ? `${master.detail}/${current}` : null, key: current,
        families, page: page.id, keys: masterKeys,
      } : null}>
      <ScopeContext.Provider value={scoped ? { id: scopeIn, families, ready: scope.come } : null}>
      {(!master || current !== null) && tabs.length > 0 && (
        <div class="tabs" role="tablist" aria-label={page.name}>
          {tabs.map((tab, i) => (
            <button type="button" role="tab" key={tab.id} id={domId("tab", space, page.id, tab.id)} class="tab"
                    aria-selected={i === route.tab} aria-controls={panelId} tabIndex={i === route.tab ? 0 : -1}
                    onClick={() => session.setTab(i)}
                    onKeyDown={(e) => {
                      const step = e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0;
                      const to = e.key === "Home" ? 0 : e.key === "End" ? tabs.length - 1 : i + step;
                      if ((step !== 0 || e.key === "Home" || e.key === "End") && to >= 0 && to < tabs.length) {
                        e.preventDefault();
                        session.setTab(to);
                        const el = document.getElementById(domId("tab", space, page.id, tabs[to]?.id ?? ""));
                        el?.focus();
                      }
                    }}>
              {tab.name}
            </button>
          ))}
        </div>
      )}
      {page.flow && <FlowSteps page={page} body={flowBody} step={step} />}
      <div class="page__body" id={panelId} role={tabs.length > 0 ? "tabpanel" : undefined}
           aria-labelledby={tabs.length > 0 ? domId("tab", space, page.id, tabs[route.tab]?.id ?? "") : undefined}>
        {(!master || current !== null) &&
          sections.map((section) => <SectionCard key={section.id} space={space} page={page} section={section} />)}
        {page.flow && <FlowButtons space={space} page={page} step={step} body={flowBody} inert={pageWords !== null} />}
        <RelatedCard space={space} page={page} />
      </div>
      </ScopeContext.Provider>
      </MasterContext.Provider>
      </div>
      </div>
      <HelpDialog space={space} page={page} open={helpOpen} onClose={() => setHelpOpen(false)} />
      <ConfirmDialog space={space} action={confirming} onClose={() => setConfirming(null)}
                     onConfirm={(action) => void run(action, true)} />
    </main>
    </GreyedContext.Provider>
    </LivePage.Provider>
  );
}
