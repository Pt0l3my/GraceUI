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
hold, through `poll`, with the IME placed at its caret, so a mod's own Dear ImGui can take typing. Hotkeys and the log
are declared for the increments that build them. Before it hooks anything,
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
| `docs/proposals/` | Proposals put to the owner: the framework (0001) and the map (0002) |
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
