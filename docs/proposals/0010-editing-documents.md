# Proposal 0010: Editing a document (what the profile editor needs from GraceUI)

**Status: proposal, 2026-11-22, put to the owner as G38, and to EldenNet's session for §6 (EldenNet's roadmap
S7.2.a). The owner approved all three items as recommended the same day, to be built now in 0.2.x (G38); where this
text and the design differ once it is built, the design holds. EldenNet's session answered §6 the same day (§7).** A bare § is a section of [DESIGN.md](../DESIGN.md); PAGES is EldenNet's `docs/design/pages.md`; NPC is
EldenNet's `docs/design/npcs-and-dialogue.md`.

## 1. Why

EldenNet's O43 (the owner's, 2026-11-07) puts a profile editor in v1, at EldenNet v0.6, in both views. With it, the
player adds notes to how a character is played, and gives a voice of their own to someone they have met. It also covers
the Tarnished's own two-field bio (O46). Since G20 (a) the editor's pages are EldenNet's (its S7.2.b), built from
GraceUI's components. GraceUI's list of what the editor needs ([profile-editor-needs.md](../profile-editor-needs.md))
dates from 2026-11-07, before G20, O46 and O86. §2 brings it up to date. What is left for GraceUI is the components, and
none of today's fits:

- **A profile is a document, not settings.** It has 2 to 20 fields per target, with limits that vary by field and by
  target. It is checked as a whole, saved with a revision number and drafted by a model. Settings rows hold one value
  each, with one home in the settings description (invariant 12). D195's small form holds up to four single-line fields
  of up to 200 bytes, and gives back nothing but ok or a refusal's words.
- **There is no text field in game.** D57 designs GraceUI's own field, but it is not built: D194's row secrets and
  D195's forms read "Filled in the browser view" in game, and search has no field there.
