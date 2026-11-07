# Study: SkyrimNet's dashboard, how it groups an AI framework, and what EldenNet takes

Written 2026-11-07 for GraceUI's information architecture and cited as **SND**. [GraceUI DESIGN](../DESIGN.md) §4
adopts its structure (D38–D48); §4.11 there holds the binding answer for each SkyrimNet page EldenNet has no
counterpart for. Everything below is in our own words. SkyrimNet's page and category names are given only as facts
about its structure; no SkyrimNet text, prompt, code, icon, styling or theme name is reproduced, and its name is not
used to promote GraceUI or EldenNet.

## 0. Sources, method and tags

### 0.1 What was looked at

- **The owner's screenshots** of SkyrimNet's beta30 dashboard: four WebP images (Home, AI › Providers, Voice ›
  Text-to-Speech, History › Memories) and thirteen PNG images (Behaviour › Prompts, System › Logs and eleven
  Settings pages). Viewed as images only. They show the owner's account name, local network and loopback addresses
  with ports, a custom provider's name and a local install path; none of it is recorded here.
- **The SkyrimNet devkit's documentation** (beta30-rc2): `FEATURES.md` (the feature tour), `Agents.md` (the model
  jobs) and the `modding/` guides, read as text with `grep -n` in an isolated folder. No code was read beyond what a
  document quotes.
- **The dashboard itself, rendered** (beta30-rc2, its current web app), at the owner's request ("check and run those
  to find out what the UI structure is like"). How, in §0.2.
- **EldenNet's research on SkyrimNet**: SN00 §1, SN01 §6.1–§6.3, §9, §10, §14.2–§14.3, SN06 §4 and §7.3, SN07 (ideas
  only), read with `wp`.

### 0.2 How the dashboard was run (sandbox)

- Its folder was served by Python's own HTTP server bound to 127.0.0.1, started from a folder of ours outside it,
  and opened in a headless Chromium. A route handler aborted every request to any other origin, so nothing left the
  machine; DNS resolved nothing but loopback, and service workers were blocked. The blocked requests were the
  online model-preset and add-on indexes, a provider list and the wiki.
- No file of the bundle was read, printed, searched or copied. Only what the browser drew was observed: screenshots,
  the accessibility tree of each page, and the names of the API calls the page made.
- Without the game-side server the page waits for its configuration and draws nothing, so the route handler answered
  every API call with an empty object, kept what the page saved and handed it back, and served the entry page for
  in-app paths, as the game-side server would. Only one field was seeded: that the first-run tutorial was finished
  (after the tutorial itself had been recorded).
- Pages whose data comes from the game drew empty or "loading" states (§2.10). Their contents come from the owner's
  screenshots and the devkit's documents instead. The legacy web folder has no entry page and was not rendered.
- The server and the browser were stopped at the end. Our screenshots, outlines and tools stay outside the
  repository.

### 0.3 Tags

| Tag | Source |
|---|---|
| `[W2]` `[W3]` `[W4]` `[W5]` | The owner's WebP screenshots: Home, AI › Providers, Voice › Text-to-Speech, History › Memories |
| `[P060809]` … | The owner's PNG screenshots, by the time in their file name: `060843` Prompts, `060907` Logs, `060809` Settings › Conversations, `060933` Subtitles, `060952` Hotkeys, `061023` Audio, `061036` Voice, `061125` Events, `061147` Actor filter, `061205` Dynamic bios, `061219` Memory, `061239` an add-on's page, `06123d9` Dashboard customisation |
| `[R]` | The sandboxed run (§0.2); `[R:tutorial]`, `[R:home]` and so on name the page |
| `[F:n]`, `[A:n]` | Lines of the devkit's `FEATURES.md` and `Agents.md` |
| `[M:file:n]` | Lines of a `modding/` guide (`CONTENT_ROOTS`, `MIGRATING_TO_BETA25`, `WORKFLOW_ACTIONS`, `WORKFLOW_TRIGGERS`, `WORKFLOW_PROMPTS`, `WORKFLOW_MOD_INTEGRATION`, `prompts-and-decorators`) |
| SN01 §9 … | EldenNet's research, read with `wp show` in the EldenNet checkout |

SkyrimNet spells its category names the American way; this note writes them in British spelling (Behaviour).
`Agents.md` describes an older layout of the model pages than beta30's AI › Models [A:8-41]; the screenshots and the
run are taken as current.

---

## 1. The frame

### 1.1 Sidebar categories and their pages

