# Proposal 0001: GraceUI as a UI framework for any Elden Ring mod

**Status: proposal, 2026-11-08 (EldenNet roadmap S7.8.a).** It changes nothing by itself: the decided design stands
until the owner answers G15 to G20 (§8), and each answer's effect is stated with its question. What it settles itself
is listed as proposed D-entries (D69 onwards, §4.16), each open to the owner. Evidence: [FW](../research/ui-frameworks.md)
(shared UI hosts for game mods, with the Skyrizz session's report on Skyrim's three hosts), [ML](../research/magelight.md),
[REN](../research/renderers.md). A bare § is a section of [DESIGN.md](../DESIGN.md); "§4.n of this proposal" is said in
full.

## 1. What the owner decided

On the interactive showcase (G13's later change, 2026-11-08):

> also, I intent to make GraceUI as a standalone just a UI that is capable to present EldenNet in-game, so the
> interactive UI should be part of EldenNet and not GraceUI.

Asked whether the browser view stays part of GraceUI (G14, 2026-11-08), the owner widened the question:

> I want GraceUI to be something other modders can use, like Magelight or prismaUI for SKyrim. EldenNet will require it
> as dependency, but it will always ship with GraceUI anyway.

So GraceUI becomes a UI framework that any Elden Ring mod can use, EldenNet requires it and always ships it, and
EldenNet's menu and dashboard become its first user (G14, amending G11). What that does to whose overlay draws in game
(G10), what draws other mods' pages (G1), the browser view, the interface other mods use and which pages are EldenNet's
own is this proposal's subject.

## 2. The framework in one page

```
One game process: one hudhook overlay, owned by GraceUI (G15 (a))

  eldennet.dll ──┐                         Game\GraceUI\graceui.dll
  another mod ───┼── GraceUI_RequestApi ─►  grace-host: hudhook 0.9.3 (dx12), its one MinHook, the Present hook,
  another mod ───┘    (the public C ABI)      the window procedure, DInput and XInput, capture, notices, textures
        ▲                                   registry of spaces and panels; input arbitration (one holder)
        │ commands, events (pulled)         grace-view + grace-draw: page data → the menu as vertex lists
        │                                   draw-list checker for every mod's panels
        └────── topics, results ──────────► grace-serve (G17 (a)): 127.0.0.1:8642, GraceUI\web\, every opted-in space
```

- **What a mod writes** (G16 (a)). A *space*: its pages and settings as data in GraceUI's component vocabulary, with
  topics it publishes and commands it answers in the bridge protocol (§6), all across a plain C ABI. GraceUI draws the
  pages in game with Dear ImGui and the Grace theme (G1) and, if the mod opts in, in the player's browser. For anything
  the vocabulary cannot show, a mod adds *panels*: in-game surfaces it draws itself as checked vertex lists in Dear
  ImGui's default `ImDrawVert` layout with 16-bit indices, made with GraceUI's Rust drawing kit or with its own
  headless Dear ImGui in that default build (checked in 1.89.2 and 1.92.6), so no ImGui context, allocator or Rust type
  ever crosses a DLL boundary (FW §4). No HTML in game.
- **One host per process** (G15 (a)). `graceui.dll` holds the only hudhook, MinHook and Present hook; EldenNet links
  none and draws its subtitles, chat box, call-outs, labels, toasts, banner and F11 view through GraceUI, like any mod.
- **Found by name, versioned by table.** A mod finds `graceui.dll` by name and one export, from a worker thread with a
  bounded wait; the export returns an append-only table per ABI major; every struct starts with its size; a mod too new
  for the installed GraceUI gets an explicit "too old" answer with both versions, never silence.
- **One menu, many spaces.** One key (F5) opens one menu; each mod is a space with its own rail of categories and pages;
  a switcher appears only when two or more spaces are registered, so an EldenNet-only player sees today's menu.
- **Honest input.** One holder at a time; a request is granted or refused with the holder named; nothing deposes,
  nothing queues behind an Ok, and one exit stays the player's whoever holds input. GraceUI reads no game memory:
  danger, loading and game menus come from optional world-state providers (EldenNet when present), combined, and any
  provider's danger closes every view at once.
- **EldenNet's pages are EldenNet's content** (G20 (a)), shipped in EldenNet and registered at start; GraceUI keeps the
  frame, the components, the theme, the server and a demo space with invented content (D82).
- **Shipped once, in one place.** `Game\GraceUI\` is the only copy; mods require it and never carry a private copy
  elsewhere; a second copy idles; EldenNet's launcher installs its bundled GraceUI only where none or an older one is
  found (D83).
- **Timing** (G18 (a)). Built from GraceUI 0.1 and used by EldenNet from its v0.1; opened to other modders with a guide,
  samples and a frozen ABI 1 after EldenNet v1.0, as GraceUI 1.1.

## 3. What changes and what does not

Marks: **Unchanged**; **Changes** (whatever the answers); **Depends on Gn** (the effect of the recommended answer is
given; the other answers keep the item as it is unless said).

| Item | Says now | Under this proposal | Mark |
|---|---|---|---|
| G1 renderer | Dear ImGui with the Grace theme in EldenNet's overlay; HTML in game a later, owner-gated spike | The renderer stands for every mod's page data; "EldenNet's overlay" becomes GraceUI's; no HTML in game for any mod | Depends on G15, G16 |
| G2 standalone mod | Own folder and releases; carried by EldenNet; "other mods … v1 still leaves out"; cost counted without registry, arbitration or sandbox | Folder, carrying and update by hand stand; the cost basis no longer holds (registry, arbitration and a handshake per consumer return); G2's "keeps playing whatever it finds" is amended by D88 | Changes; depends on G15 (D88) |
| G3 Preact | The browser app | Holds for GraceUI's web app, which renders every opted-in space | Depends on G17 |
| G4 always on | EldenNet serves the dashboard from the end of its gate and safety steps | GraceUI serves it whenever a space asks for "always" (EldenNet does, only once its gate and safety steps pass); no socket opens or stays open after an anti-cheat hit, GraceUI's own or a provider's (§4.9 of this proposal); the player's switch still turns it off | Depends on G17 |
| G5, G7, G8 roster, WIP, Playthrough | EldenNet's content in GraceUI §4 | Unchanged in substance; recorded in EldenNet with its pages (D82) | Unchanged |
| G6 Deck scale | `ui.scale = 1.35`, one scale for all EldenNet text | Still one scale and one default: `ui.scale` becomes GraceUI's setting (D84), written as 1.35 on a Deck's first run as G6 decided, and EldenNet's subtitles and other text follow it through the host's display-changed event, which carries the scale | Unchanged (moved) |
| G9 pairing | `dashboard.lan`, a code shown in game, paired devices refused EldenNet's three key commands | Moves with the server; a space marks its own sensitive commands | Depends on G17 |
| G10 overlay | (b): EldenNet keeps it; `graceui.dll` hooks nothing | Reopened: GraceUI hosts | Depends on G15 |
| G11, G14 | GraceUI as EldenNet's menu; widened to a framework | Recorded | Unchanged |
| G12 SkyrimNet | May be used in GraceUI | Whether it may reach other mods through GraceUI | Depends on G19 |
| G13 showcase | EldenNet's | Consistent with D82 | Unchanged |
| Invariant 1 projections | No view receives raw content (EldenNet's spoiler rule) | Framework form: a view shows only what its space published; EldenNet's form moves to EldenNet | Changes; the move depends on G20 |
| Invariant 2 absence | Nothing not reached is shown; the roster exception | Framework form: GraceUI never shows, counts or hints at what a space did not publish; the rest is EldenNet's | Changes; the move depends on G20 |
| Invariant 3 one command path | EldenNet's one entry point | One per space; GraceUI routes a command only to the space that owns the page | Changes |
| Invariant 4 loopback | EldenNet's server rules | The same rules, in GraceUI's server | Depends on G17 |
| Invariant 5 secrets | Write-only | Holds for every space's masked field | Unchanged (widened) |
| Invariant 6 no outside requests | CSP, bundle scan | Holds; no mod ever supplies web code (D81) | Unchanged (widened) |
| Invariants 7, 8 close and masking | One close routine; the closing press masked | Across every input holder, the menu and panels alike | Changes (widened) |
| Invariant 9 danger closes | EldenNet's `safe_context` | Every view closes on any provider's danger signal; without a provider, no danger close, documented (D78) | Changes |
| Invariant 10 present thread | On both sides of the ABI | Also binds every opted-in `on_present` callback, with a budget (D76) | Changes (widened) |
| Invariant 11 no upload after load | hudhook leaks a buffer per upload | A per-mod texture budget and a registration window (D76) | Changes |
| Invariants 12, 13, 14 | Page data one source; one token file; no loops | Per space; Grace's token file for every space's pages; no loops anywhere | Unchanged (widened) |
| Invariant 15 one ABI | EldenNet drives `graceui.dll`; "EldenNet keeps playing" | One public ABI checked at every connect; EldenNet is one consumer; "keeps playing" is replaced (D88) | Depends on G15 |
| Invariant 16 hooks nothing | No hudhook, ImGui, MinHook, thread, socket, file or game access | Replaced by "one host, one hook set" (§4.1 of this proposal) | Depends on G15, G17 |
| New invariants | — | 17 Honest focus; 18 One copy (§4.5, §4.11 of this proposal) | Changes |
| §0, §1 scope | "No host API for other mods in v1" | A framework; the public API opens with G18's answer | Changes |
| §2 architecture | EldenNet hosts; two deliverables | Rewritten around the host (§4.1–§4.4 of this proposal) | Depends on G15 |
| §2.5 pages as data | EldenNet's IA; "adds the component to both views" | The public page format; a mod cannot add a component, so the vocabulary is versioned with the protocol | Changes |
| §3 the Grace theme | EldenNet's menu | The one look of every space's pages (D80) | Unchanged (widened) |
| §3.16 atlas | EldenNet's surfaces and the menu share one atlas | Shared by every mod that connects before warm-up, with a face budget per mod | Changes |
| §3.17 never | "Copy SkyrimNet's dashboard styling, icons, code or text": never | Written before G12; whichever G19 answer, this line needs aligning with G12 | Depends on G19 |
| §4 information architecture | EldenNet's pages, SP1–SP9, PV1–PV8 | Moves to EldenNet (D82); GraceUI keeps the frame, page parts, search mechanics and PV3, PV6, PV7 as framework rules | Depends on G20 |
| §5 input | "Capture stays EldenNet's" | GraceUI's, with arbitration (D77) | Depends on G15 |
| §6 protocol | EldenNet's topics and commands | Generic envelope with a space per channel; EldenNet's payloads are EldenNet's schema | Changes |
| §7 security | EldenNet's server; both views untrusted | Adds other mods in the process (native, trusted as any DLL) and per-space isolation (D81) | Changes |
| §8 budgets | Menu ≤ 0.8 ms p99 | Per mod and in total (§4.4 of this proposal) | Changes |
| §9 tests, §10 increments, §11 risks | EldenNet's case | New rows (§4.13, §6, §7 of this proposal) | Changes |
| D19 menu preferences | In `settings.toml` under `ui.menu`, never in browser storage | In GraceUI's own settings in `Game\GraceUI\user\`, still never in browser storage (D84) | Changes |
| D37 versions | Minors follow EldenNet's increments | Until 1.0; then GraceUI's own semantic versions (D74) | Changes |
| D51–D54 folder, version file, bundling, mismatches | GraceUI writes nothing in its folder at run time; EldenNet keeps playing | D51 amended by D84: GraceUI writes at run time only under `user\`, which `graceui.toml` does not list and no release contains; D52 stands, its hash list excluding `user\`; D53 stands with the launcher's install step (D83); D54 replaced (D88) | Changes (D51); depends on G15 (D54) |
| D55, D56, D57 | In-process bridge; vertex lists across the boundary; GraceUI's own text field | D55 per space; D56's checker serves every mod's panels; D57 stands | Changes (D55, D56) |
| D58–D61, D66 | Port, off switch, cookie, keys bound to address, pairing | D58–D60 and D66 move with the server; D61 stays EldenNet's, a rule of its provider settings that moves with its pages (D82, §5 of this proposal, item 9), since GraceUI writes no mod's settings | Depends on G17 |
| EldenNet O25 | The pin and its rules in `en-overlay` | Move to GraceUI's `grace-host` by the owner's amendment | Depends on G15 |
| EldenNet O47, O48 | No me3, no fromsoftware-rs | Bind GraceUI as a framework too; GraceUI listed as a native in a player's own loader is "merely compatible" | Unchanged |
| Read-only towards the game | GraceUI never reads or writes game memory | Kept: game state comes from a provider (D78); hooks are on system DLLs only | Unchanged |

## 4. The design, part by part

Each part gives the choice, its reasons, what it gives up and its risk. Parts that need the owner name the question.

### 4.1 The one overlay and the Present hook (G15)

- **Choice.** GraceUI hosts, in `graceui.dll` itself (D69): `grace-host` takes `hudhook = "=0.9.3"` with `dx12` only,
  `imgui` only through `hudhook::imgui`, MinHook only through `hudhook::mh`, never the eject path (every O25 rule,
  moved); it hooks DXGI Present, the window procedure (with EldenNet's designed `WM_CHAR` surrogate fix and IME
  placement) and DInput `GetDeviceState` and XInput `XInputGetState` for capture and masking (R12's detours, moved).
  GraceUI finds `GetDeviceState` as EldenNet does, through a throwaway device's vtable in `dinput8.dll`. Where EldenNet
  hooks `XInputGetState` in "whichever XInput DLL `eldenring.exe` imports" (EldenNet ARCH §3.2), GraceUI reads no
  import table of the game's: it hooks `XInputGetState` in every XInput DLL already loaded at install, found by
  `GetModuleHandleW` by name
  (`xinput1_4.dll`, `xinput1_3.dll`, `xinput9_1_0.dll`), which keeps the read-only rule; one loaded later is not hooked
  and is logged (S4 confirms which the game loads). The pure crates (`grace-protocol`, `grace-view`, `grace-draw`) keep
  their no-Win32 rule. EldenNet links no hudhook or MinHook, and no ImGui renderer backend; its F11 view keeps a
  headless Dear ImGui of its own (§5 of this proposal, item 2).
- **Invariant 16 becomes "One host, one hook set":** `grace-host` is the only code that holds hudhook 0.9.3, a MinHook
  or a Present hook for GraceUI and its users; no consumer links hudhook or a MinHook, or hands GraceUI an ImGui
  context; a consumer may build its draw lists with a headless Dear ImGui of its own, with no renderer backend and no
  hooks (§4.2 of this proposal); the pure crates take no Win32; CI checks the crate graph (only `grace-host` depends on
  hudhook), `graceui.dll`'s imports against a reviewed allowlist (hudhook's needs named), and EldenNet's import table
  and symbols for any hook library.
- **Before it hooks**, the host runs EldenNet's anti-cheat check (EldenNet ARCH §5.3: an `EasyAntiCheat` module or a
  `start_protected_game.exe` parent); the single-instance guard has already run in the first request (§4.4 and §4.11 of
  this proposal). Its hooks are on system DLLs, so it needs no game-build gate; EldenNet's own gate stays EldenNet's.
- **On an anti-cheat hit** (D88) GraceUI keeps EldenNet's decided L1 banner: it installs its hooks as EldenNet's banner
  path does today (EldenNet ARCH §1.4, §3.2), with capture never on and the input detours pure pass-through, and draws
  notices only: its own, and `notice` from consumers, which stays callable after `connect` has answered `inactive
  (anti-cheat)`. It serves no menu, panel, input or socket for the rest of the session. The S0-E spike checks this path
  again. *Not proposed:* hooking nothing on a hit, so that EldenNet's banner gives way to the launcher's next-run hint;
  it would change what EldenNet decided its players see.
- **Reasons.** One process holds one hudhook and one MinHook safely (FW §4; ER02 §7.1); a framework other mods use
  without EldenNet must be present without it; every shared host found in the game is one module (FW §2); the owner's
  "EldenNet will require it" then holds literally; nothing is built yet, so moving now costs a design pass, while moving
  after v1 would rewrite EldenNet's surfaces twice.
- **Gives up.** G10's isolation: a fault in the host stops EldenNet's subtitles and chat as well as the menu (a hard
  fault ends the game under any answer); EldenNet cannot draw even its banner without GraceUI, so its launcher checks
  GraceUI first (D88); hook code now ships in a release players replace by hand.
- **Risk.** GraceUI 0.1 must be ready for EldenNet v0.1 (§6); a second hooking overlay from another mod is outside
  GraceUI's control either way (D86).
- **Not proposed.** A second, hook-free GraceUI module beside the host (G10 (b)'s split kept inside GraceUI) adds a
  private ABI and a second copy to guard for no gain in isolation, since both run in one process (D69). A dual mode,
  where EldenNet hosts when present, is two hand-off paths around a one-MinHook rule.

### 4.2 What a mod writes, and what G1 means for other mods' pages (G16)

- **A space** (D75): an id (`[a-z0-9-]{3,32}`, unique, refused if taken), a display name, a version, the GraceUI floor
  it needs, the views it wants (`game`, `browser`), its rail of categories and pages, and a settings description in
  §6.9's shape. Page data is handed over as UTF-8 JSON bytes at `register_pages`, from wherever the mod keeps it
  (compiled in or its own folder); GraceUI never reads another mod's files. Its blocks are GraceUI's components bound to
  the space's own topics.
- **Topics and commands.** The mod publishes state and stream frames (§6.1, §6.3) for its topics with `publish`, and
  drains subscriptions and commands with `poll` on its own thread (§4.4 of this proposal). It validates every command
  itself (invariant 3, per space) and writes its own settings; GraceUI writes no mod's files.
- **Panels** (D76): an in-game surface a mod draws itself, on a layer (`hud` < `panel` < `menu` < `modal` < `notice`),
  with an anchor, bounds and whether it may take input. The mod submits draw lists: vertices in `ImDrawVert` layout
  (position f32×2, UV f32×2, colour RGBA8), u16 indices relative to each command's vertex range, commands with a clip
  rectangle, a texture id and ranges, exactly the shape §2.9 already checks. A producer may be GraceUI's Rust kit
  (`grace-draw` and `grace-view` as crates, with glyph tables from the shared atlas, `glyph_table` in §4.3 of this
  proposal) or the mod's own headless Dear ImGui (no renderer backend, no hooks) with the default `ImDrawVert` layout
  and 16-bit `ImDrawIdx`, the same in 1.89.2 and 1.92.6 (FW §4); a build that overrides either (Dear ImGui allows both
  in `imconfig.h`) converts its lists before `submit`, and the checker refuses anything else. Textures are registered
  once, before the mod's first submission, within a budget; a mod on Dear ImGui 1.92 or later keeps
  `ImGuiBackendFlags_RendererHasTextures` off and builds its atlas once.
- **What G1 means.** G1's reasons hold for every mod: hudhook 0.9.3 cannot update a texture without leaking a buffer
  and stalling the present thread (REN §2.1), so no mod gets HTML, video or live images in game; page data is drawn
  with Grace in both views, panels only in game. HTML in game stays G1's later spike, with its three approvals.
- **Reasons.** Both views from one source and no third-party script in the browser (§4.9 of this proposal); panels let
  a mod draw anything without sharing a context, an allocator or a Rust type (FW §4, F3); EldenNet needs panels for its
  own surfaces under G15 (a), so offering them to others costs only documentation and tests.
- **Gives up.** Web skills, React and HTML pages, which Magelight UI and PrismaUI offer; pages limited to the
  vocabulary, which grows only with GraceUI's releases; panels have no browser view.
- **Risk.** Modders used to HTML hosts may find the vocabulary narrow; a mod's panel code is native and can still crash
  the game, and players will report it against GraceUI (§7).

### 4.3 How a mod finds GraceUI, and the versioned C ABI

- **Finding it** (D70). The official helper (header-only C, and a Rust crate `graceui-sys` with a thin safe wrapper)
  runs on the mod's own worker thread, never in `DllMain`: `GetModuleHandleW(L"graceui.dll")` and its export; if absent
  or null, a scan of loaded modules for the export, taking the first that answers; if none, and the mod allows it,
  `LoadLibraryExW` of `<folder of eldenring.exe>\GraceUI\graceui.dll` with the DLL-folder and System32 search flags;
  polled every 250 ms for at most 30 s (EST). Absence means the mod's UI is off, never an error dialog.
- **Exports.** `GraceUI_Version()` (three numbers; needs no start, callable from any thread) and
  `GraceUI_RequestApi(uint32_t major)`, which returns a pointer to that major's table or null. The first request starts
  the host (D71).
- **The table** (`GraceUiApi1`) starts with its size, its ABI minor and GraceUI's version; then `has_cap(name)`,
  `connect`, `disconnect`, `register_pages`, `publish`, `poll`, `answer`, `register_texture`, `register_face`,
  `glyph_table`, `create_panel`, `submit`, `show_panel`, `hide_panel`, `request_input`, `release_input`,
  `register_hotkey`, `notice`, `set_world_state` and `log`. A minor only appends; a consumer calls a slot only if `size`
  covers it and checks `has_cap` for a feature.
- **Faces and glyphs.** `register_face` adds a face to the shared atlas before warm-up, within the per-mod face budget
  (§4.8 of this proposal), and is refused after it. `glyph_table` returns a read-only table of the shared atlas's glyphs
  with its generation number. If the atlas is rebuilt on a scale change (D29), every consumer gets an "atlas rebuilt"
  event with the new generation; `submit` carries the generation a list was built against when it uses the shared
  atlas, and the checker refuses a list from an older one, so no stale glyph position is drawn.
- **Rules** (D73, from the Skyrizz session's suggested ABI and Elden Ring practice, FW §1.2, §3): plain C, default x64
  packing; no C++ vtables, Rust types, STL, exceptions, panics or heap ownership across the boundary; every struct a mod
  passes starts with its size, and the host reads the smaller of the two sizes and defaults the rest, refusing one
  below the 1.0 size; strings are UTF-8 with a length and copied before the call returns; a user pointer on every
  callback; one compatibility rule written once in the header; an SPDX MIT line in every public header.
- **Answers.** `connect` returns `ok`, `too_old` (with the installed and the needed version: GraceUI shows a notice
  naming the mod and both versions, and logs one line), `duplicate_id`, `inactive` (with the reason) or `refused`.
- **Versions** (D74). One rule: the same major, and a minor at least the one the mod was built against, or slots checked
  by size. A new major is a new table from the same export; major N is served for at least two minor releases and 12
  months after N+1 (EST, the owner may set another window). From GraceUI 1.0 its version is its own semantic version
  (amending D37 from then on); `graceui.toml` and a feature table in the guide give the "added in" version of every slot
  and capability.
- **Reasons.** Every peer host uses this shape and it survives renaming and duplicates (FW §3, F4, F5); a version every
  mod can read and an explicit too-old answer avoid the silence the Skyrizz session met on two hosts (FW §1.2).
- **Gives up.** A public ABI is a support burden for years; Rust consumers need the `sys` crate rather than Rust types.
- **Risk.** A slot added wrongly cannot be removed; mitigated by the release header check (§2.9's rule, kept) and by
  keeping ABI 1 a preview until G18's answer.

### 4.4 Threads and calls

- **Choice** (D71, D72). GraceUI does nothing at load. The first `GraceUI_RequestApi` takes the single-instance mutex
  (§4.11 of this proposal) on the calling thread, once-guarded and before any table is returned: a `CreateMutexW`, cheap
  and outside the loader lock, since D70's helper never calls from `DllMain`. Only the copy that creates it goes on; it
  starts one short-lived init thread (anti-cheat check, wait for the game's window, install the hooks), which then
  exits; after that GraceUI runs on the game's present thread, its window procedure and the input detours, plus the
  server's threads if G17 (a). It never calls a mod on an unknown thread: commands, subscriptions, input for panels and
  host events are pulled with `poll`, from a bounded queue per mod, on the mod's own thread, with an optional event
  handle the host signals without waiting. `publish` and `submit` copy on the caller's thread (into a bounded ring and a
  triple buffer); the present thread only reads the newest.
- **Opt-in `on_present`.** A panel may ask for a callback on the present thread, for drawing that must follow every
  frame (EldenNet's subtitle fades and world labels). It binds invariant 10: no wait, no lock, no allocation per frame
  after warm-up, and a budget (0.5 ms p99 per mod, EST); a callback over budget for 120 frames running is suspended for
  the session with one notice naming the mod. It must not unwind across the boundary.
- **Budgets.** Menu open: ≤ 0.8 ms p99 at 1440p, as now; all panels together with the menu closed: ≤ 1.0 ms p99 (EST,
  S4 measures); a submission of more than 256 KiB of vertices or 64 commands is refused and logged (EST).
- **No game thread.** There is no call that posts work to the game's thread, because GraceUI calls no game code (the
  read-only rule) and the game has no task interface for plugins.
- **Reasons.** Third-party code off the present thread by default; one frame of latency is invisible for menus and HUD
  text, and Magelight's pages also run on a thread of their own, one game frame late, from 1.7.0 (unreleased at the
  commit read; Magelight `docs/TROUBLESHOOTING.md:196-211`, via the Skyrizz session, 2026-11-08).
- **Gives up.** "GraceUI starts no thread": it starts one at start and, under G17 (a), the server's.
- **Risk.** A mod that misuses `on_present` stalls the game; the budget catches slowness, not a deadlock.

### 4.5 Input, focus and capture: the honest answer

- **Choice** (D77). One input holder in the process at a time: the menu, a panel that takes input (EldenNet's chat box),
  or a modal. `request_input` is answered at once: `granted`, or `refused` naming the holder (its space and kind), or
  `not_now` with the reason (a world-state provider's danger, a game menu open, not ready). It never deposes the
  holder and never queues behind an Ok; a refused mod may ask to be told when input is free and then ask again
  (invariant 17, Honest focus). The holder keeps it until it releases, or until the one close routine ends it (danger,
  loading, focus loss, a fault, two seconds without a render), which releases capture, masks every held key and button
  until release (invariants 7 and 8, across every holder) and tells the holder why.
- **Keys.** Hotkeys belong to the framework (D77): a mod registers a named action with a default key plus modifiers or
  a pad chord; a later mod whose default is taken is registered unbound, with a line on GraceUI's Hotkeys page naming
  the holder; the player binds every mod's keys there. Hotkeys stand aside while any holder has input or a text field
  has focus. Escape stays the host's until a view reports ready, and a view that fails to load gives the input back.
  The menu key while a panel holds input shows "‹Mod› has the controls" and opens nothing.
- **The player's exit** (D77). One exit no mod can capture: a second press of the menu key within 2 s of "‹Mod› has the
  controls", or Escape held for 1 s (EST, S4 checks both; on a pad the menu key is Steam Input's F5, D33), runs the one
  close routine for the current holder, whoever it is and whether or not its view has reported ready. The host watches
  both in its own window procedure and input detours, before any holder sees them; the routine releases capture, masks
  the press until release (invariants 7 and 8) and tells the holder `closed (player)`.
- **Events for everyone.** Every consumer can watch an "input held by" event, so EldenNet's lines addressed to the
  player wait while any holder has input (D34, widened).
- **What GraceUI cannot do.** Negotiate with other overlays' own input hooks (an ImGui panel with its own Present and
  input hooks, a tool with its own window procedure), or stop other mods polling keys while the player types; both are
  documented, as §5.2 does now.
- **Reasons.** Silent deposing, a true that does nothing and an Ok that only queues are the failures the Skyrizz
  session met (FW §1.3, F6); first holder wins is Magelight's rule and the one a player can predict.
- **Gives up.** A mod cannot take the controls from another; the player closes one view before opening the next.
- **Risk.** A mod that never releases input keeps others out until the close routine fires; the player's exit (above)
  is never a holder's to capture, so the player can always end it.

### 4.6 Game awareness: when a view may open, and what closes it

- **Choice** (D78). GraceUI stays read-only towards the game. Each space may close its own views and refuse to open them
  (a gate state with a reason). Any consumer may register as a **world-state provider** and publish danger, loading,
  a game menu or text entry open, the title screen, multiplayer and an anti-cheat hit. Load order is not fixed (FW §3),
  so no claim is refused: GraceUI accepts every provider and combines them. Any provider's danger closes every view at
  once and cancels confirmations (invariant 9, widened); a view opens only when every provider allows it, or at the
  title; any provider's anti-cheat flag closes the server (§4.9 of this proposal). The Compatibility page lists each
  provider and what it last reported. EldenNet is a provider when present, publishing only these flags from its
  `safe_context`, its `ui` group and T10's anti-cheat check.
- **Without a provider**, views open at any time, there is no danger close, and the docs say so; GraceUI's capture
  still masks game input while a holder has it, so Escape and B never reach the game.
- **Reasons.** A framework that read game state would need EldenNet's game-access layer, its patch work and O48's
  independent checks (ER02); a provider gives every mod EldenNet's safety when EldenNet runs, at no cost to the rule.
- **Gives up.** A player with no provider can be hit while a menu is open; Escape may meet a game menu behind a view.
- **Risk.** A provider bug closes everyone's views, or a provider that never allows keeps them shut; the flags are few,
  EldenNet tests its own, and the Compatibility page names the provider holding a view shut.

### 4.7 The menu's shape for several mods

- **Choice** (D79). One menu on one key (`menu.key`, F5 by default, GraceUI's own setting), the frame (rail, top bar,
  search, key hints) GraceUI's. Each space brings its own rail of categories and pages. With one space the menu is
  exactly that space (EldenNet's menu as designed); with two or more, a space switcher heads the rail and LB/RB with a
  modifier, or Ctrl+PageUp/PageDown, change space (EST, S4 checks free keys). GraceUI's own small space sits at the
  rail's foot: Menu, Hotkeys, Browser view, Installed mods and Compatibility (each consumer, its version and state), and
  About with every licence. Search covers every space, each result badged with its space; a space can register a hotkey
  that opens the menu at its own pages. Mods' panels live outside the menu, on their layers.
- **Reasons.** One capture owner and one close routine are what keep input safe (invariants 7 to 9); a shared frame is
  what makes a framework feel like one (SkyrimNet's plugin settings page plays this role for Skyrim's plugins); players
  with EldenNet alone see no change.
- **Gives up.** A mod's own full-screen look and own key for its pages (its panels keep both).
- **Risk.** A long rail on a pad with several spaces; the switcher keeps one space's rail at a time.

### 4.8 The Grace theme and theming

- **Choice** (D80). Grace (§3) is the one look of every space's pages, in both views, from one token file (invariant 13,
  contrast checked in CI). A space cannot ship tokens in v1; a per-space accent token, checked for contrast at
  registration, is a later seam. Panels are drawn by their mods and look as their mods choose. The shared atlas holds
  Grace's faces, EldenNet's and those of any mod connected before warm-up, each added with `register_face`, at most two
  faces per other mod (EST), within D29's limits; a late mod brings its own atlas texture. A rebuild on a scale change
  raises the atlas's generation (§4.3 of this proposal).
- **Reasons.** One look and one contrast check for page data; fonts are bound by the one atlas and hudhook's upload leak
  either way (§3.16).
- **Gives up.** Mods' branding in the menu.
- **Risk.** Modders may want their colours; the accent seam answers that without a second token file.

### 4.9 The browser view, its server and their security (G17)

- **Choice** (under G17 (a)). `grace-serve` moves into `graceui.dll` with a small runtime of its own (two threads, EST),
  and serves GraceUI's web app at `http://127.0.0.1:8642` (D58) with every rule of §7.2 unchanged: loopback only unless
  the player switches `lan` on, Host, Origin and Fetch Metadata checks, the session cookie on the loopback listener
  only, tickets, a strict CSP, no CORS, only the files `graceui.toml` lists, rate limits, pairing with a code shown in
  game for other devices (G9, D66). It runs while any space registers with `browser = "always"` (EldenNet, by G4) or
  the player turns it on in GraceUI's space; the player's off switch wins (D59).
- **The gate clause of §7.2 item 1 and G4, kept** (D81). (1) EldenNet registers its space for the browser view, with
  `browser = "always"`, only once its gate and safety steps pass, and withdraws it on T10's anti-cheat hit; a later L1
  trip of another kind keeps it served, as §7.2 item 1 says. (2) GraceUI never opens a socket after its own
  anti-cheat hit (§4.1 of this proposal). (3) A later hit, from GraceUI's own module re-check (EldenNet ARCH §5.3's
  module clause, every 60 s on one of the server's threads while it runs) or from any provider's anti-cheat flag (§4.6
  of this proposal), closes the server for every space for the rest of the session. (4) An unknown game build binds
  EldenNet's space alone: GraceUI has no build gate (§4.1 of this proposal), so another mod's space may still be served
  while EldenNet is inactive for that reason, and EldenNet's own space is not, by rule (1).
- **One origin, isolated by our code** (D81). The web app is GraceUI's only code: no mod supplies HTML, script or CSS,
  so a space's pages in the browser are its page data rendered by our components, and a mod's strings are always text,
  never markup. Every frame carries its space; a page subscribes only within its space; the server routes each command
  only to the queue of the space that owns the page; no space reads another's topics through GraceUI; limits apply per
  connection and per space. A space marks its sensitive commands, which paired devices are refused (EldenNet marks
  `secrets.set`, `secrets.clear` and `providers.update`, as G9 decided). A space appears in the browser only if its
  registration asks for the `browser` view.
- **In process there is no isolation**: every mod is native code with the player's rights. GraceUI's checks protect
  against mistakes (malformed page data, oversized frames, out-of-range draw lists), never against a hostile DLL (§7.1,
  widened).
- **Reasons.** One hub: each mod publishes once and both views show it (D55 generalised); one page source (invariant
  12); no other Elden Ring host offers a browser view (FW F7). (`grace-dev`'s browser harness, D85, serves any space's
  page data with no game under either answer, so it is no reason for this one.)
- **Gives up.** `graceui.dll` opens a socket and runs threads; a mod's command bug becomes reachable from the browser as
  from the game; GraceUI keeps the server's security for every mod. G17 (b) would keep, even with G15 (a), a
  `graceui.dll` that opens no socket, and G4's port as EldenNet's alone behind its gate.
- **Risk.** A framework port on players' machines; kept loopback-only, opt-in for other mods and covered by the server
  suite with several spaces.

### 4.10 EldenNet's pages as content EldenNet ships (G20)

- **Choice** (D82, under G20 (a)). EldenNet's space (its categories, pages, settings description, help texts, Home
  cards), its fixtures, its spoiler and roster goldens, SP1–SP9, the EldenNet halves of PV1–PV8 and DESIGN §4's content
  move to EldenNet's repository once the answers are in (roadmap S7.8.c), compiled into `eldennet.dll` and registered at
  its start. GraceUI keeps the frame and page parts (§4.2), search mechanics, the settings row (§4.7), the components,
  the theme, PV3, PV6 and PV7 as framework rules, and a demo space with invented names and our own words for its own
  tests and harness.
