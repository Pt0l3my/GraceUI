# GraceUI

The main menu and dashboard of **EldenNet**, an LLM-driven NPC framework for Elden Ring. One set of pages, shown in game
on a hotkey (F5 by default) and in the player's own web browser at an address on their PC, which EldenNet serves while
the game runs, unless anti-cheat or an unknown build keeps EldenNet inactive.

**Status: design.** Nothing is built yet. The design is in [docs/DESIGN.md](docs/DESIGN.md), with the owner's
decisions (§12) and the decisions it settles (§13).

## What it will be

- **A home page** that shows at a glance whether EldenNet is working, who is around, what was just said and what the
  model is doing.
- **The categories of an AI framework**, in a sidebar: AI (providers, models, the self-test, requests), Voice (text
  and subtitles now, speech later), Characters (your playthrough, the named NPCs, every kind of NPC and what it may
  say), History (events, conversations, memories, the world), Behaviour (conversation, speaking up, combat dialogue,
  awareness, reactions, pacing), System (status, performance, traces, logs) and Tools (an event monitor and a
  bug-report builder that leaves secrets out), with Settings, Docs and Content packs at the foot.
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
files live in one folder, `GraceUI`, in the game's `Game` folder (beside `eldenring.exe`). To update GraceUI on its
own, close the game and extract a newer GraceUI release into the `Game` folder so that its `GraceUI` folder replaces
the old one. EldenNet checks the folder at every start; if GraceUI is missing or does not match, EldenNet keeps
working and says what to install. While the game runs, open `http://127.0.0.1:8642` in a browser on the same PC to see
the dashboard.

## Repository layout (planned)

| Path | What |
|---|---|
| `docs/DESIGN.md` | The design |
| `docs/research/` | The studies behind it, in our own words |
| `docs/eldennet-changes.md` | What EldenNet's design needs to change for GraceUI |
| `crates/grace-protocol` | The bridge protocol's types (Rust; TypeScript generated from them) |
| `crates/grace-view` | The menu's structure, focus and input rules, without a renderer |
| `crates/grace-draw` | The in-game view's drawing: the menu as vertex lists, without a renderer |
| `crates/graceui` | The in-game menu's DLL, `graceui.dll`, loaded by EldenNet |
| `crates/grace-abi` | The C ABI between `graceui.dll` and EldenNet |
| `crates/grace-serve` | The loopback server for the browser dashboard, run inside EldenNet |
| `crates/grace-host` | EldenNet's side of the ABI: loading `graceui.dll` and drawing its vertex lists in EldenNet's overlay |
| `pages/` | The menu's categories and pages, as data |
| `tokens/grace.toml` | The Grace theme's colours, sizes and timings |
| `web/` | The browser app (TypeScript) |
| `tools/grace-dev` | A fake EldenNet for developing and testing the UI |
| `fixtures/` | Test states with invented names |
| `mock/` | A static look mock of the menu |

## Licence

MIT; see [LICENSE](LICENSE). Fonts, when added, are under the SIL Open Font License and come from their publishers;
each ships with its licence and is credited in `NOTICE`. GraceUI contains no game files, text, fonts, icons or art,
and no code, styles, text or art from other UI projects. The rules are in [CLAUDE.md](CLAUDE.md).