| Category | Pages | Source |
|---|---|---|
| Home | One page, no sub-pages | [W2] [R:home] |
| AI | Providers, Models, Vision, GameMaster | [W3] [R] |
| Voice | Text-to-Speech, Speech-to-Text, Bard Singing, Samples & Effects (two link tabs) | [W4] [R] |
| Characters | One page, no sub-pages | [W2] [R:characters] |
| History | Events (with an event-schema explorer reached from its Related card), Memories, Diaries, World Knowledge (four link tabs), Places | [W5] [R] |
| Behaviour | Prompts, Actions (three views), Triggers, Universal Translator | [P060843] [R] |
| System | Logs (two views), Traces, Performance, Packages | [P060907] [R] |
| Tools | File Editor, Game Data Explorer, Event Monitor, AI Assistant, a cloud GPU rental page; a debug-tools page is reached from Settings and is not listed | [R]; [F:157-175] |
| Foot | Settings, Wiki, Plugins, then the signed-in account and a community-chat button | [W2] [R] |

Eight categories, 28 listed pages and three foot pages. The Tools category is collapsed in every owner screenshot;
its contents come from the run and agree with the devkit's tour [F:157-175].

### 1.2 Sidebar behaviour

- The product's wordmark in the accent colour at the top, with a button that collapses the sidebar [W2] [W3] [R].
- Each category is an icon and a label, separated by thin rules; a chevron marks the ones with pages [W2].
- The categories form an accordion: one is open at a time [R]. The open category shows its pages indented under it,
  joined by a thin vertical guide line; the open category and the current page both carry an accent bar at the left
  and a tinted fill [W3] [W4] [W5].
- The foot is pinned to the bottom under a rule. Settings highlights like a page when open [P060809].

### 1.3 Top bar

A thin strip above the page, with no ground of its own, holding up to three compact menus at the right [W2] [W3] [W5]
[P060907] [R]:

| Control | What it does | Shown | Source |
|---|---|---|---|
| Refresh interval (clock icon) | How often live pages re-fetch: off, 1, 5 or 10 seconds | Pages with live data; absent on Text-to-Speech, Prompts and Settings | [W2] [W4] [P060843] [R] |
| Theme (palette icon) | Picks one of six colour presets named after the game's factions and moods; the same choice as Settings › Dashboard › Customisation | Most pages | [W2] [P06123d9] [R] |
| Quick toggles (power icon) | A menu of the master switch, about ten feature switches (the director, NPC and player reactions, narration, songs, translation, bio updates, memory generation, vision, actions) and the dashboard language | Every page | [R:home] |

The middle control is a theme picker, not a character or playthrough selector [W2] [P06123d9].

### 1.4 Page frame

- **Header**: a large title, a circled help mark and a one-line grey description [W3] [W4] [W5] [P060809]. The help
  mark opens a dialog: what the page is, a short "how to use it" list, and links into Settings [R:memories].
- **Header actions** at the right: Home's quick actions and its Customise button [W2]; Settings' buttons for
  pending changes to recommended defaults (with a count), restarting the tutorial and the debug tools [P060809].
- **Tabs**: underlined text tabs, the active one in the accent colour [W3] [P060809]. A tab row can also name
  alternatives of one thing, such as one tab per speech engine with a dot on the engine in use [W4]. A two-part
  **segmented switch** is used for two views of one list (system logs or model requests; file names or file
  contents) [P060907] [P060843].
- **Sections**: rounded cards with an uppercase accent label, an optional one-line description and a chevron;
  collapsed, they show only label and description [W4]. Inside, an "Advanced" disclosure hides rare fields [W3] [W4].
- **Related**: a card at the foot of a page listing linked pages, each a title, a one-line description and an arrow
  [W4]; the links are often deep links into a setting or a section of another page [R].
- **Empty states**: a centred grey sentence with a smaller hint under it; the healthy state ("no recent errors") in
  green [W2].

### 1.5 Setting rows

- A name and a description that ends by stating the default [P060809] [P060933] [P061023].
- The control at the right for switches, menus, key binders and checkboxes; sliders run the full width under the
  name, with the value and its unit in the accent colour at the right (game units, multipliers, milliseconds,
  percentages, seconds) [P060809] [P060933] [P061023].
- A setting made moot by its parent is shown dimmed, never hidden [P060933] [P061023].
- A key binder shows the key (or "unset"), a button to set it and one to clear it [P060952].
- Chip multi-selects for choosing several named items [P061205]; editable tables with fields, switches and a slider
  per row and a delete button at the end [P061125].

### 1.6 Data pages

- **Stat tiles** above a table: a big accent number and a small uppercase caption [W5]. Level tiles that are also
  filters, the chosen one outlined [P060907].
- **Filter bar**: search, menus for actor and type, sort, a minimum-importance slider, a direction button [W5]; or
  level range, page size and a "showing x–y of z" count [P060907].
