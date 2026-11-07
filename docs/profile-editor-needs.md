# What GraceUI needs for the profile editor (from EldenNet's character-profile design)

Input for the next GraceUI design pass; not yet applied to docs/DESIGN.md. Source: EldenNet DESIGN §7.7 and NPC §4.5–§4.8
(commit 588f083 on EldenNet's design/v1-proposal). The owner decided EldenNet O43 on 2026-11-07, which puts
the editor in v1, at EldenNet v0.6 with the other settings pages, in both views. G = docs/DESIGN.md.

Its `G:` line numbers refer to DESIGN.md at commit a69b818; recording G2 and G4–G9 moved them. Under G8, Named NPCs'
Profile tab now shows the general profile for every character, met or not, so anything added to it (such as
`subjects`) must hold at a first meeting.

*Updated 2026-11-07 for EldenNet O43 (as updated), O45 and O46 (Open):* every named NPC and named boss now has a shipped
bio, in the roster from the start (GraceUI D67), and every NPC with a profile, the player's own included, converses and
remembers (O45). The player's own character profiles are therefore mostly for named NPCs without a shipped bio (overhaul
additions, and the DLC's before EldenNet v1.1), and their fields follow the bio O46 settles.

## 1. Decisions GraceUI must revisit (record in G §12 or §13, with EldenNet O43 as the reason)

| GraceUI rule | Today | Needed |
|---|---|---|
| D2 "no content editors in v1" (G:53, G:82, G:1257, G:1978) | No content editors | One exception: the profile editor (the player's own instructions, subjects and character profiles). Content packs stays read-only |
| SP1 (G:1285) "the only authored texts shown are `public.summary`, a world stage's `line`, our labels and our help" | — | Add: a profile's `subjects` (EldenNet lints them against every fixture state, like `public.summary`, and applies the opening-state Stranger Test, PL11, because the roster shows them), and the player's own profile text (it is theirs). Still never a private field, stage, gist, road, knowledge entry, `knows`/`knows_when`, tie, forbid list, beckon or seed of a shipped profile |
| SP4 (G:1294) "bosses never by name outside a live bar" | — | Named bosses with a shipped bio are in the roster from the start (D67); a boss without one may appear by name in the editor's "people you've met" list and as a profile slot once its bar has shown in this journey (EldenNet's `sightings`). Never on NPC types' boss row |
| PV4 (G:1311) chat and model text only on listed pages | — | Add the profile editor as the one page that shows draft text from the `draft` model, unsaved, until the player accepts it |
| Named NPCs "Rules" (G:964) | Never a stage, road … (SP1) | Unchanged for shipped profiles; the editor tab shows the player's own fields |

## 2. Pages and where the editor lives

- **Characters › Named NPCs** (`characters.named`): a fourth tab **Your notes** for every persona slot: the player's
  layer (§3). Its Profile tab shows the persona's `subjects` (and every roster bio's) after the public summary.
- **Characters › NPC types** (`characters.types`): a card's detail gains **Your notes** (the card's layer). Profile
  slots appear among the named slots: the roster's `character:<id>` slots from the start, by the name the game shows at
  a first meeting (D67), and the player's own `mine:<id>` slots by runtime name once seen; the `boss` and `invader`
  rows' note becomes "named ones speak from their own profiles; others have no lines in this version unless your own
  profile gives them a voice". Allowances stay here and on Named NPCs (one home each, invariant 12); the editor holds no
  allowance.
- **Characters › Your characters** (new page, `characters.mine`): the player's own character profiles, plus **New from
  someone you've met**, a picker over this journey's sightings (named NPCs that were the best talk candidate, boss-bar
  holders once their bar showed, invaders once they engaged), by runtime name; search by runtime name only (SP9). When a
  `named:<param>` slot with allowances becomes a profile, GraceUI shows that its allowances moved to the new slot
  (EldenNet copies them).
- **AI › Director (WIP)** (G:921–924): reword the statement: "A later version may break a long silence by handing an NPC
  near you one of the subjects it likes to talk about." Our words; no other framework's name.

## 3. Fields per form (caps from EldenNet's `profiles` description topic, never hard-coded)

| Form | Fields | Notes |
|---|---|---|
| Layer over a persona | `instructions` (≤ 35 words, ≤ 210 chars), `subjects` (0–8, ≤ 10 words and ≤ 50 chars each; replaces the shipped list when set), `register` (our labels for the five registers), reply length (10–60 words) | Shows the shipped `subjects` and `public.summary` read-only for reference |
| Layer over a card or a shipped character | `instructions`, `subjects`, `register`, line length (6–12 words) | Card: its label; shipped character: its on-screen name |
| The player's own character | `brief` (≤ 24 chars: how other NPCs see it), `who`, `manner`, `cadence` (≤ 40 words each), `allegiance`, `home` (≤ 20 words), `knows` (0–6 lines ≤ 20 words), "knows once…" entries (≤ 6; a picker of world stages that hold now, shown by their line; text ≤ 20 words), `lexicon` (≤ 8 names), `subjects`, `instructions`, `speech` (speaks / sounds only / silent), `replies`, `ambient`, `base` (a card, by its label), seeds per key (0–3 lines each, ≤ 10 words) | The binding is shown as "met: <runtime name>" and is never edited by hand in the UI; its fields follow EldenNet O46 once decided, and under its recommendation the form gains the bio's conversation fields, each optional |

