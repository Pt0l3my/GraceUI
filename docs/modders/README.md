# GraceUI for mod authors

GraceUI is a UI framework for Elden Ring mods: one overlay in the game, a menu of pages in game on F5, and the same
pages in the player's browser. A mod gives GraceUI data (its panels' draw lists, its notices, its pages and their
topics and commands) and GraceUI draws it, in the Grace theme, in both views. EldenNet is its first user.

**Status.** GraceUI 0.2 is a preview. The framework opens to other modders with GraceUI 1.1, after EldenNet v1.0
(DESIGN G18): until then the C ABI (ABI 1) and the bridge protocol (protocol 1) are used by EldenNet and by GraceUI's
own example mods, and they grow by minors that only add. This guide is written alongside them.

## The parts

| Part | What it covers | State |
|---|---|---|
| [Features by version](features.md) | Every protocol minor and ABI minor, the GraceUI version that brought it, and the caps a mod asks for | Written |
| [The space and its pages](pages.md) | `space.toml`, the page data a space registers: categories, pages, tabs, sections, blocks, actions, flows | Written |
| [Components](components.md) | Every component, the topic body each draws, in game and in the browser | Written |
| [The settings description](settings.md) | Every setting type, its bounds, availability and effect; the settings topic and its commands | Written |
| [The protocol](protocol.md) | Topics, commands, results and errors as the views and GraceUI send them | Written |
| [The C ABI](abi.md) | `GraceUI_RequestApi`, `connect` and its answers, each slot | Written |
| [Panels and your own Dear ImGui](panels.md) | Draw lists, their checks and budgets; Dear ImGui's core without a backend | Written |
| [Input and focus](input.md) | The input holder, hotkeys and pad chords, the closing press, Steam's floating keyboard | Written |
| [Threads and budgets](threads.md) | Which calls go on which thread; the present thread's budget | Written |
| [Secrets, privacy and spoilers](privacy.md) | What a view may see; secrets write-only; projections only | Written |
| [Versions and distribution](versions.md) | One copy in `Game\GraceUI\`; requiring a version; what a mod ships | Written |
| [Troubleshooting and bug reports](troubleshooting.md) | GraceUI's log, its notices, what to put in a report | Written |

The code in this guide is the example mods' own. Each block is a run of lines from one of `examples/`, named in a
comment beside it in this guide's source, and a check keeps them the same (`tools/guide/snippets.py`, D284): what you
read here compiles in both Windows builds and runs in GraceUI's harness.

## Tools you can use now

- **grace-dev** (`tools/grace-dev/`): a fake host for any space. It serves a space's pages and fixtures to the browser
  view on loopback, plays a scenario of state changes and answers to commands, and dumps a page's in-game layout
  (`--dump`).
- **The page tool**: `python3 -I tools/pages/build.py --lint <space.toml>...` checks a space's sources against this
  GraceUI, and `--build <space.toml> --out <folder>` compiles one into the page data and settings description a mod
  registers, with a C header of both if asked ([Compiling a mod's space](pages.md#compiling-a-mods-space)).
- **The space check**: `npm run -s check:space -- <space folder> [--increment X.Y]` in `web/` opens the real browser
  view over a space's files and prints what it shows, for a golden of your own.
- **The example mods** (`examples/`), each with invented content:
  - `hello-notice` (C): finds GraceUI, connects and shows one notice; with GraceUI absent or too old its UI is simply
    off.
  - `hello-panel` (C++): a panel drawn with Dear ImGui's core, headless, with no backend and no hook.
  - `hello-debug-view` (C++): a banner notice replaced in place, and a view toggled by a hotkey of GraceUI's, drawn by
    its own headless Dear ImGui.
  - `hello-space` (C++): a space of its own, in game and in the browser: its page compiled from its `space.toml` at
    its build, its topics built only while watched, its settings and commands checked by its one entry point and
    answered, and its thread waiting on GraceUI's wake event.

## Rules every mod keeps

These are GraceUI's invariants as a mod meets them (DESIGN, "Invariants"):

- **One overlay.** A mod links no hook library and no Dear ImGui renderer backend, hooks none of the functions GraceUI
  hooks, and draws through GraceUI's C ABI alone.
- **Projections only, and absence is silent.** A view shows what the mod's space publishes and nothing else: no raw
  content, and no count, greyed row or hint of what it has not published.
- **One command path.** Every change goes through the space's own command entry point, which checks it; a view is
  untrusted input.
- **No HTML in game, and nothing from outside.** Pages are data drawn by GraceUI's components; no page requests
  anything outside its own origin.
- **Secrets are write-only.** A key or a password is never published, logged or echoed; a view sees whether one is set.
- **Budgets.** Nothing on the present thread waits or allocates per frame after warm-up, and textures are registered
  in the registration window.
