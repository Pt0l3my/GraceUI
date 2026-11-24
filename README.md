# GraceUI

The main menu and dashboard of **EldenNet**, an LLM-driven NPC framework for Elden Ring, and the UI framework it draws
through. One set of pages, shown in game on a hotkey (F5 by default) and in the player's own web browser at an address
on their PC, which GraceUI serves while the game runs, unless anti-cheat or an unknown build keeps EldenNet inactive.
GraceUI holds the game's one overlay, so other Elden Ring mods can draw their own pages and panels through it, as
Magelight UI and PrismaUI let mods do for Skyrim; when it opens to other modders is still to be decided.

**Status: design, and the first code.** The overlay host's render half and input half are built: `graceui.dll` starts on
a mod's first request, hooks the game's frames and draws Dear ImGui on them, filters the game's window procedure, reads
DirectInput and XInput, and keeps the game from the input of whoever holds it. Mods reach it through ABI 1, a preview
until GraceUI 1.1 (`src/grace-abi/include/graceui.h`): they find it with `graceui-sys`, connect, show notices in the
Grace look, register textures and faces before their first frames, and draw panels on five layers with checked draw
lists, built with GraceUI's shared atlas or a headless Dear ImGui of their own; a panel can take the controls, and the
player can always take them back; any mod can report danger, which ends the view that holds input. **GraceUI 0.2.0**
adds the pages: a mod registers its space (its page data and settings description) and speaks the bridge protocol
across the ABI; F5 opens GraceUI's menu in game, drawn by the host, on every mod's space and GraceUI's own; and the
browser view (a Preact app served by GraceUI's own server on loopback) shows the same pages from the same data, with
`grace-dev` to preview a space's pages without the game. **GraceUI 0.2.1** (ABI 1.1) gives a mod whose panel holds
the controls what the player does there, if it asks: keys, characters, the pointer, a pad's buttons and the end of its
hold, through `poll`, with the IME placed at its caret, so a mod's own Dear ImGui can take typing. **GraceUI 0.2.2**
(ABI 1.2) keeps GraceUI's settings between sessions, draws its own pointer, and tells every mod the display: its size
and Menu size at connect and in an event, with faces it bakes at the display's scale. **GraceUI 0.2.3** adds the Installed mods page: every mod
connected, and a switch for each that quiets its notices, which then wait in a list on the page rather than over the
game. **GraceUI 0.2.4** gives the in-game menu the mouse: hover, a click that does what Enter does, a slider
dragged, the wheel. **GraceUI 0.2.5** fixes what an independent security review found: an anti-cheat found after
start now closes the browser view for the session, and a destructive action always asks first. **GraceUI 0.2.6** draws
a mod's key-value lists, check lists, stat tiles and sentences in game, from what the mod publishes, and fills
GraceUI's own About and Browser view pages. **GraceUI 0.2.7** adds what proposal 0006 settled for mods' pages: one
topic feeding several blocks, a block's own words when it is empty, log tails newest first, pages a mod greys with its
reason, and a shortcut row's control in game; its browser view's rail and tables now match the game's (D191).
**GraceUI 0.2.8** adds what proposal 0007 settled (protocol 1.2): a button on each row of a table, a picker whose
choices a mod publishes, a key field on each row, and small forms. **GraceUI 0.2.9** lets a row's key read "Refused"
or "Not needed", and takes a row's changed mark from the mod. **GraceUI 0.2.10** shows a picker's empty default as
"Default"; **0.2.11** leaves out a row key's line in game when no row takes a key. **GraceUI 0.2.12** lets a page's
sections serve as cards, each with a chip, a live status and buttons of its own (protocol 1.3); **0.2.13**'s browser
view paces every frame it sends, so a page with many topics loses none. **GraceUI 0.2.14** gives a mod's hotkey a
pad chord (ABI 1.3): a button held, then another, as EldenNet's D-pad Up then View opens its chat box, with the game
losing no press of its own; checked with a simulated pad, the timings to be confirmed on testers' pads. **GraceUI
0.2.15** lists every mod's hotkeys on its Hotkeys page, with their keys, chords and conflicts, and never loses
a holder's end when its mod's events pile up. **GraceUI 0.2.16** lets the player change any mod's hotkey there, its key
or its pad chord, and the menu's own key, kept between sessions; a mod can read its hotkey's key to name it (ABI 1.4). **GraceUI
0.2.17** gives each mod a wait handle that GraceUI sets whenever something waits for its poll (ABI 1.5). **GraceUI 0.2.18**
brings GraceUI's own text field to the menu in game: text and secret settings, forms, row secrets and search are typed
there now, with the clipboard and the IME, rather than in the browser view alone. **GraceUI 0.2.19** gives a page a
list and its detail, in both views, the detail drawing a topic for the chosen row alone (protocol 1.5). **GraceUI 0.2.20**
lets a space name commands that may take up to 90 s, such as a model's draft (protocol 1.6). **GraceUI 0.2.21** adds an
editor to both views: a document a space publishes, edited field by field, its unsaved edits kept for the session, then
checked, saved, reset, restored or drafted by the space's own commands (protocol 1.7). **GraceUI 0.2.22** lets a save
choose the list's next row, as when a profile written for a character becomes the player's own. **GraceUI 0.2.23**
lets the menu's search in game find every mod's settings, as the browser view's already did. **GraceUI 0.2.24** records
how far its start-up got, so that a test run that stalls says where. **GraceUI 0.2.25** lets a phone or another
computer at home open the browser view once the player switches Other devices on and pairs it with a six-digit code
shown in game; each paired device is listed with Forget, and none may change the browser view, set a mod's secrets or do
anything else a mod marks sensitive (protocol 1.8). **GraceUI 0.2.26** lets a mod take a file from the browser view on
this PC: a drop zone on its page sends one file, at most 32 MiB, checked on arrival and handed to the mod, which GraceUI
keeps nothing of (protocol 1.9, ABI 1.6). **GraceUI 0.2.27** records, in its test build, how far its first frames got,
so that a game that stops there says where; **GraceUI 0.2.28**'s test observer records the faults a run meets and the
code that met them. **GraceUI 0.2.29** moves the menu's pointer by the mouse's own motion, so that a game that keeps its
cursor at the centre for its camera no longer pulls the pointer back. **GraceUI 0.2.30** keeps the menu open across an
Alt+Tab, letting go of the mouse and keys while the player is away and taking them again on return, and Tab no longer
moves between elements in game. **GraceUI 0.2.31**'s test build can serve the browser view on a free port of the system's
(`GRACEUI_TEST_PORT=free`), and grace-dev's dump draws a master list's chosen row. **GraceUI 0.2.32**'s test build
guards its own heap blocks and names the code that made a damaged one, for the crashes seen at a game's quit. **GraceUI 0.2.33** keeps the menu's pointer where the mouse took it, still or moving, and draws its focus ring only once a key or the pad is used. **GraceUI 0.2.34**'s test build reports the game window's focus and the presses that reach the game, for presses a player says did not register. **GraceUI 0.2.35**'s test build says where each hooked function leads, so that a test can show one hook holder in the process. **GraceUI 0.2.36** ships EB Garamond Italic in its `fonts\` folder, for mods to register as a face of their own. **GraceUI 0.2.37** gives the in-game menu a pad mode: its key hints follow the last device used and are drawn as the pad's buttons in our own glyphs, and Y searches, View hides the hints and the right stick scrolls. **GraceUI 0.2.38** (ABI 1.7) tells a mod whether a hotkey was pressed by its key or its pad chord, and its notice says so plainly when GraceUI's own copy is the idle one. **GraceUI 0.2.39** fixes three things in the menu that EldenNet's preview shots found: the links back to a mod's space follow its increment, the rail's focus ring shows whole, and a related link's ring no longer crowds its words. **GraceUI 0.2.40** draws the menu's keyboard hints as key caps. **GraceUI 0.2.41** draws the arrows in those caps, larger and centred. **GraceUI 0.2.42** (protocol 1.10) lets a mod pin categories to the rail's foot, and an empty block says "Nothing to show." without promising more. **GraceUI 0.2.43** (protocol 1.11) gives a mod's tables and master lists a filter bar: filters, a sort and a search, done in the menu itself. **GraceUI 0.2.44** (protocol 1.12) lets a mod's pages hold documents (headings, lists, notes, code and tables, with links between them), a form field of several lines, and a report's preview whose optional parts the player can leave out. **GraceUI 0.2.45** (protocol 1.13) gives the top bar a mod's quick switches, opened in game with the pad's Start, and live updates that a player can slow or pause so that a moving page can be read. **GraceUI 0.2.46** shows a mod's documents in game, and a report preview's longer fields whole. **GraceUI 0.2.47** (protocol 1.14) lets a mod's pages follow a scope the player chooses in the top bar, such as a playthrough, and lets its tables and lists read their rows a page at a time, with Show more. **GraceUI 0.2.48** fixes four things in that paging: a late answer drawn in game, a refused list that never asked again in the browser, a refused Show more that said nothing, and a count that was not whole. Hotkeys
and the log are declared for the increments
that build them. Before it hooks anything,
GraceUI checks for anti-cheat, and on a hit shows notices only; a second copy loaded from another folder idles and is
named in a notice. All of it is checked by a Direct3D 12 harness under Wine, with two example mods of our own, and the
rules by unit tests on Linux ([BUILD.md](BUILD.md)). The design is in [docs/DESIGN.md](docs/DESIGN.md),
with the owner's decisions (indexed in §12, in full in [docs/DECISIONS.md](docs/DECISIONS.md)) and the decisions it
settles (§13). The framework and the map were put to the owner as proposals, which keep their reasoning
([docs/proposals/](docs/proposals/)). The roadmap for both GraceUI and EldenNet is EldenNet's `docs/ROADMAP.md`.

## What it will be

- **A home page** that shows at a glance whether EldenNet is working, who is around, what was just said and what the
  model is doing.
- **The categories of an AI framework**, in a sidebar: AI (providers, models, the self-test, requests), Voice (text
  and subtitles now, speech later), Characters (your playthrough, the named NPCs, every kind of NPC and what it may
  say), History (events, conversations, memories, the world), Behaviour (conversation, speaking up, combat dialogue,
  awareness, reactions, pacing), System (status, performance, traces, logs) and Tools (an event monitor and a
  bug-report builder that leaves secrets out), with Settings, Docs and Content packs at the foot.
- **A map of your journey** (planned for 1.1): your own game's map, drawn from your install for the map pieces you
  hold and never shipped, with the people you met and where, the story steps you saw, how the world changed and notes
  of lore from your memories.
- **A framework for other mods**: a mod can add its own pages, drawn in the same style in game and in the browser,
  and its own panels in game, through one versioned interface; GraceUI keeps the overlay, the controls and the
  dashboard's security for all of them.
- **Elden Ring in spirit, our own in substance**: a dark "Grace" theme with one gold light, Elden Ring words inside
  the pages, and no game assets of any kind.
- **Spoiler-safe**: no page shows a stage, flag, name or item the player has not reached; the roster of every named
  NPC, every named boss and every NPC type is listed from the start, with only what a first meeting would show.
- **Safe in game**: it opens only at safe moments (the game never pauses), and danger closes it at once, keeping every
  change.
- **Private**: the browser dashboard listens on 127.0.0.1 only, so only this PC can open it, unless the player
  switches on their home network and pairs a device with a code shown in game; it refuses requests from other web
  pages; keys go in and never come out, and nothing personal reaches a log or a report.

## Installing and updating (planned)

GraceUI is a mod of its own, and each EldenNet release includes the GraceUI release current at its release date. Its
files live in one folder, `GraceUI`, in the game's `Game` folder (beside `eldenring.exe`). To update GraceUI on its own,
close the game and extract a newer GraceUI release into the `Game` folder so that its `GraceUI` folder replaces the old
one. EldenNet's launcher checks the folder before the game starts, installs the GraceUI that EldenNet carries where none
or an older one is installed, and explains anything it cannot put right. Other mods that use GraceUI need it installed
and never carry a copy of their own. While the game runs, open `http://127.0.0.1:8642` in a browser on the same PC to
see the dashboard.

## Repository layout (planned)

| Path | What |
|---|---|
| `docs/DESIGN.md` | The design |
| `docs/research/` | The studies behind it, in our own words |
| `docs/eldennet-changes.md` | What EldenNet's design needs to change for GraceUI |
| `docs/proposals/` | Proposals put to the owner: the framework (0001), the map (0002), a holder's events (0003), the display (0004), pad chords (0005), additions for mods' pages (0006), row actions, live pickers, row secrets and forms (0007), sections as cards (0008), and rebinding hotkeys (0009) |
| `src/grace-protocol` | The bridge protocol's types (C++; they and the web app's TypeScript types are generated from one schema) |
| `src/grace-input` | The input holder's rules: capture, masking, the player's exit and characters made whole, without Win32 |
| `src/grace-view` | The menu's structure, focus and input rules, without a renderer |
| `src/grace-draw` | The in-game view's drawing: the menu as vertex lists, without a renderer |
| `src/graceui` | `graceui.dll`, the host: the game's one overlay, the public C ABI, the menu and the server |
| `src/grace-abi` | The public C ABI and its header, `graceui.h`, with the first one kept to check it only grows; and its rules, pure: the mods connected, the notices, the checks on a mod's strings |
| `src/graceui-sys` | The helper a mod uses to find GraceUI and call it (a header-only C helper, with a thin C++ wrapper) |
| `src/grace-serve` | The loopback server for the browser dashboard, inside `graceui.dll`: our own small server (G30), also published as a library for hosts outside the game, EldenNet's launcher first (G29) |
| `src/grace-host` | The overlay inside `graceui.dll`: Dear ImGui's DX12 and Win32 backends with MinHook, its hooks, the controls and the drawing of every mod's panels |
| `pages/` | GraceUI's own space and the demo space, as data (EldenNet's pages live in EldenNet, G20) |
| `tokens/grace.toml` | The Grace theme's colours, sizes and timings |
| `web/` | The browser app (TypeScript) |
| `tools/grace-dev` | A fake host for developing and testing any mod's pages without the game |
| `tools/grace-harness` | A Windows harness that runs the in-game view without the game |
| `examples/` | Example mods, with invented content: `hello-notice`, in C, a notice; `hello-panel`, in C++, a panel drawn by its own headless Dear ImGui |
| `fixtures/` | Test states with invented names, for the demo space (EldenNet's are EldenNet's, G20) |
| `mock/` | A static look mock of the menu |

## Licence

MIT; see [LICENSE](LICENSE). Fonts are under the SIL Open Font License and come from their publishers; each ships
with its licence and is credited in `NOTICE` (Noto Sans and Noto Sans Mono so far, fetched by the build, never kept in
this repository). GraceUI contains no game files, text, fonts, icons or art,
and no code, styles, text or art from other UI projects. The rules are in [CLAUDE.md](CLAUDE.md).
