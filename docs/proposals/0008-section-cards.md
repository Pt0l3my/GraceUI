# Proposal 0008: Sections as cards: a chip, a live status and their own actions

**Status: proposal, 2026-11-21, put to the owner and EldenNet's session (G36); EldenNet's session agreed the same day
(§7). The owner approved it on 2026-11-21 (G36); built as D202 in GraceUI 0.2.12, where this text and the
design differ, the design holds.** EldenNet's session met this
building AI › Models (its PAGES §4.6, its third pages slice): PAGES asks for **one card per job slot**, and the slice
used one settings block under the Jobs table "until GraceUI has cards". It adds to the page data and to what GraceUI's
views draw, for every mod (G16, G18), so it is put here rather than settled alone. A bare § is a section of
[DESIGN.md](../DESIGN.md).

## 1. What a job's card needs

PAGES describes each card: the job's name with its slot id as a chip; its provider and its model, each a picker (both
built: D193); its fallback model and provider (pickers too); a health dot and word; its fixed budgets read-only (a
key-value list: built, D184, D185); and on Prepared lines, one more setting and a button, "Prepare lines now".

A section is already drawn as a card in both views (§3.8): a title, an optional line, its blocks, an Advanced
disclosure, collapsible. Each job's settings can be a section of AI › Models, homed there as any setting is
(invariant 12). Three things are missing.

## 2. Proposed

1. **A chip** on a section: `"chip": "dialogue"`, 1 to 24 characters of the page data, drawn after the title in the
   faint face (both views). Static, as the title is.
2. **A live status** on a section: `"status": {"topic": "health", "field": "dialogue"}`, a state topic the space
   declares, its body (or the member `field` names, as D185) `{"word": "Ready", "tone": "moss"}`, tones as the lamp's
   (moss, ember, glint, rot). Drawn in the section's head as a dot and the word, beside its collapse mark, in both
   views. While the topic has published nothing for it, nothing is drawn (invariant 2).
3. **Actions of a section's own**: `"actions": [...]`, 1 or 2, of the same shape as a page's header actions
   (`id`, `label`, `command`, `tone`, `confirm`), checked alike at registration (a declared command; a destructive one
   with its confirm words, D173) and by the server ("confirm": true, D183). The browser draws them in the section's
   head; in game they are focus stops at the section's top, after its head, a destructive one asking for its second
   press. Inert on a greyed page (D188).

With these, AI › Models is: the preset at the top, then one section per job, each with its chip, its health, its
pickers, its budgets, and on Prepared lines its button. *Recommended.*

## 3. Alternatives

- **(b) A cards component built at run time**: a topic lists the cards (one per job) and the page data gives a card's
  template. It suits lists the space only knows at run time, but a card's settings would need keys made at run time,
  against a settings description fixed at registration and one home per setting (invariant 12). EldenNet's jobs are
  fixed for each increment, so sections serve them. Not recommended now; worth its own proposal when a page lists items
  only known at run time with settings of their own.
- **(c) Nothing**: the settings block under the Jobs table stays (EldenNet's stand-in).

## 4. Not covered

Home's grid of cards (PAGES §4.6.1: three or five columns, a Customise drawer that shows, hides and orders the cards,
kept in `ui.home_cards`) is a page layout of its own, with its own questions (the drawer in game, the order on a pad).
It is for a later proposal, when EldenNet builds Home's grid.

## 5. Cost

The three fields on a section in `protocol.toml` (protocol 1.3, all optional), the page lint and registration checks,
the server's list of destructive commands, both views' section head, the demo space and the goldens. No change of the
ABI. EldenNet's AI › Models then moves its jobs into sections.

## 6. For the owner and EldenNet's session

Whether to build it, and whether (a) all three, as recommended, or a part of them.

## 7. EldenNet's answer (2026-11-21)

It fits PAGES §4.6's Models page as EldenNet means it, and EldenNet's view is (a), all three: the title with the slot id
as a chip; the pickers (D193) and the budgets (D184, D185) as built; the health dot and word from its `health` topic, a
member per slot, in the lamp's tones; "Prepare lines now" as a section action. (b) is not needed, its jobs being fixed
for each increment, and Home's grid later is right. Three notes, none a change: a card with no model of its own reads
"Default" (D199), its help saying "Default: the Conversation model."; "Prepare lines now" is refused at EldenNet's
entry point, in words, except at rest or at the title, so it needs no enabled state of its own, and a greyed page makes
it inert (D188); on a paired device, `pools.prebake` joins the commands to refuse (0007 §5).

