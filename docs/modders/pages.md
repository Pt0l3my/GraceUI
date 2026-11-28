# The space and its pages

A mod's pages are data. It describes them once, as a **space**: its rail of categories and pages, the topics those
pages draw, the commands they send, and its settings. GraceUI draws the same data in game and in the browser, with its
own components, so a mod writes no HTML, no script and no stylesheet (D81). This part describes that data. Every
component is described in [Components](components.md).

GraceUI's own built-in spaces are written as TOML, `src/grace-pages/spaces/<id>/space.toml`, and compiled by
`tools/pages/build.py`. The same file format is checked for any space by the page lint:

    python3 -I tools/pages/build.py --lint path/to/space.toml

The lint is the authority on what is allowed: the rules below are its rules, numbered as in its own header. A mod hands
GraceUI the compiled page data and settings description with `register_pages` ([The C ABI](abi.md)); the same tool
compiles them from the mod's own `space.toml` ([Compiling a mod's space](#compiling-a-mods-space)).

## The file's parts

| Table | What it holds |
|---|---|
| `[space]` | The space's id and name, its commands and their kinds, its quick switches and its scope |
| `[[topic]]` | Each topic its pages draw: a state (one body, replaced) or a stream (rows, appended) |
| `[[category]]` | The rail's categories, in order, and the pages each lists |
| `[[setting]]` | Every setting, with its one home page ([The settings description](settings.md)) |
| `[[page]]` | Each page, with its tabs or sections, under `[[page.tabs]]`, `[[page.sections]]` and `[[page.actions]]` |

## `[space]`

| Field | Meaning |
|---|---|
| `id`, `name` | The space's id (1 to 64 of a-z, 0-9, `.`, `_`, `-` and `/`) and its name on the rail |
| `settings_schema` | The settings description's schema number |
| `commands` | Every command its pages send, `settings.set` and `settings.reset` included if it has settings |
| `slow_commands` | Commands answered within 90 s, not 10, such as a model's draft (1.6) |
| `sensitive_commands` | Commands refused from a paired device, from this PC alone (1.8) |
| `file_commands` | Commands that take one uploaded file, by the upload block alone; each sensitive too (1.9) |
| `quick` | The top bar's quick switches: a setting's shortcut by its key, or a session switch with its own command and state (1.13) |
| `scope` | The top bar's chooser on the space's scoped pages, such as a playthrough: its label and a state topic of its choices (1.14) |

## `[[topic]]`

| Field | Meaning |
|---|---|
| `id` | The topic's id |
| `shape` | `state` (one body, replaced on each change) or `stream` (rows with rising sequence numbers) |
| `ring` | A stream's rows kept for a view that comes later; a stream names it, a state does not |
| `family` | A family of state topics, `<id>/<key>`, each watched and published on its own, never the family itself (1.5) |
| `pc_only` | Never watched from a paired device (1.20) |
| `private` | It holds a player's or a model's words: drawn in its own blocks alone, never a status, a choice, a switch's state, nor anything GraceUI searches, titles, logs or keeps (1.21) |

Two topics are the framework's own. `settings` is the settings body GraceUI serves from the space's description and
values. `space.state`, if declared, holds the space's greyed pages: `{"greyed": {"<page>": "<words>"}, "inactive":
"<words>"}` (D188).

## `[[category]]`

| Field | Meaning |
|---|---|
| `id`, `name` | The category's id and its name on the rail |
| `pages` | Its pages, in order; every page of the rail is in exactly the category it names (rule 2) |
| `foot` | At the rail's foot, under a rule, after every other category (1.10) |
| `icon` | Its line icon, one of GraceUI's own: home, spark, voice, people, map, history, sliders, pulse, tools, gear, grace, lamp, book, chat, cube, shield (1.22). Give one to every category or to none |

## `[[page]]`

| Field | Meaning |
|---|---|
| `id`, `category` | The page's id, and the category it belongs to |
| `name`, `description` | Its title (at most 64 characters) and its line under the title (at most 160) |
| `help` | `purpose` (at most 512) and `steps`, three to five of at most 256 each (rule 3) |
| `related` | Up to eight pages for its Related card: the space's own, or a built-in space's as `<space>/<page>`, never itself |
| `increment` or `tag` | One of the two: the increment it ships in (`"0.2"`: absent before it), or `wip` or `later` |
| `actions` | Up to four header actions (below) |
| `tabs` or `sections` | One of the two: up to eight tabs, each with an id, a name and its sections; or up to 32 sections |
| `master` | A master list at the left, its rows a state topic's or read a page at a time; the tabs and sections its chosen row's detail (1.5) |
| `live` | Its topics move by themselves, and the player's Live updates may hold them (1.13) |
| `scoped` | It follows the space's scope (1.14) |
| `browser_only` | In the browser alone (1.17); no setting is homed on it |
| `shown_with` | Present only while this switch of the space's is on (1.17) |
| `gate` | Words asked once a session before it opens, with a confirm label; a browser-only page's (1.17) |
| `ribbon` | A band at its top while it is open, at most 32 characters (1.17) |
| `pc_only` | Never in a paired device's view (1.20) |
| `flow` | A setup flow outside the rail, its tabs its steps (1.23, below) |

### Sections

| Field | Meaning |
|---|---|
| `id`, `title` | The section's id and its card's label (at most 64) |
| `description` | A line under the label (at most 256) |
| `collapsed` | It starts folded |
| `blocks`, `advanced` | Its blocks (up to 32), and those behind its Advanced disclosure (up to 16) |
| `chip` | A short static label beside the title, at most 24 characters (1.3) |
| `status` | A live status, `{word, tone}`, from a state topic of the space's or a member of its body (1.3) |
| `actions` | Up to two actions of its own, as a header's (1.3) |

### Blocks

| `kind` | What it holds |
|---|---|
| `settings` | `keys`: the rows of settings homed on this page; each setting appears on exactly one settings block, on its home page (rule 4) |
| `shortcuts` | `keys`: rows of settings homed elsewhere, editable in place, each with a "Lives in" link to its home |
| `component` | A `component` bound to a `topic`, with its `props`; a table's `row_actions`, `row_secret`, `filter_bar` and `page` source ([Components](components.md)) |
| `form` | A small form: a `command`, one to four `fields` (each an id, a label and its most bytes; `prose` for several lines) and a `submit` label |
| `editor` | A document on a state topic, edited field by field, with its own save, check, reset, restore and draft commands (1.7) |
| `upload` | A file command's drop zone in the browser: its `command`, a `submit` label and an `accept` hint (1.9) |
| `document` | A family's member drawn as a document, on a page with a master list (1.12) |

### Actions

A header action or a section action:

| Field | Meaning |
|---|---|
| `id`, `label` | Its id and its button's words |
| `command` | The space's command it sends |
| `tone` | `primary`, `secondary` or `destructive`; a destructive one has a confirmation and sends `"confirm": true`, and GraceUI refuses its command without it (`confirm_required`) before the space hears of it, from every view |
| `confirm` | Words asked before it is sent |
| `confirm_after_ms` | Its second press counts only this long after the first, 1 to 3000 ms (1.21) |
| `state` | A header switch: a boolean member of a state topic of the space's, its command sent with `{on}` (1.15) |
| `opens` | Another page of the space's, opened once its command is answered ok, if the player is still on the page that sent it (1.23) |

## Flows

A page with `flow` is a setup flow outside the rail, such as EldenNet's First light (1.23, D278). No category lists
it; its `category` is the rail's row left open while it shows. Its tabs are its steps, two to twelve, in order. It has
no sections of its own, no master list and no setting homed on it, and is neither live nor scoped.

| `flow` field | Meaning |
|---|---|
| `topic` | A state topic of the space's own (neither a family nor private): the flow's state |
| `command` | The command its buttons send, with `{step, go}` |
| `optional` | Steps the player may skip, never the last |
| `done` | The last step's button; "Done" without it |
| `then` | The page Done opens once answered; the space's first page without it |

The space owns where the flow is. Its state holds:
- `step`: the step shown; the first while none is published;
- `done`: the steps checked;
- `blocked`: its words for what Next waits for, at most 160 characters;
- `start`: asking to be shown.

Back, Skip, Next and Done send the command with `step`, the step the view showed, and `go`: back, skip, next or done.
The space checks `step` against its own, answers, and publishes the step it moved to; the view follows what it
publishes.

When the state says `start`, the menu's next opening and the dashboard's first load land on the flow, once a session
each. GraceUI never opens the menu for it: invite the player with a notice of your own.

## Compiling a mod's space

The page tool compiles a mod's `space.toml` as it does GraceUI's own spaces, after the lint passes (D283):

    python3 -I tools/pages/build.py --build path/to/space.toml --out path/to/space [--minor N] [--header FILE] [--check]

| Option | What it does |
|---|---|
| `--build FILE` | The mod's `space.toml`, linted with the `topics.json` beside it as its fixtures, if there is one. A space with the id of one of GraceUI's own is refused |
| `--out DIR` | Writes `pages.json` (the page data) and `settings.json` (the settings description): a space folder, as grace-dev and the space check read it; add a `topics.json` of fixtures to it for them |
| `--minor N` | The protocol minor the page data names: that of the GraceUI the mod names as its floor (§6.4), 0 to this GraceUI's; by default this GraceUI's own. GraceUI refuses page data above its own minor (D264) |
| `--header FILE` | Also writes a C header (C89 or C++, any compiler) with both documents as arrays of ASCII JSON, `<ID>_PAGES` and `<ID>_SETTINGS`, with `<ID>_PAGES_LENGTH` and `<ID>_SETTINGS_LENGTH`: `<ID>` is the space's id in capitals, every other character an underscore, after `SPACE_` if it starts with a digit. That is what the mod hands `register_pages` |
| `--check` | Writes nothing, and fails if an output is not what the source builds: for a mod's CI, where the outputs are committed |

The lint checks a space against the GraceUI the tool comes with. A space whose floor is older can be linted by the
floor's own tool as well, for a component or a field newer than the floor.

`examples/hello-space` runs it at its build: its `CMakeLists.txt` writes the space folder and the header into the build
tree from `space/space.toml`, with `--minor 22` for its floor, GraceUI 0.2.70.

## The lint's rules, in short

The lint's header (`tools/pages/build.py`) holds them in full, numbered 1 to 9:
1. Ids are unique.
2. The rail lists every page in its category, the foot last.
3. A page has tabs or sections, an increment or a tag, three to five help steps, and Related links that exist.
4. Every setting has exactly one home.
5. A setting's description is valid (its default, bounds and availability).
6. A component's topic exists and has the shape it draws.
7. An action's command is the space's, and a destructive one asks first.
8. Fixtures, where they exist, have every topic, of its shape.
9. Flows keep the rules above.

A page that needs something no component offers asks GraceUI for it: only GraceUI adds a component, in both views at
once, with the protocol minor that brings it.
