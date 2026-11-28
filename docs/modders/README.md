# GraceUI for mod authors

GraceUI is a UI framework for Elden Ring mods: one overlay in the game, a menu of pages in game on F5, and the same
pages in the player's browser. A mod gives GraceUI data (its panels' draw lists, its notices, its pages and their
topics and commands) and GraceUI draws it, in the Grace theme, in both views. EldenNet is its first user.

**Status.** GraceUI 0.2 is a preview. The framework opens to other modders with GraceUI 1.1, after EldenNet v1.0
(DESIGN G18): until then the C ABI (ABI 1) and the bridge protocol (protocol 1) are used by EldenNet and by GraceUI's
own example mods, and they grow by minors that only add. This guide is written alongside them; the parts marked
*to come* are not written yet.

## The parts

| Part | What it covers | State |
|---|---|---|
| [Features by version](features.md) | Every protocol minor and ABI minor, the GraceUI version that brought it, and the caps a mod asks for | Written |
| [The space and its pages](pages.md) | `space.toml`, the page data a space registers: categories, pages, tabs, sections, blocks, actions, flows | Written |
| [Components](components.md) | Every component, the topic body each draws, in game and in the browser | Written |
| The settings description | Every setting type, its bounds, availability and effect | *To come* |
| The protocol | Topics, commands, results and errors as the views and GraceUI send them | *To come* |
| The C ABI | `GraceUI_RequestApi`, `connect` and its answers, each slot | *To come* |
| Panels and your own Dear ImGui | Draw lists, their checks and budgets; Dear ImGui's core without a backend | *To come* |
| Input and focus | The input holder, hotkeys and pad chords, the closing press, Steam's floating keyboard | *To come* |
| Threads and budgets | Which calls go on which thread; the present thread's budget | *To come* |
| Secrets, privacy and spoilers | What a view may see; secrets write-only; projections only | *To come* |
| Versions and distribution | One copy in `Game\GraceUI\`; requiring a version; what a mod ships | *To come* |
| Troubleshooting and bug reports | GraceUI's log, its notices, what to put in a report | *To come* |

## Tools you can use now

- **grace-dev** (`tools/grace-dev/`): a fake host for any space. It serves a space's pages and fixtures to the browser
  view on loopback, plays a scenario of state changes and answers to commands, and dumps a page's in-game layout
  (`--dump`).
- **The page lint**: `python3 -I tools/pages/build.py --lint <space.toml>...` checks a space's sources against this
  GraceUI.
- **The space check**: `npm run -s check:space -- <space folder> [--increment X.Y]` in `web/` opens the real browser
  view over a space's files and prints what it shows, for a golden of your own.
- **The example mods** (`examples/`), each with invented content:
  - `hello-notice` (C): finds GraceUI, connects and shows one notice; with GraceUI absent or too old its UI is simply
    off.
  - `hello-panel` (C++): a panel drawn with Dear ImGui's core, headless, with no backend and no hook.
  - `hello-debug-view` (C++): a banner notice replaced in place, and a view toggled by a hotkey of GraceUI's, drawn by
    its own headless Dear ImGui.

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
