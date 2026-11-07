# GraceUI: design

**Status: proposal, 2026-11-07.** Nothing is built yet. The owner decided G1, G2 and G4–G9 (§12); two questions are
**Open** (G3 and G10), each with a recommendation; the decisions this design settles are in the log (§13, D1–D66).

GraceUI is EldenNet's main menu and dashboard: the place where a player sees whether EldenNet is working, who is
around and what was said, and where every EldenNet setting lives. EldenNet is an AI framework, so GraceUI is laid out
the way SkyrimNet's dashboard groups a framework's elements: a collapsible sidebar of categories (Home, AI, Voice,
Characters, History, Behaviour, System, Tools; then Settings, Docs and Content packs), a top bar, a Home grid of
cards and regular pages (§4, D38). Its appearance is its own, in the spirit of SeverActions' menu (the "Grace"
theme), and every page holds EldenNet's own features and settings, in Elden Ring words.

**How to read the references.** "§n" is a section of this document. EldenNet's documents keep their own codes:
"EldenNet DESIGN §n" (its master design), ARCH, NPC and MEM (its architecture, NPC and memory documents), its spikes
S0–S10, success criteria C1–C14 and owner decisions O1–O44. GraceUI's research notes are ML (Magelight UI), LOOK
(the look and the Grace theme), IA (information architecture), REN (renderers) and SND (SkyrimNet's dashboard), in
[research/](research/README.md).

---

## 0. At a glance

- **One menu, two views.** The same categories, pages, settings rows and bridge protocol are shown in game on **F5**
  (rebindable) and in the player's own **web browser** at `http://127.0.0.1:8642`, which EldenNet serves while the game
  runs, unless anti-cheat or an unknown build keeps EldenNet inactive (G4, §7.2 item 1). F10's settings panel folds into
  the menu. F11's debug view stays separate (D5, D6).
- **Framework categories** (D38–D48). A sidebar of categories with pages: Home; AI (providers, models, self-test,
  requests); Voice (text and subtitles now, speech in v1.5); Characters (playthrough, named NPCs, NPC types); History
  (events, conversations, memories, world); Behaviour (conversation, speaking up, combat, awareness, reactions, pacing);
  System (status, performance, traces, logs); Tools (event monitor, bug report, Workshop); at the foot Settings, Docs
  and Content packs. A top bar with search, a status lamp, live updates, the journey in view, the theme and quick
  switches. Every setting has one home; Settings holds only those no category page can hold (D40). Later-version
  features appear as statement pages without controls, tagged "WIP" (G7).
- **In game, safe moments only.** The game never pauses. The menu opens only when EldenNet's `safe_context` holds
  and no game menu is open, or at the title screen. Danger closes it at once and keeps every change (D7).
- **Renderer (G1, decided by the owner).** In game, the menu is drawn with **Dear ImGui and a native Grace theme** inside
  EldenNet's one existing hudhook overlay: no second Present hook and no new licence; under G10's recommendation (Open),
  GraceUI's own DLL draws through that overlay and hooks nothing. The browser gets the
  full web version (HTML, CSS, TypeScript). HTML in game (Ultralight in CPU mode) stays a later, owner-gated spike,
  because hudhook 0.9.3 cannot upload changing textures without leaking memory (REN §2.1).
- **A mod of its own (G2, decided by the owner).** GraceUI has its own releases and one folder, `Game\GraceUI\`,
  with a version file; each EldenNet release carries the GraceUI release current at its date, and a player can
  update GraceUI on its own by copying a newer folder over it. EldenNet checks what it finds at every start and keeps
  playing whatever it finds (§2.7, §2.9).
- **The overlay (G10, Open; recommended).** EldenNet keeps its one overlay and loads `graceui.dll`, which lays out and
  tessellates the menu and hooks nothing; EldenNet checks its vertex lists and copies them into ImGui's draw list.
- **Browser app (G3, Open; recommended).** Preact with TypeScript, signals for live data, built with Vite.
- **Browser dashboard (G4, decided by the owner).** **Always on**, as SkyrimNet's: the player opens the address and
  types nothing else. Loopback by default, with Host, Origin and Fetch Metadata checks and a session cookie (§7). Other
  devices on the home network only if the player switches that on, each after entering a pairing code shown in game
  (G9).
- **The roster and the playthrough (G5, G8, decided by the owner).** Every NPC type and every named NPC's general
  profile are listed from the start, with only what a first meeting shows; a new Characters › Playthrough page shows
  the people this playthrough has met, their status and their stories as seen.
- **Smaller answers.** The Steam Deck starts at a 1.35 text scale (G6); later-version entries carry a "WIP" tag (G7).
- **Spoiler-safe.** GraceUI receives only player-safe projections from EldenNet's core; no page shows a stage, flag,
  name or item the player has not reached, the roster's first-meeting profiles aside (G5, G8); absence is silent (§4.8).
- **Grace theme.** Ash-umber grounds, one gold light for "here, selected, focused, primary" and for the section labels
  a player scans for (D48), parchment ink at four levels with every readable text at 4.5:1 or better, OFL fonts from
  their publishers, flat and still (§3).
- **Minimal and stable.** No game-data explorer, no content editors, no sound, no remote access and no host API for
  other mods in v1. The menu writes settings through EldenNet; EldenNet stays read-only towards the game.

## 1. Purpose and scope

### 1.1 What GraceUI is

- **EldenNet's main UI**, by framework category (§4): Home (status, setup, nearby characters, what is happening, lines
  and model calls); AI (providers, models, the self-test, requests); Voice (text and subtitles, and speech from v1.5);
  Characters (this playthrough, the named NPCs, NPC types and their allowances); History (events, conversations,
  memories, the world); Behaviour (conversation, speaking up, combat dialogue, awareness, reactions, pacing); System
  (status, performance, traces, logs); Tools (an event monitor, a bug-report builder, the developer Workshop);
  Settings, Docs and Content packs.
- **Owned by EldenNet's needs.** The protocol carries EldenNet's views and commands. GraceUI is EldenNet's menu,
  released as a mod of its own (G2); it is not a UI host for other mods in v1 (G10), and it holds no game knowledge
  of its own: every name, place and fact it shows comes from EldenNet at run time.
- **A separate repository and a mod of its own** (D1, G2). EldenNet links GraceUI's host kit (`grace-abi`,
  `grace-host`, `grace-protocol`, `grace-serve`) and carries GraceUI's release folder; GraceUI depends on nothing of
  EldenNet's.

### 1.2 In scope for v1

- The menu in game (from EldenNet v0.6) and the browser dashboard (from EldenNet v0.2, always on).
- GraceUI as a mod of its own: its folder, version file, handshake and the handling of a missing, older, newer or
  mixed copy (§2.7, §2.9).
- Every EldenNet setting, with one home page each, and every player-facing view §4 lists (§4.5, §4.10).
- First light, the setup wizard (D16).
- The Grace theme in both views, two variants (Grace and Grace Clear), scale 75–150%, HDR through EldenNet's overlay.
- Mouse, keyboard and pad in game; the Steam on-screen keyboard for text on a pad; keyboard and mouse in the browser.
- A desktop harness that runs the web app against a fake EldenNet, and a Windows harness for the in-game view (§9).

### 1.3 Not in v1

- A game-data explorer, content editors (personas, cards, knowledge, triggers, prompts), agents or a content hub (D2).
- Pages for SkyrimNet features EldenNet does not have: vision, songs, voice cloning and effects, translation,
  packages, the tool server, GPU rental (§4.11). Later EldenNet features (a director, acts, text-to-speech and
  speech-to-text before v1.5) are statement pages with no controls (G7).
- Sound of any kind (D27). Voice previews arrive with EldenNet's TTS in v1.5, through EldenNet's audio.
- Remote access, TLS, accounts, analytics, any request to the internet from a page; other devices on the home network
  unless the player switches that on and pairs each one (G9).
- A host API for other Elden Ring mods (G2, G10).
- HTML rendered in game (G1).
- EldenNet's other surfaces: the lower-third subtitles, chat box, call-out box, labels and feed, toasts, banner,
  pause badge, rollback modal and F11 debug view stay in EldenNet's overlay (EldenNet DESIGN §12). They may adopt
  Grace's tokens; that is EldenNet's choice.

### 1.4 Success criteria (checked from the increment each subject ships in)

| # | Criterion | Measure |
|---|---|---|
| U1 | No spoiler through the UI | Every page's goldens over EldenNet's fixture states show nothing outside the projections it was sent; counts, search and titles carry nothing veiled or absent (§9) |
| U2 | No input leak | EldenNet's input-leak tests, extended to F5, Esc, B and every exit path, pass on Windows and Proton |
| U3 | Every exit path closes cleanly | The close matrix (§5.1) passes in game: capture released, the closing press masked, changes kept |
| U4 | Frame cost | Menu open: ≤ 0.8 ms p99 on the present thread at 1440p, ≤ 1.5 ms on the Steam Deck. Menu closed: no cost beyond EldenNet's C3 (§8) |
| U5 | Local server holds | The server security suite (§7.2) passes with zero failures |
| U6 | The two views agree | Each page's structure (rows, labels, values, order) matches between views (§9.3) |
| U7 | Legible | Every readable text 4.5:1 or better on its ground; control edges 3:1 or better; checked from the token file in CI |
| U8 | Nothing personal leaves | No key, path, user name, SteamID, character name, local address or chat text in any GraceUI log, URL, title or report (PV1–PV8) |
| U9 | A mismatched GraceUI never stops EldenNet | The ABI and handshake tests (§9.1) pass, and in game a GraceUI patch dropped over the bundled copy, an older copy and a half-copied folder each give §2.9's behaviour |

---

## 2. Architecture

### 2.1 Shape

```
eldennet.dll (one process, one hudhook overlay)          Game\GraceUI\ (GraceUI's own folder, G2)

  core (T6) ── builds ──► projections per topic (player-safe; SP1–SP9),
     ▲                    encoded once per revision as bridge frames, only while watched
     │                         │                                      │
     │                  ingest on T6 (C ABI)                          ▼
     │                         ▼                           grace-serve (Net runtime T7–T8)
     │            graceui.dll: grace-view + grace-draw     HTTP + WebSocket on 127.0.0.1:8642,
     │            lays out and tessellates the menu         always on (G4); serves GraceUI\web\
     │                         │ vertex lists (T3)                    │  ▲
     │                         ▼                                      │  │ JSON over WebSocket
     │            grace-host (in en-overlay) checks them              ▼
     │            and copies them into hudhook::imgui's     the player's browser: the web app
     │            draw list                                 (Preact + TypeScript, from GraceUI\web\)
     │
     └──── commands (one validating entry point) ◄──── both views
```

- **EldenNet's core owns the truth.** It builds every view the UI shows as a player-safe projection (§4.8), applies
  every command after validating it, and writes `settings.toml`. GraceUI never reads EldenNet's files, the game or
  the stores directly.
- **Two views of one menu.** The in-game view (`graceui.dll`) and the web app interpret the same page descriptions
  (§2.5) with the same component vocabulary, the same tokens (§3.15) and the same protocol frames (§6).
- **Two deliverables.** GraceUI's folder (the DLL, the web app, the fonts) is GraceUI's release and can be replaced by
  hand (§2.7); GraceUI's host kit is compiled into EldenNet. While G10 is Open the overlay, its hooks and EldenNet's own
  surfaces stay in EldenNet, and `graceui.dll` hooks nothing (§2.9).
- **One command path.** Both views send the same commands into one validating entry point in the core. A browser tab
  is treated exactly like the in-game menu, and both like untrusted input (§7.3).

### 2.2 Parts

| Part | Kind | Holds | Runs where |
|---|---|---|---|
| `grace-protocol` | Rust crate (`serde` only; no I/O, threads or clocks) | Topic payloads, commands, results, error codes, version constants. The TypeScript types are generated from it, and CI fails on drift | EldenNet's core and server; `graceui.dll` |
| `grace-abi` | Rust crate (no dependencies) | The C ABI between `graceui.dll` and EldenNet: `repr(C)` structs, the two append-only function tables, constants; a C header generated for review; CI fails if a table shrinks or reorders against the last release (§2.9) | Both sides |
| `grace-pages` | Data (TOML) | The information architecture as data: the sidebar's categories and pages, the top bar's controls, Home's cards, each page's id, name, one-line description, help text and Related links, its tabs and sections, rows by setting key, component blocks bound to topics, and the increment it ships in (or its later-version tag, shown as "WIP") | Compiled into `graceui.dll` and the web app |
| `grace-view` | Rust crate (pure) | Page descriptions loaded into a widget tree; the focus and navigation model; the pad ledger; hotkey rules; search; text layout; GraceUI's own text field (D57). No ImGui, no Win32, so it is tested on Linux | `graceui.dll` |
| `grace-draw` | Rust crate (pure) | Tessellates `grace-view`'s tree with the Grace tokens into vertex and index lists: rectangles with per-corner rounding, hairlines snapped to device pixels, quads, circles, paths, text runs from the atlas's glyph tables, the shadow nine-slice and the grain tile (§3.16). No ImGui, no Win32 | `graceui.dll`; runs on T3 |
| `graceui` | Rust `cdylib`: `graceui.dll` | The one export and the entries of §2.9 (the only `unsafe`, in `graceui::abi`); parsing frames on T6; drawing on T3. No hook, ImGui, MinHook, thread, socket or file | Loaded by EldenNet from `Game\GraceUI\` |
| `grace-host` | Rust crate (Windows) | EldenNet's side of the ABI: finds and checks `Game\GraceUI\`, loads `graceui.dll`, the handshake, bakes its faces into the one atlas and uploads its two textures once at load, passes input, checks and copies vertex lists into ImGui's draw list through `hudhook::imgui` only, hands the IME caret, capture and the DLL's requests (clipboard, opening the dashboard's address) to `en-overlay`, and turns the menu off on a fault; its `unsafe` only in `grace_host::{abi, draw}` (EldenNet R9); the Win32 calls behind what it hands over (placing the IME window, confining the cursor, the clipboard, opening the dashboard's address) are EldenNet's own, in R9's existing modules (`en-overlay::hooks`, `en-gal::sys`; eldennet-changes §3 item 4, §4 items 3, 5 and 7, §8 item 4), since GraceUI never depends on EldenNet | Linked into EldenNet's overlay; runs on T1, T3 and T6 |
| `grace-serve` | Rust crate | The loopback HTTP and WebSocket server with every rule of §7.2, serving the web files `graceui.toml` lists, ticketing and rate limits | Linked into `eldennet.dll`; tasks on EldenNet's Net runtime (T7–T8) |
| `tokens/grace.toml` | Data | Every colour, size, space, radius and duration, by role. A build step writes CSS custom properties for the web app and Rust constants for `grace-draw` | Build time |
| `web/` | TypeScript app | The browser view: shell, components, pages, the protocol client, a development-only injection panel | Built into `GraceUI\web\`; the player's browser |
| `tools/grace-dev` | Rust binary | A fake EldenNet: the real `grace-serve` and `grace-protocol` over fixture states, accepting commands, and a host for `graceui.dll`'s entries on Linux (§9.2). It never touches a game | Developer machines and CI |
| `fixtures/` | Data | Scenario states with invented names and our own words, mirroring EldenNet's fixture states | Tests and the harness |
| `mock/` | Static HTML and CSS | A look mock of the frame and a few pages, placeholder data only | A browser, from disk |

GraceUI's Rust crates follow EldenNet's dependency discipline (ARCH §2.3): `grace-protocol`, `grace-abi`, `grace-view`
and `grace-draw` take no I/O, Win32, clock or game knowledge. Only `grace-host` takes `hudhook = { version = "=0.9.3",
default-features = false, features = ["dx12"] }`, and it reaches `imgui` only through `hudhook::imgui`, exactly as
EldenNet's `en-overlay` does, so Cargo resolves one hudhook and one MinHook for the process. `graceui.dll` links
neither: a CI check reads its import table and symbols against a deny list and a reviewed allowlist. Imports come from
system DLLs only; no import from `ws2_32.dll` or any other socket API; no `CreateThread`, `_beginthreadex` or
`CreateRemoteThread`; no `CreateFileW` or `CreateFileA`; no `LoadLibrary*` or `GetProcAddress` beyond the Rust
runtime's own use (std's dbghelp loader), and no `WriteFile` or `GetStdHandle` beyond std's panic-output path, each
named with its reason in the check's allowlist; no hudhook, ImGui or `MH_` symbol. `init` installs a panic hook that
sends one short line through `GraceHost.log` and writes nothing to stderr. No GraceUI crate depends on an EldenNet
crate.

### 2.3 Threads (no new thread)

EldenNet's thread inventory (ARCH §1.2) stays as it is: "No other thread is created" still holds.

| EldenNet thread | GraceUI work |
|---|---|
| T1 Init | `grace-host` finds, checks and loads `graceui.dll`, and the handshake, after the settings step and before the overlay's hooks are installed (§2.9) |
| T3 Present (hudhook render callback) | Calls `graceui.dll`'s `frame` after EldenNet's world surfaces and before its toasts and banner, checks the vertex list and copies it into ImGui's draw list. GraceUI reads its newest snapshot through `arc-swap`, never waits, never allocates per frame after warm-up, and sends commands through the host's non-blocking `send` |
| T4 Window procedure | EldenNet's existing filter: characters, IME messages, focus loss. GraceUI adds nothing here; it receives that input in `frame` on T3 |
| T6 Core | Builds projections for watched topics at most 4 times a second each, and only while the menu is open or a browser is connected; encodes each revision once and gives it to `graceui.dll` (`ingest`) and to the server; drains the command ring; validates and applies commands; writes settings (debounced 300 ms) |
| T7–T8 Net (tokio) | `grace-serve`'s listener, connections and pushes, from the end of init's gate and safety steps to exit while `dashboard.enabled` is on (G4; never after a gate or safety failure or a later anti-cheat hit, §7.2 item 1). Off means no socket and no task |
| T10 Watcher | Unchanged: hot reload sees EldenNet's own settings writes and applies them as any edit (ARCH §11.6) |

A panic inside `graceui.dll` is caught at its entry and returned as an error code; `grace-host` then turns the menu
off for the session, releases capture, shows one toast and never calls it again, and EldenNet's other surfaces keep
drawing. A fault in a server task closes that connection; a second fault stops the server for the session with one
toast.

### 2.4 Data flow

1. **Watching.** A view subscribes to the topics its page needs (§6.5). The in-game view subscribes when the menu
   opens and drops them when it closes; the browser subscribes per page. With nothing watched, the core builds
   nothing.
2. **Projections.** The core builds a topic's payload from its own state, applies the spoiler and privacy rules
   (§4.8, §4.9), stamps a revision number, encodes it once as a frame (§6.3) and publishes it. State topics replace
   the whole payload; stream topics append rows to a bounded ring with sequence numbers.
3. **Showing.** `graceui.dll` parses a new revision on T6 and draws its newest snapshot each frame. `grace-serve`
   sends a state topic when its revision changes (coalesced to 4 Hz at most) and stream rows in batches.
4. **Changing.** A control sends a command. The core validates it against the settings description (§6.9), applies
   it, writes the file after 300 ms of quiet, and publishes the new revision to every view. There is no Save button,
   and the last write wins if both views change one setting (D14).

### 2.5 Pages as data

- **Page descriptions.** `grace-pages` holds the IA of §4 as data: the sidebar tree, the top bar, Home's cards and
  every page. A page has an id (`behaviour.pacing`), a category, a name, a one-line description, help text (what it is
  for, how to use it, Related links), the increment it ships in or a later-version tag, header actions, tabs, and
  sections of blocks. A block is either a list of setting keys (their rows come from EldenNet's settings
  description) or a component bound to a topic.
- **Components** (the shared vocabulary): settings rows (switch, slider, segmented choice, tri-state, duration, key
  capture, text, masked secret, model picker); shortcut row; stat tiles; filter bar; table with sort, selection,
  per-row actions and a row drawer; editable table cells; master–detail; card grid with Customise; collapsible
  section with an Advanced disclosure; segmented switch; Related card; help dialog; statement page; journal;
  conversation transcript; live feed; check list; log tail; report preview; computed sentence; action button with an
  optional confirm modal; wizard steps; empty and stale states.
- **Rule.** A page that needs something no component offers adds the component to both views, with its structural
  test (§9.3). There are no hand-written pages in one view only, except Workshop, which is browser-only (D10).
- **Why data.** Most of the menu is settings, which already come from EldenNet's description (D8). Describing the
  rest as data too keeps two views of the same menu from drifting (U6), and keeps the IA reviewable in one place.

### 2.6 The in-game renderer (G1)

Decided (G1, 2026-11-07): Dear ImGui with the Grace theme (§3.16), inside EldenNet's one hudhook 0.9.3 overlay. The
reasons, the options and what the owner gives up are in G1 (§12). In short:

- hudhook 0.9.3 gives a renderer ImGui draw lists and textures. Its texture replacement copies the whole image,
  leaks a new upload buffer on every call and waits for the GPU on the present thread, and it exposes no device,
  queue or command list (REN §2.1, §2.2). Anything that produces pixels (Ultralight's CPU mode, CEF, Servo) or GPU
  work (an Ultralight D3D12 driver) therefore needs a change to hudhook, which EldenNet DESIGN O25 reserves to the
  owner.
- ImGui needs no texture updates after load, adds no licence or binary, and rides the Proton path EldenNet must make
  work anyway (REN §4.1).
- The Grace theme was designed flat and still (hairlines, flat grounds, fades only), so nearly all of it can be drawn
  with ImGui primitives. What is lost is typographic detail (kerning, OpenType features) and authoring speed.
- Since G2, the menu's code is GraceUI's own DLL. It still draws only through this one overlay, as vertex lists
  EldenNet copies into ImGui's draw list, and hooks nothing (§2.9, G10).

### 2.7 Packaging and updates (G2)

GraceUI is a mod of its own (G2, decided by the owner): its own releases, one folder in the game's `Game` folder, a
version file, and updates by copying a newer folder over the old one (D51–D54). It is still EldenNet's menu: without
EldenNet it does nothing.

```
ELDEN RING\Game\GraceUI\        beside eldenring.exe
  graceui.dll     the in-game menu: pages, layout, focus and input rules, drawing as vertex lists (§2.9)
  graceui.toml    the version file
  web\            the browser app: index.html and content-hashed scripts, styles and WOFF2 fonts
  fonts\          the OFL faces the in-game menu bakes (static TTF), each with its OFL.txt
  LICENSE.txt  NOTICE.txt  README.txt
```

- **The version file**, `graceui.toml`, written by GraceUI's release job (example values):

  ```toml
  graceui = "0.6.2"          # GraceUI's version; minors follow EldenNet's increments (D37)
  abi = [1, 0]               # the C ABI's major and minor (§2.9)
  protocol = [1, 4]          # the bridge protocol's MAJOR.MINOR (§6.4)
  caps = ["monitor.decisions", "playthrough"]
  released = "2027-03-01"
  [files]                    # every file of the folder, with its size and SHA-256
  "graceui.dll" = { size = 1843200, sha256 = "…" }
  "web/index.html" = { size = 2311, sha256 = "…" }
  ```

  EldenNet loads only what it lists, and the server serves only the web files it lists (D52).
- **Compiled in.** Page descriptions and tokens are compiled into `graceui.dll` and into the web app; the only loose
  parts are the DLL, the web app and the fonts.
- **Updating by hand.** Close the game; extract GraceUI's release zip, which holds the one `GraceUI` folder, into
  `ELDEN RING\Game\` so that it replaces the old folder. Windows locks a loaded DLL, so an update with the game
  running fails; the web files are read once at start, so a change applies at the next start. Files a newer release
  no longer lists are ignored and never served; `README.txt` says they may be deleted.
- **What EldenNet's release carries** (D53): its own files and the `GraceUI` folder of the GraceUI release current at
  its release date, byte for byte. EldenNet's release job checks the folder against GraceUI's published
  `graceui.toml` and runs the handshake against it; EldenNet's release notes name the GraceUI version, and a build
  constant records it. EldenNet links GraceUI's host kit from the same tag (`grace-abi`, `grace-host` and
  `grace-protocol` in its overlay and core, `grace-serve` in its DLL), pinned by git tag and commit in `Cargo.lock`;
  `cargo deny` allows that one git source.
- **Before the in-game menu ships** (EldenNet v0.2 to v0.5) the folder holds `graceui.toml`, `web\` and the licence
  files; `graceui.dll` and `fonts\` join in v0.6.
- **The web app** is built by GraceUI's CI and committed at each release tag under `web/dist/`, with a check that
  rebuilding gives the same bytes; the release copies it into the folder's `web\`. EldenNet's build needs no Node
  toolchain (D32).
- **Versions.** GraceUI's minor versions follow EldenNet's increments (0.6 for EldenNet v0.6), with patch numbers of its
  own, so GraceUI 0.6.x is for EldenNet 0.6 and a patch can be dropped in on its own. The ABI and the protocol have
  their own numbers (§2.9, §6.4) (D37).
- **Integrity, not signatures.** The sizes and hashes catch a corrupt or half-copied folder. They are not a signature:
  whoever can write the game folder can replace `eldennet.dll` too.
- **Loaded only by EldenNet.** `graceui.dll`'s `DllMain` does nothing, and only EldenNet calls its one export, so a
  loader that lists it as a native of its own starts nothing.

### 2.8 Failure contract (what the player sees)

| Failure | Behaviour |
|---|---|
| The menu panics, returns an error code or sends a vertex list that fails its check, or the overlay trips | Menu off for the session; capture released; toast "The menu is unavailable this session. Settings still work in settings.toml."; EldenNet keeps running |
| GraceUI missing, half-copied, or made for another EldenNet | The rows of §2.9's table; EldenNet keeps playing |
| No overlay render for 2 s while the menu holds input | Capture released (EldenNet DESIGN §8.1); the menu closes |
| The dashboard port is taken | Server off for the session; one toast and one log line naming the port and where to change it (Settings › Browser view, or `dashboard.port`); never another interface or port |
| A browser tab from an earlier game start | The session cookie no longer matches: the page reloads itself once and gets a new one; nothing to copy |
| Protocol major mismatch (a stale cached page) | "This page needs updating: reload it" (§6.4); never a crash |
| A topic's projection fails to build | That block shows "Unavailable" and System › Status counts it; the rest of the page works |
| Lost contact (browser) | The last snapshot stays, dimmed and read-only, with "Reconnecting" and Retry (§3.9) |
| A command refused | The control returns to its previous value and shows the reason in words (§6.7) |

### 2.9 GraceUI's DLL and EldenNet (G2; G10 Open, following (b))

Exactly one module owns hudhook, the Present hook and MinHook (EldenNet O25). While G10 is Open the design follows its
recommendation: EldenNet keeps the overlay, its own surfaces and its hooks, and loads `graceui.dll`, which hooks
nothing (D56).

| Side | Holds | Never |
|---|---|---|
| `graceui.dll` (GraceUI's release) | `grace-protocol`, `grace-pages`, `grace-view` (page model, focus, pad ledger, search, layout, the text field, D57), `grace-draw` (tessellation with the Grace tokens) and `graceui::abi` (the exports, the only `unsafe`) | hudhook, ImGui, MinHook, hooks, threads, sockets, files, clocks, game memory |
| EldenNet, through GraceUI's host kit | `grace-host` in `en-overlay` (find, check, load and drive the DLL; bake its faces into the one atlas and upload its two textures at load; pass input; check vertex lists and copy them into ImGui's draw list; hand the IME caret, capture and the DLL's requests (clipboard, opening the dashboard's address) to `en-overlay`'s own IME, capture, clipboard and open-address code), `grace-serve` (§7.2), the core's projections and its one command entry point | Drawing a GraceUI page itself; trusting anything the DLL returns unchecked |

**The C ABI** (`grace-abi`, `repr(C)` only):

- `graceui.dll` exports one function, `graceui_entry(abi_major: u32) -> *const GraceUiApi`, which returns null for a
  major it does not speak.
- `GraceUiApi` is an append-only table that starts with its size and its ABI minor: `init` (T1), `fonts` (T3, at
  warm-up and after an atlas rebuild), `ingest` (T6), `open` and `close` (T3, with the reason: F5, Esc or B, danger,
  loading, focus loss, fault) and `frame` (T3). A minor adds entries at the end; EldenNet calls an entry only if the
  table's size covers it.
- `GraceHost`, EldenNet's table handed to `init`, grows the same way: EldenNet's version, its protocol version and
  caps, `send` (one outbound frame, copied into a bounded ring of 32 frames of at most 16 KiB; it never blocks and
  answers "full") and `log` (a line for EldenNet's scrubbed logger; GraceUI writes no log of its own).
- `frame` takes the input since the last frame (key names, characters already paired by EldenNet's `WM_CHAR`
  handling, mouse, XInput state, display size, effective scale, time) and fills an output: vertices in ImGui's
  `ImDrawVert` layout (position f32×2, texture coordinate f32×2, colour as four bytes R, G, B, A), u16 indices
  relative to each draw command's own vertex range, and draw commands (clip rectangle, texture, vertex range of at
  most 65,536 vertices, index range), whether it wants the keyboard and mouse, the cursor shape, the caret rectangle
  for the IME, keys and buttons to mask until release, and requests (close; put this text on the clipboard after a
  click, PV7; open the dashboard's own address). EldenNet never opens an address GraceUI names.
- Buffers are borrowed: a pointer GraceUI returns stays valid until its next call of the same entry; nothing is freed
  across the boundary; strings are UTF-8 with a length.
- Every entry catches panics and returns an error code, with `panic = "unwind"`. In `graceui.dll`, `unsafe` lives
  only in `graceui::abi`, and on EldenNet's side only in `grace_host::{abi, draw}`, each block with a `// SAFETY:`
  note; both join R9's in-process list (eldennet-changes §8 item 4).
- `grace-host`'s check of each frame (the mismatch table below) also confirms that each command's indices fall within
  its own vertex range, and it copies each command with its vertex offset: hudhook 0.9.3's dx12 renderer sets
  `RENDERER_HAS_VTX_OFFSET` and honours per-command vertex offsets, so a page of more than 65,536 vertices is split
  across commands, never across draw lists (S4 confirms it in game).
- A C header generated from `grace-abi` is kept for review; CI fails if a table shrinks or reorders against the last
  release's header.

**Threads.** `init` runs on T1, `ingest` on T6, the rest on T3. Inside the DLL the T6 side publishes parsed snapshots
with `arc-swap` and the T3 side reads the newest, so T3 never waits; after warm-up neither side allocates per frame
(invariant 10). No thread is created (§2.3). T3 never frees a snapshot: the T6 side keeps each superseded one in a
retire slot and frees it on a later `ingest`, once T3 has marked a newer revision as loaded. The allocation test
counts frees on T3 as well as allocations.

**The handshake**, at every start:

1. On T1, after the settings step and before T1 installs the overlay's hooks (EldenNet ARCH §1.4), `grace-host`
   finds `GraceUI\` beside `eldenring.exe`, reads `graceui.toml` (64 KB at most) and checks the size and SHA-256 of
   `graceui.dll` and of each face in `fonts\`. If the handshake has not finished when hudhook initialises, the menu
   stays off for the session rather than rebuilding the atlas.
2. It loads `graceui.dll` by its full path, with only the DLL-folder and System32 search flags, and calls
   `graceui_entry(1)`.
3. `init` returns GraceUI's version, which must equal `graceui.toml`'s; its protocol version, whose major must equal
   EldenNet's, and its caps; its faces (file, sizes, glyph ranges; at most ten, D29); and its two textures (the shadow
   nine-slice and the grain tile).
4. When hudhook's render loop starts, `grace-host` bakes those faces into the one atlas with EldenNet's own, uploads
   the two textures once, and hands GraceUI the glyph tables (`fonts`).
5. GraceUI's first frame through `send` is `hello`; the core answers `welcome` through `ingest`, as for a browser
   (§6.3), without a ticket: the channel is the identity (§7.3).

`graceui.dll` is never unloaded (EldenNet R14).

**Mismatches** (D54). In every row EldenNet keeps playing: its subtitles, chat box, call-outs, labels, toasts, banner
and F11 are its own.

| Found | In game | In the browser |
|---|---|---|
| No `GraceUI` folder | No menu. F5: "The menu isn't installed: GraceUI is missing. EldenNet still works, and its settings are in settings.toml." | A plain notice page built into EldenNet, saying the same |
| `graceui.toml` unreadable, or a file's size or hash differs from it | Menu off: "GraceUI's files don't match each other: copy the whole GraceUI folder again." | The same notice if a web file differs |
| No `graceui_entry`, null for ABI major 1, or a protocol major other than EldenNet's | Menu off: "This GraceUI (0.x) is not for this EldenNet (0.y): install the GraceUI that came with EldenNet, or a newer one made for it." | `bye` with `version` and the same words (§6.4) |
| A lower protocol minor (an older GraceUI) | Works. Settings whose home page it lacks appear under Settings › All settings as "Newer settings", so none is out of reach. System › Status › Compatibility: "GraceUI 0.6.1 is older than the one this EldenNet came with (0.6.3)." | The same |
| A higher protocol minor (a newer GraceUI) | Works with the caps EldenNet announces; a page that needs another is absent, as before its increment (§4.2.2) | The same |
| A caught panic, an error code, or a vertex list that fails `grace-host`'s check (counts, each command's indices within its own vertex range, clip rectangles within the display, known texture ids) | Menu off for the session, capture released, one toast (§2.8), logged once | Unaffected |

F5 while the menu is off shows its notice as a toast; F11 and System › Status › Compatibility (in the browser) show the
installed and expected versions and which file failed its check, by relative name, never a path (PV5).

**The bridge across it** (D55). The in-game view speaks the bridge protocol of §6 as a browser tab does, with the C ABI
as its transport: the same `{"v": 1, "t": …}` frames in UTF-8 JSON, the same handshake and versioning (§6.4) and the
same limits (§6.8). The core encodes each watched topic's revision once and gives the same bytes to the browser and to
the DLL, which parses them on T6. Display strings still come ready from the core (§6.1).

---

## 3. The Grace theme

The theme is LOOK §3, adopted with the settlements recorded in §13. LOOK §1 describes the menu that inspired it in our
own words; nothing of that menu's CSS, markup, images, sigils, texts or theme names is used (CLAUDE.md).

### 3.1 Principles

1. **One light.** Gold marks where you are, what is selected, what has focus and what is primary, and names the
   parts of a page a player scans for: section labels, Home's card titles and stat values (D48). Never decoration.
2. **Ash before umber.** Grounds are low-saturation warm greys, darker and greyer than the menu that inspired them: a
   chart table at a site of grace, lit by one warm light.
3. **Legible first.** Every readable text 4.5:1 or better; control edges 3:1 or better; type larger than a desk-only
   menu, for a TV and a 7-inch Deck.
4. **Flat and still.** Hairlines, flat layers, fades only. No blur, no loops, no parallax.
5. **Tokens, then themes.** Every colour, size, space, radius and duration is a token; variants are token swaps.
6. **Absence is silent.** What the player has not reached is not shown, counted or hinted at (SP5). The only veiled
   look is for a perceived character whose name the game has not shown.

### 3.2 Palette: Grace (dark, the default)

| Token | Value | Role |
|---|---|---|
| `ground.well` | `#0B0A08` | Inputs, meter tracks, key caps |
| `ground.0` | `#100E0B` | Rail, outer frame, scrim base |
| `ground.1` | `#161410` | Page ground (the top bar has no ground of its own) |
| `ground.2` | `#1D1A15` | Cards, detail pane, settings panel |
| `ground.3` | `#26221B` | Selected row, raised button, active chip, tag |
| `ground.4` | `#2F2A21` | Pressed, hover on raised, tooltip |
| `ink.hi` | `#E8E2D0` | Titles, names, active items (EldenNet's ash-white) |
| `ink.body` | `#CFC6B1` | Table text, prose, values |
| `ink.mute` | `#A69D89` | Inactive rail items and tabs, secondary facts, italic flavour |
| `ink.faint` | `#9A927F` | Eyebrows, column heads, counts, key-hint labels: the floor for readable text |
| `ink.off` | `#5F584B` | Disabled only; never information |
| `gold` | `#C9A65A` | Accent bar, underline, primary fill, meters, runes |
| `gold.hi` | `#E3C579` | Active text, focus ring, active icon |
| `gold.name` | `#C8B27A` | NPC and speaker names (EldenNet's name gold, NPC §10.1) |
| `gold.deep` | `#8C6F35` | Strong rule, selected card border, the lozenge ornament |
| `on.gold` | `#15120C` | Text on gold fills |
| `rule` | gold at 14% (≈ `#2F281A` on ground 1) | Hairlines, row separators (decorative) |
| `rule.strong` | gold at 28% (≈ `#483D25`) | Under column heads and page titles |
| `edge` | `#7E7460` | Borders that identify a control |
| `rot` | `#D9806F` | Danger text and icons, negative deltas |
| `rot.fill` | `#8F3A2F` | Danger button fill |
| `rot.edge` | `#9E3B32` | Danger borders and EldenNet's call-out edge; never text |
| `ember` | `#DB9550` | Warnings, always with an icon (D25) |
| `moss` | `#A3B37E` | Success, "on", healthy |
| `glint` | `#94B6D2` | Information, system and provider events, the stale banner |
| `tint.hover` | gold at 5% over the ground | Hover |
| `tint.select` | gold at 10% over the ground | Selected, with the 2 px bar |

Feed and badge families: combat `rot`, dialogue `gold.name`, world and quest `moss`, system and provider `glint`.
Each is always paired with an icon or a word, because gold and ember are almost equal in lightness (1.08:1).

### 3.3 Contrast (WCAG 2.x, rechecked for this document)

| Text | well | g0 | g1 | g2 | g3 | g4 |
|---|---|---|---|---|---|---|
| `ink.hi` | 15.3 | 14.9 | 14.2 | 13.4 | 12.2 | 11.0 |
| `ink.body` | 11.7 | 11.4 | 10.8 | 10.2 | 9.3 | 8.4 |
| `ink.mute` | 7.4 | 7.2 | 6.8 | 6.5 | 5.9 | 5.3 |
| `ink.faint` | 6.4 | 6.2 | 6.0 | 5.6 | 5.1 | 4.6 |
| `gold` | 8.6 | 8.3 | 8.0 | 7.5 | 6.9 | 6.2 |
| `gold.hi` | 11.8 | 11.5 | 11.0 | 10.4 | 9.4 | 8.5 |
| `gold.name` | 9.5 | 9.3 | 8.9 | 8.4 | 7.6 | 6.9 |
| `rot` | 6.9 | 6.7 | 6.4 | 6.0 | 5.5 | 4.9 |
| `ember` | 7.9 | 7.7 | 7.4 | 7.0 | 6.4 | 5.7 |
| `moss` | 8.8 | 8.5 | 8.2 | 7.7 | 7.0 | 6.3 |
| `glint` | 9.3 | 9.1 | 8.7 | 8.2 | 7.5 | 6.7 |

- Every text level passes AA on every ground. `edge` is 3.1–4.3:1 on every ground (WCAG 1.4.11). `on.gold` on `gold`
  is 8.1:1; `ink.hi` on `rot.fill` is 5.8:1. `rot.edge` (2.1–3.0:1) and `gold.deep` are never used for text.
- The focus ring (`gold.hi`) is only 1.4:1 against a gold surface, so it always sits 2 px off the element, on the
  ground (§3.9).
- Ground steps are subtle (1.05–1.14:1) on purpose: a ground is not information, and every boundary that matters
  also has a rule or an edge.
- **Grace Clear**, the high-contrast variant: a black well, `ink.hi` `#F4EFE2` (18.3:1 on black), `ink.mute`
  `#C2BAA6` (10.9:1), no faint level, 2 px edges and a 3 px focus ring.
- CI recomputes this table from `tokens/grace.toml` and fails below the thresholds (U7).

### 3.4 Fonts (OFL, from their publishers)

| Role | Family | Use |
|---|---|---|
| Display | **Cinzel** (Natanael Gama), static instances of 500 and 600 | Wordmark, page titles, the NPC name on its page, large stat values. At 20 px or more only |
| Text serif | **EB Garamond** (Georg Duffner and Octavio Pardo): Regular, Italic, Medium, SemiBold | Rail items, card titles, prose (summaries, journals, help), italic flavour lines. Already EldenNet's in-game serif |
| UI sans | **Noto Sans** (Google, Noto project): Regular, Medium, SemiBold | Tabs, labels, eyebrows, buttons, settings rows, table cells. Already EldenNet's UI sans |
| Mono | **Noto Sans Mono**: Regular, Medium | Numbers in tables, counts, key caps. Already EldenNet's mono |

- Each font is taken from its publisher's own release, ships with its `OFL.txt` and is credited in `NOTICE` when it
  is added. Never from SeverActions, Magelight or a game (D22).
- Subset to EldenNet's atlas ranges (Latin, Latin-1, Latin Extended-A, General Punctuation) and grow the ranges with
  localisation. No system font is ever used, in either view, so Proton and the Deck look the same as Windows.
- **Numbers.** Tables, counts and figures use Noto Sans Mono or Cinzel, whose figures are lining. EB Garamond's
  default old-style figures are accepted in prose. No font is modified in v1 (§3.16).
- **"Plain text" option** (`ui.menu.plain_text`): EB Garamond prose becomes Noto Sans and Cinzel titles become Noto
  Sans SemiBold, for players who find serifs hard to read.

### 3.5 Type scale

CSS px at 1080p and scale 100%; ratio about 1.2.

| Token | Size / line | Family, weight | Tracking, case | Use |
|---|---|---|---|---|
| `display.xl` | 40 / 44 | Cinzel 500 | +0.04 em | First light's and statement pages' mastheads |
| `display.lg` | 30 / 36 | Cinzel 500 | +0.03 em | Page titles in the header, a named NPC's name; the wordmark at 22 |
| `display.md` | 22 / 28 | Cinzel 600 | +0.02 em | Stat values, a detail pane's heading |
| `title` | 19 / 26 | EB Garamond 600 | 0 | Detail-pane card titles, the detail-pane name, a table's NPC name |
| `prose` | 17 / 26 | EB Garamond 400 and italic | 0 | Summaries, journals, help |
| `rail` | 17 / 24 | EB Garamond 500 | 0 | Rail items |
| `ui` | 15 / 22 | Noto Sans 400 or 500 (buttons 600) | 0, sentence case | Settings labels, table cells, buttons |
| `ui.sm` | 13.5 / 20 | Noto Sans 400 | 0 | Descriptions, sub-lines, rail pages, top-bar values |
| `tab` | 13 / 18 | Noto Sans 600 | +0.10 em, capitals | Tabs |
| `eyebrow` | 12 / 16 | Noto Sans 600 | +0.14 em, capitals | Section labels and Home card titles (in `gold`, D48); column heads and stat captions (in `ink.faint`) |
| `num` | 14 / 20 | Noto Sans Mono 400 | 0, tabular | Table numbers; counts 13; key caps 12 |

Minimums (D23): prose 16 px or more (EldenNet's NPC §10.7 rule applies to prose), running UI text 13.5 px, eyebrows
12 px, tags 11 px in capitals.

### 3.6 Spacing, grid, radii, elevation and ornament

- **Base unit 4 px**: 2, 4, 8, 12, 16, 20, 24, 32, 40, 48, 64.
- **Frame at 1080p and 100%**: rail 240 px open and 56 px collapsed (24 px side padding, category rows 40 px, pages
  32 px, a hairline between categories; §4.2.5 gives the other widths); top bar 44 px; page gutter 32 px; detail pane
  360–420 px; page content capped at 1600 px; the whole menu capped at 1.9 × the screen height and centred on
  ultrawide screens, with the rail attached to its left edge.
- **Tables**: rows 44 px (comfortable, the default) or 36 px (compact, a setting; pad mode forces 44); column heads
  32 px.
- **Settings rows**: 48 px, label and description on the left, the control right-aligned in a fixed 280 px column so
  controls line up down the page.
- **Radii**: 2 px for controls, chips, tags and key caps; 4 px for cards and panes; 0 for tabs and rows. Pills only for
  status badges.
- **Elevation**: flat. Cards are `ground.2` with a `rule` border. Menus, tooltips and modals get one soft black shadow
  (0 8 24 at 50%). The modal scrim is `ground.0` at 70%. Nothing lifts on hover.
- **Ornament** (ours): the **lozenge rule** (a `rule.strong` hairline with a small hollow `gold.deep` lozenge at its
  left end) under mastheads and between sections; the **grace glow** (a static radial gold glow, about 6% at its
  centre, behind the active rail item); an optional fine **ash grain** at 2–3% on grounds 0–1 against banding. Each
  is drawn or generated by us.

### 3.7 The frame

- **Rail** (left, §4.2.1–§4.2.2): the wordmark "EldenNet" in Cinzel and `gold` (the public name follows EldenNet's
  O1) with the collapse button, a faint mono build line ("v0.6 · Threshold"), the categories as an accordion with
  their pages, and the foot (Settings, Docs, Content packs) pinned under a rule.
- **Top bar** (§4.2.3): search at the left; at the right the status lamp (one word and a dot, EldenNet DESIGN §4.4's
  levels in words), live updates, the journey in view (the character's name, PV2, and "Journey 2" from NG+), the
  theme switch, quick switches and, in game, close.
- **Page** (§4.2.4): the header (title, help mark, one-line description, actions), tabs, sections, and the Related
  card at the foot; an optional **detail pane** or **row drawer** on the right for master–detail pages and tables.
- **Footer**: the key-hint line for the focused control, in key caps or pad glyphs.

### 3.8 Components

- **Rail category row**: rest `ink.mute` with a line icon in `ink.faint` and, if it has pages, a chevron; hover
  `ink.body` on `tint.hover`; the open category `ink.hi`; the category of the current page carries a 2 px `gold` bar,
  `ground.2`, the icon in `gold.hi` and the grace glow. **Rail page row**: `ui.sm`, indented 48 px, joined to its
  category by a 1 px `rule.strong` guide line; the current page `gold.hi` on `tint.select` with the bar. A
  later-version page is `ink.mute` with an 11 px capital tag, "WIP" (G7). **Collapsed rail**: 56 px of icons; a
  category's flyout is a `ground.2` card with the one shadow, listing its pages.
- **Top bar controls**: compact selects 32 px tall (an icon, a value in `ui.sm`, a chevron; `ground.2`, an `edge`
  border); the **status lamp** is an 8 px dot in `moss`, `ember`, `glint` or `rot` with its word in `ui.sm` (colour is
  never the only signal); the **Quick switches** menu is a list of shortcut rows, each with its "lives in …" link.
- **Section card**: `ground.2`, 4 px radius, a `rule` border; the label an eyebrow in `gold` (D48), a one-line
  description in `ui.sm` `ink.mute`, a chevron at the right; "Advanced" is a quiet text disclosure with a chevron.
- **Home card**: a section card on the grid with its title as a `gold` eyebrow; Enter or a click on the title opens
  its page. In Customise, each card shows a switch and, in game, "Move up" and "Move down".
- **Filter bar**: a `ground.2` strip holding search, compact selects and a direction button, 40 px tall. **Segmented
  switch**: two or three halves, the chosen one `ground.3` with `gold.hi` text.
- **Related card**: rows of a linked page's title (`ui`, `ink.hi`), its description (`ui.sm`, `ink.mute`) and an arrow
  in `ink.faint`, `gold.hi` on focus.
- **Help dialog**: a 560 px modal with the page's purpose, its steps as a numbered list and its Related links.
- **Statement page**: the header, prose at 72 characters a line, Related links, and a "WIP" tag beside
  the title, whose tooltip says when (G7); no controls and no greyed rows.
- **Tabs with counts**: `tab` in `ink.mute` with a `num` count in `ink.faint`; active `gold.hi` with a 2 px `gold`
  underline. Counts count only what the player has reached.
- **Filter chips**: rectangular, 2 px radius, 28 px tall, an `edge` border and a leading lozenge, hollow when off and
  filled when on; on also gets `gold.hi` text and `tint.select`.
- **Search**: a `ground.well` field with an `edge` border, an italic `ink.mute` placeholder, a clear button and a
  result count.
- **Tables**: eyebrow column heads over `rule.strong` (the sorted one `gold.hi` with an arrow); `rule` separators;
  names in EB Garamond 17 px `ink.hi` (NPCs in `gold.name`) with a `ui.sm` sub-line; numbers right-aligned `num`;
  11 px capital tags ("MET", "HERE", "NO LINES YET"); selected rows `ground.3` with a 2 px `gold` bar. Row actions
  show on hover or focus, and always for the focused row in pad mode.
- **Detail pane**: `ground.2` cards, 4 px radius, 16 px padding; a title, a row of stat tiles, sections led by
  eyebrows, then actions (primary first; destructive last, on their own row, behind a confirmation). No portraits and
  no game imagery; a lozenge monogram of our own where a picture would go.
- **Stat tiles**: the value in `display.md` and `gold` (D48), an eyebrow caption in `ink.faint` under it, a unit in
  `ui.sm` `ink.mute`, an optional 3 px meter (`gold` on `ground.well`, `rot` over a limit) with its numbers in text.
- **Buttons**: 36 px tall (44 in pad mode), Noto Sans 15 px 600, sentence case. Primary: `gold` fill, `on.gold` text.
  Secondary: `ground.3`, `edge` border. Quiet: `gold` text only. Danger: `rot.edge` border and `rot` text, filling
  `rot.fill` on hover. Disabled buttons give their reason in a tooltip.
- **Settings controls**: a real two-state **switch** (40 × 22, the word "On" beside it); a **slider** (4 px well track,
  18 px `gold.hi` thumb, a tabular value with its unit, numeric entry); a **segmented choice**; a **tri-state**
  (inherit, on, off) that shows the inherited value and its source; a **duration** field with units; **key capture**
  (pulses once while waiting, never in a loop; conflicts in `ember`); a **changed-from-default** lozenge with a reset
  button.
- **Key hints**: a key cap (`ground.well`, `edge` border, mono 12 px) and a `ui.sm` `ink.faint` label. Pad glyphs are
  our own neutral drawings (a face-button diamond with the position marked and a letter), never platform or game
  button art; they follow the last device used.
- **Toasts and banners inside the menu**: a 3 px left accent in `gold`, `ember` or `rot`, Noto Sans 15 px, as
  EldenNet's toasts.
- **Modals**: 440, 560 or 800 px wide; `ground.2`, a `rule.strong` border, the one shadow and the scrim.
- **Tooltips**: `ground.4`, an `edge` border, `ui.sm`; after 400 ms of mouse hover, at once on pad focus.
- **Scrollbars**: 6 px, no track, a thumb of `ink.mute` at 30% that widens on hover; hidden in pad mode, where focus
  scrolls the page.

### 3.9 States (every interactive element)

| State | Treatment |
|---|---|
| Hover (mouse only) | `tint.hover` and one ink step up; no movement |
| Focus (keyboard or pad) | A 2 px `gold.hi` ring, 2 px off the element, plus a 1 px `ground.well` inner ring on gold-filled elements; 3 px in Grace Clear. Always visible after keyboard or pad input |
| Pressed | One ground step darker for 80 ms |
| Selected or current | A 2 px `gold` bar on the leading edge, `tint.select` or `ground.3`, `ink.hi`, and the ARIA state in the browser |
| Disabled | `ink.off`, no hover, a tooltip with the reason; still focusable when the reason matters |
| Changed from default | A `gold` lozenge before the label and a reset button |
| Error | An `rot.edge` border, a `rot` message under the field, an icon and words |
| Pending | A static three-dot mark and the word "Saving…"; never a spinning loop |
| Stale (browser lost EldenNet) | The page dims to 70%, with a `glint` banner "Reconnecting" and Retry |
| Veiled character | A perceived character whose name the game has not shown: "Someone you have not met", in `ink.faint` italic EB Garamond with a hollow lozenge; no detail, not counted, not searchable. Never used for a story item, which is simply absent (SP5) |

### 3.10 Motion

- **Open**: the scrim and the panel fade in over 120 ms (EldenNet DESIGN §12's fade in).
- **Close by the player**: a 200 ms fade, shorter than the subtitles' 300–400 ms, because control returns to the game
  and the player is waiting. **Close on danger**: instant (D24).
- **Page change**: a 120 ms cross-fade of the page body only; the rail and top bar stay still.
- **Opening a category, a section, a flyout or a drawer**: its contents appear at once with a 100 ms fade; nothing
  slides or grows.
- **State changes**: 100 ms colour, background and border transitions. Nothing moves: no lifts, slides or scaling.
- **Live feeds** update in place at 4 Hz or less; a new row is tinted for 1 s, then fades over 300 ms.
- **No looping animation** anywhere.
- **Reduced motion** (`ui.menu.reduce_motion`, and `prefers-reduced-motion` in the browser): every duration becomes 0
  except a fade of 60 ms or less.

### 3.11 Sound

None in v1, in either view (D27). The web app has no `<audio>` or Web Audio. When EldenNet's TTS arrives (v1.5, O8),
voice previews play through EldenNet's audio path at the player's request, never from a page.

### 3.12 Accessibility

- **Contrast** as §3.3; Grace Clear as a variant.
- **Colour is never the only signal**: status carries an icon and a word; moss and rot differ in lightness as well as
  hue; a warning always has its icon.
- **Text**: scale 75–150% (§3.13); "Plain text"; prose lines at most 72 characters.
- **Input**: everything is reachable and operable by keyboard alone and by pad alone, with visible focus. Mouse hit
  targets 32 × 32 px or more; 44 px tall in pad mode. Nothing depends on hover: what a hover reveals is also in the
  detail pane.
- **The browser view** is semantic HTML with ARIA roles (tablist, grid, switch, slider, dialog), so screen readers
  work there. In game no OS screen reader can reach the overlay; the browser is the accessible surface, and the menu
  says so in Settings › Menu.
- **Motion**: the reduced-motion setting; no flashing.

### 3.13 Scale 75–150% and screens

- **One scale for all EldenNet text**: the effective scale is `(H / 1080) × ui.scale` (NPC §10.7). In game it sets
  the baked font sizes and the layout units; in the browser it is a root zoom over CSS px.
- **Breakpoints** on the resulting width in CSS px: 1600 or more, rail, page and detail pane side by side; 1200–1599,
  the detail pane narrows to 320; 960–1199, the detail pane becomes a drawer opened with Enter or A on a row; under
  1100, the rail collapses to its 56 px icon rail with names on focus and flyouts (§4.2.2). Home's grid fits two to
  five columns (§4.2.5).
- **Steam Deck (1280 × 800, 7 inches)**: at `ui.scale` 1.0, 15 px text lands at about 11 device px, too small. By G6
  (decided) the first run on a Deck writes 1.35, which gives about 15 device px and the drawer layout.
- **4K** behaves like 1080p at 2× device px; fonts are baked at the effective size, so they stay crisp.
- **Ultrawide**: content capped at 1.9 × H and centred, the game visible and dimmed on both sides.
- **75%** suits desk players at 1440p and above who want density; compact rows can go with it.

### 3.14 HDR

- Grace is defined once, in sRGB. In game the menu goes through EldenNet's overlay HDR path (NPC §10.7: linearise,
  scale by `paper_white_nits / 80`, default 203), like every EldenNet surface; there is no second palette.
- **Ceiling**: the brightest UI colour is `ink.hi` (relative luminance 0.76), about 154 nits at the default paper
  white; `gold.hi` about 117 and `gold` about 82. No pure white, no additive blend and no glow above paper white; the
  grace glow is alpha over the ground.
- **Backdrop** (D26): opaque in HDR, because a 1,000-nit sun behind a 96% backdrop would still show at about 40 nits
  as a blotch behind text; 96% `ground.0` in SDR; `ui.menu.backdrop = auto | dim | opaque` lets the player choose.
- **Banding**: grounds are flat; the only gradient (the grace glow) is small and dithered by the ash grain.
- The compatibility matrix's HDR machine checks `gold` and `rot` for over-saturation (EldenNet DESIGN §1.3 C1).

### 3.15 Variants and tokens

- v1 ships **Grace** and **Grace Clear** (`ui.menu.theme`). A light "parchment" variant is possible later as a token
  swap; it is not needed over a dark game.
- Tokens are named by role (`ground.*`, `ink.*`, `gold.*`, `rule*`, `edge`, `rot*`, `ember`, `moss`, `glint`,
  `tint.*`, `font.*`, `size.*`, `space.*`, `radius.*`, `dur.*`) in one file, `tokens/grace.toml`. A build step writes
  CSS custom properties and Rust constants, so the two views cannot drift apart.
- **Icons**: our own line icons (1.5 px stroke on an 18 px grid), drawn as vector paths for the browser and, in game,
  as paths `grace-draw` tessellates (§3.16). Elden Ring concepts (grace, runes, flasks) are simple abstract
  marks of ours, never traced from game icons. A permissively licensed set (Lucide, ISC; Tabler, MIT) may be used
  for generic icons after its licence is checked.

### 3.16 Drawing Grace in game (G1; G10 Open, following (b))

`grace-draw` tessellates each element below into vertex lists; EldenNet's overlay copies them into the draw list of
ImGui 1.89.2 through hudhook 0.9.3, so ImGui's renderer draws them (§2.9).

| Grace element | In game | Cost or loss |
|---|---|---|
| Grounds, hairlines, radii, scrim | Filled and stroked rectangles with per-corner rounding, and lines; hairlines snapped to device pixels | None |
| Lozenge rule, focus ring, switch, slider, chips | Quads, filled circles and paths | None |
| The one soft shadow | A pre-blurred nine-slice texture of ours, uploaded once at load | Small: fixed blur radius |
| Grace glow, ash grain | A vertex-coloured triangle fan; a tiled noise texture of ours, uploaded once at load | Small |
| Fades and transitions | Our own alpha tweening per element | None |
| Cinzel, EB Garamond, Noto | TTF from `GraceUI\fonts\`, baked by EldenNet into the one atlas through stb_truetype, static instances only, per effective size; GraceUI lays text out from the glyph tables it is handed | Variable fonts ship as static weights |
| Tracking (+0.02 to +0.14 em) | Added by GraceUI's text layout to each advance | None: no tracked bake is needed |
| Kerning | Not in ImGui's bakes. If the owner's look review asks, GraceUI's text layout applies the font's pair kerning to display text only, from a table extracted from the OFL font at build time | Titles in Cinzel and EB Garamond look slightly loose until then |
| OpenType features (lining figures, small caps) | Not applied | Numbers use mono or Cinzel (§3.4); small caps are never faked |
| Tables and flex-like layout | Our own layout helpers in `grace-view`; ImGui's tables are not used (D28) | Development time, not looks |
| Accessibility tree | None | The browser view is the accessible surface |

**Atlas budget (D29).** EldenNet's surfaces and the menu share one ImGui font atlas, and hudhook uploads it as RGBA
once, leaking one upload buffer per rebuild (REN §2.4). So:

- the menu bakes at most ten faces: Cinzel 40 and 22 (capitals, figures and punctuation only); EB Garamond SemiBold 19,
  Regular 17 (rail items too, in game) and Italic 17; Noto Sans Regular 15, SemiBold 15 (buttons) and Regular 13.5; one
  Noto Sans SemiBold 12.5 shared by tabs and eyebrows (tracked by layout); Noto Sans Mono 13. `display.lg` (30) is drawn
  from the 40 bake scaled down, and a size within 1 px of an EldenNet surface's bake reuses that bake;
- the atlas stays within 2048 × 2048 at 1080p–1440p and scale up to 1.25, and within 4096 × 4096 at 4K or 150%;
  S4 measures it;
- a scale change rebuilds the atlas only when the menu is closed, at most four times a session (later changes apply
  at the next launch), and each rebuild is logged with its size.

### 3.17 What we never do

- Copy or adapt SeverActions' CSS, class structure, markup, scripts, texts (taglines included), images, sigils, map,
  grain texture or theme names.
- Take a font file from SeverActions or Magelight, or ship a game font.
- Use game fonts, icons, button prompts, frames, textures, screenshots, the Elden Ring logo or ring mark, or a name
  that implies FromSoftware's endorsement. "Grace" is an ordinary English word and the repository's name.
- Copy SkyrimNet's dashboard styling, icons, code or text. Only its structure is used: how it groups a framework
  into categories and pages and the kinds of page parts it uses (SND, D38).

---

## 4. Information architecture

GraceUI is the dashboard of an AI framework, so its structure follows the way SkyrimNet groups a framework's elements
(SND): a collapsible sidebar of categories with pages, a slim top bar, a Home grid of cards, and pages built from a
few regular parts (D38). Names are plain framework words; Elden Ring's features and settings live inside the pages,
in our own words, and game names appear only at run time (D39). The Grace
look (§3) stays, with gold now also naming a page's sections (D48). This section is binding. It replaces the first
proposal's flavour-named sections (IA §3 and §5's page set; §4.12 maps old pages to new); IA's mapping evidence and
its spoiler and privacy studies still stand.

### 4.1 Principles

1. **One home per setting.** Each setting has exactly one home page, where search leads. Other pages may show it as a
   shortcut row: the same control, marked "lives in Behaviour › Pacing". Settings holds only the settings no category
   page can hold, and links out to the rest (D40).
2. **Changes apply as they are made** (D14). No Save button; EldenNet writes `settings.toml` after 300 ms of quiet and
   hot-reloads it. Danger in game closes the menu and keeps every change; a confirmation still open is cancelled.
3. **Each row says whether it works now**: live, waits on a game check on this build, takes effect in v1.5, later
   version, or no lines in this version (§4.7). A setting that does nothing yet never looks as if it works.
4. **Player-safe projections only** (SP1). Live views show only what the Tarnished perceived (SP7).
5. **Same pages in both views** (D5). The browser adds only Workshop, behind `debug.panel` (D10).
6. **Framework categories, plain names** (D38, D39). The rail and page titles use plain words in British spelling
   (AI, Voice, Characters, History, Behaviour, System, Tools). Page text speaks to the player as "you", in our own
   words. Names of places, regions and characters appear only at run time, from EldenNet's projections (SP3);
   GraceUI's page descriptions hard-code none. Where a Purpose line below says "the Tarnished", the page's
   description reads "you".
7. **A feature's switch lives on its page** (D45). The top bar's quick switches are shortcuts to those homes.
8. **Later versions are stated, never faked** (G7). A later-version entry carries a "WIP" tag and opens a statement
   page with no controls; a feature that is not planned never appears (§4.11).
9. **Nothing personal leaves a page** (PV1–PV8).

### 4.2 The frame

#### 4.2.1 The sidebar

```
EldenNet                                   [collapse]
Home
AI                 ▸ Providers · Models · Self-test · Requests · Director (WIP)
Voice              ▸ Text and subtitles · Text-to-speech (WIP) · Speech-to-text (WIP)
Characters         ▸ Playthrough · Named NPCs · NPC types
History            ▸ Events · Conversations · Memories · World
Behaviour          ▸ Conversation · Speaking up · Combat · Awareness · Reactions · Pacing · Acts (WIP)
System             ▸ Status · Performance · Traces · Logs
Tools              ▸ Event monitor · Bug report · Workshop (browser, developer)
────────────────
Settings
Docs
Content packs
```

| Category | Pages, with their ids | First shown |
|---|---|---|
| Home | Home `home` | 0.2 |
| AI | Providers `ai.providers` · Models `ai.models` · Self-test `ai.selftest` · Requests `ai.requests` · Director `ai.director` (WIP: later) | 0.2; Director a statement from 0.6 |
| Voice | Text and subtitles `voice.text` · Text-to-speech `voice.tts` (WIP: v1.5) · Speech-to-text `voice.stt` (WIP: v1.5) | 0.4 (browser), 0.6; the two v1.5 statements from 0.6 |
| Characters | Playthrough `characters.playthrough` · Named NPCs `characters.named` · NPC types `characters.types` | 0.3; 0.3; 0.4 |
| History | Events `history.events` · Conversations `history.conversations` · Memories `history.memories` · World `history.world` | 0.4; 0.3; 0.3; 0.5 |
| Behaviour | Conversation `behaviour.conversation` · Speaking up `behaviour.speaking` · Combat `behaviour.combat` · Awareness `behaviour.awareness` · Reactions `behaviour.reactions` · Pacing `behaviour.pacing` · Acts `behaviour.acts` (WIP: later) | 0.2 (file) to 0.6; Acts a statement from 0.6 |
| System | Status `system.status` · Performance `system.performance` · Traces `system.traces` · Logs `system.logs` | 0.2 |
| Tools | Event monitor `tools.monitor` · Bug report `tools.report` · Workshop `tools.workshop` (browser, `debug.panel`) | 0.4; 0.2; 0.2 |
| Foot | Settings `settings` (groups `settings.general`, `settings.hotkeys`, `settings.menu`, `settings.browser`, `settings.developer`; tab `settings.all`) · Docs `docs` · Content packs `content` | 0.2 (browser, partly read-only), 0.6 |

Home and 29 pages in seven categories: Home and 24 pages work in v1, two arrive in v1.5, two are later-version
statements and Workshop is for developers; then three foot pages. Outside the rail: First light (the setup flow),
search and confirmation modals (§4.6.10).

#### 4.2.2 Sidebar behaviour

- **Wordmark** "EldenNet" in Cinzel and `gold` at the top (the public name, EldenNet O1), a faint mono build line
  under it ("v0.6 · Threshold"), and the collapse button at its right. GraceUI is the project's name; the frame does
  not show it, and its version appears on System › Status › Compatibility and Docs › About (G2).
- **Categories** are icon and label rows separated by hairline rules; a chevron marks those with pages. They form an
  accordion: one category open at a time. The open category shows its pages indented under it, joined by a thin
  vertical guide line. The current page and its category both carry the selected state (§3.9). Home has no chevron.
- **Collapsed rail**: icons only (56 px), each with its name on hover and on focus; a category opens a flyout of its
  pages. The rail collapses by itself under 1100 px of menu width and in the drawer layout (§3.13, G6). Collapsed or
  not is view state for the session, never a setting.
- **Later-version entries** (Text-to-speech, Speech-to-text, Director, Acts; G7): `ink.mute`, a small "WIP" tag
  whose tooltip and accessible name say when (v1.5 or a later version), focusable, each opening a statement page with
  no controls. Features that are not planned never appear (§4.11).
- **Workshop** appears only in the browser and only while `debug.panel` is on (SP8). In game, Tools shows Event
  monitor and Bug report.
- **Foot**, pinned to the bottom under a rule: Settings, Docs, Content packs. No account row, community link or
  sign-in: EldenNet has no accounts (PV rules).
- **World** is absent from the rail while `ui.menu.show_world = false` (the player's choice, not a spoiler hint).
- **At the title screen and at L1** the rail stays whole; pages that need a playthrough say so in their body (§4.4).
- **By increment**: a page joins the rail in the increment it ships in (§10); before then it is absent, not greyed.
  The later-version statements join with the full rail in 0.6.

#### 4.2.3 The top bar

A 44 px strip above the page with no ground of its own; search at the left, the rest right-aligned:

| Control | What it does | Shown on | Stored |
|---|---|---|---|
| **Search** (`/`, Ctrl+F; pad Y) | Global search (§4.3); results carry breadcrumbs such as "Behaviour › Pacing › Time between lines" | Every page | — |
| **Status lamp** | One word and a dot: Ready (EldenNet DESIGN §4.4's L4), Reduced (L3), Observing (L2), Paused (AI paused), Dormant (the session gate, loading), Inactive (L1 or off); hover or focus gives the reason; a click opens System › Status | Every page | — |
| **Live updates** (clock icon) | Live, every 5 s, or paused: throttles or holds the page's live topics so a moving table can be read | Pages with live data: Home, Requests, Events, Event monitor, Traces, Logs, Performance | View state, session only |
| **Journey** (book icon) | The playthrough in view: the character's name and "Journey 1" (NG+ cycles are later journeys); a menu lists this playthrough's earlier journeys, which History and Playthrough then show read-only. At the title: "No character loaded" | History pages, Playthrough, Named NPCs (its Met and Nearby chips) | View state, session only; the current journey again at each open |
| **Theme** (palette icon) | Grace or Grace Clear | Every page | A shortcut of `ui.menu.theme` (home: Settings › Menu) |
| **Quick switches** (power icon; pad Start) | A menu of shortcut rows, each marked "lives in …": EldenNet on; Pause AI for this session; Conversation; Call-out; Named NPCs speak unasked; Other NPCs speak unasked; NPC-to-NPC; Autonomy; Self-awareness; Status awareness; Memory; Floating labels | Every page | Shortcuts only |
| **Close** (in game) | Closes the menu (§5.1) | Every page | — |

SkyrimNet's middle control is a theme picker, not a character selector (SND §1.3); GraceUI keeps a theme switch and
adds the journey selector (D41). The character's name shows only in the Journey control and on pages about the
player's own story (PV2), never in the browser tab's title, a URL, a log or a report.

#### 4.2.4 Page frame and parts

- **Header**: the title in `display.lg` and a circled help mark, then a one-line description in `ink.mute`; header
  actions at the right (primary in gold, destructive in `rot`, at the far right). The help mark opens a dialog: what
  the page is for, how to use it in three to five steps, and its Related links. Its text lives in the page
  descriptions (§2.5).
- **Tabs**: underlined text tabs, gold when active (§3.8); a **segmented switch** for two views of one list (Events or
  Decisions on the Event monitor).
- **Sections**: cards with an uppercase gold label (D48), an optional one-line description and a chevron; a collapsed
  section shows only its label and description. Inside, an "Advanced" disclosure hides rare fields. Open or closed is
  remembered for the session.
- **Settings rows** (§4.7) state their default at the end of the help line ("Default: 2.5 s"). Dependent rows dim with
  "Off because …" rather than hide. Sliders run the full row width with the value and unit at the right.
- **Stat tiles**: a big gold number and a small uppercase caption. Counts only of things reached (SP5).
- **Filter bar**: search (only where the page's index holds no story or chat text, D21), filter menus, sort and
  direction. **Toolbar**: the primary action at the left, Refresh, the destructive action at the right in `rot`.
- **Tables**: uppercase column heads in `ink.faint`, hairline rows, per-row actions at the far right, a row's detail in
  a drawer. Editable cells (switches, cooldown sliders) on Behaviour › Reactions only.
- **Related** card at the foot of every page: each linked page's title, its one-line description and an arrow.
- **Empty states**: a centred sentence and a smaller hint; the healthy state ("No problems") in `moss`.
- **Badges**: availability (§4.7), "WIP" tags (G7), family pills, key status (Set, Missing, Rejected).
- **Statement page** (later-version entries): the header, two or three sentences on what the later version adds and
  what this version does instead, Related links, and no controls.

#### 4.2.5 Sizes

In CSS px at the effective scale (§3.13):

- **Rail** 240 px open (232 at a 1280 px menu width, 260 at 1600 px or more), 56 px collapsed. **Content** capped at
  1600 px and centred; the whole menu stays within 1.9 × H on ultrawide screens (§3.6).
- **Home's grid** has as many 300 px columns as fit, from two to five: three in a 1280 × 720 browser window, five at
  2560 × 1440, two in the drawer layout. A card's width is 1, 2, 3 or full columns; a card wider than the grid takes
  the full width.
- **Tables** drop their least important columns into the row drawer when narrow.
- **Steam Deck at 1.35** (G6): the drawer layout, the rail collapsed, Home in two columns, 44 px rows (pad mode).

### 4.3 Global search

- Opened with `/` or Ctrl+F in both views, and with Y on a pad (§5.4); its field sits in the top bar.
- Indexes category and page names, page descriptions, every setting's label, plain synonyms, help and key, reaction
  labels, type labels and tags (every type, G5), named NPCs by the name the game shows or their first-meeting name
  (SP3), System › Status check names, and Docs' titles and headings.
- A result shows its breadcrumb ("Behaviour › Pacing › Time between lines") and opens the home page with the row
  focused and tinted.
- **Never indexed** (D21): journals, keepsakes, conversations, World lines, Events and Event monitor rows, Requests
  text, Playthrough's story lines and notes, Docs' bodies. Search finds settings, pages and names as shown, so it
  cannot surface a spoiler or a private line.

### 4.4 States of the whole menu

| State | Usable | The rest says |
|---|---|---|
| Title screen (no playthrough loaded) | Home (Setup, Status, Problems), all of AI, Voice › Text and subtitles, System, Tools › Bug report, Settings, Docs, Content packs, every settings row of Behaviour, Characters › Named NPCs (the roster: Profile, Allowances, Reactions) and NPC types (every type, G5), First light | Playthrough, the History pages and the Event monitor: "Load a character to see this" |
| L1 banner (EldenNet DESIGN §4.4) | System, Tools › Bug report, Settings, Docs, Voice › Text and subtitles; in the browser only after a later L1 trip that is not anti-cheat, since the server never starts when the gate or the safety step fails and stops when anti-cheat is found later (§7.2 item 1) | Greyed: "EldenNet is inactive: reason" |
| L2 observing (no healthy model), or AI paused | All | Home and AI lead with the cause |
| Dormant (multiplayer, loading) | All; live views paused | "Paused (multiplayer)"; settings stay editable |
| Lost contact (browser) | The last snapshot, read-only | The stale state (§3.9) |

### 4.5 Every setting and its home

Keys are EldenNet's `settings.toml` (ARCH §11.5, schema 2) plus the keys GraceUI asks for (marked *new*; see
[eldennet-changes.md](eldennet-changes.md)). Effect timing is ARCH §11.6's: Now, Next moment, Next request, Next
launch.

| Key | Player label | Home | Shortcuts on | Effect |
|---|---|---|---|---|
| `enabled` | EldenNet on | Settings › General | Quick switches | Pause now, off from the next launch |
| `features.chat` | Conversation with named NPCs | Behaviour › Conversation | Quick switches | Next moment |
| `features.callout` | Call out to other NPCs | Behaviour › Conversation | Quick switches | Next moment |
| `features.speak_up` | Named NPCs speak unasked | Behaviour › Speaking up | Quick switches | Next moment |
| `features.barks` | Other NPCs speak unasked | Behaviour › Speaking up | Combat, Quick switches | Next moment |
| `features.npc_to_npc` | NPCs greet and answer each other | Behaviour › Speaking up | Quick switches | Next moment |
| `features.self_awareness` | NPCs know what they said and how they are | Behaviour › Awareness | Quick switches | Next moment |
| `features.status_awareness` | NPCs notice statuses | Behaviour › Awareness | Quick switches | Next moment |
| `features.memory` | Named NPCs remember | History › Memories | Quick switches | Next moment |
| `features.labels` | Floating labels (else the feed) | Voice › Text and subtitles | Combat, Quick switches | Now |
| `autonomy.enabled`, `autonomy.period_s` | NPCs speak up on their own; how often | Behaviour › Speaking up | Quick switches (enabled) | Next moment |
| `range.talk_m` | Speaking-to-you range | Behaviour › Pacing | Conversation | Next moment |
| `range.hear_m` | Overheard-lines range | Behaviour › Pacing | Speaking up | Next moment |
| `range.fight_m` | Combat-lines range | Behaviour › Pacing | Combat | Next moment |
| `pacing.gap_s`, `pacing.window_s` | Time between lines; the window | Behaviour › Pacing | — | Next moment |
| `pacing.group_gap_s`, `pacing.fight_max` | Between lines of one fighting group; lines per window in a fight | Behaviour › Pacing | Combat | Next moment |
| `pacing.idle_max`, `pacing.talk_max` | Idle remarks per window; talk per window | Behaviour › Pacing | Speaking up | Next moment |
| `reactions.<id>.on`, `.cooldown_s` (20 ids, NPC §9.1) | Per reaction | Behaviour › Reactions | In battle on Combat; Afflictions on Awareness; To you, Between NPCs and Calm remarks on Speaking up | Next moment |
| `allow."<kind\|type\|slot>".<key>`, `.reactions.<id>` (EldenNet DESIGN §7.6) | Per allowance | Characters › NPC types | Named NPCs (the slot's row), Combat (`combat`, `boss_fight`), Awareness (`status_reactions`, awareness); Voice › Text-to-speech (a read-only `tts` summary) | Next moment |
| `ui.scale` | Text and menu size | Voice › Text and subtitles | Settings › Menu | Now; in game the atlas rebuilds when the menu closes (D29) |
| `ui.hdr`, `ui.paper_white_nits` | HDR; paper white | Voice › Text and subtitles | — | Now |
| `ui.follow_game_subtitles` | Follow the game's subtitle setting | Voice › Text and subtitles | Conversation | Now |
| `ui.latency_toasts` | Latency toasts | Settings › Developer | Traces | Now |
| `pools.max_jobs_per_session` | Prepared-line batches per session | AI › Models (Prepared lines) | Combat | Next request |
| `safety.level` | Content safety: faithful or gentle (O17) | Behaviour › Conversation | — | Next request |
| `hotkeys.talk`, `hotkeys.skip_page` | Talk; skip page | Settings › Hotkeys | Conversation | Now |
| `hotkeys.menu` *new*, replacing `hotkeys.settings` | Open the menu (F5) | Settings › Hotkeys | — | Now |
| `hotkeys.debug` | Debug view (F11) | Settings › Hotkeys | Settings › Developer | Now |
| `ui.menu.theme` *new* | Grace or Grace Clear | Settings › Menu | Top bar | Now |
| `ui.menu.plain_text`, `.reduce_motion`, `.key_hints`, `.pad_glyphs`, `.backdrop`, `.compact_rows` *new* | The menu's own options (D19) | Settings › Menu | — | Now |
| `ui.menu.show_world` *new* | Show the World page | Settings › Menu | — | Now |
| `ui.menu.home_cards` *new* | Home's cards, their order and Setup's place | Home › Customise | — | Now |
| `dashboard.enabled` (on), `dashboard.port` (8642) *new* (G4) | Serve the browser dashboard; its port | Settings › Browser view | — | Now (the server starts, stops or moves) |
| `dashboard.lan` (off) *new* (G9) | Other devices on your network | Settings › Browser view | — | Now |
| `augur.allow_custom_rules` | Your own event rules | Behaviour › Reactions (a locked "later version" row) | — | — |
| `debug.panel`, `.record_sessions`, `.record_redact_text`, `.log_level` | Developer options | Settings › Developer | Logs (log level), Workshop (panel) | Now |

The first proposal's `ui.menu.names` is dropped: names are plain only (D39).

**Other sources.** `providers.toml` (read and write) on AI › Providers (its entries) and AI › Models (slot models and
the preset); `secrets.toml` **write-only** on AI › Providers (the UI sees set, missing or rejected, never a value);
`models.toml` (read) on AI › Models; `identity/types.toml` (read) on NPC types; persona display fields only
(`display_name_fallback`, `public.summary`, the news temperament) on Named NPCs; each persona stage's public line, for
the stage seen only, on Playthrough; a world stage's `line` only for stages that hold, on History › World; `overrides/`
and `disable.toml` (read, as relative paths, ids and counts) on Content packs. View state, never stored: live updates,
the journey in view, the rail collapsed, sections open.

**Actions** (each a command, §6.6): Pause AI for this session (Home, D15); run the self-test (AI › Self-test, Home);
find local servers (AI › Providers); prepare lines now (AI › Models; at rest or at the title only); Forget this NPC
and Forget this journey (History › Memories; confirmation, a backup, no undo but the backup); reset a row, a page or
everything; show a sample line (Voice › Text and subtitles); capture a key (Settings › Hotkeys); build, preview, copy
or save a bug report (Tools › Bug report, after EldenNet's deterministic scan); disconnect every browser (Settings ›
Browser view); pair a device while `dashboard.lan` is on (in game only), and forget one at any time (G9).

**The page lint** (invariant 12, §9.1) checks every key above against EldenNet's settings description: exactly one
`home`, and the home is one of the page ids `home`, `ai.models`, `voice.text`, `characters.types`, `history.memories`,
`behaviour.conversation`, `behaviour.speaking`, `behaviour.awareness`, `behaviour.reactions`, `behaviour.pacing`,
`settings.general`, `settings.hotkeys`, `settings.menu`, `settings.browser` or `settings.developer`. AI › Providers
holds `providers.toml` entries and keys, which are not settings rows.

### 4.6 Pages

Each page below gives its purpose, contents and controls, the topics it reads (§6.5) and the commands it sends
(§6.6), the increment it ships in, and its spoiler and privacy rules. Every page also has the header, help dialog and
Related card of §4.2.4.

#### 4.6.1 Home

- **Purpose.** Is EldenNet working, who is near, what was just said and what was asked of the model.
- **Header actions.** Pause AI (a chip that toggles; session only, D15) · Run self-test (`selftest.run`; the result
  in a toast and on AI › Self-test) · Customise.
- **Cards** (a grid of mixed widths, each with an uppercase gold title; the title, or Enter on it, opens its page):

| Card | Width (3 / 5 columns) | Contents | Reads | From |
|---|---|---|---|---|
| **Setup** (collapsible) | Full / full | Three cards side by side. *Model*: the provider and key status in words, the preset (a shortcut; home AI › Models), Test with the self-test's verdict word. *Voice*: "Text first; spoken voices arrive in v1.5" and a link. *Microphone*: "Talking aloud arrives in v1.5" and a link. "Run setup again" opens First light. Open until the self-test has passed once on this install, then collapsed | `providers`, `selftest`, `health` | 0.2 |
| **Status** | 1 / 1 | The state word and its reason; in game "You can talk now" or why not; one dot per model slot with its word; EldenNet's version, the game build (supported or not), session time; whether AI is paused; the dashboard's address, or "off" (G4) | `status`, `health` | 0.2 |
| **Nearby** | 1 / 2 | Perceived characters (SP7): on-screen name or type label, a state word, wounds in words for combatants, statuses after S8, "speaking", a met NPC's disposition word; at most twice a second; no HP numbers, no distances to hostiles; its title opens Characters › Playthrough | `nearby` | 0.3 |
| **Happening now** | 1 / 1 | Short-lived events the Tarnished perceives (Augur's TTL events, EldenNet DESIGN §6.4), each with our label and a time-left bar redrawn at the live rate; "Nothing right now" | `shortlived` | 0.4 |
| **Problems** | 1 / 1 | Recent failures in words (EldenNet's failure contract): the model unreachable, a key refused, a read group off; each with its fix and a link; "No problems" in `moss` | `errors` | 0.2 |
| **Lines** | 2 / 3 | Displayed lines (the speaker as shown, the line), conversations opened and closed, notices, reloads | `activity` | 0.2 |
| **Recent events** | Full / 2 | The last ten perceived events in our labels | `monitor.events` (its tail) | 0.4 |
| **Model calls** | Full / full | The last 20 calls: time, slot, purpose, speaker (as shown, SP3), model, first token, total, outcome (the words of AI › Requests); the displayed text only if it was shown; this hour's own-initiative calls against the bound | `calls`, `usage` | 0.2 |

- **Grid.** Three columns under 1600 px, five at 1600 px and wider (CSS px at the effective scale, §4.2.5). The
  default order is the table's. Cards are placed in that order with no packing (no `dense` flow), so the visual order
  and the focus order agree at every width; at three columns Recent events takes the full width, so no cell is left
  empty. A custom order may leave a gap; nothing reorders cards to fill it.
- **Customise** (a drawer): show or hide each card, Setup at the top or the bottom, card order (drag in the browser;
  "Move up" and "Move down" on the focused card in game). Stored as `ui.menu.home_cards`, whose home is this drawer. A
  hidden card subscribes to nothing.
- **Rules.** SP7 on Nearby, Happening now and Recent events; Lines shows only what was displayed. Chat and model text
  only on Lines and Model calls (PV4).
- **Keys and pad.** Cards are focus targets in reading order; ↑↓ inside a card's rows; A on a title opens its page.

#### 4.6.2 AI

**Providers** (`ai.providers`; 0.2 read-only, 0.6 writable in both views).

- **Purpose.** Where EldenNet's model calls go: a cloud service with the player's own key, or a local server.
- **Header actions.** Find local servers (`servers.find`; only while EldenNet's gate allows network, C4) · Add
  provider.
- **Tabs.** *Providers*: an **Advice** banner from `models.toml` (which preset suits most players and why, with "Use
  this", which opens Models with the preset chosen); one card per entry of `providers.toml`: id, cloud or local
  (`kind`), flavour (OpenAI-compatible, Ollama, llama.cpp, LM Studio or KoboldCpp; ARCH §11.1), base URL (shown on this
  page only), key status (Set, Missing, Rejected) with a masked, write-only "Set key" and "Clear key", requests at once
  (`max_concurrency`; default 2 for cloud, 1 for local), JSON answers (`json_mode`: off, JSON object, JSON schema;
  default JSON schema); *Advanced*: extra header names, whose values are write-only like keys. **Local servers found**:
  one row per probe (the four known servers on their ports, in ARCH §11.4's order; llama.cpp on 9931, then its legacy
  8080), found or not, with "Use". *Health*: per provider, healthy, cooling down (time left) or failing (the error class
  in words: unreachable, key refused, rate limited, timed out, bad reply), the last success, and the slots that use it
  (ARCH §11.2).
- **Data.** Reads `providers`, `health`. Writes `providers.update`, `secrets.set`, `secrets.clear`, `servers.find`.
- **Rules.** Keys never come back (PV2). On a paired device (G9) the page is read-only: no Add provider, Use, Set key or
  Clear key, and no field can be changed, since §7.2 item 16 refuses `secrets.set`, `secrets.clear` and
  `providers.update` there. The base URL appears only here, never in a log, report, title or URL; elsewhere a host that
  is not a known public provider is scrubbed (PV5). In the browser view the key field is never a password-type input,
  not even briefly (no show or hide toggle that flips its type), because browsers ignore `autocomplete="off"` there and
  offer to save, and sync, what is typed. It is a text input with `autocomplete="off"`, `spellcheck="false"`,
  `autocapitalize="off"` and `autocorrect="off"`, masked by the stylesheet (`-webkit-text-security: disc`; Chromium,
  Safari, Firefox 114 and later), outside any `<form>`, and cleared once sent. In game it is GraceUI's own field, drawn
  as discs and never as text (D57). On a pad it opens the Steam keyboard, masked. No spoilers.
- **Related.** Models, Self-test, Docs › What is sent to your provider.

**Models** (`ai.models`; 0.2 read-only, 0.6 writable).

- **Purpose.** Which model does each job, and the preset that sets them all.
- **Header actions.** The preset (Cloud budget, Local 12 GB, Local 16 GB+, Custom) and Apply · Reset to preset.
- **Sections.** **Advice**: EldenNet's advice for this machine in words (an 8 GB GPU is pointed at the cloud); a notice
  when the shipped advice changed in this version; the free-model warning (free models are not advised for prepared
  lines and autonomy, and free presets turn autonomy off, O37). **One card per job slot**, by player label with the
  slot id as a chip: *Conversation* (`dialogue`), *Quick lines* (`bark_live`), *Prepared lines* (`bark_pool`),
  *Memory* (`memory`), *Self-test* (`selftest`); each with its provider, model (a picker from the provider's model
  list, or text), fallback model, a health dot and word, and the fixed budgets read-only (tokens, temperature,
  first-token and total time, retries: "the same in every preset", EldenNet DESIGN §11.2). The **Prepared lines** card
  also holds `pools.max_jobs_per_session` (its home), the pool's state in words (cards ready, generating, paused in a
  fight; counts cover every type, which are all listed (G5); pool lines are never listed before they play) and
  "Prepare lines now" (`pools.prebake`; at rest or at the title only). **Decisions** (`meta`): a dimmed card with a
  "WIP" tag (a later version, G7), no controls.
- **Data.** Reads `providers`, `health`, `settings`. Writes `providers.update`, `settings.set`, `pools.prebake`.
- **Rules.** Pool lines are never listed before they play. Privacy as Providers; on a paired device (G9) whatever here
  writes `providers.update` is read-only, as on Providers.
- **Keys and pad.** Pickers open as lists; type to filter in the browser.
- **Related.** Providers, Self-test, Requests, Behaviour › Combat.

**Self-test** (`ai.selftest`; 0.2).

- **Purpose.** Is the model fast and well-formed enough for conversation?
- **Header action.** Run self-test (one at a time; gated like Find local servers).
- **Sections.** **Verdict** (quick ≤ 1.6 s, acceptable ≤ 3.0 s, slow beyond, for the estimated first page; EldenNet
  DESIGN §11.5) · **Figures** (first token, tokens per second, the JSON check, the test line from the test persona as
  shown) · **At start-up** (this session's silent self-test) · **Earlier runs this session** (time, model, verdict).
- **Data.** Reads `selftest`. Writes `selftest.run`.
- **Rules.** The test line is shown here and in First light only (PV4), never logged.
- **Related.** Models, System › Traces.

**Requests** (`ai.requests`; 0.2).

- **Purpose.** Every model call this session and what it cost, without prompt text.
- **Tiles.** Calls · Tokens in · Tokens out · Failures · Own-initiative calls this hour (against the bound) · Cost
  (only where the provider's reply reports it, D18).
- **Filter bar.** Slot · Purpose (reply, own-initiative line, call-out reply, prepared-line batch, memory work,
  self-test) · Outcome (shown, not shown, fallback model, timed out, refused, failed; pool filled for a prepared-line
  batch) · sort. No free-text search. "Not shown" covers any answer that never reached the screen: silence, an
  answer withheld by a check, or one dropped unshown when its conversation closed (NPC §5.7); an own-initiative call
  that ends so still reserved its slot (EldenNet O37). Home › Model calls uses these purpose and outcome words.
- **Table.** Time, slot, purpose, speaker (as shown, SP3, or a type label), provider and model, first token, total,
  tokens, outcome; the row drawer shows the displayed text only if it was shown, and the error class in words.
- **Data.** Reads `calls` (20 kept, 200 in developer mode), `usage`.
- **Rules.** No prompt or unshown text, ever (invariant 1); no base URL.
- **Related.** Models, System › Traces, System › Logs.

**Director** (`ai.director`; a later-version statement). A director that chooses who speaks next and starts scenes on
its own is a later version (EldenNet DESIGN §13, the `Directed` class). In this version NPCs speak through greetings,
quest business, news, NPC-to-NPC exchanges and autonomy, held by pacing and allowances. Related: Behaviour ›
Speaking up, Behaviour › Pacing. No controls.

#### 4.6.3 Voice

In v1 NPCs speak in text, so Voice opens with the text that carries their words; spoken voices follow in v1.5 (D43).

**Text and subtitles** (`voice.text`; 0.4 in the browser, D17; 0.6 in game).

- **Purpose.** How NPC lines appear on screen.
- **Sections.** **Lines**: `features.labels` (floating labels over speakers, or the feed at the side). **Subtitles**:
  `ui.follow_game_subtitles`. **Size**: `ui.scale` (75–150%; the menu and every EldenNet text follow it; in game the
  atlas rebuilds when the menu closes, D29). **HDR**: `ui.hdr` (auto, off, on), `ui.paper_white_nits`. **Preview**:
  "Show a sample line" (`preview.subtitle`; in game drawn by EldenNet's real surfaces for 3 s after the menu closes;
  in the browser a drawn sample).
- **Data.** Reads `settings`. Writes `settings.set`, `preview.subtitle`.
- **Rules.** The sample line is ours.
- **Related.** Behaviour › Conversation, Behaviour › Combat, Settings › Menu.

**Text-to-speech** (`voice.tts`; v1.5, a statement page in v1). What v1.5 adds (a voice per persona and card through
an out-of-process engine, page timing from the audio), and what never comes (cloning the cast's voices). A read-only
summary of the `tts` allowance by kind, each row a link to Characters › NPC types; voice keys once content carries
them. No controls in v1. Its v1.5 controls (engine, endpoint, test voice, volume) are designed with v1.5.

**Speech-to-text** (`voice.stt`; v1.5, a statement page in v1). Talking aloud to named NPCs arrives in v1.5
(`InputSource::Stt`); typing stays. Push-to-talk will live in Settings › Hotkeys. No controls in v1.

#### 4.6.4 Characters

Playthrough holds this journey's state; Named NPCs and NPC types hold the roster, which is the same in every
playthrough and listed from the start with only what a first meeting shows (G5, G8, D64).

**Playthrough** (`characters.playthrough`; 0.3, its story lines and World now from 0.5; D63–D65).

- **Purpose.** The people you have met on this journey, how they are now, and where their stories stand as you have
  seen them.
- **Layout.** **Tiles**: People met · Stories seen (met people with a story line shown) · Spoken with this session.
  **World now**: a card with the line of the highest-ranked world stage that holds (SP6) and a link to History › World;
  absent while `ui.menu.show_world = false`. **Filter bar**: names as shown · status · region (regions entered) · sort
  (last met, first met, name). **Master–detail** (D36). The table: the name as the game shows it (SP3), status, last met
  (a place by its runtime name in a region entered, and play time ago), their story as last seen (shortened), since you
  last spoke (the first note). The detail: the name; **Status** (the word, where and when last met, times met, the
  disposition word); **Their story** (the line for the stage seen, with when it was seen); **Related** (for each met
  person that stage's line links, their own line and a link to their row); **Has heard of** (the world stages that hold,
  each with a certainty word); **Since you last spoke** (up to three notes, each from a record: a world stage that began
  to hold since the last conversation, meetings since, a fact such as "fought beside you"); links to Named NPCs (the
  general profile), History › Memories and History › Conversations, filtered to them.
- **Status words**, from perception only (SP7): *Here now* (perceived now); *Alive when last met* (with where and
  when); *Away* (the Tarnished came within speaking range of where they were last met and did not perceive them);
  *Fell* (the Tarnished perceived their death). Never from a flag alone, so a death the Tarnished did not see reads
  "Alive when last met".
- **Data.** Reads `playthrough`, `world`. Writes nothing. An earlier journey chosen in the top bar shows read-only.
- **Rules.** Only people met on this journey (SP4, SP5). A story line only for the stage active at the last meeting
  (EldenNet's `stage_seen`), never a later or unseen one; a related story only for a met person the stage's own line
  links; never a stage id, road, gist, track, knowledge entry, next step, "has something for you" mark, fate or count
  of what is left (SP1, SP5). Lines pass the persona's lock set (SP2). No chat or model text (PV4); lines and notes are
  never searched (§4.3). At the title screen: "Load a character to see this".
- **Keys and pad.** ↑↓ the table, → or A into the detail.
- **Related.** Named NPCs, History › Memories, History › World.

**Named NPCs** (`characters.named`; 0.3 for the pilot, 0.5 for every persona).

- **Purpose.** Every named character EldenNet voices: who they appear to be, and what each may do. The same in every
  playthrough (D64).
- **Layout.** Two panes. Left: search (names as shown, or first-meeting names) and the chips Met · Nearby · All; the
  whole roster from the start (G8), by the name the game shows or, until it shows another, the first-meeting name
  (the pre-reveal fallback, SP3); then other named NPCs once seen (SP4), marked "no lines in this version". Met and
  Nearby follow the journey in view and are absent at the title screen. Right, tabs: **Profile** (the name, with the
  veiled look for a perceived character whose name the game has not shown, SP5; the **general profile**: who they
  appear to be on a first meeting, from `public.summary`, which passes the Stranger Test, EldenNet NPC §3.1; the news
  temperament in words; for a met character, an "On this journey" line with the status word and a link to their row
  on Playthrough); **Allowances** (the slot's row of `allow."persona:…"`, as shortcuts, with inherited values faint
  and their source); **Reactions** (this slot's per-reaction tri-states, `allow."persona:…".reactions.<id>`, as
  shortcuts into NPC types; cooldowns are the same for every NPC and live on Behaviour › Reactions). Every character,
  met or not, opens on Profile.
- **Data.** Reads `named`, `named/<id>`, `kinds`. Writes `settings.set` (shortcut rows).
- **Rules.** A general profile holds first-meeting knowledge only (G8): never a stage, fate, later form, reveal, quest
  step or relation the story discloses later, nor a road, gist, private field, tie, forbid list, beckon, knowledge
  entry or quest track (SP1). No boss or invader in the roster until the game names them (SP4).
- **Keys and pad.** ↑↓ the list, → or A into the detail, LB/RB the tabs.
- **Related** (per character): Playthrough (their row), History › Memories and History › Conversations filtered to
  them, NPC types.

**NPC types** (`characters.types`; 0.4 in the browser, 0.6 in game).

- **Purpose.** Every type of NPC, labelled and tagged, and what each may do (O38). The home of every allowance.
- **Toolbar.** Search · kind chips · Reset page.
- **Master.** Every kind and type from the start (G5), in EldenNet DESIGN §7.6's order: each kind's own row ("Named,
  every NPC", "Humanoid, every type", …; for a kind whose only type is the kind itself, such as boss, invader or
  mount, that row is the type), followed by its slots or types (archetype cards are the humanoid rows), then a locked
  "unknown" row. Each row: our label, tag chips (hollow while a tag rests on an unverified lead), how many the
  Tarnished perceives near now (SP7), and summary marks for Speech, Unasked and Battle.
- **Detail.** The type's general profile (what anyone would see on meeting one, in our own words, G5), then
  allowances as tri-states (inherit, on, off) showing the inherited value and its source, which is a link of EldenNet
  DESIGN §7.6's chain (named slot, type, live kind, base kind, the shipped default); a source with a row of its own
  here opens that row, on this page or from Named NPCs. A reaction's own switch on Behaviour › Reactions is a separate
  gate (ARCH §11.5), never shown as a source; a row it turns off says so in a faint second line. Grouped: Speech
  (`dialogue`; `tts` "takes effect in v1.5"), Unasked (`greet_player`, `npc_to_npc`, `autonomy`, `news`,
  `stage_talk`), Reactions (per reaction id, and `status_reactions`, which gates the four Afflictions reactions),
  Battle (`combat`, `boss_fight`), Awareness (`self_awareness`, `status_awareness`), Acts (`actions`, `gestures`,
  locked "later version", O41). `dialogue` off greys the rest. A card's detail adds its voice source and pool state; a
  type with no voice source says "no lines in this version".
- **Data.** Reads `kinds`, `settings`. Writes `settings.set`, `settings.reset`.
- **Rules.** Every type from the start (G5), each described in first-meeting words; no boss names; tags never name a
  place or a quest. Near counts follow SP7.
- **Keys and pad.** Master–detail (D36); ← → cycles a tri-state; X resets the row. Wide browser windows add a grid
  view.
- **Related.** Playthrough, Named NPCs, Behaviour › Reactions, Behaviour › Combat.

#### 4.6.5 History

Every History page shows the journey chosen in the top bar; earlier journeys are read-only. Nothing in History is
editable but Forget (D13). Before a playthrough is loaded, each page says "Load a character to see this".

**Events** (`history.events`; 0.4).

- **Purpose.** What the Tarnished saw happen on this journey, as EldenNet recorded it.
- **Tiles.** Events this journey · This session · Lines shown.
- **Filter bar.** Family (fights, statuses, speech, greetings and meetings, world) · who (names as shown, or type
  labels) · region (regions entered) · sort · direction. No free-text search.
- **Table.** Play time, the event in our label, a family pill, subject and other (SP3), region, a confidence word
  (certain, likely), who heard it (met NPCs, from the event's audience).
- **Data.** `history.page` with `view: events` (§6.6).
- **Rules.** Only events the Tarnished perceived (SP7; EldenNet's `seen` bit, eldennet-changes §2); plumbing events
  (flag and stage changes, readiness, read groups) are never listed; every text passes the lock check (SP2).
- **Related.** Tools › Event monitor, Conversations, Behaviour › Reactions.

**Conversations** (`history.conversations`; 0.3).

- **Purpose.** Every conversation with a named NPC, as it was shown.
- **Layout.** A list (the NPC as shown, when, how many lines, how it ended) and the transcript of the chosen one: the
  Tarnished's typed lines and the NPC's displayed lines, nothing unshown.
- **Data.** `history.page` with `view: conversations`; `conversation/<id>`.
- **Rules.** Lines pass the persona's lock set (SP2). Chat text on screen only (PV4): never in search, a title or a URL;
  ids are opaque numbers.
- **Related.** Memories, Characters › Named NPCs.

**Memories** (`history.memories`; 0.3).

- **Purpose.** What each named NPC remembers of the Tarnished, and the means to make them forget.
- **Header.** `features.memory` (Named NPCs remember), the page's own switch and its home (D45).
- **Tiles.** NPCs met · Keepsakes · Memory work waiting.
- **Filter bar.** NPC · basis (seen, you told them) · sort (time, weight).
- **NPC card**, with one NPC chosen: the journal's summary, the disposition word, the name the Tarnished told, when the
  journal was last written, and the facts in words, from MEM §2.2's rendered keys only (times met, where you first
  met, seen you pass by, fought beside you, saw you fall, times struck). Facts are counters EldenNet keeps, with no
  weight and no text of their own. Never the `stage_seen`, `world_seen`, `world_seen_pt`, `world_cued`, `spoke_pt` or
  `verified:` keys (SP1); "fought beside you" without the boss's name (SP4); a place by its runtime name, and only a
  region entered (SP3).
- **Table** (keepsakes only, at most 8 per NPC, MEM §2.3). NPC, when, the keepsake, weight (a number and a word),
  basis ("Seen", or "You told them" for a claim, MEM §2.5), about (you, themselves, the world), and a mark on the one
  protected keepsake (the heaviest about you with weight 0.7 or more, MEM §2.3). A name the Tarnished told is shown on
  the NPC card, never as a row (MEM §2.5 drops it as a duplicate).
- **Toolbar.** Forget this NPC (with an NPC chosen) · Forget this journey (in `rot`, at the right). Both name exactly
  what goes and what stays, take a backup and need a deliberate second press; in game, a danger close cancels them
  (MEM §9.5).
- **Sections.** **Memory work** (waiting, running, the last result in words) · **Backups** (count and times, no
  paths). A note: "Diaries and scene memories: later version."
- **Data.** Reads `memories`, `journal/<id>`. Writes `settings.set`, `memory.forget_npc`, `memory.forget_journey`.
- **Rules.** Journals and keepsakes pass the persona's own lock set (SP2). Model text on screen only (PV4); no paths.
- **Keys and pad.** Forget: A, then a second A on the confirmation's focused "Forget" after 1 s.
- **Related.** Conversations, Characters › Named NPCs, Settings › Developer.

**World** (`history.world`; 0.5).

- **Purpose.** What has changed in the world, as far as the Tarnished has seen, and who has heard (O28–O30).
- **Contents.** The world stages that hold by verified flags only (SP6), each by its impersonal `line`, in rank order;
  for each, the met NPCs who have heard, with a certainty word. No counts, locked rows, "x of n", "not yet", quest
  tracks or NPC stages (D11). EldenNet's knowledge base itself is never shown (SP1).
- **Data.** Reads `world`. Shown unless `ui.menu.show_world = false`.
- **Related.** Characters › Named NPCs, Behaviour › Speaking up.

#### 4.6.6 Behaviour

**Conversation** (`behaviour.conversation`; 0.2 in the file, 0.6 for the call-out and the page in game).

- **Purpose.** Talking with named NPCs, calling out to others, and how faithful lines are.
- **Sections.** **Conversation**: `features.chat`, with one sentence. **Call-out**: `features.callout`. **Content
  safety**: `safety.level` (faithful, gentle; O17), with "Safety and spoiler rules always apply". **Keys**: talk and
  skip page (shortcuts), reach (`range.talk_m`, a shortcut), following the game's subtitles (a shortcut). **Notes**:
  typing only at safe moments (O9), the Steam keyboard on a pad (O11), names only once told (O18), lines to the
  Tarnished wait while this menu is open (D34). A later-version line: choosing from a list of intents on a pad (v1.1).
- **Data.** Reads `settings`. Writes `settings.set`.
- **Related.** Settings › Hotkeys, Speaking up.

**Speaking up** (`behaviour.speaking`; 0.3 for named NPCs, 0.4 for all).

- **Purpose.** Everything about NPCs starting to speak on their own (O32, O34, O37, O39).
- **Sections.** **Who speaks unasked**: `features.speak_up` (named NPCs) and `features.barks` (other NPCs), homed here,
  and talk per window (`pacing.talk_max`, a shortcut). Which types may speak unasked is set per type on NPC types and
  per NPC on Named NPCs, not here. **Greetings**: the `greet_player` row (a shortcut); in v1 only named NPCs greet the
  Tarnished, because the cards are hostile. **Quest business**: the `stage_talk` row (a shortcut) and one readiness word
  per met named NPC (ready; waits for a game check on your build; not in this version); unmet personas show nothing here
  (SP5); never a stage, never a "has something for you" mark. **News**: the `news` row (a shortcut) and one
  sentence on when news is raised. **Between NPCs**: `features.npc_to_npc` (homed here), the `greet_npc` and `answer`
  rows (shortcuts) and the overheard-lines range (`range.hear_m`, a shortcut); exchanges end by themselves.
  **Autonomy**: `autonomy.enabled` and `autonomy.period_s` (homed here), the bound in words, the free-preset note.
  **Calm remarks**: the `idle`, `saw_kill` and `saw_victory` rows and `pacing.idle_max` (shortcuts). Every reaction
  shortcut here is drawn as on Reactions (principle 1): its switch and its cooldown slider (0–6 h of play), each help
  line ending with ARCH §11.5's default ("Default: on, 10 min").
- **Data.** Reads `settings`, `named` (readiness). Writes `settings.set`.
- **Related.** Reactions, Pacing, Characters › NPC types, AI › Director (WIP).

**Combat** (`behaviour.combat`; 0.4).

- **Purpose.** Combat lines for every combatant, either side, boss fights included (O42). A hub: every row is a
  shortcut to its home (D47).
- **Sections.** **Who speaks**: `features.barks` (a shortcut) · a kinds × {combat, boss fight} matrix (shortcuts
  into NPC types) · **Scripted-line holds** per kind
  (plays; held until its check passes on your build, S3; no check needed) · the In battle reaction rows · combat
  pacing (`pacing.group_gap_s`, `pacing.fight_max`, `range.fight_m`) · **Lines from**: written lines and prepared
  pools per card, with `pools.max_jobs_per_session` · labels or feed.
- **Data.** Reads `kinds`, `settings`, `health` (the pools' state). Writes through shortcuts only.
- **Rules.** Every card is listed (G5); no boss names; pool lines never listed before they play.
- **Related.** Reactions, Pacing, Characters › NPC types, AI › Models.

**Awareness** (`behaviour.awareness`; 0.4, each status as S8 passes).

- **Purpose.** What NPCs know of themselves and of statuses (O35, O40).
- **Sections.** **Self-awareness**: `features.self_awareness` (their own recent lines, and how they are now).
  **Status awareness**: `features.status_awareness`; the seven statuses in our words (frost, poison, rot, bleeding,
  madness, sleep, blight), each "live" or "waits on the status check" (S8); the Afflictions reaction rows and the
  `status_reactions` and awareness allowances per kind (shortcuts). A later-version line: fleeing, healing, gestures
  and warnings while a status builds (O41).
- **Data.** Reads `settings`, `kinds`. Writes `settings.set`.
- **Related.** Reactions, Characters › NPC types, Behaviour › Acts (WIP).

**Reactions** (`behaviour.reactions`; 0.4).

- **Purpose.** Every kind of event NPCs may react to, with its switch and cooldown. The home of all twenty
  `reactions.<id>` rows.
- **Filter chips** (families): In battle (`engaged`, `struck_hard`, `low_hp`, `ally_fell`, `foe_fell`, `player_fell`,
  `foe_fled`, `foe_healed`) · Afflictions (`status_self`, `status_plea`, `status_warn`, `status_foe`) · To you
  (`greet_player`, `stage_talk`, `news`) · Between NPCs (`greet_npc`, `answer`) · Calm remarks (`idle`, `saw_kill`,
  `saw_victory`). Autonomy is not here: its switch and period live in Speaking up.
- **Table** (editable cells): label · On · Cooldown (a slider with units, 0–6 h of play) · scope in words · lines from
  (written, prepared, live, with a cost mark) · availability · this session (played, refused) · overrides ("n types",
  a link to NPC types).
- **Foot.** A locked row: "Your own event rules: later version" (`augur.allow_custom_rules`).
- **Data.** Reads `settings`, `monitor.decisions` (session counts). Writes `settings.set`, `settings.reset`.
- **Keys and pad.** ↑↓ rows, → across cells, ← → on a cooldown changes it, Shift for ten steps.
- **Related.** Tools › Event monitor, History › Events, Characters › NPC types.

**Pacing** (`behaviour.pacing`; 0.4, tuned by testers, EldenNet X2).

- **Purpose.** How often anyone speaks unasked, and how far lines reach.
- **Header actions.** Presets Quiet · Default · Lively (D20; each writes the six pacing keys).
- **Sections.** **Gaps**: `pacing.gap_s`, `pacing.group_gap_s`. **Window**: `pacing.window_s`, `pacing.fight_max`,
  `pacing.idle_max` (off, sparse, normal, custom), `pacing.talk_max`. **Reach**: `range.talk_m`, `range.hear_m`,
  `range.fight_m`. **What this means**: a computed sentence ("At most 6 lines a minute in a fight, …"). A note: replies
  to the Tarnished are never limited.
- **Data.** Reads `settings`. Writes `settings.set`.
- **Related.** Speaking up, Combat, Reactions.

**Acts** (`behaviour.acts`; a later-version statement). NPC acts and gestures (fleeing, healing, giving, gestures) are
a later version (O41, EldenNet DESIGN §13); v1 is dialogue only and never writes to the game. The `actions` and
`gestures` allowances show locked on NPC types. No controls.

#### 4.6.7 System

**Status** (`system.status`; 0.2 for Health, the rest as their sources exist; usable at L1).

- **Purpose.** Is EldenNet healthy on this machine and build, and if not, why?
- **Tabs.** **Health**: the level and its reason; the game build and layout profile, supported or not; the eleven read
  groups with their self-test results and breaker trips; the capability matrix in words; the gate (offline, own
  world, lobby) in words. **Checks**: each feature waiting on a game check on this build, in player words, with the
  spike named for testers. **Compatibility**: supported game builds; how EldenNet was started (its launcher or another
  loader); GraceUI's installed version, the one this EldenNet came with, and its folder's check (§2.9); overlays and
  other hooks recognised at start-up by module name; Proton and Steam Deck notes; known pairs that do not work.
- **Data.** Reads `status`, `system.status`.
- **Rules.** Checks name features, never flags, stages or characters.
- **Related.** Performance, Logs, Docs › Troubleshooting.

**Performance** (`system.performance`; 0.2).

- **Tiles.** Frame cost with the menu open and closed (p99), the core step p99, memory.
- **Sections.** **Budgets** (C3's limits beside the figures) · **Tasks** (p99 per task) · **Storage** (the database's
  size, the backups' count and last time; no paths, no SteamID).
- **Data.** Reads `system.performance`.
- **Related.** Status, Traces.

**Traces** (`system.traces`; 0.2).

- **Purpose.** How long each line took, from its cause to the screen.
- **Tiles.** First page p50 and p95 against C5's targets · first token p50 · lines this session.
- **Table.** Time, kind (reply, own-initiative, call-out), slot, queue, first token, first page shown, total, verdict
  (quick, acceptable, slow). No text.
- **Data.** Reads `traces`.
- **Related.** AI › Requests, AI › Self-test, Settings › Developer (latency toasts).

**Logs** (`system.logs`; 0.2).

- **Contents.** Level tiles that filter (error, warning, info, debug, trace) · search within the scrubbed tail · the
  scrubbed log tail, newest first (500).
- **Data.** Reads `logs`.
- **Rules.** Scrubbed as reports are (PV5); no download of raw files (Bug report makes the shareable bundle).
- **Related.** Tools › Bug report, Settings › Developer (log level).

#### 4.6.8 Tools

**Event monitor** (`tools.monitor`; 0.4).

- **Purpose.** Watch events and EldenNet's decisions live: why a line did or did not play.
- **Header actions.** Pause view · Clear view (the view only).
- **Segmented switch.** *Events*: tiles (events a second, shown, total); a family filter; rows of time, event, family,
  subject and other (SP3), confidence, who perceived it, reactions requested; 500 rows in the browser, 100 in game.
  *Decisions*: each own-initiative request and its result (played, or refused by which rule, with the cooldown left)
  and its line source.
- **Data.** Reads `monitor.events`, `monitor.decisions`.
- **Rules.** SP7; plumbing events only in developer mode, under the Spoilers ribbon (SP8).
- **Related.** History › Events, Behaviour › Reactions.

**Bug report** (`tools.report`; 0.2 preview, 0.6 copy and save).

- **Contents.** What happened (free text, with "don't include personal details"); the bundle preview with every field
  visible and the optional parts switchable; copy or save only after EldenNet's deterministic scan passes.
- **Data.** Reads `report`. Writes `report.build`, `report.save`, `report.copy`.
- **Rules.** Never chat or model text, keys, SteamID, paths, the Windows user name, the character's name or local
  addresses (PV5, PV7).
- **Related.** System › Logs, Docs › Troubleshooting.

**Workshop** (`tools.workshop`; 0.2; the browser only, with `debug.panel` on).

- The last prompt and reply (keys redacted), every event unfiltered, the world view with raw verification states,
  each persona's known set and the cue that would fire now, unknown characters with "copy as TOML", recording
  controls, Augur's event kinds and schemas.
- A "contains spoilers" confirmation once a session, and the red Spoilers ribbon on every page while it is open
  (SP8).

#### 4.6.9 The foot

**Settings** (`settings`; 0.6 for all of it; Developer and Browser view readable in the browser from 0.2).

- **Purpose.** The home of the settings that belong to no category page, and an index of all the rest (D40).
- **Header actions.** Run setup again (First light) · Reset all (confirmed) · Copy settings (`settings.toml` holds no
  secret; PV7).
- **Tabs.** *Settings*: two columns. Left, a navigator card with a search box, then groups. Groups that are homes
  here: **General** (`enabled`, with "Safety and spoiler rules always apply"), **Hotkeys** (`hotkeys.talk`,
  `hotkeys.skip_page`, `hotkeys.menu`, `hotkeys.debug`; capture, conflicts, reset; pad chords only if S4 finds one,
  D33), **Menu** (`ui.menu.theme`, `plain_text`, `reduce_motion`, `key_hints`, `pad_glyphs`, `backdrop`,
  `compact_rows`, `show_world`; a shortcut to the text size; a note that the browser view is the accessible surface,
  §3.12), **Browser view** (`dashboard.enabled`, `dashboard.port`; the address with Copy address and Open in browser;
  the server's state; browsers connected, with Disconnect browsers (in game or on this PC); G4; `dashboard.lan` and,
  while it is on, the network address and Pair a device (in game only); the paired devices with Forget (in game or on
  this PC), listed whether it is on or off; G9),
  **Developer** (`debug.panel`, `debug.record_sessions`, `debug.record_redact_text`, `debug.log_level`,
  `ui.latency_toasts`). Groups that link out, each sub-item with an
  outward arrow and no rows: AI (Providers, Models), Voice (Text and subtitles), Characters (NPC types), History
  (Memories), Behaviour (Conversation, Speaking up, Awareness, Reactions, Pacing). Right, the chosen group's sections.
  *All settings*: every setting, grouped by category and page, each row a shortcut marked "lives in …"; filters for
  changed from default and for availability (live, waits on a game check, v1.5, later, off because …).
- **Data.** Reads `settings`. Writes `settings.set`, `settings.reset`.
- **Keys and pad.** The navigator and the content are two scopes; → moves into the content; Enter on a link-out item
  opens its page.

**Docs** (`docs`; 0.2 in the browser, 0.6 in game).

- **Purpose.** EldenNet's documentation, bundled and offline (PV6, D46).
- **Documents.** Getting started · Installing and starting EldenNet, and updating GraceUI · What is sent to your
  provider (the AI disclosure) · Privacy · Spoilers: how EldenNet keeps them · Keys and pad · Troubleshooting (which
  names EldenNet's command-line tools) · Writing content (personas, cards, overrides, event schemas) · Licences and
  credits (`NOTICE`) · About (EldenNet's and GraceUI's versions, build).
- **Layout.** The document list at the left, the reader at the right; in game, plain styled text.
- **Rules.** Written to name no late-game person, place or event, and reviewed like content. Search indexes titles and
  headings only.

**Content packs** (`content`; 0.2).

- **Purpose.** What content EldenNet is running: the shipped pack and the player's own overrides, with no hub and no
  downloads (D46).
- **Sections.** **Shipped content** (name, version, credit; persona and card counts, since the cast is published;
  knowledge and rules as "included", with no counts, SP5) · **Your overrides** (relative paths under `overrides/`,
  each loaded or refused, with file and line) · **Turned off** (`disable.toml`: personas and cards by id; knowledge
  entries, triggers and rules as counts) · **Reloads** (time, layer, result).
- **Data.** Reads `content`. Writes nothing in v1 (no content editors, D2).
- **Rules.** Relative paths only (PV5).

#### 4.6.10 Outside the rail

- **First light** (D16): notices (offline only, save backups, data sent to the provider, the AI disclosure) → status
  → provider (a masked key or a local probe) → model preset → self-test with its verdict (EldenNet's checks 4a–4e) →
  optional prepared lines → text size (G6) and the browser view (its address, and the switch to turn it off; G4) →
  done, with a checklist and "Open Docs › Getting started". It opens at the title screen on first run (in the browser
  too, if the player opens the dashboard first), and again from Home › Setup and Settings.
- **Modals**: the Forget confirmations, Reset page or Reset all, key capture, the Workshop spoiler confirmation.
- **Search** (§4.3) is a modal scope over the page.

### 4.7 The settings row

- The label and the control; the changed-from-default lozenge and reset; a help line that opens on focus and ends with
  the default ("Default: 2.5 s"); the effect timing as a faint word; an availability badge; on a shortcut row, a
  "lives in …" link.
- Availability badges, in player words (the tester detail is on System › Status › Checks): none (live); "Waits on a
  game check" (its spike has not passed on this build; the value is kept and does nothing yet); "Takes effect in
  v1.5"; "Later version" (locked); "No lines in this version" (an inert type); "Off because …" (a dependency is off;
  the row dims and stays).
- EldenNet's always-on rules (ARCH §11.6) are stated once per page where they matter ("Safety and spoiler rules always
  apply"), never drawn as disabled toggles.
- Every row is generated from EldenNet's settings description (§6.9). GraceUI owns only grouping and order.

### 4.8 Spoiler rules (from IA §6; binding on both views)

- **SP1. Projections, not raw content.** The core never sends the UI flag ids, catalogue aliases, notes, stage ids,
  roads, gists, private fields, forbid lists, knows, beckons, ties, heard lines, knowledge text, prompts or model text
  that was not displayed. The only authored texts shown are written to be impersonal: `public.summary` (the roster's
  general profiles, G5, G8), a world stage's `line`, a persona stage's public line for the stage seen (Playthrough),
  our labels and our help. Workshop is the one exception (SP8).
- **SP2. The lock filter.** Every content or model string passes EldenNet's per-sentence lock check at projection
  time: the Tarnished's set for global strings, the persona's own set for its journal and lines. A failing item is
  dropped whole and counted on System › Status.
- **SP3. Names as the game shows them.** On-screen names come from the runtime name of the current row, or the
  first-meeting name (the pre-reveal fallback); a veiled persona stays veiled; regions by the names of regions
  entered. GraceUI stores no name.
- **SP4. Seen before listed.** Named NPCs without a shipped general profile appear once seen; regions once entered;
  bosses and invaders never by name before the game names them, and bosses never outside a live bar. The roster is the
  exception: every NPC type and every named NPC with a general profile is listed from the start (G5, G8).
- **SP5. Absence is silent.** No counts, progress, placeholders, greyed rows or "not yet" for anything not reached.
  (A later-version statement page names a feature of EldenNet, never a part of the game, so it is not covered. The
  roster's first-meeting profiles are the owner's one exception, G5 and G8; Playthrough follows this rule in full.)
- **SP6. Strict mode.** An unverified flag never unlocks anything a page shows.
- **SP7. Perception.** Live views and History › Events show only characters the Tarnished perceives, events whose
  audience includes the Tarnished, and lines that were displayed: never hidden characters, a boss before its bar, an
  invader before the game announces it, HP numbers or distances to hostiles.
- **SP8. Developer views behind a gate**: Workshop and F11 only, with `debug.panel` on, a confirmation each session
  and a visible ribbon.
- **SP9. Search and titles** carry no story or chat text; nor do URLs.

### 4.9 Privacy rules (from IA §7)

- **PV1. This PC only**, unless the player switches on the home network, and then only devices paired with a code
  shown in game (G9): Host, Origin and Fetch Metadata checks and a session cookie on this PC or a device cookie on a
  paired device (§7.2); no remote access.
- **PV2. Write-only secrets.** Keys go in and never come out. A provider's base URL is shown on AI › Providers only.
  The character's name shows only in the top bar's Journey control and on pages about the player's own story.
- **PV3. GraceUI keeps nothing.** No page content in any log line GraceUI hands EldenNet, in browser storage or in
  caches; the session cookie is set by the server, `HttpOnly`, and lasts until the browser closes; a paired device's
  cookie (G9) lasts until the player forgets the device, and EldenNet keeps only its hash; no console output
  in release builds; no analytics.
- **PV4. Chat and model text stay on screen**: only on Home's Lines and Model calls cards, AI › Requests (shown text
  only), AI › Self-test and First light (the test line), History › Conversations, History › Memories and Workshop;
  never in a log, report, URL, title, toast history or search.
- **PV5. Scrubbing.** System › Logs and the Bug report scrub SteamID64, absolute paths and
  the Windows user name, the character's name, and base-URL hosts that are not a known public provider, and, with
  `dashboard.lan` (G9), the PC's network addresses and the paired devices' names.
- **PV6. No outside requests.** Fonts, assets and Docs ship with GraceUI and EldenNet and are served from this
  PC; `default-src 'self'`; no CDN, no online wiki or add-on index.
- **PV7. Clipboard only on a click**, after a preview.
- **PV8. Recordings never capture the menu.** EldenNet's session recordings skip menu content.

### 4.10 Every EldenNet feature and its home

| EldenNet feature | Home | Also shown (read-only or a shortcut) | Version |
|---|---|---|---|
| Providers: cloud with the player's key, local servers, local probes | AI › Providers | Home › Setup, First light | 0.2 / 0.6 |
| Provider health and error classes | AI › Providers › Health | Home › Status (dots) | 0.2 |
| Model slots, fallbacks, fixed budgets | AI › Models | — | 0.2 / 0.6 |
| Model presets and EldenNet's advice | AI › Models | Home › Setup, AI › Providers (advice) | 0.2 |
| The reserved `meta` slot, typed decisions | AI › Models (a later-version card) | — | later |
| Self-test (in First light, and silent at start-up) | AI › Self-test | Home › Setup, First light | 0.2 |
| Model call log, usage, cost | AI › Requests | Home › Model calls | 0.2 |
| Prepared lines (pools), preparing them | AI › Models (Prepared lines) | Behaviour › Combat | 0.4 |
| A director (`Directed` class) | AI › Director (statement) | — | later |
| Text presentation: subtitles, labels or feed, scale, HDR, a preview | Voice › Text and subtitles | — | 0.4 / 0.6 |
| Text-to-speech (`SpeechSink`, the `tts` allowance) | Voice › Text-to-speech | NPC types (`tts`) | 1.5 |
| Speech-to-text and push-to-talk (`InputSource::Stt`) | Voice › Speech-to-text | — | 1.5 |
| Named NPCs (personas, other named NPCs), the roster's general profiles (G8) | Characters › Named NPCs | Home › Nearby | 0.3 / 0.5 |
| NPC types, kinds, tags, named slots, allowances | Characters › NPC types | Named NPCs, Combat, Awareness; Voice › Text-to-speech (`tts`, read-only) | 0.4 / 0.6 |
| Archetype cards | Characters › NPC types (the humanoid rows) | Combat (pool state) | 0.4 |
| The mute list, unknown characters | Characters › NPC types (the locked unknown row) | Workshop (curation) | 0.4 |
| The stored event log | History › Events | Home › Recent events | 0.4 |
| Live events and decisions (Augur's tap, the decision ring) | Tools › Event monitor | Reactions (session counts) | 0.4 |
| Short-lived events (TTL) | Home › Happening now | — | 0.4 |
| Conversations | History › Conversations | Home › Lines | 0.3 |
| Journals, keepsakes, facts, memory work, backups | History › Memories | — | 0.3 |
| Forget this NPC, Forget this journey | History › Memories | — | 0.3 |
| NG+ journeys | Top bar › Journey (scopes History, Playthrough, and Named NPCs' Met and Nearby chips) | — | 0.3 |
| This playthrough: people met, their status, their stories as seen, the world now (G5, G8) | Characters › Playthrough | Named NPCs (a line per met character), Home › Nearby | 0.3 / 0.5 |
| World stages and who has heard (quest and world awareness) | History › World | Characters › Playthrough (World now, has heard of) | 0.5 |
| Quest business readiness | Behaviour › Speaking up | — | 0.3 |
| The knowledge base | Never shown (SP1); its override files on Content packs | — | — |
| Conversation (chat), call-out, content safety | Behaviour › Conversation | Quick switches | 0.2 / 0.6 |
| Speaking up, greetings, news, NPC-to-NPC, autonomy, idle remarks | Behaviour › Speaking up | Quick switches | 0.3 / 0.4 |
| Combat dialogue, boss fights, scripted-line holds | Behaviour › Combat | — | 0.4 |
| Self-awareness, status awareness, the seven statuses | Behaviour › Awareness | Home › Nearby (statuses) | 0.4 |
| Reactions and cooldowns per event type | Behaviour › Reactions | Combat, Awareness, Speaking up | 0.4 |
| Pacing and reach, presets | Behaviour › Pacing | Combat, Speaking up, Conversation | 0.4 |
| Acts and gestures | Behaviour › Acts (statement) | NPC types (locked) | later |
| Your own event rules | Behaviour › Reactions (a locked row) | — | later |
| Thoughts (`npc_thought`), diaries, scene memories, `@name` addressing, the pad intent list | No page: a later-version line on Awareness, Memories and Conversation | — | later / 1.1 |
| Spoiler layers, safety rules, context gates | No setting; stated on Settings › General and Docs › Spoilers | — | — |
| The master switch | Settings › General | Quick switches | 0.6 |
| Pause AI for the session | Home's header | Quick switches | 0.2 |
| Hotkeys | Settings › Hotkeys | Conversation | 0.6 |
| The menu's own options, the theme | Settings › Menu | Top bar (theme) | 0.6 |
| The browser dashboard (always on, G4) | Settings › Browser view | Home › Status (the address), F11, the launcher's first screen | 0.2 / 0.6 |
| Developer options, latency toasts, recordings | Settings › Developer | Logs, Traces | 0.2 |
| Degradation ladder, read groups, capabilities, gate, unknown builds | System › Status › Health | Top bar lamp, Home › Status | 0.2 |
| Checks waiting on spikes | System › Status › Checks | Row badges everywhere | 0.2 |
| Compatibility (builds, loaders, overlays, Proton, Deck) | System › Status › Compatibility | Docs | 0.2 |
| GraceUI's own version, files and compatibility (G2) | System › Status › Compatibility | F11, Docs › About | 0.2 |
| Performance, storage | System › Performance | — | 0.2 |
| Latency traces | System › Traces | F11 | 0.2 |
| Logs | System › Logs | — | 0.2 |
| Bug report | Tools › Bug report | — | 0.2 / 0.6 |
| Developer views (prompts, raw world, curation, recordings) | Tools › Workshop | F11 | 0.2 |
| Content layers, overrides, `disable.toml`, reload results | Content packs | — | 0.2 |
| Documentation, the AI disclosure, licences | Docs | First light (disclosure) | 0.2 |
| The setup flow | First light (outside the rail) | Home › Setup, Settings | 0.6 |
| Toasts, banner, chat box, call-out box, rollback modal, F11 | EldenNet's overlay, not GraceUI (§1.3) | — | — |
| Command-line tools (`en-tools replay`, `eval`, `selftest`, `probe-exe`) | Not in the UI; Docs › Troubleshooting names them | — | — |

### 4.11 SkyrimNet pages with no EldenNet counterpart

"Adapted" means the idea lives on in an EldenNet page; the rest are later, not planned or never. Later-version items
with a page of their own show as statements (G7); everything else is simply absent. The evidence is SND §5.

| SkyrimNet page or settings group | EldenNet | Why |
|---|---|---|
| AI › Vision (screenshots described by a model; a gallery) | Not planned | No EldenNet DESIGN entry; sending screen captures to a provider is a privacy question; heavy |
| AI › GameMaster | Later: AI › Director (statement) | EldenNet DESIGN §13, the `Directed` class |
| AI › Providers › Routing (rotation, failover, a first-token guard) | Adapted and simplified: one fallback per slot on Models; health on Providers | SN01 §14.2 |
| Request styles per provider, decision providers | Later (the `meta` slot, `DecisionRequest`) | EldenNet DESIGN §11.2, §13 |
| A hosted first-party account, sign-in, a community link | Never | No accounts; PV rules |
| Voice › Bard Singing | Not planned | SN01 §14.3 |
| Voice › Samples (cloning) | Never for the cast's voices | EldenNet DESIGN §1.2, Never column |
| Voice › Effects | Not planned (could follow text-to-speech) | SN01 §14.3 |
| Voice › Text-to-Speech, Speech-to-Text | v1.5 (statement pages in v1) | O8 |
| Settings › Audio & Voice: mixing, volumes, audible distance, spatial audio | v1.5, designed with text-to-speech (EldenNet's `SpeechSink`); nothing in v1 | O8; EldenNet DESIGN §12 |
| Settings › Audio & Voice: the player's voice and its placement, narration | Not planned (the player character is unvoiced; EldenNet voices NPC lines only) | SN06 §7.3 (the Tarnished is a silent protagonist); O8 |
| Settings › Audio & Voice: voicing the game's own lines | Not applicable (the game voices its own lines) | — |
| Settings › Audio & Voice: lip-sync helpers | Later (v2: native subtitles and mouth movement, S9); none in v1 | EldenNet DESIGN §1.2, §12 (ER03 §5) |
| Characters: generated profiles, profile updates | Not planned: the journal is the evolving layer; uncurated NPCs use their card or stay mute | EldenNet DESIGN §7.1, §10.3 |
| Characters: virtual entities | Not planned | SN01 §14.3 |
| Home: latest screenshot, profile and diary shortcuts, pinned characters with teleport | Not planned; teleport never (a game write) | EldenNet DESIGN §0 item 3 |
| History › Diaries | Later | EldenNet DESIGN §1.2, Deferred |
| History › World Knowledge (entries, packs, a knowledge check, an assistant) | The knowledge base is never shown (SP1); its files on Content packs; no knowledge check (Workshop shows the known set) | Spoilers |
| History › Places | Adapted: the region filter on History › Events | Regions are context only |
| History › Events: creating, editing and deleting events; the schema explorer | Never for edits (D13); schemas in Docs › Writing content and Workshop | Validation; spoilers |
| History › Memories: creating, editing, generating, testing recall | Never for edits; recall later behind `trait Recall` | D13; EldenNet DESIGN §13 |
| Behaviour › Prompts (editor, backups, preview, assistant) | Not in the UI (SP1, invariant 1); prompts are files with overrides | Prompts carry spoilers |
| Behaviour › Actions (library, eligibility, tags) | Later: Behaviour › Acts (statement) | O41 |
| Behaviour › Triggers | Adapted: Behaviour › Reactions; player rule files later | O33; `augur.allow_custom_rules` |
| Behaviour › Universal Translator | Not planned | English only (O16) |
| System › Packages (behaviour packages on actors) | Never | Zero game writes |
| Tools › File Editor | Not in v1 (D2); Workshop editors later | No content editors in v1 |
| Tools › Game Data Explorer | Never | Spoilers (SP rules) |
| Tools › AI Assistant (agents), the tool server | Not planned | SN01 §14.3; console commands would be game writes |
| Tools › cloud GPU rental | Not planned | SN01 §14.3 |
| Debug tools | Adapted: Workshop and F11 | SP8 |
| Wiki (fetched online) | Adapted: Docs, bundled offline | PV6 |
| Plugins (a hub, install, update) | Adapted: Content packs, read-only; no hub | SN01 §14.2 |
| Settings › per-character overrides | Adapted: Characters › NPC types | O38 |
| Settings › pending default changes | Adapted: the advice notice on AI › Models | `models.toml` is volatile |
| Settings › Records (item, spell and furniture names) | Never | Names come from the player's install at run time |
| Settings › Dialogue Actions (the game's own lines) | Later (v2: native lines) | EldenNet DESIGN §13 |
| Settings › NPC thoughts | Later (`npc_thought`) | EldenNet DESIGN §13 |
| Settings › NPC groups, identity links, actor filter lists | Not planned; types and tags cover them (allowances narrow by type) | EldenNet DESIGN §7.6 |
| Settings › dialogue, memory and speech text filters | Not planned (EldenNet's sanitiser and lint are fixed; the safety level) | NPC §8 |
| Settings › Memory (embeddings, scoring, re-indexing) | Not applicable in v1 (the journal is sent whole); later with recall | EldenNet DESIGN §10.1 |
| Settings › player reactions, whisper mode, continuous scenes | Not planned (the Tarnished is silent); scenes adapted as NPC-to-NPC exchanges within pacing | SN06 §7.3; O34 |
| Settings › General: usage data, automatic content updates | Never (no telemetry; no request but the provider's) | EldenNet DESIGN §1.2 |
| Settings › Dashboard: language, opening on the targeted NPC, theme presets | English only (O16); a crosshair jump is a possible later nicety; two Grace variants | — |
| Settings › Dashboard › Web server | Adapted: Settings › Browser view (always on as SkyrimNet's; loopback with a session cookie; other devices only once switched on and paired, G9) | G4, G9, D58–D60 |
| Settings › Developer: the tool server, agents, analytics | Not planned; log level and recordings in Settings › Developer | — |
| The setup tutorial, the feature guide | Adapted: First light; Docs › Getting started | D16 |

### 4.12 From the first proposal

The first proposal's flavour-named sections map to the new pages so:

| First proposal | Now |
|---|---|
| Grace (Home) | Home |
| The Named | Characters › Named NPCs (its Memories and Conversations tabs move to History) |
| Kinds | Characters › NPC types |
| Pacing | Behaviour › Pacing |
| Reactions | Behaviour › Reactions |
| Beckoning | Behaviour › Speaking up (self-awareness moves to Awareness) |
| War cries | Behaviour › Combat |
| Afflictions | Behaviour › Awareness |
| Parley | Behaviour › Conversation |
| Tidings | History › World |
| Remembrance | History › Memories and History › Conversations |
| Augury | Tools › Event monitor (and History › Events for the stored log) |
| Oracles | AI › Providers, Models, Self-test, Requests |
| Voice | Voice › Text-to-speech, Speech-to-text |
| Lettering | Voice › Text and subtitles; Settings › Menu; Settings › Browser view |
| Bindings | Settings › Hotkeys |
| Vigil | System › Status, Performance, Traces, Logs; Content packs; Settings › Developer |
| Missive | Tools › Bug report |
| Workshop | Tools › Workshop |
| All settings | Settings › All settings |
| First light | First light |

---

## 5. Input, focus and capture (in game)

The menu captures input exactly as EldenNet's chat box does (EldenNet DESIGN §8.1), with the additions ML §6 draws
from Magelight's field reports. The browser view is an ordinary web page (§5.9).

### 5.1 Opening and closing

- **Key.** `hotkeys.menu`, default **F5**, rebindable in Settings › Hotkeys, stored by name. EldenNet's S4 spike
  confirms that F5 is free in the game's default layout. The key is always masked from the game. F10 no longer opens
  anything; a schema migration removes `hotkeys.settings` (D6).
- **When it opens.** Only when EldenNet's `safe_context` holds (alive, not riding, not in a boss fight or combat, no
  engaged hostile within 25 m and none at all within 12 m, the HUD in its default state; sheltered places waive the
  distance checks) and no game menu or text entry is open, read through EldenNet's `ui` group and failing closed when
  unknown; or at the title screen. Otherwise F5 shows the status toast "Not now: you are in danger" (or the reason)
  and nothing opens. It never opens while the chat box or call-out box holds input.
- **First open of a session.** Projections are built when the menu opens, so the first frame has data; the fonts are
  already in the atlas; nothing is uploaded (U4).
- **One close routine** for every exit path. It releases capture, masks every key and button still held until it is
  released, cancels an open confirmation, keeps every change already applied, drops the menu's subscriptions and
  remembers the page and focused row for the next open in this session.

| Exit path | Fade | Notes |
|---|---|---|
| F5 again, Esc or B on the top level, the close button | 200 ms | Esc and B close a modal or a drawer first, then go back up a level, then close |
| Danger: `safe_context` stops holding (NPC §5.4) | 0 ms | Toast "The moment has passed." as for the chat box; text in a search or key field is dropped |
| Loading screen, death, quit to title, fast travel, multiplayer gate | 0 ms | The title screen may open it again |
| Focus loss, Alt+Tab | 0 ms | Key-ups sent for everything seen held |
| No overlay render for 2 s | — | EldenNet's existing release rule |
| A fault in the menu or overlay | 0 ms | Menu off for the session (§2.8) |

### 5.2 Capture

- **Capture stays EldenNet's.** `graceui.dll` only says what it wants (keyboard, mouse, keys and buttons to mask until
  release); EldenNet's capture rules decide (§2.9).
- **EldenNet's rules** (EldenNet DESIGN §8.1): one capture atomic shared with the chat box; DirectInput keyboard and
  mouse buffers zeroed; XInput neutral; ImGui draws its own cursor; keys held at close masked until released; released
  on Esc, focus loss, 2 s without a render, danger, loading and any core fault.
- **Additions** (ML lesson 8–9, for S4 to verify):
  - buffered DirectInput reads are neutralised too, if the game uses them, so typed keys never replay after close;
  - the closing press never reaches the game: Esc would open the game's menu, and B would dodge;
  - the press that opened the menu does nothing inside it until it is released;
  - a lost-keys fallback: if no key messages arrive, keys seen through DirectInput feed the menu, and the log says
    where keyboard focus was;
  - the OS cursor is confined to the window while the menu holds the mouse, and the previous clip is restored on close
    and on focus loss; its visibility is put back as it was, never forced.
- **Other mods' polled hotkeys** may still fire on letters typed into a search field. EldenNet does not hook user32
  process-wide to stop them; the compatibility notes document it.

### 5.3 Keyboard

| Key | Action |
|---|---|
| ↑ ↓ | Move between rows and list entries; in the rail, through the categories and the open category's pages |
| ← → | Change the focused value (slider by one step, segmented choice, tri-state). In the rail, → moves into the page; ← at the page's left edge goes back to the rail |
| Shift + ← → | Slider by ten steps |
| Tab, Shift+Tab | Next and previous control |
| Enter, Space | Activate; on a category, open it (accordion) and go to its first page; on a section label, open or close it; open a row's drawer; toggle a switch |
| Esc | Back: close a modal, a drawer, search or capture, then go up a level to the rail, then close the menu |
| Q, E | Previous and next tab |
| Page Up, Page Down | Previous and next page in rail order, crossing categories; later-version statements included |
| `/`, Ctrl+F | Search |
| R | Reset the focused row (a page or everything asks first) |
| F5 | Close the menu |

While a text field has focus, keys type into it; only Esc and F5 act, and only to leave the field or close (ML lesson
11). The key-hint footer always shows the keys for the focused control.

### 5.4 Pad

| Button | Action |
|---|---|
| D-pad, left stick | Move focus by geometry (`grace-view`'s navigation) |
| A | Activate, open, toggle; on a category, open it and go to its first page; on a section label, open or close it |
| B | Back: a modal or drawer first, then from the page's top level to the rail, then close; never reaches the game |
| LB, RB | Previous and next tab |
| LT, RT | Previous and next page in rail order, crossing categories |
| X | Reset the focused row (asks first for a page) |
| Y | Search (the Steam keyboard on a Deck or with Steam Input) |
| Start (Menu) | Quick switches; the press never reaches the game, like B |
| View | Toggle the key-hint footer |
| Right stick | Scroll the page |

- EldenNet passes XInput state to `graceui.dll` in each frame's input while the menu holds input; hudhook reads no
  pads (REN §4.1).
- Focus model: the rail, the top bar, the page, and the drawer or detail pane are four scopes. A modal is a scope
  that takes focus and gives it back to the control that opened it. In a table, ↑↓ moves through rows, A opens the
  row's drawer and → reaches the row's actions. Sliders and choices are edited in place with ← →.
- **Opening on a pad (D33).** No default chord in v1: every pad button already has a game function. Settings › Hotkeys
  can set one if S4 finds a chord that is free, as it does for the talk gesture (EldenNet O11). Players can map a spare
  button to F5 with Steam Input; on the Deck a back button is the natural choice. The documentation says how.
- **Glyphs**: our own neutral drawings, following the last device used (§3.8). Pad mode forces 44 px rows and shows
  row actions for the focused row.

### 5.5 Mouse and cursor

- ImGui's cursor, drawn by EldenNet's overlay in the shape `graceui.dll` asks for, so it never lags. Hover tints; the
  wheel scrolls; a held button's moves and release go to the control it was pressed on.
- Nothing is hover-only: every hover-revealed action is also in the detail pane or on the focused row.

### 5.6 Text entry

- **Fields**: search, a provider's base URL and headers, a masked API key, numeric entry, the bug report's free text;
  each is GraceUI's own single-line field (D57).
- **On a pad**: when a text field gains focus from a pad, EldenNet opens Steam's floating keyboard through
  `ShowFloatingGamepadTextInput` in the already-loaded `steam_api64.dll`, as for the chat box (EldenNet DESIGN §8.1,
  O11). The masked key field stays masked.
- **IME**: in v1 the OS composition window, positioned by EldenNet through the IMM functions at the caret rectangle
  `graceui.dll` reports (D57). Escape while composing belongs to the IME and never closes the menu. Drawing the
  composition inside the field, and our own candidate list, are later work. The IME under Proton is unproven and on
  the compatibility matrix.
- **Emoji and other characters outside the Basic Multilingual Plane**: hudhook 0.9.3 panics on a lone surrogate in
  `WM_CHAR` (REN §2.4). EldenNet takes `WM_CHAR` itself, pairs surrogates and passes the characters to
  `graceui.dll` (and to ImGui for the chat box); this is needed for the chat box from v0.2 regardless of GraceUI.

### 5.7 Focus and navigation model

- Opening restores the last page and row of this session; otherwise Home, with focus on its first card.
- Tab from the rail goes to the top bar, then into the page; Shift+Tab goes back the same way.
- Search is a modal scope over the page; Enter on a result opens the row's home page with the row focused and tinted.
- A refused or failed command returns focus to its control with the reason under it.
- `grace-view` holds this model as pure logic (focus scopes, the pad ledger, hotkey rules), so it is unit-tested
  without ImGui (§9.1).

### 5.8 While the menu is open

- **The game runs on.** Nothing pauses (EldenNet O9).
- **EldenNet's toasts and banner draw above the menu**, so danger and failures are always seen.
- **Lines addressed to the Tarnished wait** (D34): while the menu is open, EldenNet's scheduler treats the player as
  busy, as it does while the chat box is open, so greetings, quest business and news are not spent on a player who
  cannot see them. Barks and NPC-to-NPC lines continue and appear on Home's Lines card.
- **Live views stay live** at their rates (§6.5), so the player can watch the Event monitor explain a decision as it
  happens; the top bar's Live updates control holds them when a table must be read.

### 5.9 The browser view

- An ordinary page: no capture, the browser's own focus, the same keyboard map where the browser allows it (`/`,
  Q and E, Page Up and Page Down, Esc for modals). Tab order follows the visual order.
- Key capture on Settings › Hotkeys records `KeyboardEvent.code` and maps it to EldenNet's key names; keys a browser
  reserves for itself (F5 reloads the page, F11 is full screen) are captured only while the capture field holds focus,
  and a note says so.
- Gamepads in the browser are not supported in v1.

---

## 6. The bridge protocol

### 6.1 Model

- **Topics out, commands in.** EldenNet's core publishes topics (§6.5); a view subscribes to the ones its page needs.
  A view changes anything only by sending a command (§6.6), which the core validates and answers.
- **Two shapes of topic.** A **state** topic is a whole payload with a revision number, replaced on change (status,
  settings, a persona's page). A **stream** topic is a bounded ring of rows with sequence numbers, appended to (live
  activity, events, decisions, logs). There is no generic diffing or patching: state payloads are small, and streams
  only append. That keeps both ends simple and stable.
- **One set of types.** `grace-protocol` defines every payload, command and result in Rust; the TypeScript types are
  generated from it, and CI fails if they drift.
- **Display strings come ready.** The core formats what a player reads ("2.5 s", "badly hurt", "3 min ago") in the
  projection, so both views show identical words, and the in-game view never formats or allocates per frame.

### 6.2 Transports

| View | Transport | Encoding |
|---|---|---|
| In game | In process, across `graceui.dll`'s C ABI (§2.9): frames handed to `ingest` on T6; commands through the host's bounded ring with a non-blocking `send` | The same JSON frames as the WebSocket, UTF-8 (D55) |
| Browser | One WebSocket per tab to `grace-serve`, after the HTTP handshake of §7.2 | JSON text frames, UTF-8 |

`graceui.dll` and the web app come from GraceUI's folder, the core and the server from EldenNet's release; after a
player updates either they may differ, and the handshake of §6.4 settles it (§2.9). A browser may also hold an older
page.

### 6.3 Envelope and message kinds (WebSocket)

Every frame is one JSON object: `{"v": 1, "t": "<kind>", ...}`. `v` is the protocol's major version.

| Kind | Direction | Fields | Meaning |
|---|---|---|---|
| `hello` | page → EldenNet | `protocol` ("1.0"), `bundle` (the web app's or `graceui.dll`'s version), `caps`, `ticket` (§7.2; browser only) | First frame: within 2 s of connecting (browser), right after `init` (in game) |
| `welcome` | EldenNet → page | `protocol`, `caps`, `eldennet` (version and increment), `graceui`, `settings_schema`, `session` (an opaque id, never the cookie) | Handshake accepted |
| `sub`, `unsub` | page → EldenNet | `id`, `topic`, for streams `since` | Watch or stop watching a topic |
| `state` | EldenNet → page | `topic`, `rev`, `body` | A state topic's whole payload |
| `rows` | EldenNet → page | `topic`, `from_seq`, `rows`, `dropped` (rows lost to the ring) | Stream rows, in order |
| `cmd` | page → EldenNet | `id`, `name`, `args` | A command |
| `result` | EldenNet → page | `id`, `ok` or `error` (§6.7), optional `body` | The answer to one command |
| `error` | EldenNet → page | `code`, `detail` | A protocol-level error not tied to a command |
| `bye` | EldenNet → page | `reason`: `shutdown`, `superseded`, `unauthorised`, `version`, `fault` | EldenNet is closing the connection |
| `ping`, `pong` | both | — | Liveness every 15 s; two missed pongs close the socket |

### 6.4 Versioning

- **Protocol**: `MAJOR.MINOR`. A minor version adds optional fields, topics, commands or capabilities and never
  removes or changes one; unknown fields are ignored on both sides. A major version is a breaking change. Features are
  also announced by name in `caps`, so a page can ask "does this EldenNet have `monitor.decisions`?" (ML §3.15).
- **Handshake**: the same major is required. On a mismatch EldenNet answers `bye` with `version`; the page shows
  "This page needs updating: reload it" when a reload would fetch a matching web app, and otherwise names the GraceUI
  to install (§2.9). In game the menu stays off with the same words.
- **Settings schema**: `welcome` carries EldenNet's settings schema number and a hash of the settings description;
  the page reloads the description when either changes.
- **Caching**: the HTML is `no-store`; scripts, styles and fonts have content-hashed names and are cached for good.
- **Release tie**: `grace-protocol`'s version is in `graceui.toml` and in both projects' release notes; a major bump
  is a release item for both. v1 aims to keep protocol major 1 and ABI major 1 throughout.

### 6.5 Topics

| Topic | Shape | Contents (always projections, §4.8) | Rate | Pages |
|---|---|---|---|---|
| `status` | state | Level and reason, gate, safe-to-talk, pause, increment, journeys of the loaded playthrough, the dashboard's address and state | on change, ≤ 4 Hz | Top bar (lamp, journey), Home › Status |
| `health` | state | Provider and model health per slot; the prepared-line pools' state | on change | Home, AI › Providers, AI › Models, Behaviour › Combat |
| `nearby` | state | Perceived characters (SP7) | ≤ 2 Hz | Home › Nearby |
| `shortlived` *new* | state | Short-lived events the Tarnished perceives, with time left (SP7) | ≤ 2 Hz | Home › Happening now |
| `errors` *new* | state | The last 20 failures in words, each with its fix, scrubbed (PV5) | on change | Home › Problems |
| `activity` | stream, 200 | Displayed lines, conversation starts and ends, notices, reloads | as they happen | Home › Lines |
| `calls` | stream, 20 (200 in developer mode) | Model calls without text, plus the displayed text when shown | as they happen | Home › Model calls, AI › Requests |
| `usage` *new* | state | This session's calls, tokens, failures, cost where reported, own-initiative calls this hour against the bound | ≤ 1 Hz | AI › Requests, Home › Model calls |
| `selftest` *new* | state | The self-test's state, verdict and figures, this session's runs | on change | AI › Self-test, Home › Setup, First light |
| `settings` | state | Values, the settings description (§6.9), availability, changed flags | on change | Every page with settings rows, Quick switches, Settings |
| `providers` | state | `providers.toml`, `models.toml` presets, advice and budgets, key status (never a key) | on change | AI › Providers, AI › Models, Home › Setup |
| `named` | state | The roster (every named NPC with a general profile, by the name the game shows or the first-meeting name, at the title screen too; G8), facts in words (MEM §2.2's rendered keys only, SP1), quest-business readiness for met personas only (SP5) | on change | Characters › Named NPCs, Behaviour › Speaking up |
| `named/<id>` | state | One NPC's profile and allowances, its per-reaction allowances included | on change | Characters › Named NPCs |
| `playthrough` *new* | state | This journey's people met: the name as shown, the status word (from perception only), last met (a region entered, play time), the disposition word, the public line of the stage seen (`stage_seen`), related lines for met people only, up to three notes since the last conversation, and the world-now line (SP1–SP9) | on change | Characters › Playthrough |
| `memories` *new* | state | Met NPCs per journey of this playthrough, with keepsake counts and memory work | on change | History › Memories |
| `journal/<id>` | state | One NPC's journal, keepsakes and facts in words (MEM §2.2's rendered keys only; never `stage_seen`, `world_seen`, `world_seen_pt`, `world_cued`, `spoke_pt` or `verified:`) for every journey of this playthrough (earlier ones read-only), memory work | on change | History › Memories |
| `conversation/<id>` *new* | state | One conversation's displayed lines | on change | History › Conversations |
| `kinds` | state | Kinds, types, tags, the allowance resolution, near-now counts of perceived characters only (SP7), verification states, card pool state | ≤ 1 Hz | Characters › NPC types, Behaviour › Combat, Awareness; Voice › Text-to-speech (summary) |
| `world` (was `tidings`) | state | World stages that hold and who has heard | on change | History › World, Characters › Playthrough |
| `monitor.events` (was `augury.events`) | stream, 500 (100 in game) | Perceived events in our labels | batched, ≤ 4 Hz | Tools › Event monitor, Home › Recent events |
| `monitor.decisions` (was `augury.decisions`) | stream, 256 | Admission results with the refusing rule | batched, ≤ 4 Hz | Tools › Event monitor, Behaviour › Reactions |
| `system.status` *new* (from `vigil`) | state | Read groups, checks, capabilities, the gate, compatibility, GraceUI's installed and expected versions and its folder's check | ≤ 1 Hz | System › Status |
| `system.performance` *new* (from `vigil`) | state | Frame cost, task times, memory, storage (no paths) | ≤ 1 Hz | System › Performance |
| `traces` *new* (from `vigil`) | stream, 200 | Latency per line, without text | ≤ 1 Hz | System › Traces |
| `content` *new* (from `vigil`) | state | Shipped content, overrides (relative paths), turned-off items, reload results | on change | Content packs |
| `logs` | stream, 500 | The scrubbed log tail | ≤ 1 Hz | System › Logs |
| `report` | state | The bug-report preview, after the scan | on request | Tools › Bug report |
| `wizard` | state | First light's steps and their results | on change | First light |
| `workshop.*` | various | Developer views (SP8) | — | Tools › Workshop, only with `debug.panel` and the session's confirmation |

Stored history that may be long (events and conversations of a journey) is read with the `history.page` command
(§6.6), never pushed. The top bar's Live updates control throttles on the view's side: the view drops or holds
pushes, and EldenNet's rates are unchanged. A hidden Home card subscribes to nothing.

### 6.6 Commands

| Command | Arguments | Guards |
|---|---|---|
| `settings.set` | `key`, `value` | Key known; type and bounds per the description; not locked |
| `settings.reset` | `scope`: a key, a page or `all`; `confirm` for a page or all | A page or all needs `confirm: true` |
| `providers.update` | One provider's fields | Validated like `providers.toml` on load; refused from a paired device (G9, §7.2 item 16) |
| `secrets.set`, `secrets.clear` | `provider`, `value` (set only) | Write-only; the result carries only the new status; refused from a paired device (G9, §7.2 item 16) |
| `ai.pause` | `on` | Session only; never writes `enabled` (D15) |
| `selftest.run`, `servers.find` | — | Only while EldenNet's gate allows network (C4); one at a time |
| `pools.prebake` | — | At rest or at the title only |
| `memory.forget_npc` | `persona`, `confirm` | Confirmed; a backup first; refused if the menu closed on danger meanwhile |
| `memory.forget_journey` | `confirm` | As above |
| `preview.subtitle` | — | In game: shown for 3 s after the menu closes |
| `history.page` *new* | `view` (`events` or `conversations`), `journey`, `filters`, `cursor` | Read-only; 100 rows a page; journeys of the loaded playthrough only; SP2, SP3 and SP7 applied in the projection layer; `not_now` at the title screen |
| `report.build` | `parts` | Runs EldenNet's deterministic scan; the result names a failing field |
| `report.save` | — | After a passing build; writes to EldenNet's reports folder |
| `report.copy` | — | In game only: EldenNet puts the scanned text on the clipboard on this click (PV7). The browser copies from the previewed text itself |
| `wizard.step` | `step`, `answer` | First light's flow |
| `dashboard.disconnect` *new* | — | In game or from a connection on the loopback listener (§7.2 item 16); renews the session cookie and closes every connection |
| `dashboard.pair`, `dashboard.forget_device` *new* (G9) | A name for the device; a device id | `dashboard.pair` in game only, and only while `dashboard.lan` is on; `dashboard.forget_device` in game or from a connection on the loopback listener (§7.2 item 16); never from a device on the network |
| `menu.close` | — | In game only |
| `workshop.*` | various | Only with `debug.panel` and the session's confirmation |

### 6.7 Errors

| Code | Meaning | What the view shows |
|---|---|---|
| `bad_message` | Not JSON, not an object, missing fields, wrong types | Nothing (logged once, without the payload); after 3 the connection closes |
| `unknown_topic`, `unknown_command`, `unknown_key` | Not in this EldenNet | "Not available in this version" |
| `invalid_value` | Wrong type or out of bounds | The field's error state, with the bounds in words |
| `locked` | A later-version, always-on or developer-only item | The row's badge |
| `not_now` | Not allowed at this moment (gated network, a fight, the title screen) | The reason in words |
| `busy` | The same action is already running | "Already running" |
| `confirm_required` | A destructive command without `confirm` | The confirmation modal |
| `unauthorised` | No or stale session cookie, no or forgotten device cookie (G9), or a stale ticket | The page reloads once for a new cookie; a device on the home network is asked to pair (G9) |
| `rate_limited` | Over the command rate | Retried once after 1 s |
| `version` | Protocol major mismatch | "This page needs updating" |
| `internal` | A fault in EldenNet while handling it | "Something went wrong; the log has the details", and the control reverts |

Errors never echo a secret, a payload or a path. Parsing never panics: a malformed frame is dropped with a short log
note (ML lesson 23).

### 6.8 Limits

- Frames from a page: at most 16 KiB; at most 20 commands a second per connection; at most 32 subscriptions.
- Connections: at most two at once (a second tab is allowed; a third gets `bye` with `superseded` for the oldest).
- Frames to a page: pushes coalesced per topic to the rates of §6.5; one state payload at most 256 KiB (NPC types'
  `kinds` is the largest); a stream batch at most 100 rows.
- In game the command channel holds 32 commands; a full channel drops the newest with a toast, never blocks T3.

### 6.9 The settings description

EldenNet exports a description of every setting, generated from its own settings types, so labels, help and bounds
have one source for EldenNet's README, the browser and the game (D8). Each entry carries: `key`; `type` (bool,
integer, number, duration in seconds, choice, tri-state, key name, text, secret); `default`; `min`, `max`, `step`,
`unit`; `choices` with their labels; `label`; `help`; `synonyms`; `home` (a page id from `grace-pages`, §4.5); `effect`
(now, next moment, next request, next launch); `availability` (live, or the check, version or dependency that holds
it); `locked`. GraceUI's page lint checks that every key has exactly one home and that every key EldenNet exports
appears on a page.

An entry, as an example of the shape:

```json
{"key": "pacing.gap_s", "type": "duration_s", "default": 2.5, "min": 0.5, "max": 10, "step": 0.5,
 "label": "Time between lines", "help": "The shortest pause between two lines from anyone.",
 "synonyms": ["gap", "delay", "spacing"], "home": "behaviour.pacing", "effect": "next_moment", "availability": "live",
 "locked": false}
```

A command and its answer:

```json
{"v": 1, "t": "cmd", "id": 41, "name": "settings.set", "args": {"key": "pacing.gap_s", "value": 3.0}}
{"v": 1, "t": "result", "id": 41, "ok": true}
{"v": 1, "t": "state", "topic": "settings", "rev": 118, "body": {"...": "..."}}
```

---

## 7. Security

### 7.1 Threat model

- **Web pages in the player's browser** trying to reach the local server: DNS rebinding, cross-site requests and
  cross-site WebSocket hijacking (the Ollama case, CVE-2024-28224, is the precedent; REN §6).
- **Other local programs** connecting to 127.0.0.1. Nothing a loopback server checks tells the player's browser from
  a program on the same PC that copies a browser's requests: Host, Origin, Fetch Metadata and the cookie stop web
  pages, not programs. What limits programs: one running as the player can already read EldenNet's files and the
  game's memory, so the dashboard gives it nothing new; Windows keeps sandboxed apps (Store apps and other
  AppContainers) off loopback unless they are exempted; under Proton the server is a host socket, so every process of
  every Linux user on the machine can reach it, Flatpak apps with network access included, and no such isolation
  applies; keys never come back out, and a changed address clears its key (D61), so a visitor can neither read nor
  redirect a key. What remains: another user signed in on the same PC (Windows, or Linux under Proton) while the game
  runs could open the dashboard and see what it shows. With `dashboard.lan` on, anyone who can see the home network's
  plain-HTTP traffic could also copy a paired device's cookie and use it until the player forgets that device (§7.2
  items 12 and 16). Docs › Privacy says both.
- **Other devices on the home network**: none unless the player switches `dashboard.lan` on, and then only after
  pairing (G9; §7.2 item 16).
- **The in-game view and the browser are untrusted input** to EldenNet's core, alike.
- **Leaks**: keys, chat text, paths or names reaching logs, reports, URLs, titles or the clipboard unasked.
- **Supply chain**: the web app's dependencies.

### 7.2 The local server (always on, G4)

1. **Always on** (G4). The server starts with EldenNet, as soon as its settings are read, and stops with the game; it
   serves at the title screen and while dormant, and keeps serving after a later L1 trip so that Status and Bug
   report stay reachable. It never starts when init ends at L1 because the gate or the safety step failed (an unknown
   build or anti-cheat), and it stops if T10's anti-cheat re-check (ARCH channel C14) finds anti-cheat, so no socket
   stays open beside anti-cheat or an unknown build (EldenNet ARCH §1.4, as eldennet-changes §3 item 1 amends it).
   `dashboard.enabled = false` turns it off (D59); off means no socket.
2. **Bind 127.0.0.1 only** (and `::1` if offered), never `0.0.0.0`; other devices only through item 16, once the
   player switches it on (G9). The default port is 8642 (D58), configurable from 1024 to 65535; if it is taken, fail
   closed with one toast and one log line naming the port and where to change it, never fall back to another port or
   interface.
3. **A session cookie, not a token** (D60): 256 bits from the OS random source, new at every game start, never written
   to disk or logs, compared in constant time. Any top-level page load (`Sec-Fetch-Mode: navigate`, or no Fetch
   Metadata) that arrives on the loopback listener (local and peer address 127.0.0.1 or `::1`) with one of the three
   loopback Hosts of item 5 receives it as `Set-Cookie: en_session=…; Path=/; HttpOnly; SameSite=Strict`, with no
   `Domain` and no expiry, so it lasts until the browser closes; the network listener (item 16) never sets or accepts
   the session cookie. Every API request needs it. The page acts on nothing in its URL, so a link can only open the
   dashboard, never make it do something. Cookies on 127.0.0.1 are shared across ports, so a program serving another
   port could set an `en_session` of its own (one with a narrower Path is sent first) or flood this address's
   cookies, never use the real one: items 6 and 7 refuse its requests. So the server accepts a request when any
   `en_session` value it carries matches, each compared in constant time, ignores every other cookie, and answers an
   oversized `Cookie` header with a page saying to clear this address's cookies.
4. **WebSocket authentication**: the page fetches a one-time ticket with a POST that carries the cookie and passes
   items 6 and 7, and presents it in its first frame within 2 s. Nothing goes in a query string.
5. **Host check** before any routing: `Host` must be exactly `127.0.0.1:PORT`, `localhost:PORT` or `[::1]:PORT` on the
   loopback listener; on the network listener (item 16) `Host` must be that listener's own address with the port, and
   a loopback Host there gets 421; anything else gets 421. This alone defeats DNS rebinding.
6. **Origin check** on every state-changing request and **every WebSocket upgrade**: `Origin` must equal the server's
   own origin; an upgrade without `Origin` is refused (RFC 6455 §10.2).
7. **Fetch Metadata**: API requests whose `Sec-Fetch-Site` is present and not `same-origin` are refused.
8. **One credential, on this origin only**: the session cookie on the loopback listener (with item 16, a device cookie
   on the network listener instead); no other cookie and nothing in browser storage.
9. **Request shape**: static files by GET only; the ticket by POST with `Content-Type: application/json` and a size
   cap; no CORS headers, so cross-origin reads fail; no positive answer to a private or local network access
   preflight.
10. **Response headers**: `Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src
    'self' data:; font-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'`,
    plus `X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer`, `Cross-Origin-Opener-Policy:
    same-origin`, `Cross-Origin-Resource-Policy: same-origin`, and `Cache-Control: no-store` on the HTML and the API.
11. **WebSocket hygiene**: the limits of §6.8, the version handshake, identity bound to the connection and never taken
    from a payload, malformed frames dropped.
12. **No TLS** on loopback: `http://127.0.0.1` is a secure context, and a self-signed certificate would only teach
    players to click through warnings. With item 16 the home network carries plain HTTP; Docs say so.
13. **The browser is not the defence.** Browsers' local-network permission prompts differ and can be allowed by a
    click; the server's own checks are what hold.
14. **Opening the page**: the player types `http://127.0.0.1:8642` (or `localhost:8642`) or uses a bookmark, and needs
    nothing else. The address is shown in Settings › Browser view with Copy address (on a click, PV7) and Open in
    browser (EldenNet asks Windows to open the dashboard's own address, never another), on Home's Status card, in F11
    from v0.2, and on the launcher's first screen, which reads the port from `settings.toml`.
15. **Only listed files**: static files come only from `GraceUI\web\`, as `graceui.toml` lists them, read and checked
    once at start; anything else is 404, so nothing stray in the folder is served and no path reaches outside it.
    Without the folder, or if a web file fails its check, a plain notice page built into EldenNet (§2.9).
16. **Other devices (G9, decided by the owner)**: `dashboard.lan` is off by default, and use on this PC never needs a
    code. With it on, the server also listens on the PC's private network addresses only (never a public address,
    never every interface); Windows asks once whether the game may accept connections on private networks. Host must
    be one of those addresses with the port (item 5). "This PC" means a connection on the loopback listener. On the
    network listener only a device cookie is accepted (`HttpOnly; SameSite=Strict`, host-only), and without one only
    the pairing page and the code request answer: a device that has not paired sees only the pairing page. "Pair a
    device" in game shows a six-digit code there for two minutes; it works once and is void after five wrong tries.
    A paired device gets a random device cookie that lasts until the player forgets the device. EldenNet keeps only
    its hash, with the name the player gives the device and the date it paired, in a file of its own in its config
    folder (`devices.toml`), never in `settings.toml`, so Copy settings, the bug report and recordings never carry it.
    Each paired device is listed on Settings › Browser view, whether `dashboard.lan` is on or off, with a Forget
    button, which revokes it at once and closes its connections. Switching
    `dashboard.lan` off closes the network listener and every device connection; paired devices stay listed until
    forgotten. Paired devices are refused `secrets.set`, `secrets.clear` and `providers.update`.
17. **Disconnect browsers**: Settings › Browser view drops every connection and renews the cookie
    (`dashboard.disconnect`); at most two connections at once (§6.8).

### 7.3 Every command is untrusted

- One validating entry point in the core for both views; types, bounds, keys and guards are checked there, never only
  in a view.
- A view cannot name the actor: the core knows which view sent a command from the channel it arrived on.
- Destructive commands need `confirm: true` and an exact target; nothing acts on a GET.

### 7.4 Secrets

- Keys are set, replaced or cleared, and never read back; the UI sees set, missing or rejected (PV2).
- The masked field never puts its value in a log line, an error, the protocol's echo or browser storage, and clears
  itself on close.
- The key field is never offered to the browser's password manager (§4.6.2, Providers' Rules). A web-app lint checks
  that no input of type password exists, that the key fields sit in no `<form>`, and that they are cleared after
  `secrets.set`; the release checklist adds a manual check in Chromium and Firefox that no save prompt appears
  (headless browsers do not show the browser's own prompts, so this cannot be an automated test).
- Settings payloads are never logged; request lines are logged without query strings or fragments.
- **Keys are bound to their address** (D61): changing a provider's base URL or kind, from either view, clears its
  key and its extra header values, so no visitor can send a key to another address.

### 7.5 Supply chain (the web app)

- Few dependencies: Preact, its signals, the build tool and a test runner (G3). Each licence is checked before
  adoption and listed in `NOTICE` and the bundle's licence file.
- A lockfile, exact versions, installs from the lockfile only, no install scripts, and an audit in CI.
- No CDN, no remote fonts, no analytics, no telemetry, no error reporting service.

---

## 8. Performance budgets

| Item | Budget | First measured |
|---|---|---|
| Menu closed, in game | No projection built, no draw; EldenNet's overlay stays within C3 (≤ 0.3 ms a frame) | v0.6 |
| Menu open, densest page (NPC types, Reactions, Home with every card), desktop at 1440p | ≤ 0.8 ms p99 CPU on the present thread, `graceui.dll`'s `frame` and `grace-host`'s check and copy included; ≤ 0.2 ms GPU | S4 sample, then v0.6 |
| Menu open, Steam Deck at 800p | ≤ 1.5 ms p99 CPU on the present thread | S4 sample, then v0.6 |
| FPS while the menu is open (at safe moments only) | ≤ 2 FPS lost at the 60 FPS cap | v0.6 |
| First frame after F5 | No frame over 1.5 × the budget: no uploads, no atlas build | v0.6 |
| Allocation on the present thread | None per frame after warm-up, in `graceui.dll` and in `grace-host` (ARCH §1.2, T3) | v0.6, by test |
| Core projections | Only for watched topics; ≤ 0.3 ms per core step at 32 characters; ≤ 4 Hz per topic | v0.2, v0.4 |
| The in-process bridge | Encoding a revision, and `ingest` parsing it: ≤ 0.5 ms each on T6 for the largest payload (`kinds`, 256 KiB); only watched topics | S4 sample, then v0.6 |
| Font atlas | ≤ 2048² RGBA at up to 1440p and scale 1.25; ≤ 4096² at 4K or 150%; ≤ 4 rebuilds a session (D29) | S4 |
| Local server | With no tab open, one listening socket and no other work; ≤ 2 connections; ≤ 1% of one core with one tab on Home | v0.2 |
| Pushes | ≤ 64 KB/s per connection on Home with every card shown, during a fight | v0.4 |
| History pages | A `history.page` answer ≤ 50 ms of core time for 100 rows; never on T3 | v0.4 |
| Web bundle | Scripts ≤ 100 KB gzip; styles ≤ 25 KB; fonts ≤ 700 KB as subset WOFF2; first paint ≤ 1 s on loopback | 0.2 |
| Browser rendering | A page switch ≤ 100 ms; the Event monitor's 500 rows virtualised, no dropped frames while it streams | 0.4 |

---

## 9. Testing

### 9.1 Layers

| Layer | What | Where it runs |
|---|---|---|
| Pure logic | `grace-view`: page loading, the widget tree, the rail's accordion, flyouts and page order (Page Up and Page Down cross categories), focus scopes (rail, top bar, page, drawer, modal), the pad ledger (the opening press inert until released; B and Start never passed on), hotkey rules, search indexing (nothing from SP9's or §4.3's never-indexed lists), text layout; GraceUI's text field (caret, selection, masked drawing); `grace-draw`'s tessellation against golden vertex lists | Linux CI |
| Protocol | Round trips of every payload and command; the Rust-to-TypeScript generation has no drift; a fuzzer feeds malformed frames and nothing panics | Linux CI |
| ABI and handshake | Every row of §2.9's mismatch table, each way round: no folder, a half-copied folder, a wrong hash, no export, an unknown ABI major, protocol majors and minors on either side; a DLL that panics in each entry; vertex lists with bad counts, indices, clip rectangles or texture ids. Each ends as §2.9 says, with EldenNet's surfaces still drawing. The ABI header against the last release's (append only). `graceui.dll`'s imports and symbols against §2.2's deny list and reviewed allowlist (system DLLs only; no socket, thread-creation or file-opening import; std's dbghelp loader and panic-output path only as allowlisted; no hudhook, ImGui or `MH_` symbol); a frame of more than 65,536 vertices split across commands with their vertex offsets | Linux CI (with `grace-dev` as the host), Windows CI |
| Page lint | Every exported setting has exactly one home, and that home is one of §4.5's page ids; every shortcut row and quick switch names a home that exists; no unknown keys; every page has an id, a category, a description, help text and at least one Related link that resolves, and names its increment or a later-version tag; statement pages hold no controls; Settings' link-out groups hold no rows; every component used exists in both views | Linux CI |
| Server security suite | Wrong or missing `Host`; foreign or missing `Origin` on upgrades; a missing, stale or foreign session cookie; a replayed ticket; a cookie overwritten from another port; a narrower-Path `en_session` and a cookie flood from another port; files not listed in `graceui.toml`; a missing web folder; `Sec-Fetch-Site` cross-site; oversized frames; command floods; no CORS headers; every security header present; no cookie in any log line; port taken → fail closed; with `dashboard.lan` on: a Host naming an address not bound, an unpaired device reaching anything but the pairing page, five wrong codes, an expired or reused code, `dashboard.pair` from any browser, a forgotten device's cookie, `secrets.set`, `secrets.clear` and `providers.update` from a paired device, and `dashboard.lan` off closing every device connection; on the network listener, an unpaired device's page load gets no session cookie, a loopback `Host` gets 421, a forged loopback `Origin` is refused, a session cookie is not accepted, and `dashboard.forget_device` and `dashboard.disconnect` are refused; a device name or network address in a log line or report | Linux CI, against `grace-serve` |
| Web app | Component tests; each page against fixture states; the key-field lint (no password-type input, key fields in no form and cleared after `secrets.set`, §7.4); keyboard-only walkthroughs of the rail, top bar and every page; Home's Customise round trip (`ui.menu.home_cards`); Live updates holding a stream; an automated accessibility check (its licence checked first); the mock's two window sizes (1280 × 720, 2560 × 1440) | Linux CI, headless browser |
| Spoiler goldens | For each of EldenNet's fixture states (with our invented names), each page's and each Home card's rendered text, tiles and counts match a golden; no count includes a veiled or absent item; a hidden character, a boss before its bar or an unannounced invader changes no count; History › Events lists no event the Tarnished did not perceive; an earlier journey shows read-only; titles and URLs carry no chat text; the roster at the title-screen fixture shows only general profiles that pass the opening lock set; Playthrough shows nothing of anyone not met, "Alive when last met" for someone who died unseen, the line of the stage seen and never a later one, and no related story for an unmet person | Linux CI (web), Windows CI (in game) |
| View parity | Each page's structure (rows, labels, values, order, focus order), the rail, the top bar and Home's grid dumped from both views and compared with one golden (U6) | Linux CI (the in-game structure from `graceui.dll` driven by `grace-dev`), Windows CI |
| Contrast | The table of §3.3 recomputed from `tokens/grace.toml` (U7) | Linux CI |
| In-game view harness | A small Windows program that opens a Direct3D 12 window, applies hudhook 0.9.3 to itself as hudhook's own examples do, and draws the menu through `grace-host` and `graceui.dll` against `grace-dev`'s fixtures: screenshots compared within a tolerance, frame cost, atlas size | Windows CI and developer machines |
| In game | EldenNet's per-increment checks (O29): input leaks with F5, Esc and B; the close matrix of §5.1; danger closing; the title screen; frame cost; Proton and the Deck; HDR | Testers' machines, EldenNet's matrix |

### 9.2 The fake EldenNet (`grace-dev`)

- A small Rust program that links the real `grace-serve` and `grace-protocol` and answers from fixture states instead
  of a game. Developing the web app against it exercises the real server, its security checks included.
- **Fixtures**: scenario files in `fixtures/` (a first run with Setup open, a quiet road, a busy fight, the title
  screen, an L1 banner, multiplayer, lost contact, a second journey after NG+, a long history, every badge state),
  with invented names and our own words. Never game text or names.
- **Scripted time**: a scenario can play events and model calls on a timeline, so live views and their rates can be
  watched and tested.
- **Commands** are validated against the settings description and change the fixture's state, so the pages behave
  as with EldenNet.
- **Injection panel**: in development builds only, a small panel in the web app shows the frames sent and received
  and lets a developer push any payload. It is absent from release bundles, and a test checks that.
- **A host for `graceui.dll`**: `grace-dev` also implements `GraceHost`, so the in-game view runs on Linux against the
  same fixtures; its structure dumps, vertex lists and the ABI tests need no Windows.

### 9.3 Parity between the two views

Both views can dump a page as a structure (role, label, value, state, focus order) instead of drawing it, and the
frame too (the rail with its open category, the top bar, Home's grid in its customised order). The web app dumps its
DOM; `graceui.dll` dumps `grace-view`'s tree in a test mode. One golden per page and fixture state covers both, so
a change to a page description or a component shows up in both views or fails CI.

---

## 10. Increments (aligned with EldenNet DESIGN §16.2)

| EldenNet | GraceUI | Pages | Views | Exit checks |
|---|---|---|---|---|
| Before v0.2 (with spike S4) | 0.1 Foundations | Tokens, protocol, page descriptions, the web shell (the rail with its categories, accordion and flyouts; the top bar; search; the page parts of §4.2.4), `grace-dev` with fixtures, the mock. **For S4**: one settings page (Behaviour › Pacing) drawn in game through `grace-host` from `graceui.dll`'s vertex lists | Harness; S4 | Protocol, lint and contrast tests; S4's frame cost (the copy included), atlas size, the ABI round trip and screenshots for the owner's look review (G1) |
| v0.1 Eyes | — | None: EldenNet's F11 view covers testing | — | — |
| v0.2 First words | 0.2 | The `GraceUI` folder (`graceui.toml`, `web\`) in EldenNet's release; Home (Setup, Status, Problems, Lines, Model calls; Customise); AI › Providers and Models (read-only), Self-test, Requests; System › Status (Health), Performance, Traces, Logs; Tools › Bug report (preview), Workshop; Content packs; Docs; Settings › Developer and Browser view (readable) | Browser, always on (G4) | The bundled folder's check; Server security suite; spoiler goldens; no secret in any log; the `WM_CHAR` fix in EldenNet |
| v0.3 Remembrance | 0.3 | Characters › Named NPCs (the pilot NPC); Characters › Playthrough (people met, status, last met, disposition, has heard of, notes); History › Conversations and Memories with Forget; Home › Nearby; Behaviour › Speaking up (named NPCs); the top bar's Journey | Browser | Forget's confirmation and backup; SP3 and SP7 goldens; the NG+ journey fixture |
| v0.4 War cries | 0.4 | Characters › NPC types; Behaviour › Pacing, Reactions, Speaking up (all), Combat, Awareness (as S8 passes); History › Events; Tools › Event monitor; Home › Happening now and Recent events; Voice › Text and subtitles (browser); settings writable in the browser (D17) | Browser | Command validation; the event monitor's rates and ring; `history.page` and the `seen` bit; testers tune pacing (EldenNet X2) |
| v0.5 Veil | 0.5 | History › World; Named NPCs for every persona, with general profiles; Playthrough's story lines and World now | Browser | SP5 and SP6 goldens on all world stages; the roster and Playthrough goldens |
| v0.6 Threshold | 0.6 | `graceui.dll` and `fonts\` in the folder; **The menu in game on F5**: every page but Workshop, F10 folded in; Settings (General, Hotkeys, Menu, Browser view, Developer, All settings); First light; Settings › Browser view's home-network switch and pairing (G9); Behaviour › Conversation with the call-out; Voice › Text and subtitles in game; AI › Providers and Models writable; Bug report's copy and save; the later-version statement pages with their "WIP" tags (G7) | Both | U2–U4 and U6 in game; the close matrix; Proton, the Deck and HDR on EldenNet's matrix; the ABI and handshake tests; a GraceUI patch dropped over the bundled copy, in game; the pairing cases of the server suite |
| v1.0 | 1.0 | Accessibility pass, pad-only walkthrough, the Deck's scale (G6), the full matrix | Both | U1–U9 green on the release candidate |
| v1.5 | 1.5 | Voice › Text-to-speech and Speech-to-text become pages with controls (EldenNet's TTS, push-to-talk in Settings › Hotkeys) | Both | — |
| Later | — | AI › Director and Behaviour › Acts become pages; Acts allowances live; diaries and scene memories on History; your own event rules on Reactions; Workshop editors; HTML in game if G1 ever changes | — | — |

---

## 11. Risks

| Risk | Mitigation |
|---|---|
| The ImGui look falls short of what the owner wants from the SeverActions spirit (typography) | S4 draws a Grace page in game early and the owner reviews screenshots beside the browser mock; a kerning routine for display text if asked (§3.16); fallbacks in G1 (RmlUi drawn as ImGui geometry, or an Ultralight spike) |
| The two views drift | Pages as data (§2.5), one token file, generated protocol types, parity goldens (§9.3) |
| The atlas grows too large at 4K and 150% | The face budget and rebuild cap (D29); S4 measures |
| hudhook 0.9.3's input and texture hazards | EldenNet's `WM_CHAR` fix; no texture updates after load (D28) |
| An attack on the local server, now always on | The checks and suite of §7.2; keys bound to their address (D61); only listed files served; other devices kept out unless paired (G9) |
| A spoiler through the UI | Projections only (SP1), goldens per fixture state, absence silent |
| An input leak: Esc opens the game's menu, B dodges | The mask-until-release rule, the close matrix, tests on pad and keyboard |
| Proton and the Deck: IME, Steam keyboard, opening a browser under gamescope | On EldenNet's matrix; on a Deck in game mode the in-game menu is the path, and the dashboard is for desktop mode |
| Scope creep (editors, explorers, sound) | Seams only in v1 (§1.3) |
| A framework-sized rail (eleven top-level entries and 29 pages) is slow to cross on a pad | The accordion keeps one category open; LT and RT walk every page in order; search reaches any setting and Quick switches the main ones; the rail collapses on small screens (§4.2.2, §5.4) |
| Players expect SkyrimNet's pages that EldenNet does not have | Later EldenNet features are statement pages tagged "WIP" (G7); Docs explains what EldenNet does instead; not-planned pages are simply absent (§4.11) |
| A second home creeps in through Settings | Settings holds only homeless settings and links out (D40); the page lint fails a key with two homes |
| Mixed or stale GraceUI copies after a manual update | `graceui.toml` with sizes and hashes, the handshake, majors that must match, notices in words; EldenNet keeps playing (§2.9) |
| The ABI drifts between releases | Append-only tables with sizes, a CI check against the last release's header, the handshake tests both ways round (§9.1) |
| A port clash with another local tool | Fail closed with a toast naming the port and where to change it; the default 8642 avoids the ports of the local model servers EldenNet probes (D58) |

---

## 12. Decisions for the owner

**Status: the owner decided G1, G2 and G4–G9 on 2026-11-07; G3 and G10 are Open.** Each open row has a
recommended default. A question that comes up later is added here as a new row marked **Open**, with a
recommendation, and the owner's answer is recorded here with its date. G1, G2, G4, G5, G6, G8, G9 and G10 also touch
EldenNet's design; [eldennet-changes.md](eldennet-changes.md) lists what each answer changes there. G7 was added with
the framework categories (D38), G8 after their review, and G9 and G10 with the owner's answers to G2 and G4, the same
day.

| # | Question | Options | Recommended default while **Open**, and why |
|---|---|---|---|
| G1 | Which renderer draws the menu in game? | (a) **Dear ImGui with a native Grace theme** in EldenNet's one hudhook overlay; the full web UI in the browser. (b) **Ultralight in CPU mode** (the engine Magelight uses): the same HTML pages in game, uploaded as a texture through a reviewed patch to hudhook. (c) Ultralight with a Direct3D 12 GPU driver of our own. (d) CEF (Chromium) off-screen. (e) RmlUi, drawn as ImGui geometry. (f) Blitz or Servo | **Decided by the owner (2026-11-07): (a).** The owner's words: "let's use what's best compatibility with Elden Ring game engine", after weighing the lack of HTML in game; the browser keeps the full HTML pages. *Recommendation as written:* (a) for v1, with (b) kept as a throwaway spike after v1.0 if the owner wants the same HTML pages in game. *Why:* hudhook 0.9.3, pinned by O25, can show a changing web page only by leaking a new upload buffer on every update (about 15 MB at 1440p) and stalling the present thread, and it gives no access to the GPU, so (b), (c), (d) and (f) all need a change to the pinned library (REN §2). (b) also needs Ultralight's proprietary licence (a public free mod rests on its limited commercial grant), about 57 MB of runtime, an engine with a 60 FPS cap and no evidence under Proton; Magelight, the closest precedent, spent 17 of its 27 written invariants on its web engine (ML §2). (c) adds a GPU driver of our own for a still menu. (d) is about 250–300 MB with helper processes and a mixed Wine record. (f) are not ready (beta, monthly breaking changes). (e) fits O25 and is the fallback if ImGui proves too plain, but its pages are not the browser's. (a) adds no licence, binary or hook, uses the Proton path EldenNet must make work anyway and needs no texture updates. *What (a) gives up:* the in-game view is not the browser's HTML; it is drawn from the same page descriptions, tokens and protocol (§2.5), so it shows the same pages, but typography is simpler (no kerning or OpenType features, §3.16) and each component is written twice. The owner can judge the look before deciding: S4 produces in-game screenshots of a Grace page beside the browser mock. Choosing (b) later needs three approvals: the licence, the hudhook patch (an O25 change) and the size |
| G2 | Is GraceUI a library inside EldenNet's DLL, or a standalone host DLL other Elden Ring mods can use? | (a) **A library**: GraceUI's crates and web bundle linked into `eldennet.dll`. (b) **A standalone host DLL** with an API for other mods, now. (c) (a) now, and a host later if other mods ask, grown from the bridge | **Decided by the owner (2026-11-07): a standalone mod.** The owner's words: "GraceUI will be a standalone mod, but EldenNet will feature the latest version with each release (at the release date) as built in, but users will be able to update GraceUI separately (by dragging files into Elden Ring/Game directory and overwriting old files)." So GraceUI has its own releases and one folder of its own in the game's `Game` folder, with a version file (§2.7, D51–D53); each EldenNet release carries the GraceUI release current at its release date; a player updates GraceUI on its own by copying a newer release's folder over the old one with the game closed; EldenNet checks what it finds at every start and keeps playing whatever it finds (§2.9, D54). The answer leaves two things to the design: which module owns the one overlay (G10, Open), and whether other mods may use GraceUI, which v1 still leaves out (§1.3). *Recommendation as written:* (a), with (c)'s path noted, because one process holds only one hudhook overlay and one MinHook safely (ER02 §7.1) and being a host cost Magelight a versioned C ABI, a registry per mod, input-mode ownership, a sandbox, version gates and handling for mixed copies (ML §7). *What the decision costs:* a versioned C ABI with a handshake, the bridge's frames carried in process as they are to the browser (D55), the version file, and handling for a missing, older, newer or mixed copy: the ABI and version parts of Magelight's list, without its registry, sandbox or input arbitration, because GraceUI serves EldenNet alone |
| G3 | Which framework for the browser app? | (a) React. (b) **Preact** with TypeScript and signals, built with Vite. (c) Svelte 5. (d) Solid. (e) Plain web components (or Lit) | **Open. Recommended: (b).** *Why:* under G1 the web app runs only in desktop browsers, so the choice is about authoring and upkeep. Preact keeps React's component model and hooks, which people and AI assistants know best, at about a tenth of React's size; signals suit live topics that change four times a second without re-rendering pages. Its small parse cost and an es2022 target keep the door open to Ultralight's interpreter-only JavaScript if G1 ever changes (REN §4.2). Svelte 5 changed its syntax recently, so examples and generated code mix two dialects; Solid has a smaller community; web components need hand-written state handling. Licences (Preact MIT, Vite MIT) are checked again at adoption |
| G4 | Is the browser dashboard on by default? | (a) **Off by default; opt-in** in Settings › Browser view (from v0.6), in First light, or with `dashboard.enabled = true` in `settings.toml` before v0.6. (b) On by default | **Decided by the owner (2026-11-07): (b), always on.** The owner's words: "Browser dashboard will work the same way as SkyrimNet's, open the IP:port of the dashboard, always on." EldenNet serves the dashboard from the end of init's gate and safety steps to exit, unless anti-cheat is found later (§7.2 item 1); the player opens `http://127.0.0.1:8642` (D58) in a browser on the same PC and types or copies nothing else; the address is shown in game (Settings › Browser view, Home › Status, F11) and on the launcher's first screen (§7.2). Host, Origin and Fetch Metadata checks stay; the per-session token in the link gives way to a session cookie granted to any visit of the address (D60), and a changed provider address clears its key (D61). A switch can still turn the server off (D59). Other devices on the home network get in only when the player switches that on, and each only after entering a pairing code shown in game (G9). *Recommendation as written:* (a), because a listening port is attack surface even when hardened (§7.1) and Magelight moved network access to opt-in before its 1.0 (ML §3.16); §7.2's checks are what keep that surface small now |
| G5 | NPC types: are types listed before the player has seen one? | (a) Every type listed, as EldenNet DESIGN §7.6 and O38 describe. (b) **Base kinds always listed; cards, creature families and other named slots listed once the Tarnished has seen one**, recorded per playthrough (`types_seen`), with a "show types I haven't met (may spoil)" switch | **Decided by the owner (2026-11-07): (a), every type listed from the start.** The owner's words: "we will feature a full roster with character biographies, so they will be part of EldenNet's database already." And, for G5 and G8 together: "G5 and G8: we can have a page that track playthrough, shows met NPCs and their status, their quest stage, etc. that could be useful. Otherwise for general character profiles, we can have them all already shown." So NPC types lists every kind and type from the start, each with its label, its tags and a general profile: only what anyone would see on a first meeting with one, in our own words, reviewed for spoilers like shipped content; never a boss's name, and no tag names a place or a quest. This is a deliberate exception to invariant 2 and SP5 for the roster only (SP4, SP5). `ui.menu.show_unseen_types` and `types_seen` are dropped. What this playthrough has reached is on the new Characters › Playthrough page (§4.6.4, D63–D65), where invariant 2 holds in full. *Recommendation as written:* (b), because a full list names late-game creatures; the first-meeting rule and the spoiler review answer that now |
| G6 | Should the Steam Deck default to a larger text scale? | (a) `ui.scale` 1.0 everywhere, as now. (b) **On a Steam Deck, the first run writes `ui.scale = 1.35`**, detected through Steam's own `IsSteamRunningOnSteamDeck` in the already-loaded `steam_api64.dll` (the DLL O11's keyboard uses), shown and changeable in First light. (c) Ask in First light, with no detection | **Decided by the owner (2026-11-07): (b), as recommended.** The owner's words: "that's okay." *Why (as recommended):* at 1.0 on a 7-inch 1280 × 800 screen, menu text renders at about 11 device px and subtitles at about 22; at 1.35 menu text is about 15 px and the menu uses its drawer layout. `ui.scale` is one scale for all of EldenNet's text (NPC §10.7), so this changes the subtitles too, which makes it the owner's call |
| G7 | Do later-version features appear in the sidebar? | (a) Hidden until they ship. (b) **Dimmed entries with a "v1.5" or "later" tag, each opening a statement page with no controls**: Voice › Text-to-speech and Speech-to-text, AI › Director, Behaviour › Acts; never for features that are not planned. (c) One "Coming later" page | **Decided by the owner (2026-11-07): (b), with the tag "WIP".** The owner's words: "agreed. but call it WIP." The four entries (Voice › Text-to-speech and Speech-to-text, AI › Director, Behaviour › Acts) are dimmed, carry a small "WIP" tag whose tooltip and accessible name say when ("Work in progress: arrives in v1.5" or "Work in progress: a later version"), and open their statement pages, which stay; AI › Models' dimmed Decisions card carries the same tag. Settings rows keep their availability badges ("Takes effect in v1.5", "Later version"), which say when a setting starts to work (§4.7, D62). *Why (as recommended):* it keeps the framework's categories recognisable to players who know SkyrimNet's (SND §1.1), tells players what is coming without controls that do nothing (§4.1 principle 3), and costs four rail entries; diaries, thoughts and scene memories stay one-line notes on their pages. A statement names a feature of EldenNet, never a part of the game, so it is no spoiler hint (SP5). (a) leaves Voice with one page and hides why there is no speech; (c) puts unrelated features in one place |
| G8 | Are unmet published personas listed? | (a) Listed by their pre-reveal name, Allowances only, on Named NPCs and NPC types, and counted on Content packs (today's D12). (b) **Hidden on Named NPCs, NPC types, search and counts until met**, unless "Show characters and types I haven't met (may spoil)" (G5's switch, widened) is on; their allowances stay reachable through the `named` kind's own row on NPC types | **Decided by the owner (2026-11-07): every named NPC's general profile is shown from the start, met or not, and a new page tracks the playthrough.** The owner's words: "Same as G5, we will have them all ready, and the framework will inject correct NPC profile into the request payload prompt." And the follow-up quoted in G5: "G5 and G8: we can have a page that track playthrough, shows met NPCs and their status, their quest stage, etc. that could be useful. Otherwise for general character profiles, we can have them all already shown." So Characters › Named NPCs lists the whole roster from the start, each by the name the game uses at a first meeting (the pre-reveal fallback, SP3), with its general profile, its Allowances and its Reactions. A general profile holds only first-meeting knowledge (who they appear to be on a first meeting, in our own words, reviewed for spoilers like shipped content): never a stage, fate, later form, reveal, quest step or relation that the story discloses later. This is the same roster exception to invariant 2 and SP5 as G5's. The new Characters › Playthrough page (§4.6.4, D63–D65) shows, for this playthrough only, the NPCs the Tarnished has met, each with their status as the game showed it, where they were last met, the line for the stage of their own story the Tarnished last saw, related stories as reached and the world's stage, with "since you last spoke" notes; invariant 2 and SP1–SP9 apply there in full. Named NPCs without a shipped general profile still appear once seen (SP4), and bosses and invaders stay unnamed until the game names them, because a boss's name is itself a reveal. Putting the right profile into each request is EldenNet's prompt assembly as designed (EldenNet DESIGN §7.7); a page never shows the prompt's private fields. D12 is superseded. *Recommendation as written:* (b), hiding unmet personas until met, because a greyed list of strangers is a hint even when the cast is published; the first-meeting rule answers that now |
| G9 | May other devices on the home network open the dashboard? | (a) **No**: this PC only (loopback), as G4's answer is built now. (b) **A switch, off by default** (`dashboard.lan`): the server also listens on the PC's home-network address, and each device pairs once with a code shown in game, never without one. (c) **A host field**, as SkyrimNet has: the server listens on whatever address the player types, with no pairing | **Decided by the owner (2026-11-07): (b).** The question put to the owner: "the same as SkyrimNet by default, always on and opened at its address on your PC. Opening it from other devices would be a setting you switch on, and then a device must enter a short pairing code shown in game before it gets in." The owner's words: "Yes, approved." So the dashboard serves this PC only until the player switches `dashboard.lan` on, a device on the home network gets in only after entering a short pairing code shown in game, and use on this PC never needs a code. *How it works* (§7.2 item 16): `dashboard.lan` (off); when on, the server also listens on the PC's private network addresses only (never a public address, never every interface), and Windows asks once whether the game may accept connections on private networks; Host must be one of those addresses with the port, so DNS rebinding still fails; "Pair a device" in game shows a six-digit code for two minutes, one use, void after five wrong tries; a paired device keeps a random device cookie until the player forgets it, stored only as a hash in a file of its own in EldenNet's config folder (`devices.toml`, never `settings.toml`) and listed with its name and the date it paired; Forget revokes it at once and closes its connections; a paired device cannot set or clear keys or change providers, so a key is never typed over the network; Docs say that the home network carries the page in plain HTTP. *SkyrimNet, for comparison*, in answer to the owner's "G4: how does SN handle that issue?" (from the owner's screenshots and the sandboxed run of its dashboard, SND): its Settings › Dashboard › Web Server has an on switch, a host field that defaults to 127.0.0.1 and a port (8080, with 8081 for a legacy browser UI). Its page opens at that address with no link, sign-in or code. Other devices reach it only if the player changes the host to a network address, and we saw no login or pairing setting. We did not run its game-side server, so whether it checks Host or Origin is not known. *Recommendation as written:* (b), because the owner's "open the IP:port" fits a phone or a second screen beside the game, while on a network address Host and Origin checks no longer tell the player from anyone else on the network, so (c) without a pairing step would let any device on it read chat text and change settings; (a) needs nothing more; (c) is the least work and the least safe |
| G10 | Now that GraceUI is a mod of its own (G2), which module owns the one overlay? | (a) **GraceUI is the UI host**: hudhook, its MinHook and the Present hook move into `graceui.dll`; EldenNet draws its subtitles, chat box, call-outs, labels, toasts, banner and F11 through GraceUI's API, and its input detours go through GraceUI's MinHook. (b) **EldenNet keeps the overlay and loads `graceui.dll`**, which links no hudhook, ImGui or MinHook and hooks nothing: it lays out and tessellates the menu, and EldenNet checks its vertex lists and copies them into ImGui's draw list (§2.9). (c) **GraceUI's code stays linked into `eldennet.dll`**, and only its files (the web app, fonts, Docs) can be replaced by hand | **Open. Recommended: (b).** *Why:* exactly one module may own hudhook, the Present hook and MinHook (EldenNet O25; ER02 §7.1). *The owner's "standalone mod":* (a) and (b) both give GraceUI a DLL and a folder of its own that a player can replace; (c) cannot update the in-game menu without an EldenNet release. *ImGui's one context and allocator:* ImGui keeps its current context and its allocator per copy of the library, so two DLLs drawing into one context need the same version, build and allocator handed across, and a hudhook build inside `graceui.dll` would bring a second MinHook; (b) keeps ImGui out of `graceui.dll`, while (a) makes every EldenNet surface call ImGui through GraceUI. *Rust and the C ABI:* Rust types, allocators and panics do not cross a DLL boundary, so either way the contract is `repr(C)` tables, borrowed buffers and caught panics; (b)'s is frames in and vertex lists out, one call a frame, while (a)'s covers every surface and hook of EldenNet's. *Crash isolation:* one process either way, so a hard fault in either DLL still ends the game; under (b) a caught panic or a malformed vertex list turns the menu off and subtitles, chat and call-outs keep drawing, while under (a) a fault in the host takes them too. *The hudhook pin:* (b) keeps it, R12's detours and every O25 rule in EldenNet; (a) moves them into GraceUI's releases, which players replace by hand. *Updates by file:* (b)'s update can change the menu's layout, drawing and pages but never a hook; (a)'s can change the hooks. *Other mods later:* (a) is the natural host for them; (b) can grow a host API later without moving the overlay; none is in v1 (§1.3). *What (b) costs:* a vertex-list contract that `grace-host` checks, GraceUI's own text field in place of ImGui's (D57), glyph tables handed over from EldenNet's atlas, and topics encoded in process (≤ 0.5 ms of T6 per revision, §8). Until it is answered the design follows (b) |

---

## 13. Decision log

Settled by this design (2026-11-07) within the owner's request, EldenNet's decided rules and the research. Each can be
reopened by the owner.

| # | Decision | Choice | Alternatives | Why | Source |
|---|---|---|---|---|---|
| D1 | Repository | GraceUI is a separate repository; EldenNet depends on it, never the reverse | Inside EldenNet | The owner's choice; the UI has its own toolchain (TypeScript) and release rhythm | Owner, 2026-11-07 |
| D2 | Layout | SkyrimNet's dashboard idea: a home with status and live activity, searchable settings, diagnostics, an event monitor. Not in v1: a game-data explorer, content editors, agents, a content hub. (Its "rail of sections" is superseded by D38) | Copying more of it | Ideas only (EldenNet's rules); an explorer and editors are spoiler surfaces with no v1 use | IA §1 |
| D3 | Appearance | Our own Grace theme in the spirit of SeverActions' menu, with seven deliberate differences: contrast, bundled fonts, every control themed, a strong focus ring, full reduced motion, no blur or loops, larger type | Copying its look | It has no licence; its faint text fails contrast; its fonts are Windows system faces | LOOK §1.6, §3 |
| D4 | Page names | *Superseded by D39.* Elden Ring flavour names with plain subtitles, both searchable, and a plain-names option | Plain only; flavour only | The owner asked for Elden Ring tabs; plain words keep them clear | IA §11 #1 |
| D5 | Shape | One information architecture, page set, protocol and token source; two views (in game, browser); the same pages in both, Workshop browser-only | Different menus per view | The owner's request; drift is the main risk | IA §2 |
| D6 | Keys | `hotkeys.menu = "F5"` (S4 confirms); F10's panel folds into the menu and `hotkeys.settings` is removed by migration; F11's debug view stays separate | F10 also opening the menu | Fewer keys to collide; F11 is a developer surface | IA §11 #8, #12 |
| D7 | When it opens | In game only when `safe_context` holds and no game menu is open, or at the title screen; danger closes it at once and keeps every change; open confirmations are cancelled | A world freeze | The game never pauses (O9) | §5.1 |
| D8 | Pages as data | Page descriptions in GraceUI; settings rows generated from a settings description EldenNet exports | Pages written twice; labels owned by GraceUI | One source for labels, bounds and availability; parity between views | IA §11 #6; §2.5 |
| D9 | Spoilers | Projections only; SP1–SP9; the veiled look only for a perceived character whose name the game has not shown | Veiling story items | Absence is silent, as in EldenNet's prompts | IA §6, §12.3 |
| D10 | Live and developer views | Perception-filtered in player mode; unfiltered views only in Workshop (browser) and F11, behind `debug.panel`, a confirmation and a ribbon | Always unfiltered | Neither view becomes a radar or a spoiler feed | IA §11 #7, #8 |
| D11 | The World page | World stages that hold, verified only | Quest tracks and NPC stages in safe words; no world page | Tracks are other people's affairs and can hold before their effect is seen | IA §11 #4 |
| D12 | Unmet named NPCs | *Superseded by G8 (the owner, 2026-11-07) and D64.* Listed by the pre-reveal fallback name, Allowances only (questioned by G8) | Hidden until seen | The cast is published; the fallback is the pre-reveal name | IA §11 #2 |
| D13 | Memories | Read-only plus Forget in v1; earlier NG+ journeys collapsed and read-only | Editing keepsakes | Editing would bypass the memory job's validation; O20 governs prompts, not the player's own view | IA §11 #5, #13 |
| D14 | Applying changes | As made, debounced 300 ms into `settings.toml` by EldenNet; no Save button; last write wins between views | A Save button; per-setting locking | Danger can close the menu at any moment; conflicts between views are rare and harmless | IA §2 |
| D15 | Pausing | A session-only "Pause AI" command that never writes `enabled` | Reusing `enabled` | Pausing should not change the next launch | IA §11 #9 |
| D16 | Setup wizard | First light is a GraceUI flow in both views from v0.6; EldenNet's planned separate ImGui wizard is not built | A separate wizard with GraceUI as an extra | Under G1 the in-game menu is ImGui already | IA §11 #10 |
| D17 | Browser writes | From v0.4, when the keys exist; read-only before | From v0.6 | Testers tune pacing in v0.4 (X2) | IA §11 #11 |
| D18 | Cost | Calls and tokens; money only where the provider's response reports it | A price table | Prices are volatile | IA §11 #14 |
| D19 | Menu preferences | In `settings.toml` under `ui.menu`, never in browser storage | Browser storage | One file for both views | IA §11 #15 |
| D20 | Pacing presets | Quiet, Default and Lively, writing the six pacing keys; values from X2 | None | A safe way in for players who do not want six sliders | IA §11 #16 |
| D21 | Search | Settings, pages and names as shown only; never journals, keepsakes, conversations, World lines, event rows, request text or Docs' bodies (§4.3) | Full-text search | Search must not surface a spoiler or a private line | IA §3.3 |
| D22 | Fonts | Cinzel (display, 20 px or more), EB Garamond, Noto Sans, Noto Sans Mono; OFL, from their publishers, bundled; no system fonts | Forum, or EB Garamond's small caps, for display | Inscriptional capitals suit the setting; the rest are EldenNet's fonts already; Proton and the Deck lack Windows fonts | LOOK §3.4, §4 #1 |
| D23 | Type minimums | Prose 16 px or more; UI text 15 (running text 13.5); eyebrows 12; tags 11 in capitals | 16 px for everything | Dense settings and tables stay usable; NPC §10.7's 16 px is kept for prose | LOOK §4 #3 |
| D24 | Motion | Open 120 ms; close by the player 200 ms; close on danger 0 ms; page cross-fade 120 ms; no loops; a reduced-motion setting | EldenNet's 300–400 ms fade out | Control returns to the game and the player is waiting; danger outranks the look | LOOK §3.10, §4 #2 |
| D25 | Warning colour | `ember #DB9550`, always with an icon | EldenNet's toast warning `#D2A24C` | That yellow is as light as Grace's gold (1.08:1) | LOOK §4 #4 |
| D26 | HDR backdrop | Opaque in HDR; 96% in SDR; a player option | Darkening the game image first | A bright sky behind a 96% backdrop still shows at about 40 nits | LOOK §3.13, §4 #6 |
| D27 | Sound | None in v1 | UI sounds | Minimal; voice previews come with v1.5 through EldenNet's audio | ML lesson 14 |
| D28 | hudhook | Exactly 0.9.3 with no patch; `imgui-tables-api` stays off and tables are laid out by us; no texture uploads after load | A region-upload patch; enabling the feature | O25 stands; per-change uploads leak and stall | REN §2, §7 |
| D29 | Font atlas | One atlas shared with EldenNet's surfaces; at most ten menu faces; rebuilt only on a scale change with the menu closed, at most four times a session, logged | Rebuilding at every change | Each rebuild leaks one upload buffer under 0.9.3 | REN §2.4; §3.16 |
| D30 | Steam overlay browser | A hand test in S4 only; no code | An "open in the Steam overlay" button | A new kind of Steam call for an unproven path | REN §4.7 |
| D31 | Local server | *Changed in part by D60 (G4): the link carries no token, and one session cookie is used; and by D66 (G9): paired devices on the home network, each with a device cookie, once the player switches that on.* Loopback only, a per-session token in the URL fragment, a WebSocket ticket, Host, Origin and Fetch-Metadata checks, no CORS, no cookies, a strict CSP, write-only secrets, nothing logged | Fewer checks | Known attacks on local servers (DNS rebinding, CSRF, WebSocket hijacking) | §7.2; REN §6 |
| D32 | Web build | *Changed by D53 (G2): shipped in the `GraceUI\web\` folder and served from there.* Built in GraceUI's CI, committed at release tags as `web/dist`, rebuilt reproducibly in CI, embedded by `grace-serve` | Node in EldenNet's build; loose files | EldenNet's build stays Rust-only; nothing loose to mix | §2.7 |
| D33 | Opening on a pad | No default chord in v1; Steam Input or a Deck back button mapped to F5, documented; a chord later if S4 finds one free | A default chord | Every pad button has a game function | §5.4 |
| D34 | While the menu is open | Lines addressed to the Tarnished wait, as while typing; barks and NPC-to-NPC lines continue; EldenNet's toasts draw above the menu | Pausing EldenNet | Lines are not spent on a player who cannot see them | §5.8 |
| D35 | Third-party code | Our own code only; nothing from Magelight in v1, so no `NOTICE` entry yet; candidates for later reuse with attribution are listed in ML §1 | Reusing Magelight's SDK | The protocol is EldenNet-specific; the rules are simpler with none | ML §1 |
| D36 | NPC types layout | Master–detail; a grid view in the browser on wide screens | A 15-column grid everywhere | It fits the menu's width and a pad | IA §5.3 |
| D37 | Versions | GraceUI's minor versions follow EldenNet's increments; the protocol is `MAJOR.MINOR` with named capabilities | Independent numbering | Testers can match the two at a glance | §2.7, §6.4 |
| D38 | Structure | SkyrimNet's framework categories: Home, AI, Voice, Characters, History, Behaviour, System, Tools; Settings, Docs and Content packs at the foot; an accordion rail with pages, a top bar, a Home card grid, regular page parts (§4.2). Supersedes D2's rail of sections | The first proposal's flavour-named sections (FOLK, VOICES, WORLD, CRAFT, UPKEEP) | The owner, 2026-11-07: EldenNet is a framework, so the aim is categories of an AI framework, taken from how SkyrimNet groups them | Owner, 2026-11-07; SND §1, §4 |
| D39 | Names | Plain framework names in British spelling in the rail and titles; Elden Ring features and settings inside pages, in our own words; game names only at run time; flavour names and `ui.menu.names` dropped. Supersedes D4 | Flavour names with plain subtitles | A framework's categories should read as such; one set of names is easier to search and to document | Owner, 2026-11-07; SND §5.1 |
| D40 | Settings page | Holds only the settings with no category page (General, Hotkeys, Menu, Browser view, Developer), links out to the rest, and has an All settings tab | SkyrimNet's second full tree of settings | One home per setting (invariant 12); SkyrimNet itself links out for some topics | SND §3, §6 #2 |
| D41 | Top bar | Search, the status lamp, Live updates and the journey in view (view state), the theme and Quick switches (shortcuts), close in game | SkyrimNet's three controls only (refresh, theme, quick toggles) | Status must stay visible; NG+ journeys scope History; a quick switch must never be a second home | SND §1.3 |
| D42 | Home | A customisable grid of cards (`ui.menu.home_cards`) with a Setup band that collapses after the first passing self-test | A fixed layout | SkyrimNet's pattern; players choose what they watch; hidden cards cost nothing | SND §1.7 |
| D43 | Text presentation | Voice › Text and subtitles holds the line display settings in v1 | A Display page under Settings | Text is the voice until v1.5, so Voice is useful in v1 | §4.6.3 |
| D44 | History and the monitor | History › Events (stored, perceived events only), Conversations, Memories, World; live events and decisions on Tools › Event monitor | One page for both | SkyrimNet splits the record from the live monitor; SP7 applies to both | SND §2.4, §2.7 |
| D45 | Page switches | A feature's master switch lives on its page (Memories, Awareness, Speaking up, Conversation) | Every switch on Settings › General | SkyrimNet's pattern; one home each | SND §4 |
| D46 | Docs and content | Docs bundled and offline; Content packs read-only, with no hub | An online wiki; an add-on hub | PV6; D2 | SND §2.8 |
| D47 | Combat | A hub page of shortcuts; its keys live in Reactions, Pacing, NPC types and Models | Combat keys homed on Combat | Presets and the reaction table stay whole | §4.6.6 |
| D48 | Gold for wayfinding | Section labels and Home card titles are uppercase eyebrows in `gold`, and stat values are `gold`; column heads and captions stay `ink.faint`. Principle 1 of §3.1 says so | Every eyebrow in `ink.faint`, as first proposed | Dense framework pages are scanned by their section labels; `gold` passes 7.5:1 on cards (§3.3); it stays one hue and is never decoration | §3.1, §4.2.4 |
| D49 | Model-request log | AI › Requests (`ai.requests`) | A view inside System › Logs, as SkyrimNet has it | Calls belong with providers, models and the self-test; Logs stays a scrubbed log (PV5) | SND §2.6, §5.2 |
| D50 | Kind rows on NPC types | Each kind with slots or types has its own row ahead of them ("Named, every NPC", "Merchant, every type", "Summon, every type" as well as humanoid and creature); a kind whose only type is itself is one row | Kind rows for humanoid and creature only; a `named` row only under G8 (b) | Every inherited source an allowance names must have a row to open and set (`allow."<kind>"`, EldenNet DESIGN §7.6's precedence), under either answer to G8 | §4.6.4 |
| D51 | GraceUI's folder | `Game\GraceUI\` in the game's install, beside `eldenring.exe`: `graceui.dll`, `graceui.toml`, `web\`, `fonts\`, `LICENSE.txt`, `NOTICE.txt`, `README.txt`. GraceUI puts nothing anywhere else and writes nothing there at run time; EldenNet finds it from the game's own module path | Inside EldenNet's install folder; under `%APPDATA%` | The owner's G2 words name the game's `Game` folder; one folder is one thing to copy over | Owner, G2 |
| D52 | The version file | `graceui.toml`: GraceUI's version, the ABI's major and minor, the protocol's MAJOR.MINOR and caps, the release date, and every file with its size and SHA-256. EldenNet loads only what it lists, and the server serves only listed web files | The DLL alone reporting its version | A player or a report can name the version without starting the game; a half-copied folder is caught at start; nothing stray in the folder is ever served | ML §3.15; §2.7 |
| D53 | Bundling and pins | Each EldenNet release carries GraceUI's release folder byte for byte: the latest GraceUI release at EldenNet's release date, checked by EldenNet's release job with the handshake; its release notes name it; EldenNet links GraceUI's host kit (`grace-abi`, `grace-host`, `grace-protocol`, `grace-serve`) from the same tag | Rebuilding GraceUI inside EldenNet's release | The owner's G2 words; a bundled copy and a downloaded one are the same bytes | Owner, G2 |
| D54 | Missing, older, newer, mixed | EldenNet keeps playing with its own surfaces in every case. Missing: no menu, a notice on F5, settings in the file. Mixed or unreadable files, or another ABI or protocol major: the menu stays off with a notice naming what to install. An older minor works, and settings whose home page it lacks appear under Settings › All settings as "Newer settings"; a newer minor works with the capabilities EldenNet announces. The browser follows the same rules for the web app (§2.9) | Refusing to start; loading whatever is there | EldenNet's subtitles, chat box and call-outs never depend on GraceUI, and a player always has a way to their settings | §2.8, §2.9 |
| D55 | The bridge in process | The same JSON frames as the WebSocket (§6.3) cross `graceui.dll`'s C ABI: the core encodes each revision once and hands it to GraceUI on T6, where it is parsed; commands go out through a bounded ring with a non-blocking send. A binary encoding only as a later capability if §8's budget needs it | Typed Rust values; a binary encoding now | Rust types do not cross separately built DLLs; one encoding, one fuzzer and one set of golden frames serve both views | §6.2, §2.9 |
| D56 | Drawing across the boundary | `graceui.dll` lays out and tessellates; `grace-host` checks each frame's vertex list (counts, indices, clip rectangles, texture ids) and copies it into ImGui's draw list through `hudhook::imgui`; `graceui.dll` links no hudhook, ImGui or MinHook, installs no hook, starts no thread and opens no socket or file. Follows G10's recommendation while it is Open | One ImGui context shared by two DLLs; GraceUI calling ImGui through a host API | ImGui's context and allocator are per copy of the library; a second hudhook brings a second MinHook (ER02 §7.1); a narrow contract changes rarely | G10 (b) |
| D57 | Text fields in game | GraceUI's own single-line field in `grace-view` (caret, selection, keyboard editing, paste only on a deliberate press, the key field drawn as discs), with the IME window placed at the caret rectangle it reports | ImGui's `InputText` | ImGui's widgets are not reachable from `graceui.dll` under D56 | §5.6 |
| D58 | Dashboard address | `http://127.0.0.1:8642` (also `localhost` and `[::1]`); `dashboard.port` from 1024 to 65535 | SkyrimNet's 8080 | 8080 is llama.cpp's legacy port, which EldenNet probes (ARCH §11.4), and a common default of local tools; 8642 is the default of no local model server EldenNet probes, and testers confirm it is free from v0.2 | ARCH §11.4 |
| D59 | The off switch | `dashboard.enabled`, on by default, homed in Settings › Browser view; off means no socket | No switch | Always on is the owner's default; a player who wants no listening port, such as on a Deck in game mode, can still have none | Owner, G4 |
| D60 | Loopback sign-in | A session cookie (256 random bits, new at each game start; `HttpOnly`, `SameSite=Strict`, host-only, no expiry) is set on any top-level page load that arrives on the loopback listener with a loopback `Host` (never on the network listener, D66); API calls and the WebSocket ticket need it as well as `Origin` and Fetch Metadata; the page acts on nothing in its URL. Changes D31 in part | No credential on loopback, the other checks only; keeping the token in the link | The player types the address and nothing else (G4); the cookie is a second lock beside Host and Origin against web pages and survives a reload; what limits other programs is §7.1's | §7.2; REN §6 |
| D61 | Keys bound to their address | Changing a provider's base URL or kind, from either view, clears its key and its extra header values, which must then be set again | A confirmation in game for every address change | No visitor to the dashboard can send a player's key to another address | §7.4 |
| D62 | The WIP tag | "WIP" on the four later-version rail entries and their flyout rows, beside statement-page titles and on AI › Models' dimmed Decisions card; tooltips and accessible names say when; settings rows keep their availability badges | "WIP" on rows too | The owner named the entries' tag; a row's badge says when the setting starts to work (§4.1 principle 3) | Owner, G7 |
| D63 | Playthrough's place | Characters › Playthrough (`characters.playthrough`), first in Characters | History › Playthrough | It is about people and how they are now, beside the roster it complements; History holds records of what happened, and its World page stays the record of world stages | Owner, G5 and G8 |
| D64 | Roster and playthrough | Named NPCs and NPC types hold what is the same in every playthrough (general profiles, allowances, reactions, the news temperament); Playthrough holds this journey's state. Named NPCs' "Now" and "Has heard of" move to Playthrough, and the roster is usable at the title screen | One page for both | Under G8 the roster is shown before any meeting, so the journey's state needs a page bound to the journey | Owner, G8 |
| D65 | What Playthrough may say | Status from perception only (here now; alive when last met; away, when the Tarnished passed where they were last met and did not perceive them; fell, when the Tarnished saw it); a story line only for the stage active at the last meeting (EldenNet's `stage_seen`), never a later one; related stories only for met people the stage's own line links; notes only from records; never a next step, a "has something for you" mark, a fate or a count of what is left; its lines and notes are never searched (D21) | Reading flags directly; quest tracks | A flag can hold before its effect is seen (D11); `stage_seen` is what the Tarnished witnessed | SP1–SP9; MEM §2.2 |
| D66 | Pairing a device (G9) | A six-digit code shown in game for two minutes, one use, void after five wrong tries; one random device cookie per device, kept by EldenNet only as a hash with its name and date in `devices.toml` until the player forgets it; Forget revokes it at once and closes its connections, whether `dashboard.lan` is on or off; `dashboard.lan` off closes the network listener and every device connection; the network listener accepts only device cookies and never grants the session cookie; paired devices are refused `secrets.set`, `secrets.clear` and `providers.update`; this PC (a connection on the loopback listener) never needs a code | A host field with no pairing; a code per session; a code shown in this PC's browser too | The owner's G9 words; a key is never typed over the network; a code shown in a browser could be read by another user signed in on the same PC (§7.1) | Owner, G9; §7.2 item 16 |

---

## 14. Sources

- **The owner's request** (2026-11-07): a separate repository named GraceUI; a layout like SkyrimNet's dashboard; the
  appearance of SeverActions' menu (built on Magelight UI); Elden Ring tabs and settings; earlier the same day, a main
  EldenNet UI on a hotkey such as F5. Later the same day, on the first proposal: the layout was not yet like
  SkyrimNet's, and EldenNet is a framework, not a game plugin, so the menu should hold categories of an AI framework,
  taken from how SkyrimNet groups them, with what Elden Ring's version needs (D38, D39).
- **EldenNet's design** (branch `design/v1-proposal`): DESIGN §1.2, §1.3, §3.4, §4.4, §6.4, §7.6, §8.1, §9, §10,
  §11.2–§11.5, §12, §13, §16.2, §19
  (O1, O8, O9, O11, O16, O17, O18, O20, O25, O28–O42); ARCH §1.2, §2.3, §11.2, §11.5, §11.6, §12.4; NPC §5.4, §9.1,
  §10.1, §10.6, §10.7; MEM §9.5, §12.8; ER02 §7.1; SN01 §6, §9, §14.
- **GraceUI's research**: [ML](research/magelight.md), [LOOK](research/severactions-look.md),
  [IA](research/information-architecture.md), [REN](research/renderers.md), [SND](research/skyrimnet-dashboard.md)
  (from the owner's screenshots of SkyrimNet's dashboard, its devkit's documents, and the dashboard itself rendered
  in a sandbox at the owner's request; structure only).
- **The owner's answers** (2026-11-07) to G2 and G4–G8, the follow-up on G5 and G8, the owner's question how
  SkyrimNet handles G4's question of access, answered in G9's row from SND, and the owner's approval of G9's
  recommendation; all quoted in §12.
