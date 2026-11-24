# Proposal 0013: Documents, a multi-line form field and the report preview

**Status: proposal, 2026-11-24, put to the owner (G44); EldenNet's session agreed the formats the same day, with three
additions to the document block and one to the preview, all taken in below. The owner chose (a) the same day, as
recommended; where this text and the design differ, the design holds once built.** EldenNet's last two pages for v0.2
(its roadmap S11.4.a) need what no component offers: Docs, its bundled documentation, offline (in the browser at 0.2,
in game at 0.6); and Tools › Bug report, a "What happened" box and the bundle's preview with its optional parts
switchable. G16 keeps HTML out of the page data, and only GraceUI adds a component (§2.5's rule), so each is put here
as a framework feature every mod may use (G16, G18). A bare § is a section of [DESIGN.md](../DESIGN.md).

## 1. A document block (`kind = "document"`)

**Today** a page can show prose only as statements and sentences: no headings, lists, code or links between texts.
**Proposed**, on a master page (D215): the master list's topic gives one row a document, `{id, title, headings}`, and a
family member `<family>/<id>` gives the document, `{"title": "...", "blocks": [...]}`, drawn by a block of kind
`document` bound to the family. Its blocks, each one of:

- `{"heading": "...", "level": 2}` or level 3;
- `{"paragraph": spans}`;
- `{"list": [spans, ...], "ordered": false}`;
- `{"note": spans}`, drawn as an aside (a quiet card with a rule at its left), for cautions;
- `{"code": "..."}`, in the mono face, its line breaks kept, scrolled sideways where wide;
- `{"table": {"columns": [...], "rows": [...]}}`, plain text cells, as a table block draws them.

A span is `{"text": "..."}`, with at most one of `"strong": true`, `"code": true`, or a link: `"doc": "<id>"`, to
another document of the same family, with an optional `"heading": "<its words>"` naming one of that document's
headings. A link never holds a URL, and nothing is fetched (invariant 6). A link to a document or heading the space has
not published is drawn as plain text (invariant 2).

**Sizes.** A document up to 64 KiB of JSON and 400 blocks; a paragraph, list item or note up to 2000 characters; a
code block up to 16 KiB (a licence text); a heading up to 120 characters.

**Search** (D21, D219). The search reads each document's title and headings from the master list's rows, never a
body. Choosing a heading opens its document there.

**In game** (from 0.6 for EldenNet's Docs): the reader scrolls as a page does, and each link is a focus stop that
Enter follows. **In the browser** it is the reader at the right of the list.

*Recommended.*

## 2. A multi-line form field (`"prose": true`)

**Today** a form's fields are single-line (D195); the editor block's prose fields are already multi-line in both views
(the browser's textarea, the game's wrapped prose well, D214, D217). **Proposed:** a form field may say
`"prose": true`, with `max` up to 2000 characters, drawn as the editor's prose well. EldenNet's "What happened" box is
then a form, its caution ("don't include personal details") the section's description. *Recommended.*

## 3. The report preview (`report_preview`)

**Today** `report_preview` is in the component list (since 1.0) but drawn in neither view. **Proposed:** its topic's
body is `{"parts": [{"id", "label", "optional", "included", "fields": [{"label", "value", "code"}]}]}`. Each part is
drawn with every field visible; a field with `"code": true` is drawn in the mono face with its line breaks kept (the
recent log lines). An optional part has a switch that sends the block's command, `{"command": "<the block's>",
"args": {"id": "<part>", "on": true}}`, declared in the space's commands; an excluded part is drawn dimmed, its fields
hidden. The space's own checks before copying or saving (EldenNet's scan) are a check list beside it, not part of the
preview. *Recommended.*

## 4. The options (G44)

(a) **All three**, in one protocol minor after the filter bar's (recommended). (b) **The prose field and the preview
now, documents later**: EldenNet's Docs would wait, or be drawn as statements. (c) **None**: EldenNet keeps its
stand-ins (statements for Docs; a table of parts with a switch column and a key-value list a part for the preview).

## 5. Cost

`protocol.toml`: the `document` block kind with its family binding, the form field's `prose`, the preview's command
on its block; the page lint and registration's checks of each; both views (a reader in game and in the browser; the
form's prose well; the preview); the demo space's example of each (a lantern-keeper's handbook, a lamp report); the
view's, the web's and grace-dev's tests and goldens. No change of the ABI. About the size of G38's editor (D217).

## 6. EldenNet's answers (2026-11-24)

The formats suit EldenNet, with these additions, all in §1 to §3 above: (a) a link to a heading in another document
(Home's banner and the bug report link into Troubleshooting); (b) a note block, for the cautions in Privacy and in
What is sent to your provider; (c) a code block of up to 16 KiB, for the licence texts in Licences and credits (OFL is
about 4.4 KB); and (d) `"code": true` on a preview field, for the recent log lines. No images, no URLs (EldenNet's PV6
and its rule against game material); "see also" is a paragraph of document links. About's versions are filled by the
space when it publishes. Until this lands EldenNet builds the preview's stand-in.