- **Toolbar**: the primary action (create) in accent at the left, Refresh, and at the right further actions with the
  destructive one in red [W5].
- **Tables**: small uppercase grey column heads, thin row rules, per-row action icons at the far right (play, edit,
  delete in red) [W5].
- **Two panes**: a searchable list at the left with filter chips, the chosen item's detail at the right
  (Characters [R:characters]; Prompts' file tree and editor [P060843]).

### 1.7 Home

- **Quick setup**, a collapsible full-width band of three cards: the language model (preset, variant, a test button),
  text-to-speech (engine, a test button, a cloud GPU set-up button) and speech-to-text (engine, model size, a test
  button) [W2] [R:home].
- **Cards** in a grid of mixed widths, each with an uppercase accent title: latest screenshot, system status (game
  state, the director's countdown, whisper mode, uptime, version, build), nearby NPCs, short-lived events, errors,
  recent model requests (wide), recent events [W2] [R:home].
- **Header actions**: generate profile updates or diary entries for nearby NPCs or the player, with counters [W2].
- **Customise**: a panel to show or hide each card (twelve, among them load-order matches, pinned characters and the
  two generators) and to put Quick setup at the top or the bottom [R:home]. The tour adds pinned characters with a
  quick teleport [F:161].

### 1.8 Look (recorded, not adopted)

A dark charcoal page, slightly lighter cards with thin borders and rounded corners, and one warm gold accent;
primary buttons filled with the accent, destructive ones filled red, switches in the accent when on; blue for
information, orange for a running timer, green for healthy, red for errors [W2] [W3] [W5] [P060907]. GraceUI keeps its
own Grace theme (DESIGN §3), which was drawn from SeverActions' spirit before this study (LOOK); what the two share is
one gold accent on a dark ground. No SkyrimNet colour value, icon or style is used.

### 1.9 First run

A full-screen tutorial runs until finished or skipped [R:tutorial]: welcome; a community page; choosing a provider
(an aggregator marked as recommended, several cloud hosts, a local model, a custom one) and entering its address and
key; choosing models (presets or by hand) and testing each job; choosing and testing a speech engine; choosing and
testing a speech-to-text backend; four pages of hotkeys; player options (voices, reactions, memories, voice input,
subtitles); the player's profile; add-ons matched to the player's load order (online); a closing checklist. A
separate feature guide walks through the framework's features one page each [R:tutorial] [F:29].

---

## 2. Page by page

### 2.1 AI

| Page | What it holds | Source |
|---|---|---|
| Providers › Providers | A banner for the vendor's own hosted service (account and subscription, a one-click set-up); one card per built-in provider (endpoint, masked key with an eye, Advanced); a "decisions" variant of one provider that borrows the main key when left blank; user-added custom providers with a request style (chat completions, or a typed-decisions style that only the decision slot accepts) and a red Delete | [W3] [R]; [F:45-54]; SN01 §6.1 |
| Providers › Routing | Automatic failover between providers (an account perk); a routing console (providers, the plan per job, this session's routing decisions); a guard that abandons a slow first token, with its patience, minimum and cold-start waits, and demotes slow providers for the session | [R]; SN01 §6.3 |
| Models | A preset bar (choose, save, delete, import, export, reload); one card per job ("variant"), each with a test button and rows of provider, model and temperature with a switch and a gear per row, and "add a model to the rotation". The job list comes from the game-side server; the run shows only the dialogue job, and Related links name others (vision, memory, director evaluation, translation, diaries, agents) | [R]; [A:8-41]; [F:50]; SN01 §6.2 |
| Vision | The vision feature's switches (on, automatic capture, worn items), bulk item capture, a gallery of described screenshots | [R]; [F:112-114, 167] |
| GameMaster | The director's switches (on, its agent), the actions it may use, a global action cooldown, its recent requests | [R]; [F:41]; [A:13-14] |

### 2.2 Voice

| Page | What it holds | Source |
|---|---|---|
| Text-to-Speech | A header card (engine, Test voice); one tab per engine (nine to ten, local and cloud, and a generic OpenAI-compatible one); sections: configuration (endpoint, key, model, language, sampling, time-outs, parallel requests, audio tags; Advanced), fallback voices with a mapping table, a pronunciation dictionary, audio tags carried across sentences; Related: Speech-to-Text and the GPU rental page | [W4] [R]; [F:67-85] |
| Speech-to-Text | Engine and Test; tabs for a local engine, a second local engine and an external one; model download, language, name hints, open-mic notices | [R]; [F:89] |
| Bard Singing | Songs written by the model and made by a music service: on, composing, provider and backup, volume, interval, cache, style tags, a song list with import | [R]; [F:132-134] |
| Samples & Effects | Voice samples for cloning (bulk clone, voice-type list, detail) and voice effects (a recipe list and detail) | [R]; [F:87, 166] |

### 2.3 Characters

One two-pane page: a searchable list with "resolved" and "nearby" chips and a resizable split; the detail holds the
character's profile, voice and overrides. A New Character dialog creates a profile for an actor in range or a
"virtual entity" (a bodiless narrator or voice), optionally generated by the model [R:characters]; [F:38, 42, 162];
[A:21-25].

### 2.4 History

| Page | What it holds | Source |
|---|---|---|
| Events | Tiles (total, cached, cache size); all events or by actor; search, type, sort, direction; create, save, reload, clear recent; a table of id, actors and time, type, content, location. Related: event settings and the event-schema explorer (a searchable list of event types and their fields) | [R]; [F:175]; [M:WORKFLOW_TRIGGERS:248-287] |
| Memories | Tiles (memories, actors with memories, memory types); a re-index progress bar; search, actor, type, sort, a minimum-importance slider, direction; create, refresh, generate, test recall, clear all (red); a table of id, actor and time, a type pill, the first-person content, and importance as a number with a mood word; play, edit and delete per row | [W5] [R]; [F:56-61, 163]; [A:16-17, 40-41] |
| Diaries | Diary generation on, respecting the in-game day; search; entries by in-game date | [R]; [F:62, 170] |
| World Knowledge | Entries (search, by pack, re-embed, new); packs (import, new); a knowledge check (retrieval weights and limits, an NPC and a query, run); an assistant chat for writing packs | [R]; [F:63-64]; [M:MIGRATING_TO_BETA25:153-173] |
| Places | Where you are, and named spots inside locations from the player's places and installed packs | [R]; SN01 §8.1 |

### 2.5 Behaviour

| Page | What it holds | Source |
|---|---|---|
| Prompts | An editor: auto-save, save, backups, a prompt-set picker, docs, preview, an assistant; a file tree with per-file switches and a file-name or content search; a line-numbered template editor with a validity pill, a variables panel and a status line | [P060843] [R]; [M:WORKFLOW_PROMPTS:5-20, 83-99]; [M:prompts-and-decorators:7-27] |
| Actions | Three views: the action library (tiles for total, enabled, disabled, file-managed, cooling down; search, reload), actor eligibility (nearby actors and why an action applies), registered tags (read-only) | [R]; [F:142-151, 169]; [M:WORKFLOW_ACTIONS:5-13] |
| Triggers | Tiles; search, type, create, reload; a list of "when this happens, do that" rules (event, conditions, cooldown, chance, audience, a response kind such as a thought, a line, narration, a diary entry or a profile update). Related: Event Monitor, Game Data Explorer | [R]; [F:116-130]; [M:WORKFLOW_TRIGGERS:5-12, 105-129] |
| Universal Translator | Rules that turn speech patterns or other languages into lines and subtitles, with a default rule and import | [R]; SN01 §6.2 |

### 2.6 System

| Page | What it holds | Source |
|---|---|---|
| Logs | A collapsible grid of log files (size, date, download); two views, system logs and model requests; level tiles that filter; level range, page size, clear, refresh, search; one card per entry with its level, source line, message, function, time and thread. The entries shown carry local addresses and ports | [P060907] [R]; [F:175] |
| Traces | Tiles (traces, spans, average and longest); live view, export, import, clean up, clear; active traces | [R]; [F:175] |
| Performance | Thread-pool summary, activity, rates, world context, recent errors, task times | [R]; [F:175] |
| Packages | Behaviour packages the framework has applied to actors (all, or nearby), with tiles, search and sort | [R]; SN01 §9 |

### 2.7 Tools

| Page | What it holds | Source |
|---|---|---|
| File Editor | A tree of the content layers' files (base and overlay, editable or read-only, enabled), new file or add-on, and an editor | [R] |
| Game Data Explorer | A risk warning; categories of game records (quests, spells, items, factions, races, globals, messages, dialogue and more); counts, search, a table | [R]; [F:174] |
| Event Monitor | Pause, clear, auto-refresh; tiles (events a second, shown, total, filtered, persisted, short-lived); a developer-mode switch; type filter; a live list | [R]; [F:175] |
| AI Assistant | Conversations with agents that query the game through the framework's tool server | [R]; [F:171, 197-209]; [A] |
| GPU rental | A cloud GPU service: key, instances, offers, presets | [R]; [F:179-181] |
| Debug tools (from Settings) | Recall debugging, identity repair and migration, script and property inspection, a function runner, a vision test, a template tester, a crash test | [R] |

### 2.8 Foot

- **Settings**: one page with its own side list and search, and three views: settings, per-character overrides, and
  pending changes to recommended defaults [P060809] [R] (§3).
- **Wiki**: documentation fetched live from an external wiki; blocked in the run [R].
- **Plugins**: an online index of add-ons with install and update; blocked in the run [R]; [F:153-155, 172];
  [M:MIGRATING_TO_BETA25:46-82, 134-151]; [M:CONTENT_ROOTS:9-40].
- **Sign in**: a third-party account dialog for supporter perks [R].

### 2.9 The chat overlay (in game, not the dashboard)

Streaming replies, awareness radii, whisper and group modes, `@name` targeting, continuous NPC-to-NPC scenes, a chat
history with edit and delete, slash commands, the chat box's font and position [F:33-43, 99-110]. GraceUI leaves the
chat box to EldenNet's overlay (DESIGN §1.3).

### 2.10 Empty without the game-side server

Every Home card; provider cards; all but one model job; the vision gallery; the director's actions and activity;
samples and effects; the character list; events, memories, diaries, knowledge entries and places; the prompt tree;
actions and triggers; logs, traces, performance and packages; the file editor, the explorer, the event monitor and
the assistant; per-character overrides; the key binders (all unset). Settings sections, the speech and song
configuration, routing and the translator draw fully from their defaults [R]. A lesson for GraceUI: a fake back end
with fixtures (DESIGN §9.2) lets every page be built and tested away from the game.

---

## 3. Settings: groups and kinds

Settings is a second tree beside the sidebar: a left navigator with a search box and uppercase groups, each opening
into sections; the right column shows the chosen section's rows [P060809] [R]. Twelve groups in the owner's shots
(eleven in the run, where no add-on was installed and the add-ons' group was absent).

| Group | Its sections | Kinds of setting (our words) | Source |
|---|---|---|---|
| General | General; Subtitles; Hotkeys; GPU rental | The master switch, in-game notices, generated documentation, automatic content updates, usage reporting; subtitles for NPCs and the player, the framework's own subtitle window, lines over heads, fonts per script, size, colour, position, background; about eighteen hotkeys in four groups (input, dashboard, mode switches, actions) | [P060933] [P060952] [R] |
| AI Features | Director; Vision; links to Providers and Models | The director's agent, cooldowns, time-outs, radius and context; capture intervals, subjects, image sizes, limits | [R] |
| Audio & Voice | Audio; Voice; speech text filter; voice samples; links to the three voice pages | Mixing through the game's audio engine, pausing with the game, volumes, audible distance, spatial audio; the player's voice and its placement, voicing the game's own lines, narration, lip-sync helpers; text filters; sample lengths | [P061023] [P061036] [R] |
| Conversations | Conversations; dialogue filter | Awareness radii (normal and whisper), who counts as nearby, background talk distance, rewriting lines, actions inside the dialogue call, interrupting on voice input, how much history a call sees, time-outs per kind of line, line of sight, how the next speaker is chosen, use of the decision model for mood, speaker and addressee; text filters | [P060809] [R] |
| Dialogue Actions | Filters; instructions | Which of the game's own dialogue (quests, followers, merchants, trainers and more) the model may pick, and instructions per topic | [R] |
| Events & Reactions | Events; player reactions | All event processing, NPC reactions, a global reaction cooldown, hiding the player's name in death events, events loaded at start; a per-event table (event type, label, category, switches for enabled, kept, short-lived, NPCs react, interrupts, a cooldown slider, delete) | [P061125] [R] |
| NPCs | Actor filter; NPC thoughts; NPC groups; virtual entities; identity links | Allow or block lists by faction, race and sex, with a summary of what is in effect and lists from other add-ons; private thoughts and their cooldown; groups; narrators | [P061147] [R]; [M:CONTENT_ROOTS:79-85] |
| Diaries & Memories | Dynamic bios; diary generation; memory; memory filter | Profile updates (which blocks, how much context); diary length and timing; background memory work, retrieval counts, the embedding model, segmentation, scoring weights, re-indexing threads | [P061205] [P061219] [R] |
| Records | Items; spells; furniture names | Renaming nearby game records | [R] |
| Plugins | One section per installed add-on | Each add-on's own settings, in the same row kinds | [P061239] |
| Dashboard | Customisation; web server | The dashboard's language, opening on the character under the crosshair, theme presets and custom themes; the web server (on; host, 127.0.0.1 by default; port, 8080 by default; a legacy browser UI port, 8081) | [P06123d9] [R] |
| Developer | Analytics; systems; tool server; assistant | Log levels and counts, trace retention, template tracing; queues and time-outs; the tool server for outside assistants (on, host, port); the agents' limits | [R] |

Settings links out to some sidebar pages with outward arrows (providers, models, speech, listening, songs), and some
topics have both a sidebar page and a Settings section of their own: the director and vision (AI Features), and the
voice pages beside Audio & Voice's own sections [P061023] [R]. That is two homes for one topic.

---

## 4. How SkyrimNet categorises an AI framework

Read across §1–§3, the categories are layers of the framework, not topics of the game:

| Category | What belongs in it | Pattern |
|---|---|---|
| Home | Is it working, who is near, what just happened, what the model was asked | A customisable card grid with a set-up band |
| AI | Where model calls go and which model does which job; features that are mostly a model of their own (vision, the director) | Cards per provider and per job; tests |
| Voice | Sound in and out: speech, listening, songs, voices | One tab per engine; a test in the header |
| Characters | Who the actors are: profiles and their overrides | Two panes |
| History | What the framework recorded: events, memories, diaries, knowledge, places | Tiles, filter bar, table |
| Behaviour | What drives NPC output: prompts, actions, triggers, translation | Editors and rule lists |
| System | Health and diagnostics: logs, traces, performance, what it applied to actors | Tiles, level filters, lists |
| Tools | Power tools for authors and developers: file editing, a game-data browser, a live event view, agents | Mixed |
| Foot | Configuration, documentation, content | A settings tree, a wiki, an add-on hub |

Five habits recur: a feature's own switch sits on its page; the quick-toggle menu repeats the main switches
everywhere; every setting states its default; Related cards and deep links stitch the tree together; and live pages
can be slowed or paused from the top bar.

---

## 5. What EldenNet takes, adapts, defers or leaves out

**Take** = used as it is (in our own words and look). **Adapt** = the idea, changed for EldenNet. **Later** = a seam
or Deferred cell in EldenNet's design, shown as a later-version statement (G7). **Not planned** = no EldenNet design
entry. **Never** = against a rule. GraceUI DESIGN §4.11 is binding; the codes are EldenNet's unless marked.

### 5.1 The frame

| SkyrimNet element | EldenNet | Why |
|---|---|---|
| The eight categories, the accordion with a guide line, a collapsible rail | **Take** (D38) | The owner, 2026-11-07: a framework, categorised the way SkyrimNet does it |
| Category and page names | **Adapt**: plain framework names in British spelling; Elden Ring's features and settings inside pages, in our own words, game names only at run time (D39) | The same request; flavour names hid the framework |
| Characters as one page | **Adapt**: two pages, Named NPCs and NPC types | NPC types and allowances have no SkyrimNet page (DESIGN §7.6, O38) |
| Foot: Settings, Wiki, Plugins | **Adapt**: Settings, Docs (bundled, offline), Content packs (read-only) (D46) | GraceUI PV6; no hub (SN01 §14.2) |
| Foot: account row, community link, sign-in | **Never** | No accounts; GraceUI's PV rules |
| Top bar: refresh interval | **Take** as "Live updates", view state only | A moving table can be read |
| Top bar: theme picker | **Adapt**: Grace or Grace Clear (a shortcut of `ui.menu.theme`) | Two variants (DESIGN §3.15) |
| Top bar: quick toggles | **Take** as Quick switches: shortcuts only, each saying where it lives (D41) | One home per setting (invariant 12) |
| Top bar: (none) | **Add**: search, a status lamp, the journey in view (D41) | Status must stay visible; NG+ journeys scope History |
| Header with help mark and description; tabs; segmented switch; collapsible sections; Advanced | **Take** | Readable, regular pages |
| Defaults stated in every row; dependent rows dimmed | **Take** (DESIGN §4.7) | The player sees what "reset" does and why a row is off |
| Related card with deep links | **Take** | Stitches a large tree; shortcut rows already do the same for settings |
| Stat tiles, filter bar, toolbar, tables with row actions | **Take**, counting only what was reached (SP5); search only where the index holds no story or chat text (D21) | GraceUI's spoiler rules |
| Home's card grid with Customise and a set-up band | **Take** (D42) | The player chooses what to watch |
| Settings as a second full tree | **Adapt**: Settings holds only settings without a category page, links out to the rest and lists all of them in one index (D40) | Invariant 12 forbids two homes |
| Settings › per-character overrides | **Adapt**: Characters › NPC types (allowances per type and named slot) | O38 |
| Settings › pending default changes | **Adapt**: an advice notice on AI › Models when `models.toml` changed | DESIGN §11.3 |
| First-run tutorial and feature guide | **Adapt**: First light (D16) and Docs › Getting started | Our own steps, EldenNet's checks |
| Theme presets, dashboard language, opening on the crosshair target | **Not planned** beyond Grace's two variants; English only (O16); a crosshair jump is a possible later nicety | — |

### 5.2 Pages

| SkyrimNet page | EldenNet | Why |
|---|---|---|
| Home: status, nearby, short-lived events, errors, recent events, model requests | **Take**, perception-filtered (SP7), model requests without prompt text (invariant 1) | DESIGN §4.4, §6.4, §2.7 |
| Home: latest screenshot, profile and diary generators, pinned characters with teleport | **Not planned**; teleport **never** (a game write) | DESIGN §0 item 3 |
| AI › Providers | **Take**: cloud with the player's key or local, probes, write-only keys, health | DESIGN §11.1–§11.4, O7 |
| AI › Providers › Routing | **Adapt**: one fallback per job slot (AI › Models), health on Providers; no rotation or first-token race | SN01 §14.2 |
| Request styles, decision providers | **Later** (the reserved `meta` slot, `DecisionRequest`) | DESIGN §11.2, §13 |
| AI › Models | **Take**: five job slots, presets as data, fixed budgets, the self-test | DESIGN §11.2, §11.3, §11.5 |
| AI › Vision | **Not planned** | Sending screen captures to a provider is a privacy question; heavy (SN01 §14.3) |
| AI › GameMaster | **Later**: AI › Director, a statement page | DESIGN §13, the `Directed` class |
| Voice › Text-to-Speech, Speech-to-Text | **v1.5**: statement pages in v1 | O8 |
| Voice › Bard Singing; voice effects | **Not planned** (effects could follow speech) | SN01 §14.3 |
| Voice › Samples (cloning) | **Never** for the cast's voices | DESIGN §1.2 |
| Characters: hand-written profiles | **Adapt**: personas and archetype cards, read-only display fields | DESIGN §7.3, §7.4 |
| Characters: generated profiles, profile updates, virtual entities | **Not planned** (the journal is the evolving layer) | DESIGN §7.1, §10.3; SN01 §14.3 |
| History › Events | **Take**, read-only and perceived events only; the live view moves to Tools › Event monitor | DESIGN §6, §10.1; SP7 |
| Event creation, editing and the schema explorer | **Never** for edits (D13); schemas in Docs and Workshop | Validation; spoilers |
| History › Memories | **Adapt**: journals, keepsakes and facts per named NPC; Forget this NPC and Forget this journey; no create, edit, generate or recall test | DESIGN §10.1–§10.5; D13 |
| History › Diaries | **Later** | DESIGN §1.2 |
| History › World Knowledge | **Adapt**: History › World shows world stages that hold and who has heard; the knowledge base itself is **never** shown (SP1); its files appear on Content packs | O28–O30; spoilers |
| History › Places | **Adapt**: a region filter on History › Events | Regions are context only |
| Behaviour › Prompts | **Not in the UI**: prompts are files with overrides | Prompts carry spoilers (invariant 1) |
| Behaviour › Actions | **Later**: Behaviour › Acts, a statement page | O41 |
| Behaviour › Triggers | **Adapt**: Behaviour › Reactions (each event type with its switch and cooldown); player rule files later | O33; `augur.allow_custom_rules` |
| Behaviour › Universal Translator | **Not planned** | English only (O16) |
| System › Logs, Traces, Performance | **Take**, scrubbed (PV5) | DESIGN §14 |
| System › Logs › model requests view | **Adapt**: AI › Requests (slot, purpose, timings, outcome, cost; no prompt or unshown text, invariant 1) | Model calls belong with providers and models; Logs keeps the scrubbed log only (D49) |
| System › Packages | **Never** | Zero game writes |
| (none) | **Add**: System › Status (the degradation ladder, read groups, checks, compatibility) | DESIGN §4.4, §3.4 |
| (none) | **Add**: Behaviour › Conversation, Speaking up, Combat, Awareness, Pacing | O31–O42: EldenNet v1 features with no SkyrimNet page |
| Tools › Event Monitor | **Take**, perceived events and EldenNet's decisions | DESIGN §6; SP7 |
| Tools › File Editor | **Not in v1** (no content editors, D2) | — |
| Tools › Game Data Explorer | **Never** | Spoilers (SP rules) |
| Tools › AI Assistant and the tool server | **Not planned** | SN01 §14.3; console commands would be game writes |
| Tools › GPU rental | **Not planned** | SN01 §14.3 |
| Debug tools | **Adapt**: Tools › Workshop (browser only, behind `debug.panel`) and F11 | SP8 |
| Wiki | **Adapt**: Docs, bundled and offline | PV6 |
| Plugins | **Adapt**: Content packs, read-only; no hub | SN01 §14.2 |

### 5.3 Settings groups

| SkyrimNet group or section | EldenNet | Why |
|---|---|---|
| General: master switch, hotkeys | **Take**: Settings › General and Settings › Hotkeys | No category page fits them |
| General: subtitles | **Adapt**: Voice › Text and subtitles (in v1 text is the voice) | D43 |
| General: usage data, automatic content updates | **Never** | No telemetry; no request but the provider's (DESIGN §1.2) |
| Audio & Voice: mixing, volumes, audible distance, spatial audio | **Later**: v1.5, designed with text-to-speech (`SpeechSink`); nothing in v1 | O8; DESIGN §12 |
| Audio & Voice: the player's voice and its placement, narration | **Not planned** (the player character is unvoiced; EldenNet voices NPC lines only) | SN06 §7.3 (the Tarnished is a silent protagonist); O8 |
| Audio & Voice: voicing the game's own lines | **Not applicable** (the game voices its own lines) | — |
| Audio & Voice: lip-sync helpers | **Later** (v2: native subtitles and mouth movement, S9); none in v1 | DESIGN §1.2, §12 (ER03 §5) |
| Conversations: radii, nearby, history | **Adapt**: Behaviour › Pacing (reach) and Behaviour › Conversation | DESIGN §5.4, §12 |
| Conversations: whisper mode, continuous scenes, player reactions | **Not planned**; scenes adapted as NPC-to-NPC exchanges within pacing | SN06 §7.3 (the Tarnished is a silent protagonist); O34 |
| Dialogue Actions (the game's own lines) | **Later** (native lines, v2) | DESIGN §13, O42 |
| Events & Reactions: the per-event table | **Adapt**: Behaviour › Reactions, with editable switch and cooldown cells | O33 |
| NPCs: actor filter, groups, identity links | **Not planned**; types and tags cover them | DESIGN §7.6 |
| NPCs: thoughts | **Later** (`npc_thought`) | DESIGN §13 |
| Diaries & Memories: memory engine settings | **Not applicable** in v1 (the journal is sent whole); later with recall | DESIGN §10.1 |
| Dialogue, memory and speech text filters | **Not planned** (EldenNet's sanitiser and lint are fixed; the safety level) | NPC §8 |
| Records | **Never** | Names come from the player's install at run time |
| Plugins (add-on sections) | **Not planned** in v1; Content packs lists content layers only | No add-on API (DESIGN §13) |
| Dashboard: web server | **Adapt**: Settings › Browser view (always on as SkyrimNet's; loopback with a session cookie; other devices only once switched on and paired) | G4, G9, D58–D60 |
| Developer: log level, traces | **Take**: Settings › Developer | — |
| Developer: tool server, agents, analytics | **Not planned** | SN01 §14.3 |

---

## 6. Lessons for GraceUI

1. **Categories are framework layers.** EldenNet's v1 has areas SkyrimNet has no page for: pacing and reactions per
   event type (O31, O33), combat and status lines (O40, O42), NPC types and allowances (O38), and the degradation
   ladder and compatibility (DESIGN §4.4, §3.4). Behaviour, Characters and System hold them without new categories.
2. **One home per setting.** SkyrimNet's Settings repeats the sidebar's split and gives some topics two homes (§3).
   GraceUI keeps a foot Settings page only for settings no category page can hold, with outward links and one
   index of every setting (D40).
3. **A feature's switch belongs on its page**, and the top bar's quick switches are shortcuts to those homes (D41,
   D45).
4. **State the default in every row and dim rather than hide** a row whose parent is off: GraceUI's "Off because …"
   badge already does the second.
5. **Related cards and deep links** make a deep tree navigable from a pad: GraceUI's search and shortcut rows join
   them.
6. **Privacy.** SkyrimNet shows account names, local addresses with ports and file paths in pages and logs [W3]
   [P060907] [P061219] [P061239]. GraceUI shows a provider's base URL on AI › Providers only and scrubs the rest (PV2,
   PV5).
7. **Spoilers.** Its game-data explorer, knowledge pages and event editors show raw game and story data. GraceUI
   shows projections only (SP1) and lists only what the Tarnished has reached (SP4, SP5).
8. **No online dependency in a page.** Its wiki, add-on index and model presets are fetched online. GraceUI bundles
   its documents and makes no request but the provider's (PV6).
9. **Pages must work without the game.** Most SkyrimNet pages are empty without its game-side server (§2.10).
   GraceUI's fake EldenNet serves fixture states for every page (DESIGN §9.2).
10. **The top bar's middle control is a theme picker** [P06123d9]; GraceUI adds the journey in view beside a theme
    switch (D41).
