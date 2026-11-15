# GraceUI

The main menu and dashboard of **EldenNet**, an LLM-driven NPC framework for Elden Ring, and the UI framework it draws
through. One set of pages, shown in game on a hotkey (F5 by default) and in the player's own web browser at an address
on their PC, which GraceUI serves while the game runs, unless anti-cheat or an unknown build keeps EldenNet inactive.
GraceUI holds the game's one overlay, so other Elden Ring mods can draw their own pages and panels through it, as
Magelight UI and PrismaUI let mods do for Skyrim; when it opens to other modders is still to be decided.

**Status: design.** Nothing is built yet. The design is in [docs/DESIGN.md](docs/DESIGN.md), with the owner's decisions
(indexed in §12, in full in [docs/DECISIONS.md](docs/DECISIONS.md)) and the decisions it settles (§13). The framework
and the map were put to the owner as proposals, which keep their reasoning ([docs/proposals/](docs/proposals/)). The
roadmap for both GraceUI and EldenNet is EldenNet's `docs/ROADMAP.md`.

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
| `crates/grace-protocol` | The bridge protocol's types (Rust; TypeScript generated from them) |
| `crates/grace-view` | The menu's structure, focus and input rules, without a renderer |
| `crates/grace-draw` | The in-game view's drawing: the menu as vertex lists, without a renderer |
| `crates/graceui` | `graceui.dll`, the host: the game's one overlay, the public C ABI, the menu and the server |
| `crates/grace-abi` | The public C ABI and its header, `graceui.h` |
| `crates/graceui-sys` | The helper a mod uses to find GraceUI and call it (Rust, with a header-only C version) |
| `crates/grace-serve` | The loopback server for the browser dashboard, inside `graceui.dll` |
| `crates/grace-host` | The overlay inside `graceui.dll`: hudhook, its hooks, the controls and the drawing of every mod's panels |
| `pages/` | The menu's categories and pages, as data |
| `tokens/grace.toml` | The Grace theme's colours, sizes and timings |
| `web/` | The browser app (TypeScript) |
| `tools/grace-dev` | A fake host for developing and testing any mod's pages without the game |
| `tools/grace-harness` | A Windows harness that runs the in-game view without the game |
| `examples/` | Example mods, with invented content |
| `fixtures/` | Test states with invented names |
| `mock/` | A static look mock of the menu |

## Licence

MIT; see [LICENSE](LICENSE). Fonts, when added, are under the SIL Open Font License and come from their publishers;
each ships with its licence and is credited in `NOTICE`. GraceUI contains no game files, text, fonts, icons or art,
and no code, styles, text or art from other UI projects. The rules are in [CLAUDE.md](CLAUDE.md).
