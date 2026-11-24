# Proposal 0014: The top bar's controls for a space, and tables read a page at a time

**Status: proposal, 2026-11-24, put to the owner (G45) and to EldenNet's session for its formats.** §4.2.3 names six
top bar controls: search, the status lamp, Live updates, Journey, the theme and Quick switches. Search, the lamp and
the theme are built. The other three are described as EldenNet's controls (its quick switches, its journeys), but no
form exists for a mod's space to declare them, and in game "Start waits for the quick switches" (§5.4). EldenNet's
PAGES expects them: Pause AI and Conversation among the quick switches at 0.2 (PAGES §4.10), Live updates on Home and
Requests (0.2) and Events (0.4), and the Journey scoping History, Playthrough and Named NPCs' chips at 0.3. History's
Conversations (0.3) and Events (0.4) read stored history a page at a time with a command, `history.page` (§6.5, §6.6),
and no table can show rows that come that way. Since G20 these are framework features every mod may use (G16, G18), so
they are put here rather than settled alone. A bare § is a section of [DESIGN.md](../DESIGN.md).

## 1. Quick switches (power icon; pad Start)

**Proposed.** The space's page data gains `quick`, up to 12 items, each one of:

- `{ key = "<setting>" }`: a shortcut to one of the space's settings, a switch or a choice, drawn as its shortcut row
  is drawn on a page (§4.2.4), with "Lives in <its home>" as a link;
- `{ id, label, command, state = { topic, field }, home = "<page>" }`: a session switch the space owns, such as
  EldenNet's Pause AI for this session (which never writes a setting, D15). Its state is a boolean at the field of a
  state topic of the space's; a flip sends the command with `{"on": true|false}`; nothing changes until the space
  publishes the new state (invariant 1). `home` names the page that also holds the switch.

The control shows on every page of a space that declares `quick`, and nowhere else (invariant 2). In the browser it opens
a menu under the power icon; in game it is a focus stop in the top bar, and pad Start opens it from anywhere (its press
never reaches the game, §5.4's table). ↑ ↓ choose a row, Enter or ← → flip or step it, Esc or B close it. The lint
checks that a key is a switch or choice the space describes, that a command is declared and is neither a file nor a
slow command, and that `home` is a page holding that command as a header or section action.

## 2. Live updates (clock icon): Live, every 5 s, Paused

**Proposed.** A page may say `live = true`. On such a page the top bar shows the control, as view state for the session.
**Every 5 s** applies the page's component topics at most once every 5 seconds: a state topic its latest body, a stream
the items that arrived meanwhile, in order. **Paused** applies none, and a quiet line above the first block says "Paused ·
12 new" with Resume. The page's settings rows, its forms, its editors and any answer to the player's own command are
never held: only what moves by itself. Leaving the page or closing the menu keeps the choice for the session; a new
session starts Live. In game it is a focus stop in the top bar; ← → step it.

## 3. A scope for a space's pages (EldenNet's Journey)

**Proposed.** The space's page data gains `scope = { label = "Journey", topic = "status", field = "scope" }`, and a page
that follows it says `scoped = true`. The topic's field holds `{"choices": [{"id", "label", "read_only"}], "current":
"<id>"}`. On a scoped page the top bar shows the chooser, its label and the chosen choice's words; each open of the menu
or the dashboard starts at `current`. What the chosen scope does:

- a block of a scoped page bound to a topic family's name (D215) draws the member `<family>/<scope id>`, which the view
  watches like any member, so a space publishes an earlier journey's state only when it is watched;
- a paged table (§4) on a scoped page sends `"scope": "<id>"` with each page it asks for;
- a choice marked `read_only` greys the page's writing controls, as a greyed page does (D188), with the words "An earlier
  journey: read only" from the choice's label.

No scope published: no chooser, and a scoped page draws its current state as an unscoped one would (invariant 2). The
chooser's words are the space's own (EldenNet's PV2 keeps the character's name in it alone); they never go in a title,
an address or a log.

## 4. A table read a page at a time

**Proposed.** A table block, or a page's master list, may take its rows from a command in place of a topic:
`page = { command = "history.page", args = { view = "events" } }`. The view sends the command with those args and
`cursor` (absent for the first page), `filters` (each filter's chosen value, by column), `sort` (`key`, `direction`),
`search` (its text, if the bar has one) and `scope` (§3). The space answers in the result's `body`, as any command may
(up to 256 KiB): `{"columns": [...], "rows": [...], "next": "<cursor>", "total": 1234}`, at most 200 rows, `next` and
`total` optional. The view shows the rows, "1,234 in all" when `total` is given, and **Show more** at the end while there
is a `next`, which asks for the next page and adds its rows below. A refusal shows the space's words in place of rows
(EldenNet's "Load a character to see this" at the title screen).

- **Filtering is the space's.** The view holds one page, not every row, so a filter bar on a paged table sends its
  choices and asks for the first page again on each change (D250 filters only rows it holds). A filter there names its
  choices itself, `choices = [...]`, or a state topic's list, `choices = { topic, field }` (EldenNet's regions entered);
  a search is sent 300 ms after the typing stops in the browser, on Enter in game.
- **New rows.** A paged table may name `changes = "<state topic>"`, a topic whose body changes when new rows exist. On
  a change, Live updates allowing (§2), a table showing only its first page asks for it again; one showing more says
  "New rows · Show" at its top and moves nothing until asked.
- **The rest as before.** Row actions, row secrets and switch cells act on paged rows as on published ones, each naming
  its own row; a paged master list's detail is its family's member, as in D215. Leaving the page forgets its pages.
- **Rules.** The command is declared like any command and answers within 10 s; the space applies its own rules to what
  it answers (EldenNet's SP2, SP3 and SP7 in its projection layer); nothing a page answered is searched by the global
  search (D219) or stored by the view.

In game the rows are drawn as a table's, Show more is a focus stop after the last row, and a pending page shows
"Loading…" in place of the rows it will add.

## 5. The options (G45)

(a) **All four, in two protocol minors** (recommended): Quick switches and Live updates first, which EldenNet's 0.2
pages want now; the scope and paged tables next, for its 0.3 History pages. (b) **All four in one minor**, later, when
0.3's pages are built. (c) **Only the scope and paged tables**, the top bar's other two left as described. (d) **None**:
EldenNet keeps Pause AI on Home's header only, no live page holds still, and History waits.

## 6. Cost

`protocol.toml`: `quick`, `live`, `scope` and `scoped`, the table's and the master's `page` with `changes`, and the
filter's `choices`; the page lint and registration's checks of each; both views (the browser's top bar and its menus,
the game's top bar stops and Start; the held topics; the scoped members; the paged table's requests, Show more and its
refusal); the demo space's examples (a quick switch of each kind, the lamp log live, a "Season" scope over the lamp
records, a paged history of lamp lightings with a filter bar), served by the demo space's own handler; the view's, the
web's, grace-dev's and the harness's tests and goldens. No change of the ABI. About the size of G43 and G44 together.