- **Reasons.** The owner's "the interactive UI should be part of EldenNet and not GraceUI" (G13's later change); the
  spoiler rules are EldenNet's domain; a framework must not carry one user's content.
- **Gives up.** One repository for the whole IA; GraceUI's page lint and parity tests run on EldenNet's pages in
  EldenNet's CI, against a pinned GraceUI.
- **Risk.** Drift between EldenNet's pages and GraceUI's vocabulary; the lint ships as a GraceUI tool that EldenNet's CI
  runs.

### 4.11 Packaging, updates, the dependency and the two-copies problem

- **Choice** (D83). `Game\GraceUI\` is the only copy (D51), with `graceui.toml` listing every file's size and hash
  (D52). A mod states its floor and requires GraceUI; it never carries a private copy in its own folder. GraceUI keeps
  its user files in `Game\GraceUI\user\` (its settings, paired devices, its log), which no release contains, so copying
  a release over keeps them (D84). EldenNet carries the GraceUI release current at its date (D53) in a staging folder,
  and its launcher, before starting the game, installs it into `Game\GraceUI\` only when none is there or the installed
  one is older, never over a newer one, and verifies the hashes. An installed older release of the same major is
  replaced. One of an older major is replaced only while the bundled release still serves that major (D74's window), so
  mods built for it keep working; otherwise the launcher installs nothing, explains in E08's manner that mods built for
  the older major would stop working, naming those GraceUI's last log shows connecting on it, and installs only when
  the player confirms. A player updates GraceUI by copying a newer release over the folder, as G2 decided.
- **One copy (invariant 18).** A named mutex per process (`Local\GraceUI.Host.<pid>`), taken in the first
  `GraceUI_RequestApi` before any table is returned (§4.4 of this proposal), makes a second copy loaded from another
  folder idle: it installs no hook, answers every request with null, including its first, and the active copy shows one
  notice ("A second copy of GraceUI was loaded from another folder and is idle") and names it by relative name only
  (PV5). The helper's export scan finds the active copy.
- **Loading.** Any consumer may load GraceUI by its canonical path, and a player may also list it as a native in their
  own loader; both give the same module, and listing it alone starts nothing until a mod asks.
- **Reasons.** Bundled copies fight file by file in mod managers (FW §1.4, F8), and in this game a second hooking copy is
  a second MinHook (FW §4); the launcher already runs before the game, so it can install without a running DLL's lock.
- **Gives up.** A mod cannot pin "its" GraceUI; GraceUI must stay backward compatible within a major.
- **Risk.** A player extracting an old archive by hand over a newer GraceUI: the too-old notice names both versions and
  the mod that needs the newer one.

### 4.12 Docs, samples and harnesses

- **Choice** (D85). A modders' guide in `docs/modders/`: getting started, the space and page format, every component,
  the settings description, the protocol, the C ABI with its feature table, panels and draw lists (with "your own Dear
  ImGui" with its layout and texture rules), input and focus rules, threads and budgets, secrets (masked fields, and
  the advice to clear a key when its address changes, as EldenNet's D61 does), versions and the compatibility rule,
  distribution (require, never bundle a private copy), troubleshooting, a bug-report checklist (GraceUI's version, each
  consumer and its version, other overlays seen, Windows or Proton), "what you may do" under MIT, and how GraceUI is
  built (§4.14 of this proposal). Three example mods of our own with invented content: a C mod with page data only, a
  Rust mod with live topics and commands, a C++ mod drawing a panel with its own headless Dear ImGui.
- **Harnesses.** `grace-dev` becomes a fake host for any space: it loads a mod's page data and fixture states and
  serves them in the browser view with no game, for authors and CI. `grace-harness` (Windows) creates a Direct3D 12
  window, loads `graceui.dll` and a mod's DLL and feeds input, so an author can see the in-game view without the game
  (WARP, Direct3D 12's software device, in CI: LIKELY to work, to be confirmed). A small C mock of the ABI for authors'
  unit tests. GraceUI's log's first line names its version, ABI and protocol.
- **Reasons.** What Magelight and Meridian offer and what Skyrizz had to build itself (FW §1.4).
- **Gives up.** Writing and keeping a guide current is lasting work.
- **Risk.** Samples that drift; CI builds and runs them in the harness.

### 4.13 Tests

New, beside §9's: the ABI each way round (an older and a newer consumer, a smaller and a larger struct, null for an
unknown major, calls after `render_dead` doing nothing); two consumers, one faulty, the other still drawing; a second
copy at a second path going idle; two copies requested at once, from two threads, yielding exactly one non-null table;
a consumer that loads GraceUI by path while a loader also listed it; focus arbitration (refusals name the holder,
nothing deposes or queues, the close routine releases every holder, the closing press masked for every holder); the
player's exit in the close matrix (a second menu-key press and Escape held each end every kind of holder, a panel that
has reported ready and never releases among them, with the press masked); providers (two providers combined: either's
danger closes every view, a view opens only when both allow); the anti-cheat path (notices only, no menu, panel,
input or socket) and a later hit closing the server for every space; per-space routing in the server suite (a page
cannot reach another space's commands or topics; sensitive commands refused from a paired device); a fuzzer for page
data, settings descriptions, frames and draw lists (malformed input never panics and stays within limits); a list
with overridden vertex layout or 32-bit indices refused; an atlas rebuild refusing lists of the old generation; the
texture and face budgets; the `on_present` budget and suspension; parity goldens per space; the example
mods in the harness; the import, symbol and crate-graph checks of invariant 16's new form, on both `graceui.dll` and
`eldennet.dll`. EldenNet's spoiler, roster and Playthrough goldens run in EldenNet's CI (D82).

- **Reasons.** Every new boundary (other mods, several holders, a second copy, the server's spaces) is where the hosts
  the Skyrizz session studied went wrong (FW §1.3, §1.4), so each gets a test before the API opens.
- **Gives up.** CI time, and a Windows runner for the harness and the example mods.
- **Risk.** The harness on WARP may not show what a real GPU and Proton show; S4 and EldenNet's matrix stay the in-game
  proof.

### 4.14 Licences, the public face and the community's view of AI (G19)

- **Licences.** GraceUI, its headers, SDK, samples and docs are MIT, with an SPDX line in every public header. O25's
  rule moves with the host: `NOTICE` reproduces the notice of every licence file found in a linked dependency's source
  (hudhook's MIT; the BSD-2 notices of MinHook and HDE, `vendor/minhook/LICENSE.txt`; Dear ImGui's MIT inside
  `imgui-sys`; imgui-rs under MIT OR Apache-2.0; the `windows` crates and the rest), and a `licence-files` stage in
  GraceUI's CI fails without them. Nothing of
  er-mods-rs (no licence) or SeverActions (no licence) is used; nothing of me3 or fromsoftware-rs in any form (O47,
  O48). Magelight's MIT SDK could be studied for shape, but D35's "nothing in v1" stands.
- **Public face** (D87). GraceUI is published on EldenNet's channels (EldenNet O22: GitHub and a Nexus mirror), with its
  own page; README, the guide and every release page carry an AI-assisted disclosure, the kind Nexus is reported to
  require (LIKELY, FW §6): a human-owned design; code, text and docs written with AI assistance and reviewed by hand; no
  AI-generated images; the tests that guard it. No presence in me3's spaces; a note, as EldenNet's, that a player may
  list GraceUI as a native in their own me3 profile. Release pages publish file hashes, because unlicensed look-alike
  "mod menu" repositories exist (FW §2).
- **SkyrimNet-derived material** is G19's question.
- **Reasons.** Licence clarity decides adoption (FW F9); reception will be split, and an honest statement is what the
  middle ground asks for (FW §6, F10).
- **Gives up.** Modders aligned with me3 and fromsoftware-rs will not adopt it; GraceUI seeks no help there.
- **Risk.** Reception; addressed by openness, not by hiding.

### 4.15 The Skyrizz session's points, one by one

The session's suggested ABI (its §6) and its points to copy and avoid (its §10), with GraceUI's verdict.

| # | Point | Verdict | Where |
|---|---|---|---|
| ABI 1 | One plain C export `Name_RequestApi(version)`, found by `GetModuleHandle` and `GetProcAddress`, null for an unknown version | Adopt, adapted: `GraceUI_RequestApi(major)`, plus an export scan and the canonical path as fallbacks | §4.3, D70 |
| ABI 2 | One append-only table per major, starting with the API version and the packed host version, each slot naming its version and a capability | Adopt; the version as three numbers, the slot history in the guide's feature table | §4.3, D73 |
| ABI 3 | No C++ vtables across the boundary | Adopt, widened: no Rust types, STL, exceptions, panics or heap ownership | D73 |
| ABI 4 | Every struct a caller passes starts with its size; the host copies the smaller and defaults the rest | Adopt, with a floor: below the 1.0 size is refused | D73 |
| ABI 5 | One compatibility rule, written once in the header | Adopt | D74 |
| ABI 6 | A user pointer on every callback | Adopt | D73 |
| ABI 7 | A stated thread for each callback, and a call that posts work to the game thread | Adapt: stated threads, and a pull queue on the mod's own thread; no post to the game's thread, since GraceUI calls no game code | §4.4, D71 |
| ABI 8 | A shutdown or render-dead event after which nothing calls in | Adapt: `inactive` and `render_dead` events after which calls do nothing; no process-exit event, since nothing is done at detach (EldenNet R14) | §4.3 |
| ABI 9 | Breaking changes as a new export, the old one served for a stated window | Adapt: a new major from the same export (one host name per process); each major served for a stated window | D74 |
| ABI 10 | A feature-history table and a version every DLL can read | Adopt: `GraceUI_Version()`, `graceui.toml`, the guide's table | D74 |
| Copy 1 | An append-only C ABI with a version and a capability query | Adopt | D73 |
| Copy 2 | A version every mod can read, and an explicit "too old" answer with a page and a log line | Adopt: `too_old` with both versions, a notice naming the mod, one log line | §4.3 |
| Copy 3 | A permissive licence stated in the header | Adopt: SPDX MIT in every public header | §4.14 |
| Copy 4 | Escape stays the host's until the page is ready; a failed load gives the input back; hotkeys stand aside while anyone types | Adopt, with the player's exit that no view captures | §4.5 |
| Copy 5 | A key-plus-modifier binding owned by the framework | Adopt, with pad chords | D77 |
| Copy 6 | One documented thread for page calls, a post-to-game-thread call, a user pointer | Adapt, as ABI 7 | §4.4 |
| Copy 7 | Bounds, anchors and a documented z-order, stated against other overlays too | Adopt: five layers, anchors and bounds; documented that an overlay drawing after GraceUI at Present covers it and GraceUI cannot draw under the game's own UI | §4.2, D86 |
| Copy 8 | Pages loaded from the mod's own folder | Adapt: a mod hands page data as bytes, from its own folder or compiled in; GraceUI never reads another mod's files | §4.2 |
| Copy 9 | A desktop harness and a mock host for CI; the loopback browser view as the natural one | Adopt: `grace-dev` for any space, `grace-harness`, a C mock | §4.12 |
| Copy 10 | A troubleshooting page, a first log line naming the version, a bug-report checklist | Adopt | §4.12, D84 |
| Copy 11 | A full set of lifecycle events, and calls that do nothing after shutdown | Adopt: ready, load failed, closed with reason, input granted, refused, lost and free, display changed (with the scale), atlas rebuilt, inactive, render dead; "reloaded" becomes "pages replaced" | §4.3 |
| Copy 12 | An honest focus answer | Adopt as invariant 17 | §4.5 |
| Avoid 1 | An Ok that only queues | Adopt | §4.5 |
| Avoid 2 | Views that start shown; ready events before create returns | Adopt: panels start hidden; every event is pulled, so none arrives before create returns | §4.4 |
| Avoid 3 | Resizing by rebuilding and reloading the page | Adopt: layout reflows; only the atlas rebuilds, only while the menu is closed (D29), with a new generation | §4.3, §4.8 |
| Avoid 4 | Taking the screen size from the first frame | Adopt: the swap chain's size and the effective scale every frame, and a display-changed event | §4.3 |
| Avoid 5 | Different web features for one page across hosts | Adopt: no mod web code; both views draw one vocabulary from the same fixtures, with parity goldens | §4.2, §4.13 |
| Avoid 6 | Native dialogs that fail silently | Adopt: no message boxes in game; confirmations are GraceUI's own modal | §4.5 |
| Avoid 7 | Encouraging mods to bundle the framework | Adapt: mods require it; EldenNet, which the owner decided always ships it, installs it through its launcher only where none or an older one is | §4.11 |
| Avoid 8 | Licence terms that leave a consuming mod guessing | Adopt: MIT throughout, with a "what you may do" paragraph | §4.14 |

### 4.16 Proposed D-entries (settled here, open to the owner)

Each would join DESIGN §13 when the answers are folded in (S7.8.c). Those marked with a G-number hold only under that
question's recommended answer.

| # | Decision | Choice | Holds under |
|---|---|---|---|
| D69 | The host's shape | `graceui.dll` itself is the host; `grace-host` the only crate with hooks; no second GraceUI module | G15 (a) |
| D70 | Finding GraceUI | Fixed name, one export, an export scan, the canonical path; from a worker, bounded wait; absence is "off" | G15 (a) |
| D71 | Start and threads | Nothing at load; one short init thread on the first request, in the copy that holds the mutex; mods pulled on their own threads; opt-in `on_present` with a budget | G15 (a) |
| D72 | Single instance | A per-process named mutex, taken once-guarded in the first `GraceUI_RequestApi` before any table is returned; a second copy answers null from its first request, idles and is named in one notice | G15 (a) |
| D73 | ABI rules | §4.3's rules and slots (`register_face` and `glyph_table` with an atlas generation among them) and the table in §4.15 | — |
| D74 | Versions and support | One compatibility rule; each major served two minors and 12 months after the next (EST); semantic versions from 1.0 (amends D37 then) | — |
| D75 | Spaces | Id, name, version, floor, views, page data as bytes, its own settings description | — |
| D76 | Panels and draw lists | Five layers; draw lists in the default `ImDrawVert` layout with 16-bit indices from any producer, a consumer's headless Dear ImGui among them, other builds converting first; textures once within a budget; D56's checker for all | G16 (a) |
| D77 | Input and hotkeys | One holder, honest answers, framework-owned bindings with conflicts shown; the player's exit (a second menu-key press within 2 s, or Escape held 1 s, EST) that no holder captures | G15 (a) |
| D78 | Game awareness | Read-only kept; per-space gates; world-state providers, every claimant accepted and combined: any provider's danger closes every view, a view opens only when every provider allows; each shown on the Compatibility page | — |
| D79 | The menu's shape | One menu and key; spaces with a switcher from two; GraceUI's own space; search across spaces | — |
| D80 | Theme | Grace for every space's pages; panels their own look; an accent seam later; a face budget per mod | — |
| D81 | Isolation and the gate clause | One origin; no mod web code; space-scoped frames, subscriptions and commands; sensitive commands per space; §7.2 item 1's gate clause kept as §4.9's four rules (no socket beside anti-cheat; an unknown build binds EldenNet's space alone) | G17 (a) |
| D82 | EldenNet's pages | EldenNet's content in EldenNet's repository; GraceUI keeps the frame and a demo space | G20 (a) |
| D83 | Packaging | One canonical copy; require, never bundle a private copy; EldenNet's launcher installs, never downgrades, and replaces an older major only while the bundled release still serves it, else explains and asks | — |
| D84 | GraceUI's own files | `Game\GraceUI\user\` for its settings, devices and scrubbed log, never in a release and outside `graceui.toml`'s hash list (amending D51 and D19); its preferences (menu key, the one `ui.scale` of G6, which EldenNet's text follows, theme variant, hotkeys, server) are GraceUI's, migrated out of EldenNet's `settings.toml` | — |
| D85 | Docs and harnesses | The guide, three example mods, `grace-dev` for any space, `grace-harness`, a C mock | — |
| D86 | Other overlays | Documented; a log warning when Present's first bytes already hold a jump at install (SPECULATION as a cue); matrix rows | G15 (a) |
| D87 | Public face | EldenNet's channels (O22), a true AI-assisted disclosure everywhere (code, text and docs written with AI assistance and reviewed; no AI-generated images), no presence in me3's spaces, hashes on release pages | — |
| D88 | EldenNet without GraceUI, and anti-cheat | EldenNet's launcher checks GraceUI before start and explains what is missing; in the process, a failed connect leaves EldenNet headless at L2 with a log line, as ARCH §3.2's overlay-error row; on an anti-cheat hit GraceUI hooks as EldenNet's banner path does, draws notices only and serves no menu, panel, input or socket, and EldenNet's banner goes through `notice`, callable after `inactive (anti-cheat)`; amends G2's "keeps playing whatever it finds" | G15 (a) |

## 5. What EldenNet must change

To be added to [eldennet-changes.md](../eldennet-changes.md) once the answers are in, as a new section for G15 onwards,
and folded into EldenNet's design in S7.8.c (before S7.1 folds the items they touch, so nothing is folded twice). Under
the recommended answers:

1. **O25 amended by the owner** (G15 (a)): the pin and every rule move to GraceUI's `grace-host`; EldenNet's
   CLAUDE.md "hudhook pinned" bullet and GraceUI's "One overlay" bullet change with the owner's words; EldenNet's
   `licences` CI stage denies hudhook, MinHook and any other hook library, and allows `imgui` only without a renderer
   backend; its greps check that no hook library is linked.
2. **`en-overlay` becomes EldenNet's display layer**: its surfaces are panels, built with GraceUI's `grace-draw` and
   `grace-view` crates (pinned by tag, as §2.7 pins the host kit now) and drawn through an `on_present` callback; its
   faces are registered with GraceUI through `register_face` before warm-up, and it follows the atlas's generation; its
   HDR handling is GraceUI's. The F11 debug view (tables, live events, journals, prompts: EldenNet DESIGN §14) keeps its
   imgui-rs code on a headless context of EldenNet's own (`imgui` 0.12, the Dear ImGui 1.89.2 that hudhook 0.9.3
   bundles, with no backend), its draw data submitted as a panel while it is open, its font atlas registered once as a
   texture and its input taken through `request_input`; so F11's views keep their imgui-rs design, and only its frame
   glue is designed anew for v0.1.
3. **Input**: EldenNet's DInput and XInput detours, window-procedure filter, `WM_CHAR` fix and IME placement move to
   GraceUI, which finds the XInput DLL by module name rather than the game's import table (§4.1 of this proposal); the
   chat box and call-out box take input through `request_input`; EldenNet's keys (the chat key, F11, the
   talk gesture's chord) are registered hotkeys; the Steam on-screen keyboard is opened by GraceUI for its own fields and
   by EldenNet for its chat box, as now.
4. **ARCH**: threads T3, T4 and T5 become GraceUI's (EldenNet's T3 work is its `on_present` callback, under the same
   rules); C12's capture statics become GraceUI's state, read through events; R12 becomes "EldenNet hooks nothing";
   §1.4 and §3.2's Hooks step become "connect to GraceUI" on T1 after the safety step; ARCH §10.3's "Overlay failed to
   install" row covers a failed connect.
5. **The banner and toasts** use GraceUI's notice layer; the banner path at L1 needs GraceUI's host. On an anti-cheat
   hit, GraceUI is already notice-only (§4.1 of this proposal, D88), `connect` answers `inactive (anti-cheat)` and
   EldenNet's banner goes through `notice`, so ARCH §5.3's "A hit at init → L1 banner" becomes "a hit at init → the
   banner through GraceUI's notice-only mode", and ARCH §10.3's banner and toast rows name GraceUI's notice layer as
   their surface (its "Anti-cheat present" row included). An unknown build or another L1 trip: EldenNet connects as
   usual and shows its banner through `notice`.
6. **The launcher** checks `Game\GraceUI\graceui.toml` (present, hashes, major, floor) before start, installs its
   bundled GraceUI only where none or an older one is, and over an older major only while the bundled release still
   serves it (D83), and explains a failure in E08's manner (D88).
7. **The server** (G17 (a)): EldenNet links no `grace-serve`; it publishes each revision once to GraceUI, registers its
   space with `browser = "always"` (G4) only once its gate and safety steps pass, withdrawing it on T10's anti-cheat
   hit (§4.9 of this proposal), and marks its sensitive commands (G9); `dashboard.*`, `hotkeys.menu` and
   `ui.menu.*` leave `settings.toml` by migration for GraceUI's own settings (D84); `devices.toml` moves to GraceUI's
   user folder; the launcher's first screen names the address GraceUI serves.
8. **The world-state provider**: EldenNet publishes danger, loading, game menu or text entry open, the title screen,
   multiplayer and an anti-cheat hit from `safe_context`, its `ui` group and T10 (D78), as one provider among any
   others, and keeps gating its own space.
9. **EldenNet's pages** (D82, G20 (a)): its page data, fixtures and goldens, SP1–SP9, its PV rules and D61 join
   EldenNet's documents and CI; GraceUI's page lint runs there.
10. **D34 widened**: lines addressed to the player wait while any input holder is active, from GraceUI's event.
11. **DESIGN §12 and §13**: the presentation row names GraceUI as the host; §13's seams name the `UiPort`, with GraceUI as
    its implementation.
12. **The compatibility matrix** (ARCH §12.4) gains rows: another GraceUI consumer loaded; GraceUI without EldenNet; two
    copies of GraceUI at two paths; GraceUI listed as a native in a loader; an older GraceUI over a newer one; an
    installed older major, inside and outside the bundled release's support window; another mod's hooking overlay
    beside GraceUI's; another mod as a second world-state provider.
13. **Failure contract**: GraceUI DESIGN U9 and D54's "EldenNet keeps playing" are replaced by D88; EldenNet's v0.1
    exit checks gain "one hook library in the process: GraceUI's".
14. **Unchanged**: the `UiPort`'s core side (projections, one command entry point, the settings export), the scheduler's
    other rules, R14, R16 (EldenNet keeps its own single-instance mutex), the read-only rule.

Under G15 (b), items 1 to 7 and 12 fall away and EldenNet grows a host API inside `en-overlay` later instead; under
G17 (b), item 7 falls away and EldenNet keeps `grace-serve` as designed; under G20 (b), item 9 falls away.

## 6. Increments

Aligned with EldenNet's (DESIGN §16.2; roadmap S10 onwards), under the recommended answers. GraceUI's rows today start
at 0.1 with spike S4; the host has to come first, because EldenNet v0.1's banner and F11 view draw through it.

| EldenNet | GraceUI | Adds for the framework | Exit checks |
|---|---|---|---|
| v0.1 Eyes | **0.1 Host** (new) | `graceui.dll` as host: hudhook 0.9.3, its MinHook, Present, the window procedure with the `WM_CHAR` fix, DInput and XInput, capture and masking, the anti-cheat check, the single-instance guard; ABI 1 as a preview; panels, draw lists, textures at load, faces and glyph tables, notices (with the anti-cheat notice-only mode); the player's exit; EldenNet's banner as a notice and its F11 view moved onto a headless Dear ImGui of its own and submitted as a panel (§5 of this proposal, item 2); our example consumer in CI | One hook library in the process; S0-E's present-hook half and the first-build MinHook check (roadmap S10.1.c, e) on GraceUI's host; two consumers each way round; a second copy idles; the frame cost |
| v0.2 First words | **0.2 Foundations** | Tokens, protocol, page data, the web shell, `grace-dev` for any space (today's 0.1); spike S4's page drawn by the host; EldenNet's subtitles, chat box and call-out box as panels with `request_input`; the server in GraceUI (today's 0.2) | S4; the server suite with two spaces; per-space routing; the input-leak tests across holders |
| v0.3 to v0.5 | 0.3 to 0.5 | EldenNet's pages as now, from EldenNet's repository (D82); the hotkey page; search across spaces | As now; EldenNet's goldens in EldenNet's CI |
| v0.6 Threshold | 0.6 | The menu in game as now; the space switcher and GraceUI's own space; the demo space beside EldenNet's | U2 to U4 and U6 across holders; the close matrix for every holder, the player's exit among them; the providers' danger close |
| v1.0 | 1.0 | As now; ABI 1 and the page format frozen; semantic versions from here (D74) | U1 to U9 as rewritten |
| After v1.0 | **1.1 Open** (G18 (a)) | The modders' guide, the three example mods, `grace-harness`, the C mock, GraceUI's own release page | The examples build and run in the harness in CI; the guide's snippets compile |

## 7. Risks

| Risk | Mitigation |
|---|---|
| A public ABI's support burden outlives EldenNet's needs | A preview until G18's answer; append-only tables; a stated window per major; the release header check |
| EldenNet v0.1 now waits on GraceUI's host, and its own v0.1 surfaces change | The host is mostly the hudhook glue `en-overlay` would have had; it is built in GraceUI 0.1 from the same spike work (S0-E). EldenNet's side costs its own glue: the banner becomes a `notice` call, and F11's frame code moves from hudhook's context to a headless one submitted as a panel (§5 of this proposal, item 2); its views keep their imgui-rs design |
| A third-party mod's fault (a panel, an `on_present` callback) stops EldenNet's surfaces or the game, and is reported against GraceUI or EldenNet | Budgets and suspension; checked draw lists; the bug-report checklist names every consumer; documented: native mods share one process |
| Two copies of GraceUI, or an older one over a newer one | The mutex; the export scan; the launcher's install step; the too-old notice naming both versions |
| Another hooking overlay (hudhook or Present based) beside GraceUI goes silently dead, or kills GraceUI's | Documented in the guide and the matrix; the install-time warning (D86); cannot be prevented by GraceUI |
| The server becomes a port for every mod's commands | Loopback only, mods opt in, no mod web code, per-space routing, sensitive commands, the server suite with several spaces |
| Modders used to HTML hosts find page data too narrow | Panels for anything else; the vocabulary grows by minors; HTML in game stays G1's gated spike |
| Scope creep against EldenNet's "minimal and stable" | The framework's additions are designed now but opened after v1.0 (G18 (a)); EldenNet uses only what it needs |
| Reception of an AI-built framework | The disclosure, the reviewed design, the tests in the open; no presence where AI is refused |
| Proton and the Deck | hudhook supports Wine; a short import list; the harness on Wine where possible; the Deck's view is in game |
| Texture and atlas budgets shared across mods | Per-mod budgets, a registration window, the limits logged |
| Name and download impersonation | Hashes on release pages; one canonical page |

## 8. Questions for the owner (G15 onwards, in the order recommended)

Each would be added to [DECISIONS.md](../DECISIONS.md) as an entry marked Open and to DESIGN §12's index when the lead
records them. Everything else in this proposal is settled as D69 to D88 (§4.16 of this proposal) and can be reopened.
G14's record names five consequences to put to you: whose overlay (G15), what draws other mods' pages and the
interface they use (G16, with G18 for when it opens), the browser view (G17) and which pages are EldenNet's own (G20);
G19 follows from G12.

### G15: Who owns the one overlay?

- **As it would be put.** Now that GraceUI is a framework other mods can use (G14), which module owns the one hudhook
  overlay, its MinHook, the Present hook and the input hooks? **(a) GraceUI** (recommended): they move from EldenNet
  into `graceui.dll`, with every rule of EldenNet's O25; EldenNet draws its subtitles, chat box, call-outs, labels,
  toasts, banner and F11 view through GraceUI like any mod, and links no hook library. **(b) EldenNet, as G10
  decided**: GraceUI keeps drawing through EldenNet's overlay, and other mods can use GraceUI only while EldenNet is
  loaded and active, through a host API EldenNet grows later.
- **In plain words.** Only one copy of the overlay library GraceUI and EldenNet use can run safely in the game. Should
  that copy be GraceUI's, so any mod can use it on its own, or stay EldenNet's, so GraceUI only works for other mods
  while EldenNet runs?
- **Recommendation: (a).** One process holds one hudhook and one MinHook safely, and every shared host that works in
  this game is one module that others call (FW F1, F2); a framework other modders use cannot depend on an AI mod that
  starts offline through its own launcher; your words "EldenNet will require it" then hold literally; nothing is built,
  so moving now costs a design pass, while moving later would rewrite EldenNet's surfaces twice. *What (a) gives up:*
  G10's isolation (a fault in GraceUI's host stops EldenNet's subtitles and chat as well as the menu); EldenNet cannot
  show anything, not even its banner, without GraceUI, so its launcher checks GraceUI first; the hook code ships in the
  releases players replace by hand; GraceUI's first increment must be ready for EldenNet v0.1.
- **What each answer changes.** (a) amends G10 and EldenNet O25 (the pin and its rules move to GraceUI; both CLAUDE.md
  files change), rewrites invariant 16 as "one host, one hook set", replaces invariant 15's "EldenNet keeps playing" and
  amends G2's "keeps playing whatever it finds" (D88); DESIGN §2, §3.16, §5 and §8 are rewritten around the host; D69 to
  D72, D77, D86 and D88 apply; §5 of this proposal's items 1 to 7 and 12 go to eldennet-changes; GraceUI gains a 0.1
  Host increment with EldenNet v0.1. (b) keeps G10, O25 and invariants 15 and 16 as they are; the framework is limited
  to EldenNet's presence; G17's recommendation becomes (b).

### G16: What do other mods write, and what draws it in game?

- **As it would be put.** What does a mod that uses GraceUI provide? **(a) Page data and panels** (recommended): its
  pages and settings described as data in GraceUI's components (drawn in game with the Grace theme and, if it opts in,
  in the browser), topics and commands in the bridge protocol, and, for anything else, in-game panels it draws itself as
  checked vertex lists, with GraceUI's drawing kit or its own headless Dear ImGui with the default vertex layout and
  16-bit indices (checked in 1.89.2 and 1.92.6; other builds convert before submitting, and the checker refuses
  anything else); no HTML in game. **(b) Page
  data only**: no panels for other mods (EldenNet's own surfaces still use them). **(c) HTML pages in game**, as
  Magelight UI and PrismaUI offer: this reopens G1 and needs a hudhook patch (an O25 change), Ultralight's licence and
  about 57 MB, or another engine.
- **In plain words.** Magelight and PrismaUI let modders write web pages. GraceUI would instead let them describe their
  pages as data, which GraceUI draws in the game's style and in the browser, plus panels they draw themselves. Is that
  the right offer, or should mods get web pages in game?
- **Recommendation: (a).** G1's reasons apply to every mod: the pinned hudhook cannot update an image without leaking
  memory and stalling the frame (REN §2.1). Page data shows in both views from one source and keeps other mods' code out
  of the browser; panels let a mod draw anything without sharing ImGui's internals, which differ between versions (FW
  §4); EldenNet needs panels anyway. *What (a) gives up:* modders cannot bring HTML, CSS or React; pages are limited to
  GraceUI's components; panels show only in game; pictures are static, so no video or live images.
- **What each answer changes.** (a): D75 and D76 apply; G1 extends to every mod unchanged. (b): D76 serves EldenNet
  only and the guide drops panels; mods with needs beyond the vocabulary cannot use GraceUI. (c): G1 is reopened with
  its three approvals (licence, hudhook patch, size), invariant 11 and REN §2's findings are revisited, and S4 gains an
  HTML spike.

### G17: Is the browser view a GraceUI feature for every mod?

- **As it would be put.** The browser dashboard (G4, G9) shows the same pages as the menu. Who serves it? **(a)
  GraceUI, for every mod that opts in** (recommended if G15 is (a)): the local server and the web app move into
  `graceui.dll` with every rule of §7.2 and pairing; EldenNet's dashboard is its space there, always on as G4 decided.
  **(b) EldenNet, for its own pages only** (recommended if G15 is (b)): GraceUI keeps the web app and the server as a
  kit that EldenNet runs, as designed now; other mods have no browser view. **(c) EldenNet's own web app**: GraceUI is in
  game only, and the browser dashboard, G3, G4 and G9 move to EldenNet.
- **In plain words.** Should the "open it in your browser" dashboard be something GraceUI offers any mod, or stay
  EldenNet's alone?
- **Recommendation: (a), given G15 (a).** One hub: each mod publishes once and both views show it, from one page source;
  no other Elden Ring host offers a browser view (FW F7). Your words ("a UI that is capable to present EldenNet
  in-game", "like Magelight or prismaUI") put the game first, and (a) keeps that: the browser shows a mod's pages only
  when the mod asks. (c) would write every component twice, in two repositories (G14's recommendation). The study
  behind this proposal leaned the other way, towards (b), for your in-game words and no framework-wide port; this
  recommendation weighs the one hub higher. *What (a) gives up:* `graceui.dll` opens a network port on the PC and runs
  threads, and a mod's command bug becomes reachable from the browser as it is from the game. (b) keeps, even with
  G15 (a), a `graceui.dll` that opens no socket, and G4's port as EldenNet's alone behind its gate; other mods' authors
  still get a browser test bench under either answer (`grace-dev`, D85).
- **What each answer changes.** (a): G4, G9, D58 to D60 and D66 move with the server to GraceUI, and D61 stays
  EldenNet's, with its pages (D82); invariant 4 is GraceUI's server; D81 applies, with §7.2 item 1's gate clause kept
  as §4.9 of this proposal's four rules; §5 of this proposal's item 7 goes to eldennet-changes. (b): the server stays in
  EldenNet as designed (eldennet-changes §3 unchanged), and other mods' pages are in game only. (c): the web app, G3, G4
  and G9 move to EldenNet, invariants 4 to 6 and 12 become EldenNet's, and the components are written in both
  repositories.

### G18: When does the framework open to other modders?

- **As it would be put.** **(a) After EldenNet v1.0, as GraceUI 1.1** (recommended): the ABI is designed now, EldenNet
  uses it from GraceUI 0.1, an example mod of ours exercises it in CI, and the guide, the examples and a frozen ABI 1
  are published with 1.1. **(b) With GraceUI 1.0**, at EldenNet v1.0. **(c) Early**, as a preview marked unstable from
  GraceUI 0.2.
- **In plain words.** Should other modders get GraceUI only once EldenNet 1.0 is out, at the same time, or as early as
  possible with a warning that it may change?
- **Recommendation: (a).** EldenNet v1 is to be "deliberately minimal and stable"; a public ABI is a promise kept for
  years, and it is better frozen after a real user has driven it through a full release; Magelight broke its API twice
  before its first public release, in 0.29.0 and 0.30.0 (Magelight `CHANGELOG.md:1135-1210`, `:1290`, via the Skyrizz
  session, 2026-11-08). *What (a) gives up:* other modders wait about
  one release, and early feedback from them is lost.
- **What each answer changes.** (a): §6's 1.1 Open row; the guide and examples after v1.0. (b): those move into 1.0's
  exit checks. (c): a preview label, a changelog of breaking changes and support questions from 0.2; the ABI's freeze
  waits for 1.0 either way.

### G19: May SkyrimNet-derived material reach other mods through GraceUI?

- **As it would be put.** G12 lets GraceUI use SkyrimNet's code, dashboard and styling, as Min approved for building on
  SkyrimNet. If GraceUI is a framework, anything SkyrimNet-derived in it reaches every mod and player that uses it.
  **(a) Not until confirmed** (recommended): GraceUI keeps to its own code and the Grace theme; whatever is adopted from
  Min's blueprint for EldenNet's pages lives in EldenNet; once the blueprint arrives, you ask Min, if you wish, whether
  what GraceUI would adopt may be passed on through a public framework. **(b) Yes**: G12 covers GraceUI as a framework,
  credited as Min wishes.
- **In plain words.** Min agreed that EldenNet and GraceUI may build on SkyrimNet. Does that agreement also cover other
  people's mods getting SkyrimNet-derived parts through GraceUI?
- **Recommendation: (a).** G12 and EldenNet O53 record Min's approval for EldenNet's foundation; neither says whether it
  reaches third parties, and claiming more endorsement than Min has given is ruled out; GraceUI's look is our own anyway
  (D3). *What (a) gives up:* the question G13 will ask after the blueprint can place adopted parts only in EldenNet's
  pages, not in the shared components, so the web app may take longer.
- **What each answer changes.** (a): §3.17's last line is aligned with G12 by saying SkyrimNet-derived material stays
  out of GraceUI until confirmed; NOTICE stays free of SkyrimNet entries. (b): §3.17's line is replaced by G12's rule
  for GraceUI as a framework, and NOTICE carries SkyrimNet's credits and licences as Min wishes.

### G20: Where do EldenNet's pages live?

- **As it would be put.** G14 leaves open which pages are EldenNet's own once GraceUI is a framework. EldenNet's menu
  and dashboard content (its categories, pages, settings description, help texts and Home cards), its fixtures, the
  spoiler, roster and Playthrough goldens, the spoiler rules SP1–SP9, EldenNet's halves of the privacy rules PV1–PV8
  and DESIGN §4: **(a) In EldenNet's repository** (recommended), as content EldenNet ships and registers with GraceUI
  at start; GraceUI keeps the frame, the components, the theme, PV3, PV6 and PV7 as framework rules, and a demo space
  with invented content (D82). **(b) In GraceUI's repository**, as its first user's bundled pages, as designed now.
- **In plain words.** EldenNet's own pages, and the rules that keep them free of spoilers, could move to EldenNet, so
  that GraceUI holds only the parts every mod shares, or stay in GraceUI as now. Which should it be?
- **Recommendation: (a).** Your words "the interactive UI should be part of EldenNet and not GraceUI" (G13's later
  change); the spoiler rules are EldenNet's domain; a framework other modders use should not carry one user's content
  and its spoiler review. *What (a) gives up:* one repository for the whole information architecture; GraceUI's page
  lint and parity tests then run on EldenNet's pages in EldenNet's CI against a pinned GraceUI, and a change to a
  component and to EldenNet's pages can land in two repositories.
- **What each answer changes.** (a): D82 applies; DESIGN §4, SP1–SP9, EldenNet's halves of PV1–PV8, D61, its fixtures
  and goldens move to EldenNet in S7.8.c; invariants 1 and 2 take their framework form in GraceUI and their EldenNet
  form in EldenNet; §5 of this proposal's item 9 goes to eldennet-changes. (b): DESIGN §4, SP1–SP9, the goldens and
  invariants 1 and 2 stay in GraceUI as they are, beside the framework rules; GraceUI's release carries EldenNet's page
  data as a bundled space that EldenNet's `connect` activates, and the framework carries one user's content.
