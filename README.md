# GraceUI

The main menu and dashboard of **EldenNet**, an LLM-driven NPC framework for Elden Ring. One set of pages, shown in
game on a hotkey (F5 by default) and, if the player turns it on, in their own web browser.

**Status: design.** Nothing is built yet. The design is in [docs/DESIGN.md](docs/DESIGN.md), with eight questions open
for the project owner (§12) and the decisions it settles (§13).

## What it will be

- **A home page** that shows at a glance whether EldenNet is working, who is around, what was just said and what the
  model is doing.
- **The categories of an AI framework**, in a sidebar: AI (providers, models, the self-test, requests), Voice (text
  and subtitles now, speech later), Characters (the named NPCs, every kind of NPC and what it may say), History
  (events, conversations, memories, the world), Behaviour (conversation, speaking up, combat dialogue, awareness,
  reactions, pacing), System (status, performance, traces, logs) and Tools (an event monitor and a bug-report builder
  that leaves secrets out), with Settings, Docs and Content packs at the foot.
- **Elden Ring in spirit, our own in substance**: a dark "Grace" theme with one gold light, Elden Ring words inside
  the pages, and no game assets of any kind.
- **Spoiler-safe**: no page shows a stage, flag, name or item the player has not reached.
- **Safe in game**: it opens only at safe moments (the game never pauses), and danger closes it at once, keeping every
  change.
- **Private**: the browser dashboard is off by default, listens on 127.0.0.1 only, needs a per-session link, and
  nothing personal reaches a log or a report.

## Repository layout (planned)

| Path | What |
|---|---|
| `docs/DESIGN.md` | The design |
| `docs/research/` | The studies behind it, in our own words |
| `docs/eldennet-changes.md` | What EldenNet's design needs to change for GraceUI |
| `crates/grace-protocol` | The bridge protocol's types (Rust; TypeScript generated from them) |
| `crates/grace-view` | The menu's structure, focus and input rules, without a renderer |
| `crates/grace-imgui` | The in-game view, drawn with Dear ImGui inside EldenNet's overlay |
| `crates/grace-serve` | The loopback server for the browser dashboard |
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