Live counters (words and characters) per field; multi-line entry for prose; on a pad, Steam's floating keyboard as in
G §5.6 (add these fields to its list).

## 4. Validation and states (EldenNet validates; GraceUI only shows)

- `profile.check` returns **errors** (save blocked: schema, caps, template syntax, the safety word list, a forbidden
  binding) and **warnings** (shown, never blocking: a name outside the profile's names, a game word, an effect
  adjective, a quoted sample line, a negation, a seed rule). Each carries field, code and line; never the offending text
  (GraceUI shows the field and highlights its own copy). Spoiler checks on the player's own text report nothing (SP5):
  EldenNet keeps a locked word out of lines and never points at it, so no warning, mark or count shows one.
- **Slot states** to show: saved; warnings (count per field); refused (field, line); held "not met yet"; held "until
  EldenNet has checked this character's own scripted lines"; "waits for a verified flag" (a "knows once…" entry); "the
  shipped profile was updated since you wrote this" (with the shipped public text to compare). These describe the
  player's own content, not the game, so SP5 does not forbid them.

## 5. Commands and topics (through EldenNet's one validating command path; invariant 3)

| Command | Arguments | Guards |
|---|---|---|
| `profile.check` | `target`, `doc` | No write; ≤ 8 KB; recorded as target and size only, never its text (PV8) |
| `profile.save` | `target`, `rev`, `doc` | Safe moment; `rev` must match (else `conflict`: "changed elsewhere; reload"); EldenNet's watcher validates and writes atomically (C17), keeping one previous version |
| `profile.reset` | `target`, `field` or `all`; `confirm` for `all` | `all` deletes the layer file |
| `profile.restore` | `target` | Restores the previous version |
| `profile.new` | `sighting` | Creates a `mine:` profile with its `[[seen]]` record |
| `profile.delete` | `target`, `confirm` | The player's own character only; unbinds its NPC |
| `profile.draft` | `target`, `notes` (≤ 400 chars) | One at a time; ≤ 20 a session; only while EldenNet's gate allows network and AI is not paused; returns a draft, never writes |

| Topic | Contents |
|---|---|
| `profiles` | The player's files: target, kind (layer or own character), rev, state, warning count; no text |
| `profiles/<target>` | The player's own fields; for a layer, the shipped profile's public summary or label and its `subjects`; the field caps |
| `sightings` | This journey's people met, by runtime name (projected; SP3: GraceUI stores no name) |

Limits (G §6.8): a profile document fits a 16 KiB frame (files are ≤ 8 KB); count `profile.check` calls in the 20
commands a second.

## 6. The "draft with AI" flow

1. A notes field (≤ 400 chars) with help: "Describe who this is in your own words. Your notes, and your current text when you revise, are sent to <provider>."
2. **Draft** button, disabled with its reason when the gate refuses network, AI is paused, a draft is running or 20 have
   been used this session.
3. The draft shows beside the current fields, field by field, unsaved: accept, edit or discard each. A discarded draft
   (safety) shows only a notice. Spoiler checks are silent: nothing marks where EldenNet dropped a sentence (SP5).
4. Only **Save** writes. Leaving the page with an unsaved draft asks first. Drafts never persist anywhere in GraceUI.

## 7. Privacy (PV rules)

- PV3: no profile text or draft in browser storage, caches, logs or console; unsaved text lives in the page only.
- PV4: draft text only on the editor (above). Profile text never in a URL, title, toast history or search (SP9).
- PV5: the bug report carries no profile text: EldenNet's bundle lists counts and states only; GraceUI's report preview
  says so.
- PV8: the editor is menu content, so recordings skip it; only the documents sent by `profile.check` and
  `profile.save` and, on the Draft click, the notes reach EldenNet; EldenNet's recordings keep a check's target and
  size and a save's target, rev and size only, and scrub draft notes and replies in any bundled recording.
- PV7: copy-to-clipboard of a profile only on a click, after a preview.

## 8. Help texts (ours, British spelling)

- Layer: "Your notes are added to how this character is played. They never replace what the character knows."
- Own character: "You can give a voice to someone you've met who has none. They talk with you when the moment is calm,
  remember what passes between you, and speak short lines on their own. In a fight, a boss or an invader speaks only
  once EldenNet has checked its own scripted lines."
- Subjects: "Things this character likes to talk about when there's a quiet moment. One is offered each time; they may
  take it up or not."
- Examples use invented characters only (for instance a bridge's armoured keeper), never a game character's text.
