// SPDX-License-Identifier: MIT
// page.tsx: one page of a space (DESIGN §4.2.4): its header with the help mark and its dialog (purpose, steps,
// Related), the header actions (a destructive one behind its confirmation), the tabs, the section cards with their
// Advanced disclosures, the blocks (settings rows, shortcut rows, components) and the Related card. Everything drawn is
// in the page's description (invariant 12); nothing absent is hinted at (invariant 2).
import { useState } from "preact/hooks";
import type { Json, Page, PageAction, PageBlock, PageSection } from "../protocol.ts";
import { pageById, parseLink, sectionsOf } from "../model/layout.ts";
import { refusalWords } from "../model/session.ts";
import { GreyedContext, useSession } from "./context.ts";
import { ComponentBlock, useTopic } from "./components.tsx";
import { Arrow, Chevron, Cross, HelpMark, Lozenge, Warning } from "./icons.tsx";
import { focusHeading } from "./navigate.ts";
import { SettingRow } from "./row.tsx";
import { advancedOpen, domId, isCollapsed, sectionId, toggleAdvanced, toggleSection } from "./view-state.ts";
import { ConfirmDialog, Modal } from "./modal.tsx";
import { FormBlock } from "./form.tsx";

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

function SectionCard({ space, page, section }: { space: string; page: Page; section: PageSection }) {
  const collapsed = isCollapsed(space, page.id, section);
  const headId = domId("sect-head", space, page.id, section.id);
  const bodyId = domId("sect", space, page.id, section.id);
  const advId = sectionId(space, page.id, section.id);
  const advBody = domId("adv", space, page.id, section.id);
  const advOpen = advancedOpen.value.has(advId);
  const hasAdvanced = (section.advanced ?? []).length > 0;
  return (
    <section class="card" data-section={section.id} aria-labelledby={headId}>
      <h2 class="card__head">
        <button type="button" id={headId} class="card__toggle" aria-expanded={!collapsed}
                aria-controls={collapsed ? undefined : bodyId} onClick={() => toggleSection(space, page.id, section)}>
          <span class="eyebrow">{section.title}</span>
          <Chevron open={!collapsed} />
        </button>
      </h2>
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
  if (!route || !page) {
    return null;
  }
  const stateBody = stateData?.kind === "state" ? stateData.body : undefined;
  const greyed = { page: page.id, words: (id: string) => greyedWords(stateBody, id) };
  const pageWords = greyed.words(page.id);
  const space = route.space;
  const sections = sectionsOf(page, route.tab);
  const tabs = page.tabs ?? [];
  const panelId = domId("panel", space, page.id);
  const run = async (action: PageAction, confirmed: boolean) => {
    setNotice(null);
    const answer = await session.action(space, action);
    if (!answer.ok && answer.error === "confirm_required" && !confirmed) {
      setConfirming(action);
    } else if (!answer.ok) {
      setNotice({ page: page.id, text: refusalWords(answer) });
    }
  };
  return (
    <GreyedContext.Provider value={greyed}>
    <main class={pageWords !== null ? "page page--greyed" : "page"} id="page" data-page={page.id} data-space={space}
          aria-labelledby="page-title">
      <header class="page__head">
        <div class="page__line">
          <h1 id="page-title" class="page__title" tabIndex={-1}>{page.name}</h1>
          <TagMark tag={page.tag} />
          <button type="button" class="help-mark" aria-label={`Help: ${page.name}`} aria-haspopup="dialog"
                  onClick={() => setHelpOpen(true)}>
            <HelpMark />
          </button>
          {(page.actions ?? []).length > 0 && (
            <div class="page__actions">
              {(page.actions ?? []).map((action) => (
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
      {tabs.length > 0 && (
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
      <div class="page__body" id={panelId} role={tabs.length > 0 ? "tabpanel" : undefined}
           aria-labelledby={tabs.length > 0 ? domId("tab", space, page.id, tabs[route.tab]?.id ?? "") : undefined}>
        {sections.map((section) => <SectionCard key={section.id} space={space} page={page} section={section} />)}
        <RelatedCard space={space} page={page} />
      </div>
      <HelpDialog space={space} page={page} open={helpOpen} onClose={() => setHelpOpen(false)} />
      <ConfirmDialog space={space} action={confirming} onClose={() => setConfirming(null)}
                     onConfirm={(action) => void run(action, true)} />
    </main>
    </GreyedContext.Provider>
  );
}