- **Neither view draws `master_detail` yet.** The protocol names it, but no view draws it. EldenNet's Characters pages
  (Named NPCs, NPC types and the editor's lists) are master–detail (D36).
- **A topic is declared one at a time.** Each topic is declared by name at registration. §6.5's `named/<id>`,
  `journal/<id>` and `conversation/<id>` are a topic per row, which nothing declares or routes yet. The editor needs
  one more of them: `profiles/<target>`.
- **A command must be answered within 10 s** (D169). The draft helper takes up to 60 s (NPC §4.7).

## 2. The needs brought up to date (S7.2.a)

| The need of 2026-11-07 | Now |
|---|---|
| D2: "no content editors in v1" | The profile editor is the one exception, by O43; Content packs stays read-only (D213, recorded now: the owner has decided it) |
| SP1: add a profile's `subjects` and the player's own text | EldenNet's rule since G20 (a) (PAGES §4.8); NPC §4.7 and PAGES §4.6.4 already state it. GraceUI's form of it is invariants 1 and 2: the editor shows only what its topic publishes |
| SP4: a boss without a bio named in "people you've met" once its bar has shown | EldenNet's (PAGES §4.8; its `sightings`) |
| PV4: draft text shown on the editor only | EldenNet's (PAGES §4.9). GraceUI's PV3 binds the framework: §3.6 |
| §2: the editor's pages and where they live | EldenNet's (S7.2.b) |
| §3: fields per form | EldenNet's. Since O46, the player's own character follows the five-layer bio, and the Tarnished's own bio is a third form. GraceUI hard-codes no field and no limit: they come from the space's topic (§3.3) |
| §3: the subjects checked against every fixture state | Retired by O86 (no checks against fixture states); EldenNet reviews the subjects at the opening state (its PL11). Nothing for GraceUI |
| §4, §5: validation, states, commands, topics | EldenNet validates and holds the states. GraceUI draws them (§3.3) and routes the commands to the space (invariant 3) |
| §6: the "draft with AI" flow | EldenNet runs it (NPC §4.7). GraceUI shows the draft beside the fields, unsaved (§3.3), and lets the command run up to 90 s (§3.4) |
| §7: privacy | PV3 binds GraceUI (§3.6); PV4, PV5 and PV8 are EldenNet's |
| §8: help texts | EldenNet's words, in its page data |
| G §5.6: Steam's floating keyboard for these fields | Stands. It comes with the text field (§3.5); testers check it on a pad (D205) |

## 3. What GraceUI would add (protocol 1.5)

### 3.1 Topic families

A space may declare a topic as a family: `{"id": "profiles", "shape": "state", "family": true}`. Its members are
`profiles/<key>`, where the key is 1 to 64 bytes of the id characters, no `/`. A view subscribes to a member, and the
space hears `GRACEUI_EVENT_WATCHED` with that member's sub frame, as for any topic (D169). It publishes the member while
a view watches it. GraceUI drops the member's last state when no view watches it any more, and keeps at most 64 members
of one family watched (EST). Nothing else changes for a space at the ABI: the member's id comes in the frame. This also
serves EldenNet's `named/<id>`, `journal/<id>` and `conversation/<id>` (§6.5).

### 3.2 Master–detail in both views (D36)

Master–detail is a block of two parts:
- The master is a table bound to a topic, with the filter bar's search and chips if the page has them.
- The detail is a list of blocks bound to a family's member, chosen by the selected row's key.

In the browser the two parts sit side by side, the detail under the master in a narrow window. In game they are two
panes: → or A goes into the detail, and ← or B comes back (PAGES' "Keys and pad"). Only the selected row's member is
watched. When nothing is selected the detail shows the space's `empty` words. The detail may hold any block, tabs
included (Named NPCs' Profile, Allowances and Reactions).

### 3.3 The editor block

A block of `kind = "editor"`, in a master–detail's detail or on a page of its own. It is bound to a family member whose
body holds the document:

```json
{"rev": 4, "state": {"word": "Saved", "tone": "ok", "line": ""},
 "fields": [
  {"id": "instructions", "label": "Your notes", "kind": "prose", "max_words": 35, "max_chars": 210,
   "help": "Your notes are added to how this character is played.", "value": "Hums an old road song..."},
  {"id": "subjects", "label": "Likes to talk about", "kind": "list", "max_items": 8, "max_words": 10, "max_chars": 50,
   "reference": ["the river's moods", "old bridges"], "reference_label": "Shipped", "value": []}
 ]}
```

- **Field kinds:**
  - `line`: one short line, such as a brief of 24 characters.
  - `prose`: a paragraph that wraps, with no new lines (§5 (3)).
  - `list`: up to `max_items` short lines that can be added, removed and moved up or down. Each line may carry a
    choice from a topic of the space's, as D193's picker (a "knows once…" entry: a world stage that holds now, then its
    text).
  - `choice`: from the field's own `choices` (speech: speaks, sounds only or silent).
  - `pick`: from a topic's choices, as D193 (a base card by its label).
  - `number`: within its `min` and `max`.
  - `switch`: on or off.
  - `shown`: read-only, never sent back ("Met: …").

  Every field may carry `help`, `locked` with its words, and `reference`: read-only text beside the field, such as the
  shipped `subjects` or `public.summary`, which the field never sends back. The limits are the space's, read from the
  body: a field takes no more than `max_chars`, and counts its words and characters live, turning to the warning tone
  past `max_words`. GraceUI checks nothing else: the space decides.
- **Commands**, named in the block and declared by the space:
  - `check` and `save` send `{target, rev, doc}`. The `doc` is `{field id: value}` for every field but the `shown` ones:
    a string, a list of strings (or of `{"choice", "text"}`), a number or a bool. `check` writes nothing.
  - `reset` sends `{target, field}`, or `{target, "all": true}` after its confirmation.
  - `restore` (the previous version) and `draft` are optional.

  The answer's body (`result.body`, which goes to the one view that asked and is never kept, D169) may carry
  `messages`: `[{field, item?, level: "error" | "warning", words}]`. Each message is shown under its field, in the
  space's words, never with the offending text (NPC §4.7: "the toast and the editor name the file, field and line, never
  the text"). A save refused because `rev` is stale answers `conflict`, a new error code: the view says "Changed
  elsewhere since you opened it" and offers Reload, which asks first when there are unsaved edits.
- **Unsaved edits** stay in the view's memory and are marked "Unsaved". Leaving the page asks first in both views; in
  the browser, closing the tab also brings up the browser's own prompt. When the menu closes in game, see §5 (2): no
  close path waits for an answer (invariants 7 and 9).
- **The draft.** Draft sends `{target, notes}` from the block's notes field. Its words, cap and help come from the space
  (the provider's name, and what is sent). The button is greyed with the space's words while the space's `state` says
  why (`draft_blocked`: the gate, AI paused, a draft running, 20 used). Its answer's body holds `draft: {field id:
  value}` and may hold a `notice` (a draft discarded for safety: the notice alone). The draft shows beside each field it
  fills, with Use this and Keep mine, and Use all at the top. A draft taken becomes an unsaved edit; only Save writes. A
  draft lives in the view that asked, and leaving the page discards it.

### 3.4 Commands that take longer

A space may name commands in its page data as `slow_commands` (EldenNet's `profile.draft`). The answer deadline for
these is 90 s rather than 10 s (D169's `GRACEUI_ANSWER_DEADLINE_MS`). The view shows the control as running, and other
commands are not held behind it. A slow command still counts once in the 20 a second and in the space's queue of 32.

### 3.5 The text field in game (D57)

GraceUI's own field, in `grace-view`, holds the text and works out the caret, the selection and the line wraps. The host
supplies the characters through its `WM_CHAR` pairing (§5.6), as it does for a holder.

- **Editing keys:** ← → by character, Ctrl+← → by word, Home and End, Shift to select, Backspace and Delete, Ctrl+A,
  Ctrl+C, Ctrl+X and Ctrl+V. Paste only on the press (D57).
- **While the field has focus**, keys type into it; Esc leaves it and F5 closes the menu (§5.3).
- **The IME** composition window is placed at the caret (D57). On a pad, Steam's floating keyboard opens (§5.6).

The same field makes D194's row secrets (masked), D195's forms and search typeable in game. The "Filled in the browser
view" lines go.

### 3.6 Privacy, the framework's part (PV3)

- **Never stored.** Typed text and drafts go in no browser storage, cache, URL, title or console. Neither the server
  nor the registry logs a command's arguments (D194) or an answer's body. A family member's body is held in memory
  only while a view watches it.
- **Never searched.** Search never indexes an editor's values or references (§4.3).
- **Never in a link.** Neither the selected row's key nor any text goes into the address, the title or the browser's
  history.

## 4. Cost

- **The protocol (1.5):** `family`, the `editor` block, `slow_commands` and the `conflict` code, checked by the page
  lint and at registration.
- **`grace-spaces`:** families in the registry; slow deadlines.
- **`grace-view` and `grace-draw`:** the text field, master–detail and the editor in game.
- **The web:** master–detail and the editor.
- **The demo space:** a Keepers' notes page with invented characters, so that goldens, the DOM tests and `grace-dev`
  cover it.
- **Tests:** unit tests for the field's editing, for families, for the deadline, and a harness scenario that types in
  game through injected `WM_CHAR`s.
- **No change to the ABI** (a family member comes in the frames already sent).
- **EldenNet:** its Characters pages and the editor in this shape (S7.2.b).

## 5. For the owner

1. What to build, and when:
   - **(a) All of it now**, in GraceUI 0.2.x, in this order: the text field, families with master–detail, slow
     commands, the editor. Each is tried on the demo space with invented content, so that EldenNet builds its
     Characters pages and the editor on parts that work. *Recommended*: Named NPCs comes at EldenNet 0.3 and needs
     families and master–detail first; the text field also makes D194's and D195's fields and search work in game.
   - **(b)** Families, master–detail and the editor now, in the browser only. In game they would read "Edited in the
     browser view" until the menu's increment (0.6), when the text field comes.
   - **(c)** Wait until EldenNet's pages pass (S7.2.b) settles the editor's fields, and build to it.
2. When the menu closes in game (any close: F5, the player's exit, danger) with unsaved edits in the editor:
   - **(a) Keep them** in the menu's memory until the game closes, never on disk, and reopen the page with them marked
     "Unsaved", with Save and Discard. *Recommended*: invariant 7's close keeps changes; danger never waits for an
     answer (invariant 9); a save is a write the player asks for.
   - **(b) Drop them**, with one notice: "Your unsaved notes for ‹name› were not kept."
3. Prose fields as one paragraph each, with no new lines:
   - **(a) Yes.** *Recommended*: every field of the player's files is one paragraph, at most 60 words, and Enter
     leaves the field, as it does a single-line one.
   - **(b)** New lines allowed.

## 6. For EldenNet's session

1. Do the eight field kinds cover NPC §4.5's fields and O46's player fields, the seeds per key included (one `list` per
   key, labelled by EldenNet)?
2. Does `doc` as `{field id: value}`, with EldenNet writing its TOML, suit its watcher's save path (C17,
   `ProfileWrite`)?
3. Do check messages as `{field, item, level, words}` in the answer's body, with `conflict` for a stale `rev`, suit
   `profile.check` and `profile.save`?
4. Does the draft's answer as `{draft: {field id: value}, notice?}` suit NPC §4.7's helper, as a slow command?

## 7. EldenNet's answers (2026-11-22)

All four §6 questions are answered yes. The answers add five things to the editor block (§3.3), each to be built with
it (part d):

1. **The kinds cover the fields.** NPC §4.5 and O46's player fields map onto the eight kinds:
   - `line`: the brief, a display name's fallback.
   - `prose`: identity, `public.*`, `private.*`, `voice.*`, allegiance, home, instructions, the Tarnished's two
     fields.
   - `choice`: register, speech, pronouns.
   - `pick`: the base card.
   - `number`: the two word caps.
   - `switch`: replies, ambient.
   - `list`: knows, lexicon, subjects (the shipped list as the reference), and one list per seed key, at most 3 items.
   - `shown`: the "Met" of `[[seen]]`.

   A "knows once…" entry is a list item `{choice, text}`.
   - **(a) A list item may carry an opaque `key`** of the space's, sent back with it in `doc`. EldenNet keeps an
     entry's own checks across a reorder by it. An item the player adds has none.
   - **(b) A list item may be `locked`, with words**: shown, and moved with the others, but not edited, and sent back
     unchanged. This is for a hand-written "when" that the picker does not offer.
2. **`doc` as `{field id: value}` suits EldenNet's save path.** EldenNet merges it with what the editor never shows
   (the schema, `base_hash`, `[[seen]]`, keys only a hand edit sets, locked items) and writes the TOML.
   - **Field ids may be dotted** (`public.summary`, `seed.greet_player`).
   - **(c) `null` as a value**, both in a field's `value` and in `doc`, means "not set here", which is not the same as
     empty: a layer can leave its subjects, its register or a length to the shipped profile.
3. **The messages suit `profile.check` and `profile.save`.**
   - **(d) A message with no `field` is about the whole file** (a newer schema, the 8 KB cap), and is shown at the
     top of the block.
   - EldenNet answers a save within the 10 s, when its watcher's result returns. A full write queue is answered as
     `busy`. It then publishes the member's body again, with the new `rev`.
4. **The draft's answer suits EldenNet's helper.**
   - **(e) No cancel command is needed.** If the view stops watching the member while a draft runs, EldenNet cancels
     its call, as it already does for anything nobody watches.

EldenNet's view on the owner's three questions was (a), (a) and (a), the answers the owner gave.
