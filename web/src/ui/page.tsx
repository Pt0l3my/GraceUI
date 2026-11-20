// SPDX-License-Identifier: MIT
// page.tsx: one page of a space (DESIGN §4.2.4): its header with the help mark and its dialog (purpose, steps,
// Related), the header actions (a destructive one behind its confirmation), the tabs, the section cards with their
// Advanced disclosures, the blocks (settings rows, shortcut rows, components) and the Related card. Everything drawn is
// in the page's description (invariant 12); nothing absent is hinted at (invariant 2).
import { useLayoutEffect, useRef, useState } from "preact/hooks";
import type { ComponentChildren } from "preact";
import type { Page, PageAction, PageBlock, PageSection } from "../protocol.ts";
import { pageById, parseLink, sectionsOf } from "../model/layout.ts";
import { refusalWords } from "../model/session.ts";
import { useSession } from "./context.ts";
import { ComponentBlock } from "./components.tsx";
import { Arrow, Chevron, Cross, HelpMark, Lozenge, Warning } from "./icons.tsx";
import { focusHeading } from "./navigate.ts";
import { SettingRow } from "./row.tsx";
import { advancedOpen, domId, isCollapsed, sectionId, toggleAdvanced, toggleSection } from "./view-state.ts";

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

function ConfirmDialog({ space, action, onClose, onConfirm }: {
  space: string; action: PageAction | null; onClose: () => void; onConfirm: (action: PageAction) => void;
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
            <button type="button" class={`button button--${action.tone === "destructive" ? "danger" : action.tone}`}
                    data-confirm={action.id}
                    onClick={() => {
                      onClose();
                      onConfirm(action);
                    }}>
              {action.label}
            </button>
          </div>
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
  if (!route || !page) {
    return null;
  }
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
    <main class="page" id="page" data-page={page.id} data-space={space} aria-labelledby="page-title">
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
                        onClick={() => (action.confirm !== undefined || action.tone === "destructive"
                          ? setConfirming(action) : void run(action, false))}>
                  {action.label}
                </button>
              ))}
            </div>
          )}
        </div>
        <p class="page__desc">{page.description}</p>
        <div class="lozenge-rule" aria-hidden="true"><Lozenge /></div>
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
  );
}
