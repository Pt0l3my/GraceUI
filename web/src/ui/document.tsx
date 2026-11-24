// SPDX-License-Identifier: MIT
// document.tsx: a document block (G44, D251): the chosen row's document, drawn from the shape model/document.ts puts it
// in: headings, paragraphs, lists, notes as asides, code in the mono face, plain tables. A link goes to another
// document of the list, or to one of its headings, by choosing its row; a link to a document the list does not hold is
// plain text (invariant 2). Never a URL, never markup: every string is text.
import type { JSX } from "preact";
import { useContext, useEffect } from "preact/hooks";
import type { PageBlock } from "../protocol.ts";
import { parseDocument } from "../model/document.ts";
import type { DocBlock, Span } from "../model/document.ts";
import { MasterContext, topicFor } from "./context.ts";
import { useTopic } from "./components.tsx";
import { chooseByHand, domId } from "./view-state.ts";
import { signal } from "@preact/signals";

/** The document a followed link asked for, by "space/page/doc", and the element id of its heading if it named one:
 * scrolled to once that document is drawn, and let go then, found or not (invariant 2). */
const wanted = signal<{ doc: string; heading: string | null } | null>(null);

function headingId(space: string, page: string, doc: string, heading: string): string {
  return domId("doc", space, page, doc, heading.toLowerCase());
}

function Spans({ spans, space, page, keys }: { spans: Span[]; space: string; page: string; keys: readonly string[] }) {
  return (
    <>
      {spans.map((s, i) => {
        if (s.style === "strong") {
          return <strong key={i}>{s.text}</strong>;
        }
        if (s.style === "code") {
          return <code key={i}>{s.text}</code>;
        }
        if (s.style === "link" && s.doc !== undefined && keys.includes(s.doc)) {
          const doc = s.doc;
          return (
            <a key={i} href="#" class="doc__link" data-doc={doc} data-heading={s.heading}
               onClick={(e) => {
                 e.preventDefault();
                 wanted.value = { doc: `${space}/${page}/${doc}`,
                                  heading: s.heading !== undefined ? headingId(space, page, doc, s.heading) : null };
                 chooseByHand(space, page, doc);
                 // Focus on the chosen document's row, where the reader now is, as in game.
                 document.getElementById(domId("option", space, page, doc))?.focus({ preventScroll: true });
               }}>
              {s.text}
            </a>
          );
        }
        return <span key={i}>{s.text}</span>;  // plain, or a link to a document the list does not hold
      })}
    </>
  );
}

function Block({ b, space, page, doc, keys }: { b: DocBlock; space: string; page: string; doc: string;
                                                keys: readonly string[] }): JSX.Element {
  switch (b.kind) {
    case "heading":
      return b.level === 2
        ? <h3 class="doc__h2" id={headingId(space, page, doc, b.text)}>{b.text}</h3>
        : <h4 class="doc__h3" id={headingId(space, page, doc, b.text)}>{b.text}</h4>;
    case "paragraph":
      return <p class="doc__p"><Spans spans={b.spans} space={space} page={page} keys={keys} /></p>;
    case "note":
      return <aside class="doc__note"><p><Spans spans={b.spans} space={space} page={page} keys={keys} /></p></aside>;
    case "list": {
      const items = b.items.map((item, i) => (
        <li key={i}><Spans spans={item} space={space} page={page} keys={keys} /></li>
      ));
      return b.ordered ? <ol class="doc__list">{items}</ol> : <ul class="doc__list">{items}</ul>;
    }
    case "code":
      return <pre class="doc__code"><code>{b.text}</code></pre>;
    case "table":
      return (
        <table class="table doc__table">
          <thead><tr>{b.columns.map((c) => <th scope="col" key={c.id}>{c.label}</th>)}</tr></thead>
          <tbody>{b.rows.map((r, i) => <tr key={i}>{r.map((cell, k) => <td key={k}>{cell}</td>)}</tr>)}</tbody>
        </table>
      );
  }
}

export function DocumentBlock({ space, block }: { space: string; block: PageBlock }) {
  const master = useContext(MasterContext);
  const topic = topicFor(master, block.topic);
  const data = useTopic(space, topic);
  const body = data?.kind === "state" ? data.body : undefined;
  const view = parseDocument(body);
  const page = master?.page ?? "";
  const doc = master?.key ?? "";
  const want = wanted.value;
  useEffect(() => {
    // A followed link's document, once it is here: at its heading, or at its top if it has none of that name.
    if (want === null || body === undefined || want.doc !== `${space}/${page}/${doc}`) {
      return;
    }
    const at = want.heading !== null ? document.getElementById(want.heading) : null;
    (at ?? document.getElementById(domId("detail", space, page)))?.scrollIntoView({ block: "start" });
    wanted.value = null;
  }, [body, want, space, page, doc]);
  if (body === undefined) {
    return <p class="empty__line">Nothing to show.</p>;
  }
  if (view.error === "too_large") {
    return <p class="empty__line">This document is too large to show.</p>;
  }
  if (view.error !== undefined) {
    return <p class="empty__line">Nothing to show.</p>;
  }
  return (
    <article class="doc" aria-label={view.title || undefined}>
      {view.title !== "" && <h2 class="doc__title">{view.title}</h2>}
      {view.blocks.map((b, i) => <Block key={i} b={b} space={space} page={page} doc={doc} keys={master?.keys ?? []} />)}
    </article>
  );
}
