# Changes EldenNet's design needs for GraceUI

A short list for EldenNet's design (branch `design/v1-proposal`), from [GraceUI's design](DESIGN.md). EldenNet's codes
(DESIGN, ARCH, NPC, MEM, ER02) refer to its documents; "GraceUI §n", G1–G30 and D1–D136 to GraceUI's. Nothing in §1 to
§10 is changed in EldenNet yet, apart from the roster, which EldenNet's design now records (its O43 as updated, O45 and
O46; §9 item 1); §11, the framework's changes, was folded into EldenNet's design on 2026-11-15 (roadmap S7.8.c), and
where it and an earlier item disagree, §11 holds. Items marked **(G*n*)** come from that entry of GraceUI's
[DECISIONS.md](DECISIONS.md) (indexed in GraceUI §12), every one of which the owner has decided (G3 and G10 on
2026-11-07, as recommended; G10 superseded by G15 on 2026-11-08). All of it stays minimal: one overlay, GraceUI's since
G15, and read-only towards the game; EldenNet starts no new thread for the UI, while GraceUI starts its own init thread
and the server's (GraceUI D71, D81). *Later (2026-11-16): both designs were re-based on C++ (EldenNet O60, roadmap
S8.1.e), and the owner put Dear ImGui 1.93.0 and MinHook 1.3.4 in hudhook's place (EldenNet O25, GraceUI G15); the
notes of that date below say what this changes in each item that names hudhook or Rust, and GraceUI D106 to D110 hold
the rest.* *Later (2026-11-16): by G20 (a), what GraceUI §4.1, §4.2.1, §4.4 to §4.6 and §4.8 to §4.12 held, with
SP1–SP9, PV1, PV2, PV4, PV5, PV8 and D61 and the fixture states and goldens, is EldenNet's `docs/design/pages.md`
(PAGES), under the same numbers; references to them below resolve there through GraceUI's pointers. §13 lists what
G18–G20 and G29 change in EldenNet, folded the same day.*

## 1. The main UI on F5 through GraceUI

1. **DESIGN §12 and NPC §10.1**: replace "settings (F10, safe moments only)" with "the main menu (F5, GraceUI; safe
   moments and the title screen)". The menu is drawn in EldenNet's one hudhook overlay with Dear ImGui and the Grace
   theme **(G1)**, after the world surfaces and below the toasts and the banner: GraceUI's `graceui.dll` lays it out
   and tessellates it, and EldenNet's overlay copies the vertex lists into ImGui's draw list **(G10 (b))**.
   *Later (2026-11-15): G15 (a) supersedes G10 (b); the one overlay is GraceUI's own, and the menu draws in it (§11).*
   *Later (2026-11-16): that overlay is Dear ImGui 1.93.0 with its DX12 and Win32 backends and MinHook 1.3.4, in
   hudhook's place (EldenNet O25 as the owner changed it).*
2. **NPC §10.6** (the F10 panel) is superseded by GraceUI §4 (information architecture): a framework's categories,
   Home, AI, Voice, Characters, History, Behaviour, System and Tools, with Settings, Docs and Content packs at the foot
   (GraceUI D38), which replace the first proposal's flavour-named sections. Point NPC §10.6 and DESIGN §12's
   "Settings" bullet to GraceUI §4.2 and §4.5, and drop NPC §10.6's out-of-date "ambient lines" and bark-density
   rows.
3. **Settings schema** (ARCH §11.5): add `hotkeys.menu = "F5"` and remove `hotkeys.settings`, with a migration (schema
   3, or an amendment to schema 2 before v0.6, because `deny_unknown_fields` refuses unknown keys). Add
   `ui.menu.{theme, plain_text, reduce_motion, key_hints, pad_glyphs, backdrop, compact_rows, show_world,
   home_cards}`, `dashboard.{enabled = true, port = 8642}` **(G4)** and `dashboard.lan = false` **(G9)** in the
   same amendment. `ui.menu.show_unseen_types` is not needed: the owner lists every type from the start **(G5)**. The
   first proposal's `ui.menu.names` is dropped: names are plain only (GraceUI D39). *Later (2026-11-15): under D84
   these keys (and `ui.scale`) are GraceUI's own settings, in its `user\` folder; EldenNet's schema does not add them,
   and a migration moves any it already holds (§11 item 7).* *Later (2026-11-16): `deny_unknown_fields` is Rust's
   name; in C++, ARCH §11.5's strict parsing refuses unknown keys in the same way, so the reason stands.*
4. **S4** (DESIGN §16.1) adds: F5 is free in the default layout; one Grace settings page drawn in game, with its frame
   cost at 1440p and on the Deck, the atlas size, and screenshots for the owner's look review **(G1)**; buffered
   DirectInput reads; the `WM_CHAR` panic reproduced; the Steam overlay browser reaching 127.0.0.1 (a hand test,
   D30); opening a URL from the game under Proton; the ABI round trip with `graceui.dll` and the cost of copying its
   vertex lists, and a menu page of more than 65,536 vertices drawing correctly through hudhook 0.9.3's per-command
   vertex offsets **(G10 (b))**. *Later (2026-11-15): the same checks run on GraceUI's host, which draws S4's page
   itself; EldenNet's part is its panels and the connect (§11).* *Later (2026-11-16): with hudhook gone there is no
   `WM_CHAR` panic to reproduce, and S4 checks the host's surrogate pairing instead; the large page draws through the
   per-command vertex offsets of Dear ImGui's DX12 backend (GraceUI §2.9, §5.6).*
5. **The wizard** (DESIGN §11.5) becomes GraceUI's First light in v0.6 (D16); the rollback modal and F11's debug view
   stay in EldenNet's overlay. *Later (2026-11-15): they stay EldenNet's own, drawn as its panels through GraceUI's
   host (§11 item 2).*
6. **The v0.6 increment** (DESIGN §16.2): "F10 pages" becomes "the GraceUI menu on F5 (every page but Workshop)".

## 2. The bridge (a `UiPort` in the core)

1. A **player-safe projection layer** in `en-core` (GraceUI §4.8, SP1–SP9): topics built only while watched, at most
   4 Hz each, display strings formatted in the core, published with `arc-swap`. *Later (2026-11-16): in C++, through
   `std::atomic<std::shared_ptr<const T>>`.*
2. **One validating command entry point** for both views (a new bounded channel into T6; `try_send` from T3 and from
   the server). *Later (2026-11-15): EldenNet pulls its commands from GraceUI with `poll` on T6 (§11 items 4 and 7).*
   *Later (2026-11-16): in C++ a channel is a bounded queue of EldenNet's own, and `try_send` its non-blocking push
   (ARCH §1.3).*
3. A **settings description export** from EldenNet's own settings types: key, type, default, bounds, unit, choices,
   label, help (ending with the default in words), synonyms, home, effect timing, availability, locked (GraceUI §6.9,
   D8). Each `home` is one of GraceUI §4.5's page ids (`ai.models`, `voice.text`, `characters.types`,
   `history.memories`, `behaviour.conversation`, `behaviour.speaking`, `behaviour.awareness`, `behaviour.reactions`,
   `behaviour.pacing`, `settings.general`, `settings.hotkeys`, `settings.menu`, `settings.browser`,
   `settings.developer`, and `home` for Home's own key).
4. New views: a bounded **decision ring** (256 admission results with the refusing rule, topic `monitor.decisions`),
   a bounded **event tap** on Augur's bus (500, topic `monitor.events`),
   the **model call log** as a projection, a session-only **Pause AI** command (D15), a **subtitle preview** command.
   For GraceUI's framework categories (GraceUI §6.5): projections `selftest` (the self-test's state and figures),
   `usage` (this session's calls, tokens and own-initiative calls against the bound), `shortlived` (perceived TTL
   events with time left), `errors` (the last 20 failures in words, scrubbed), `memories` (met NPCs per journey) and
   `conversation/<id>` (displayed lines); `tidings` renamed `world`; the diagnostics view split into
   `system.status`, `system.performance`, `traces` and `content`; `health` also carries the prepared-line pools'
   state, and `named` each met persona's quest-business readiness word (none for unmet personas, SP5). `kinds`
   carries near-now counts of perceived characters only (SP7): a hidden character, a boss before its bar or an
   unannounced invader changes no count. For Characters › Playthrough, a `playthrough` projection (§9 item 4 below)
   **(G8)**.
5. **Crate rules** (ARCH §2.3): `en-core` may depend on `grace-protocol`; `en-overlay` on `grace-abi` and
   `grace-host` (which takes `imgui` only through `hudhook::imgui`, as O25 requires); `eldennet` on `grace-serve`. No
   EldenNet crate links `grace-view` or `grace-draw`: they are inside `graceui.dll`. `cargo deny` allows GraceUI's git
   source, pinned by tag and commit **(G2)**. *Later (2026-11-15), G15 (a): `en-overlay` links `grace-abi` through
   `graceui-sys`, and `grace-draw` and `grace-view` for its panels; no EldenNet crate links `grace-host` or
   `grace-serve` (§11 items 1, 2 and 7).* *Later (2026-11-16): crates are C++ modules of the same names (GraceUI D106);
   EldenNet fetches GraceUI's consumer modules at configure time, pinned by tag, commit and SHA-256 and listed in its
   `THIRD_PARTY.md`, which its dependency check allows in place of `cargo deny`; `grace-host` compiles Dear ImGui's
   backends and MinHook itself, so `hudhook::imgui` lapses (EldenNet O25 as changed).*
6. **The scheduler**: while the menu is open, lines addressed to the Tarnished wait as they do while the chat box is
   open; barks and NPC-to-NPC lines continue (D34).
7. **A `seen` bit on stored events** (MEM's `events` table): set when the Tarnished perceived the event, by the live
   tap's SP7 rule, so History › Events can list a journey's record without the events the player never saw. The event
   log's retention is unchanged. If EldenNet declines it, History › Events shows only this session's tap.
8. **A read-only, paged `history.page` query** over a journey's events and conversations (100 rows a page, journeys
   of the loaded playthrough only, refused at the title screen), built in the projection layer with SP2, SP3 and
   SP7 applied, on T6 and never on the present thread (GraceUI §6.6).
9. **The bridge in process** **(G2)**: the core encodes each watched topic's revision once as a bridge frame
   (GraceUI §6.3, UTF-8 JSON) and gives the same bytes to the server and, through `grace-host`, to `graceui.dll`'s
   `ingest` on T6; it drains a bounded command ring (32 frames of at most 16 KiB) that `graceui.dll` fills from T3
   without blocking. Budget: encoding and parsing ≤ 0.5 ms each for the largest payload (GraceUI §8). *Later
   (2026-11-15): the frames cross with `publish`, once for both views, and commands come back through `poll`
   (GraceUI §2.9, D55 as amended).*

## 3. The local server (always on, G4)

*Later (2026-11-15): under G17 (a) GraceUI runs the server (D81), so item 1's tasks on EldenNet's Net runtime and item
7's serving fall away, and `devices.toml` (item 8) lives in GraceUI's user folder (D84); the rules themselves stand,
in GraceUI. §11 item 7 lists what EldenNet keeps.* *Later (2026-11-16): `grace-serve` is our own small C++ server, on
two threads of GraceUI's own (G30, decided by the owner that day, as recommended; GraceUI §7.6), and the rules stand,
with more of them for its own code (GraceUI §7.2 items 19 to 25).* *Later (2026-11-16): G29 (a), decided that day:
`grace-serve` is also one library for hosts outside the game, under the same rules, and EldenNet's launcher serves the
dashboard off game through it (§13 item 4).*

1. `grace-serve` runs as tasks on the Net runtime (T7–T8) from the end of init's gate and safety steps to exit, at the
   title screen and after a later L1 trip, never when the gate or the safety step fails (an unknown build or
   anti-cheat), and stopped by T10's anti-cheat re-check (ARCH channel C14). ARCH §1.2 starts T7–T8 at Ready, so they
   start earlier for the server alone, which makes no model request (C4). "No other thread is created" still holds.
   ARCH §1.4's "An L1 session never opens a database or a socket" becomes "never opens a database, and opens the
   dashboard's socket only once the gate and safety steps pass". `dashboard.enabled = false` means no socket.
2. **C4 wording** (DESIGN §1.3): serving 127.0.0.1 is not a model request; the dashboard keeps serving while EldenNet
   is dormant, with live views paused.
3. **Privacy** (DESIGN §14): the server's session cookie (and G9's device cookies) never reaches a log, a report or
   a recording; request lines are logged without fragments or query strings; PV1–PV8 join the bug-report scan's
   rules, and with `dashboard.lan` (G9) the scrub also covers the PC's network addresses and the paired devices'
   names (GraceUI PV5). The paired devices live in `devices.toml`, never in `settings.toml`, so Copy settings, the
   report's settings part and recordings never carry them.
4. **F11** gains "Copy dashboard address" and "Open in browser" from v0.2 **(G4)**.
5. **The launcher's first screen** (DESIGN §3.2) names the dashboard's address, reading the port from
   `settings.toml` **(G4)**.
6. **Keys bound to their address** (GraceUI D61): a `providers.update` that changes a provider's base URL or kind
   clears its key and extra header values, from either view.
7. **Only listed files**: `grace-serve` serves `GraceUI\web\` as `graceui.toml` lists it, read and checked once at
   start, and a plain notice page built into EldenNet when the folder is missing or fails its check (GraceUI §2.9)
   **(G2)**.
8. **Other devices on the home network** **(G9)**: `dashboard.lan` (off by default); when on, the server also binds
   the PC's private network addresses only, a device must enter a six-digit pairing code shown in game (two minutes,
   one use, void after five wrong tries), and paired devices are kept as hashed device cookies, with their names and
   pairing dates, in a file of their own in EldenNet's config folder (`devices.toml`, never `settings.toml`) until
   forgotten; add `devices.toml` to the config-folder row of DESIGN §11.4. The network listener accepts only device
   cookies and never sets or accepts the session cookie; a loopback Host on it gets 421; "this PC's browser", from which
   `dashboard.forget_device` and `dashboard.disconnect` are allowed, means a connection on the loopback listener, and
   `dashboard.pair` works in game only. Paired devices are refused `secrets.set`, `secrets.clear` and
   `providers.update`; loopback use never needs a code (GraceUI §7.2 items 3, 5 and 16, D66).

## 4. The overlay (en-overlay)

*Later (2026-11-15): under G15 (a) the overlay is GraceUI's. Items 1 to 3 and 5 move to GraceUI's host, item 7's host
kit falls away (EldenNet connects through `GraceUI_RequestApi` instead), and items 4 and 6 become GraceUI's own rules
(D29, D76); §11 lists what EldenNet keeps.* *Later (2026-11-16): hudhook gave way to Dear ImGui 1.93.0's DX12 and
Win32 backends and MinHook 1.3.4 in GraceUI's host (EldenNet O25 as changed). Item 1's reason, hudhook's panic on a
lone surrogate, lapses with it, and the host still pairs surrogates for its holders (GraceUI §5.6); item 7's "hudhook's
initialisation" is the host's own, and the panics of items 6 and 7 are exceptions caught at the boundary (GraceUI §2.9,
D110).*

1. **`WM_CHAR` fix, before v0.2** (it affects the chat box too): hudhook 0.9.3 panics on a lone surrogate. Take
   `WM_CHAR` and `WM_UNICHAR` in `before_wnd_proc`, pair surrogates, pass the characters to `graceui.dll` in `frame`'s
   input, and to ImGui for the chat box.
2. **Pad input**: pass XInput state to `graceui.dll` while the menu holds input.
3. **IME**: position the OS composition window, through IMM, at the caret rectangle `graceui.dll` reports.
4. **Atlas**: rebuild on a scale change only while the menu is closed, at most four times a session, logged (D29).
5. **Capture**: one close routine shared with the chat box; neutralise buffered DirectInput reads; confine the OS
   cursor and restore its previous clip; never force the OS cursor's visibility; a lost-keys fallback that logs where
   focus was; log the present layers recognised at startup, by module name.
6. **Menu fault isolation**: a panic, an error code or a failed vertex check from `graceui.dll` turns off the menu
   alone for the session.
7. **`grace-host`** (GraceUI §2.9) **(G2, G10 (b))**: on T1, after the settings step and before T1 installs the
   overlay's hooks (add this step to ARCH §1.4's sequence), find `GraceUI\` beside `eldenring.exe`, check
   `graceui.toml`, load `graceui.dll` by full path with only the DLL-folder and System32 search flags, call
   `graceui_entry(1)` and `init`, and never unload it. In hudhook's initialisation, bake GraceUI's faces (at most ten,
   from its `fonts\`) into the one atlas and upload its two textures once. On T3, call `frame`, check its vertex list
   (counts, indices, clip rectangles, texture ids) and copy it into ImGui's draw list; pass input, the IME caret and the
   cursor shape; hand its requests (close, clipboard after a click, open the dashboard's own address) to `en-overlay`'s
   own code (§8 item 4). If the handshake has not finished when hudhook initialises, the menu stays off for the session
   rather than rebuilding the atlas. `GraceHost`'s `send` and `log` each start with `ffi_guard` and are covered by R3's
   grep (run over `grace-host` at the pinned tag); calls into `graceui.dll` rely on its own caught panics and error
   codes, and a tripped flag stops further calls.

## 5. Criteria and presentation

1. **C3** (DESIGN §1.3): menu open ≤ 0.8 ms p99 on the present thread at 1440p and ≤ 1.5 ms on the Deck; menu
   closed adds nothing.
2. **Input-leak tests** (the "no input leaks" exit check from v0.2, DESIGN §16.2, and the matrix's input-leak axis,
   ARCH §12.4) cover F5, Esc and B and every exit path of GraceUI §5.1.
3. **A server criterion**: GraceUI's security suite passes with zero failures (GraceUI U5).
4. **Fades** (DESIGN §12): the menu has its own timings (120 ms in, 200 ms out, 0 ms on danger; D24).
5. **Type minimum** (NPC §10.7): the 16 px minimum applies to prose and in-world surfaces; the menu's UI text is 15 px
   and its eyebrows 12 (D23).
6. **Warning colour** (NPC §10.1): proposed `#DB9550` for toasts too, always with an icon, because `#D2A24C` is as
   light as the gold (D25).
7. **Deck scale (G6, decided by the owner)**: the first run on a Steam Deck writes `ui.scale = 1.35`. *Later
   (2026-11-15): `ui.scale` is GraceUI's setting (D84), and EldenNet's text follows it through the host's
   display-changed event.*

## 6. Compatibility matrix (ARCH §12.4)

Add to "Other mods", each run with the menu open and closed and the dashboard on and off:

| Entry | Expected |
|---|---|
| ReShade, with and without its own overlay open | The menu draws and takes input; no input reaches ReShade's overlay while the menu holds it; any conflict documented |
| RTSS / MSI Afterburner on-screen display | The menu draws; any conflict documented (the practice tool lists RTSS as unsupported, ER02 §7.1) |
| A frame-generation or upscaler mod (DLSS or FSR frame generation) | The menu is not dropped or blacked out on generated frames (Magelight's field reports, ML §5 A); otherwise documented, with capture released after 2 s without a render |
| Driver-level frame generation (NVIDIA Smooth Motion, AMD Fluid Motion Frames) | As above |
| Another hudhook-based overlay (for example the practice tool) | Two MinHook copies in one process (ER02 §7.1): GraceUI detects the module, logs it and documents the pair as incompatible (D86) |
| Discord, Xbox Game Bar and NVIDIA app overlays | No input leak, no lost capture |
| The Steam overlay (already listed) | Shift+Tab with the menu open releases capture cleanly; the hand test of D30 |
| GraceUI missing, half-copied, older or newer than the one EldenNet came with, or for another ABI or protocol major | EldenNet's launcher installs or explains; in the process, GraceUI §2.9's rows (D88) |
| `graceui.dll` listed as a native by another loader | Nothing starts until a mod asks; then the same module serves every consumer |

*Later (2026-11-15):* §11 item 12 adds the framework's rows (another consumer, GraceUI without EldenNet, two copies,
an older major in and out of its window, another hooking overlay, a second world-state provider).

And: Proton × the menu × IME × the Steam keyboard; the Deck in game mode (menu only) and desktop mode (the dashboard
too); HDR × the menu (the ceiling and the opaque backdrop, D26); a non-ASCII install folder and Windows user name;
binding 127.0.0.1 raises no firewall prompt on Windows, and binding the network address raises one, once, when the
player switches `dashboard.lan` on (G9).

## 7. Small fixes found on the way

1. ARCH §11.5's example sets `greet_npc = false` under `[allow."card:vulgar_militia"]`, but `greet_npc` is a reaction
   id; the allowance keys are `npc_to_npc` and `reactions = { greet_npc = false }` (IA §12.1).
2. DESIGN §7.6's page lists every type and every persona slot, met or not; the owner kept that (G5, G8), each
   shown by its general profile only, and widened it to every named NPC and named boss with a bio (EldenNet O43 as
   updated; D67; §9 below).
3. DESIGN §7.6's page line ("named slots first, then kinds and their types") gains each kind's own row ahead of its
   slots or types, the `named` kind's included (`[allow."named"]`, also `merchant` and `summon`), so every inherited
   source an allowance names has a row of its own to open (GraceUI §4.6.4). G8's answer does not change this.
4. Record in EldenNet's `docs/DECISIONS.md` (indexed in its DESIGN §19), when EldenNet takes these changes up: G1, G2,
   G4–G6, G8, G9 and G10 as decided by the owner (2026-11-07), in the owner's words as GraceUI's DECISIONS.md gives
   them (G10: "approved", (b)). G3 (the browser app's framework:
   Preact, "yes") and G7 (the WIP tag) are GraceUI's alone and change nothing in EldenNet. *Later (2026-11-15):* also
   G15–G17 (2026-11-08; G10 superseded) and the map's G21–G27; EldenNet's O25 already records G15.

## 8. GraceUI as its own mod (G2)

1. **Install layout** (DESIGN §11.4): add `<game>\Game\GraceUI\` (GraceUI's folder: `graceui.dll`, `graceui.toml`,
   `web\`, `fonts\`), which EldenNet reads and never writes. *Later (2026-11-15): GraceUI writes its own `user\` there
   (D84), and EldenNet's launcher installs the folder where none or an older one is (D83).*
2. **Release** (DESIGN §16; ARCH §6.10): each EldenNet release zip carries the `GraceUI` folder of the GraceUI release
   current at its release date, byte for byte; the release job checks it against GraceUI's published `graceui.toml`
   and runs the handshake; the release notes name its version; a build constant records it, for the "older than the
   one this EldenNet came with" notice (GraceUI §2.7, D53).
3. **Failure contract** (DESIGN §2.7): add GraceUI §2.9's rows (GraceUI missing, mixed or for another EldenNet: the
   menu off with a notice), with EldenNet's own surfaces unaffected. *Later (2026-11-15): replaced by D88 (§11 item
   13): EldenNet's surfaces draw through GraceUI, so a failed connect leaves EldenNet headless.*
4. **Safety rules** (DESIGN §2.6) *(Later, 2026-11-15: under G15 (a) R12 becomes "EldenNet hooks nothing", and
   `grace-host`'s `unsafe` is GraceUI's own, inside `graceui.dll`; EldenNet's R9 list keeps only its consumer glue; §11
   items 1 and 4)*: R12 unchanged, since `graceui.dll` hooks nothing; R3 for `GraceHost`'s `send` and `log`; R14, it is
   never unloaded. **R9 and ARCH §2.4**: `grace-host` needs `unsafe` in the game process (loading the DLL by full path,
   `GetProcAddress`, calls through `GraceUiApi`'s pointers, slices over borrowed vertex buffers, the `extern "C"`
   `GraceHost` callbacks, writes into ImGui's draw list), and so does `graceui::abi`. R9's in-process list gains
   `grace_host::{abi, draw}` and `graceui::abi`; `grace-host` takes `en-overlay`'s row of ARCH §2.4
   (`#![deny(unsafe_code)]`, `#[allow]` only on those two modules, the same lint list and the eject path's
   `disallowed_methods`); the IME, cursor-clip, clipboard and open-address calls go in EldenNet's existing allowed
   modules (`en-overlay::hooks` or `en-gal::sys`; the IME, the cursor clip and opening the address are new there, §3
   item 4 and §4 items 3, 5 and 7), and ARCH §2.4's last column gains them, so `grace-host` needs no third one; and
   EldenNet's clippy-win job, its "`unsafe` outside the allowed modules" grep and R12's backstop greps (the eject path,
   `use hudhook::*`) run over `grace-host` at the pinned tag, since it is the second crate in the process that depends
   on hudhook. *Later (2026-11-16): in C++, `unsafe` and its lints become the CI check that confines raw addresses,
   `reinterpret_cast` and hook code to named files (ARCH §2.4), with `clang-tidy` for clippy; GraceUI's own are its
   DLL's `abi` files and `grace-host`'s `abi`, `hooks` and `draw` files, checked in GraceUI's CI (D110), and the
   eject-path and `use hudhook::*` greps lapse with hudhook (EldenNet O25 as changed).*
5. **Bug report** (DESIGN §14): names GraceUI's installed and expected versions and which file failed its check, by
   relative name only.

## 9. The roster and the Playthrough page (G5, G8)

1. **The content plan (decided by the owner, 2026-11-07; EldenNet O43 as updated, O45; O46 Open).** EldenNet v1.0 ships
   our own character profile, holding a general, stage-independent bio, for every named NPC and every named boss of the
   base game, beside the six personas and the seven kind profiles; every NPC with a profile holds a free conversation
   and keeps memories, matched to the speaker by ID (O45). The bio's structure and the DLC's timing are EldenNet O46
   (Open; recommended: one bio for every named NPC, the DLC's roster in v1.1). Stages stay a separate feature:
   Characters › Playthrough and the personas' stage tracks. The bios are generated after a game scan and research, once
   Min's engine blueprint has arrived and O46 is decided, and land in batches from EldenNet v0.5 to v1.0 (EldenNet
   DESIGN §7.8).
2. **A first-meeting field, reviewed for spoilers.** Personas have `public.summary` (NPC §3.1: ≤ 60 words, public, the
   Stranger Test). GraceUI now shows it before any meeting, so the release review confirms it holds at a first meeting
   (the earliest fixture state, before any stage). Character profiles, cards and types gain the same public field (≤ 60
   words for a character, ≤ 30 for a card or type; EldenNet O46 recommends `public.summary` ≤ 60 words in every bio,
   named as the persona's), checked by PL11 at the opening fixture state and by PL7 at every state (EldenNet NPC §4.8),
   and reviewed like shipped content: never a stage, fate, later form, reveal, quest step or relation the story
   discloses later.
3. **The roster in `named`**: every named NPC and every named boss with a shipped general profile, in name order, by the
   name the player's install gives, or `display_name_fallback` until the game shows another name, at the title screen
   too, a boss the game never shows calm before its fight marked, per entry in the index, for the "Boss" tag (D67);
   bosses and invaders without a bio only once the game names them (SP4).
4. **A `playthrough` projection** (GraceUI §6.5): for each journey of the loaded playthrough, the named NPCs met; status
   from perception only (here now; alive when last met; away, when the Tarnished came within speaking range of where
   they were last met and did not perceive them, a small new record per profiled NPC (EldenNet O45); fell, from a stored
   death event with the `seen` bit, §2 item 7); where and when last met (MEM §2.2's facts); the disposition word; the
   public line of the stage in `stage_seen` (MEM §2.2), never of a later stage; related people only if met and named by
   that stage's `public.links`; up to three notes since the last conversation, from records, with the since-line's
   comparison (MEM §12.6); the world-now line (the highest-ranked world stage that holds, SP6).
5. **Per-stage public lines**: each persona stage may carry `public.line` (≤ 25 words: what the Tarnished has seen of
   the story by that stage, in our words) and `public.links` (the related personas that line may name), linted
   against the Tarnished's lock set at that stage and reviewed like content; a stage without a line shows nothing.
   *Later (2026-11-16): EldenNet's O86.* There is no lock set: each line is reviewed when written, at its stage, and
   nothing lints it against a set of terms (EldenNet MEM §7.5; SP2 as re-cut).
6. **The prompt**: "the framework will inject correct NPC profile into the request payload prompt" (the owner, G8) is
   EldenNet DESIGN §7.8's match by profile id (O45); GraceUI shows only the public fields.
7. **Memories and conversations for every profiled NPC (O45)**: the `memories`, `journal/<id>` and `conversation/<id>`
   projections cover every NPC with a profile, keyed by its profile id; a keepsake about another NPC is projected only
   when every event it cites had the Tarnished in its audience (SP7, D68).

## 10. The map (proposal 0002; G21–G26 decided 2026-11-15)

What EldenNet must add if the owner accepts [proposal 0002](proposals/0002-map.md) as recommended. Nothing is built
before the design fold (EldenNet roadmap S7.8.c) and the map spike (S7.9.c).
*Later (2026-11-15): decided and folded.* The owner answered G21–G26 on 2026-11-15 (G27 on 2026-11-14); the map ships
in 1.1, after EldenNet v1.0 (G24 (a)), drawn in the framework's map panel (D91, under G16 (a)); GraceUI §4.13 holds the
design and D89–D98 its decisions. The items below stand with their Later notes.

1. **The map image reader** (G21 (a)): the player's own map, read from their game at run time by a route the spike
   finds (the game's memory, or its archives with keys read from the player's game and never shipped); a fit check
   against the vanilla map's size and aspect (D93); a cache in EldenNet's user folder, never uploaded, logged or
   reported (D94); masking in the core (D90): tiles that touch a reached area for the browser, and the in-game texture
   as G26 decides. If no route works, the drawn base is the map.
   **Exact in every base** (the owner, 2026-11-14; D97): the drawn base is built on the player's PC from their own
   game, its shape traced from their map image (or from the game's own map and terrain data where a map mod does not
   fit), its areas from the game's map data and its placements by the game's own conversion tables; it is cached
   locally, and no shape is shipped or committed.
   *Later (2026-11-15), from the research (proposal 0002 §14):* the reader composes the game's own tiles for the map
   pieces the player holds (the game's own masks say which variant), at each zoom level, so the base needs no masking;
   the fit check becomes a check of the tile set's structure; the run-time read of the game's archives is what the
   spike has left. *G26 decided (a), as put again:* the in-game texture is composed at start from the tiles for the
   pieces held, and composed and uploaded once more when a piece is gained (GraceUI invariant 11's one exception).
2. **World positions to map positions**: the open world's tile grid; the legacy dungeons' own frames (a conversion
   table is likely in the game's parameters, to be verified, O48); the underground and the expansion as separate
   sheets. The numbers may be read for research by a DataPiler job; EldenNet verifies them in game.
   *Later (2026-11-15):* found (EldenNet WHY§78): one transform per open-world sheet, about 20 units; the underground
   and the dungeons through the game's own conversion table, about 40; the game's area image gives the area under a
   point at 4 m (WHY§80, §82). EldenNet still verifies them in game.
3. **The sightings store** (D89): for each profiled NPC, each place and time the Tarnished perceived them, with the
   stage then; it extends §9 item 4's "where and when last met".
4. **Map overlays by world stage** (O51): a table of our own drawings keyed by the world stages that hold (a flood, a
   beacon gone dark, a city in ash), shown only where the area is reached.
5. **The `map` projection**: marks, lines, areas, labels, tooltip and detail text, timeline events; public fields only
   (SP1), names as the game shows them (SP3), no counts of anything unreached (SP5), verified flags only (SP6).
6. **Lore-note proposals** (G23 (a)): from two or more memories tied to one place or person, citing them, batched on a
   light slot under pacing, through the lock filter (SP2); kept, edited or dropped by the player; stored per
   playthrough.
   *Later (2026-11-15): G23 decided (c).* The notes are added to the map by the model itself, by the same rules; the
   player edits or removes them, a removed note is not written again from the same memories, and a setting (Lore notes
   by AI, on by default) turns it off.
   *Later (2026-11-16): EldenNet's O86.* There is no lock filter: a note is written from the player's memories, which
   hold only what each NPC could know (EldenNet MEM §3.3, §7), and SP2 as re-cut shows only what the game showed or
   EldenNet wrote from the same grounded inputs.
7. **Terrain** (G27 (a), decided 2026-11-14): the terrain's heights read from the player's install (the source to be
   found by S7.9.c: map pieces, collision or a height texture, each structure verified, O48), built once on the PC
   with the drawn base and cached; projected for the browser's 3D relief and rendered once as the in-game relief
   image; masked to what is reached.
   *Later (2026-11-15):* the relief is part of the base, so it shows only what the player's own game map shows for the
   pieces held (D90), and in game it is composed with the map's texture (D98).
   *Later (2026-11-15):* the source is found: the ground models at the 1 km level, placed by the tile numbers in
   their names alone (WHY§90, roadmap S7.9.g). The run-time read of them and a rule for their gaps stay with S7.9.c.
   *Later (2026-11-15):* the expansion and the underground read too (WHY§92, S7.9.h): the run-time read also takes
   the far-view models, to fill where the expansion's land is made of other models, and each underground map's
   pieces with its placement file. Which of the underground's stacked floors to show stays with S7.9.c.
   *Later (2026-11-15):* the underground's floor rule (WHY§94, S7.9.i): the topmost walkable floor reachable from where
   each map's characters and resting places stand, so the run-time read also takes the placement files' characters
   as seeds; where levels stack, the in-game relief shows the upper one.
8. **The map key** (G25 (a)): a setting, off by default; with it on, GraceUI catches the press and the game never sees
   it; pressed again or held, it opens the game's map. Checked with the spike's input items and S4.

## 11. GraceUI as the host (G15–G17, decided 2026-11-08)

What G15 (a), G16 (a) and G17 (a) change in EldenNet ([proposal 0001](proposals/0001-ui-framework.md) §5, D69–D88).
Folded into EldenNet's design on 2026-11-15 (roadmap S7.8.c), ahead of S7.1, so that nothing is folded twice: where an
item above and one here disagree, this one holds. It replaces §1 item 1's "EldenNet's one hudhook overlay" and the G10
(b) parts of §1 items 4 and 5, §3 items 1, 5, 7 and 8 where they name EldenNet's server, and §4 items 1 to 3, 5 and 7;
§4 items 4 and 6 become GraceUI's own (D29, D76).

1. **O25 amended by the owner** (G15 (a)): the pin and every rule move to GraceUI's `grace-host` in `graceui.dll`;
   EldenNet's `licences` CI stage denies hudhook, MinHook and any other hook library, and allows `imgui` only without
   a renderer backend; its greps check that no hook library is linked. *Later (2026-11-16): the owner put Dear ImGui
   1.93.0's DX12 and Win32 backends and MinHook 1.3.4, pinned, in hudhook's place in `grace-host` (O25 as changed);
   EldenNet's `licences` stage, its C++ dependency check now (ARCH §12.1), refuses MinHook and every other hook library
   and allows Dear ImGui's core only, at the host's version, with no backend.*
2. **`en-overlay` becomes EldenNet's display layer**: its surfaces (subtitles, the chat box, the call-out box, toasts,
   world labels) are panels, built with GraceUI's `grace-draw` and `grace-view` crates (pinned by tag) and drawn
   through an `on_present` callback within its budget; its faces are registered with `register_face` before warm-up,
   and it follows the atlas's generation; its HDR handling is GraceUI's. The F11 debug view keeps its imgui-rs code on
   a headless context of EldenNet's own (`imgui` 0.12, the Dear ImGui 1.89.2 that hudhook 0.9.3 bundles, with no
   backend), its draw data submitted as a panel while it is open, its font atlas registered once as a texture and its
   input taken through `request_input`; only F11's frame glue is designed anew for v0.1. *Later (2026-11-16):
   `grace-draw` and `grace-view` are C++ modules, fetched pinned by tag; F11's view is C++ on Dear ImGui 1.93.0's core
   with no backend, the host's version, in place of imgui-rs on `imgui` 0.12 (O25 as changed).*
3. **Input**: the DInput and XInput detours, the window-procedure filter, the `WM_CHAR` fix and IME placement move to
   GraceUI, which finds the XInput DLLs by module name, not through the game's import table; the chat box and call-out
   box take input through `request_input`; EldenNet's keys (the chat key, F11, the talk gesture's chord) are
   registered hotkeys; the Steam on-screen keyboard is opened by GraceUI for its own fields and by EldenNet for its chat
   box, as now. *Later (2026-11-16, GraceUI's 0.1 host, its input half, D124 to D130): a key held as a holder takes
   input does nothing in it until released, its characters included, so NPC §5.1's rule of swallowing the first
   `WM_CHAR` that matches the hotkey within 100 ms is GraceUI's general rule now, and the chat box needs none of its
   own; NPC §5.5's packet number "bumped once" becomes one more for each grant and each close (D128); `GetDeviceData`
   stays unhooked unless S4 finds the game reads it (D127, as NPC §5.5 has it).*
4. **ARCH**: threads T3, T4 and T5 become GraceUI's (EldenNet's T3 work is its `on_present` callback, under the same
   rules); C12's capture statics become GraceUI's state, read through events; R12 becomes "EldenNet hooks nothing";
   §1.4's and §3.2's Hooks step becomes "connect to GraceUI" on T1 after the safety step; §10.3's "Overlay failed to
   install" row covers a failed connect.
5. **The banner and toasts** use GraceUI's notice layer. On an anti-cheat hit GraceUI is notice-only (D88), `connect`
   answers `inactive (anti-cheat)` and EldenNet's banner goes through `notice`, so ARCH §5.3's "a hit at init → L1
   banner" becomes "a hit at init → the banner through GraceUI's notice-only mode", and ARCH §10.3's banner and toast
   rows name GraceUI's notice layer. An unknown build or another L1 trip: EldenNet connects as usual and shows its
   banner through `notice`.
6. **The launcher** checks `Game\GraceUI\graceui.toml` (present, hashes, major, floor) before start, installs its
   bundled GraceUI only where none or an older one is, and over an older major only while the bundled release still
   serves it (D83), and explains a failure in E08's manner (D88).
7. **The server** (G17 (a)): EldenNet links no `grace-serve`; it publishes each revision once to GraceUI, registers its
   space with `browser = "always"` (G4) only once its gate and safety steps pass, withdrawing it on T10's anti-cheat
   hit (D81), and marks its sensitive commands (G9). `dashboard.*`, `hotkeys.menu` and `ui.menu.*` leave
   `settings.toml` by migration for GraceUI's own settings, and `devices.toml` moves to GraceUI's user folder (D84); the
   launcher's first screen names the address GraceUI serves. "No other thread is created" holds for EldenNet again:
   the server's threads are GraceUI's. *Later (2026-11-16): the launcher's first screen, and F11's Copy dashboard
   address and Open in browser (§3 item 4), show `http://127.0.0.1:PORT` only, the one address GraceUI shows, with the
   port read from GraceUI's settings; `localhost` and `[::1]` are redirected to it (GraceUI §7.2 items 14 and 23, D84,
   D112).* *Later (2026-11-16): `eldennet.dll` still links no `grace-serve`; EldenNet's launcher links it as the library
   for hosts outside the game (G29 (a), §13 item 4).*
8. **The world-state provider**: EldenNet publishes danger, loading, a game menu or text entry open, the title screen,
   multiplayer and an anti-cheat hit from `safe_context`, its `ui` group and T10 (D78), as one provider among any
   others, and keeps gating its own space.
9. **EldenNet's pages** wait for G20 (Open). Under its recommended answer, (a), its page data, fixtures and goldens,
   SP1–SP9, its PV rules and D61 join EldenNet's documents and CI, and GraceUI's page lint runs there (D82). *Later
   (2026-11-16): G20 decided (a), and folded the same day (§13 item 1).*
10. **D34 widened**: lines addressed to the player wait while any input holder is active, from GraceUI's "input held
    by" event.
11. **DESIGN §12 and §13**: the presentation row names GraceUI as the host; §13's seams name the `UiPort`, with GraceUI
    as its implementation.
12. **The compatibility matrix** (ARCH §12.4) gains rows: another GraceUI consumer loaded; GraceUI without EldenNet;
    two copies of GraceUI at two paths; GraceUI listed as a native in a loader; an older GraceUI over a newer one; an
    installed older major, inside and outside the bundled release's support window; another mod's hooking overlay
    beside GraceUI's; another mod as a second world-state provider.
13. **Failure contract**: GraceUI U9 and D54's "EldenNet keeps playing" are replaced by D88; EldenNet's v0.1 exit
    checks gain "one hook library in the process: GraceUI's".
14. **Unchanged**: the `UiPort`'s core side (projections, one command entry point, the settings export), the
    scheduler's other rules, R14, R16 (EldenNet keeps its own single-instance mutex), the read-only rule.

## 12. EldenNet's plugins (EldenNet O65–O69, decided 2026-11-15)

GraceUI's part of EldenNet's own decisions (EldenNet's proposal 0001); EldenNet's design holds the rest.

1. **The hook list** (D102): GraceUI publishes the functions it hooks, and EldenNet's CI checks its own hook targets
   against it once EldenNet has a hook of its own (EldenNet O65, R12).
2. **The Plugins page** (D104, which superseded D103's place on Content packs) lists EldenNet's plugins in rows, with
   EldenNet's commands `plugins.enable`, `plugins.mode` (Cluster or Detached, EldenNet O70), `plugins.order`,
   `plugins.remove`, `plugins.restart`, and `plugins.install`, which EldenNet marks sensitive (D81); EldenNet publishes
   the `plugins` topic.
3. **The upload route** for the browser's drop zone (G28, decided 2026-11-15, as recommended): EldenNet's
   `plugins.install` takes its file through GraceUI §7.2 item 18, as a command marked sensitive and taking a file.
4. **The plugins-stopped pop-up** (D105, EldenNet O70): EldenNet asks GraceUI for it when a plugin host stops, with
   the plugin, the cause and how sure it is; the choice comes back as `plugins.restart`. In game GraceUI shows a
   notice first and the modal only at a safe moment.

## 13. EldenNet's pages, the framework's opening and the server off game (G18–G20, G29; decided 2026-11-16)

What G18 (a), G19 (a), G20 (a) and G29 (a), decided by the owner on 2026-11-16, as recommended, change in EldenNet.
Folded into both designs the same day (EldenNet's roadmap S7.8.d and S7.12.e).

1. **EldenNet's pages** (G20 (a), D82; proposal 0001 §5 item 9): its page data, fixtures and goldens, SP1–SP9, its PV
   rules and D61 join EldenNet's documents and CI, and GraceUI's page lint runs there. Done: EldenNet's
   `docs/design/pages.md` (PAGES) holds GraceUI's §4 content under its numbers, SP1–SP9 (PAGES §4.8), PV1, PV2, PV4, PV5
   and PV8 (PAGES §4.9), the fixture states and the spoiler, roster and Playthrough goldens (PAGES §5) and D61 (PAGES
   §6), with invariants 1 and 2 in their EldenNet form (PAGES §3); EldenNet's DESIGN §12 points to it. EldenNet's pages
   are compiled into `eldennet.dll` and registered at its start; they run GraceUI's page lint and parity tests in
   EldenNet's CI against a pinned GraceUI.
2. **The framework opens after EldenNet v1.0** (G18 (a)): GraceUI 1.1 publishes the guide, the examples and a frozen
   ABI 1; until then ABI 1 is a preview, which EldenNet pins by tag as it does now. Nothing else changes in EldenNet.
3. **SkyrimNet-derived material** (G19 (a)): none in GraceUI; whatever EldenNet takes from SkyrimNet lives in
   EldenNet's own pages, under EldenNet's own licence rules (its O53, O84).
4. **The server off game** (G29 (a)): GraceUI publishes `grace-serve` as one library for hosts outside the game, with
   every rule of GraceUI §7.2 and invariant 4 (GraceUI §7.2 item 26). EldenNet's launcher links it, and serves the
   dashboard with the game closed through it once EldenNet's engine runs there (O76); `eldennet.dll` links none. The
   home-network switch and pairing (G9) stay in the game at first, so the launcher serves on loopback only. In EldenNet:
   DESIGN §3.2, ARCH §4.13 and ARCH §2.3's launcher row; the rest of O76 to O79 is folded with S7.12.c.

*Later (2026-11-16, D115):* GraceUI's own settings (`hotkeys.menu`, `ui.scale`, `ui.menu.*`, `dashboard.*`) are homed on
GraceUI's own pages (Menu, Hotkeys, Browser view), and EldenNet's pages carry only links to them. EldenNet keeps two
keys of its own instead of GraceUI's: `ui.home_cards` (was `ui.menu.home_cards`) and `ui.show_world` (was
`ui.menu.show_world`), in its `settings.toml`; its Settings page keeps General, Hotkeys (its own: talk, skip page,
debug) and Developer.

## 14. ABI 1, the anti-cheat check and two copies (S10.4.b, 2026-11-16)

What GraceUI's 0.1 host settled on 2026-11-16 (D131 to D136; EldenNet's roadmap S10.4.b) asks of EldenNet. None of it
is folded into EldenNet's design yet.

1. **Connecting** (D131, D136; ARCH §3.2's Connect and ConnectB): T1 finds GraceUI with `graceui-sys` (`graceui_sys.h`,
   or its C++ wrapper `graceui::Api::find`), on T1 and never in `DllMain`, allowing the load from `Game\GraceUI\`
   (`GRACEUI_SYS_LOAD`), for at most 30 s, then connects as `eldennet` (its id, `[a-z0-9-]{3,32}`, also its space's
   id), its display name, its version, its GraceUI floor (0.1.0 for v0.1) and the caps it uses (`notices` for v0.1).
   The answers: ok, as designed; inactive with `GRACEUI_INACTIVE_ANTI_CHEAT`, the banner through `notice` (item 2);
   inactive with another reason (the overlay could not start, or turned itself off), too_old (GraceUI has already
   shown a notice naming EldenNet and both versions), duplicate_id (another mod took the id) or refused: a failed
   connect, EldenNet headless at L2 with one log line, as ARCH §10.3's "Overlay failed to install" row has it.
2. **The banner and toasts as notices** (D134): a notice is a tone (normal, warning or danger: gold, ember or rot), a
   key, a duration and 1 to 512 bytes of UTF-8 text. The banner is one notice under a key of EldenNet's with a duration
   of 0, shown until EldenNet withdraws it with an empty text under the same key, or replaces its text in place; the
   pause badge likewise under its own key; a toast is a timed notice. At most four of a mod's notices show at once, a
   fifth answering full: the banner, the pause badge and DESIGN §12's two toasts fit. GraceUI 0.1 draws notices plainly
   at the top right, three lines at most, until S10.4.c gives them the Grace look.
3. **ARCH §5.3** (D133): "GraceUI makes the same check before it hooks" becomes "GraceUI makes the same check in its
   first `GraceUI_RequestApi`, before it returns its table and before it hooks". Both use the same two clauses; where
   EldenNet's own check hits and GraceUI's did not (an anti-cheat module loaded between the two), `connect` answers ok
   and EldenNet shows its banner through `notice` all the same: its gate stays its own.
4. **ABI 1 is a preview** (D131, D132; §13 item 2): EldenNet pins GraceUI by its release and compiles against that
   release's `graceui.h`. In S10.4.b only `has_cap`, `connect`, `disconnect` and `notice` work, which is all the
   banner needs; F11's view waits on S10.4.c and d (`create_panel`, `submit`, `request_input`, `register_hotkey`),
   whose slots answer not_yet until then. A slot is called only if the table's size covers it
   (`GRACEUI_API1_COVERS`), and a feature used only if `has_cap` names it.
5. **Two copies** (D135): ARCH §12.4's row "two copies of GraceUI at two paths" expects the second to idle and the
   active one to name it in a notice, by its name relative to the game's folder.

## 15. Panels, draw lists, textures, faces, input and providers (S10.4.c, 2026-11-16)

What GraceUI's 0.1 host settled on 2026-11-16 (D137 to D144; EldenNet's roadmap S10.4.c) asks of EldenNet. None of it
is folded into EldenNet's design yet.

1. **The order at T1** (D138, D139): connect with the caps EldenNet uses (`notices panels textures faces input
   world-state`, each checked with `has_cap`), then, before anything is submitted, register every face (at most eight
   for `eldennet`, EST) and every texture (at most 8 textures and 8 MiB, EST); then create the panels (at most 16,
   EST) and submit. The first `submit` closes EldenNet's registration window (a later texture answers `too_late`), and
   the overlay's warm-up, at the game's first frames after the hooks, seals the faces: EldenNet connects on T1 before
   the game draws, so its faces are in time; a face that comes too late answers `too_late`, and EldenNet then draws its
   text from GraceUI's own face 1 (Noto Sans 15) or its own atlas texture.
2. **Text in panels** (D139): `glyph_table` answers `not_ready` until warm-up; then each face's glyphs, sorted by code
   point, the pen at the top of the line as Dear ImGui's, with the atlas's texture id and generation 1 for the session.
   A list that draws glyphs names that generation in `atlas_generation`. EldenNet's subtitles, call-out box and feed
   lay their text out from these tables (its NPC §10's sizes as faces of its own) or, for F11, from its own Dear ImGui's
   atlas registered as a texture.
3. **Draw lists in the panel's own pixels** (D137): a list's origin is the panel's top-left corner, whatever the
   anchor; clip rectangles within the panel; at most 256 KiB of vertices and of indices and 64 commands. A refused list
   leaves the panel its last one, with one log line for the connection. EldenNet's F11 view on a headless Dear ImGui of
   its own sets the display size to the panel's, sets `ImGuiBackendFlags_RendererHasVtxOffset`, and appends its
   ImDrawLists into one list, each command keeping its vertex offset, as `examples/hello-panel` does.
4. **Layers** (D76, D137): the subtitles, labels and feed on the hud layer; the chat box and call-out box on the panel
   layer; the rollback modal on the modal layer; toasts, the banner and the pause badge as notices, which draw above
   every layer in the Grace look (D141), in Noto Sans 15 from GraceUI's own `fonts\` folder.
5. **Input** (D142): the chat box asks `request_input` for its own shown panel made with `GRACEUI_PANEL_TAKES_INPUT`
   (a modal holder for the rollback modal, on the modal layer); the answer is at once, and a refusal names the
   holder's mod id. Hiding the panel ends the holder; the player's exit always does. What the holder types reaches it
   through `poll`, with §6's protocol (GraceUI 0.2): until then the chat box can take the controls from the game but
   receives no characters, so EldenNet's v0.1 has no chat box in game, as its increments plan.
6. **EldenNet as a world-state provider** (D143): `set_world_state` with its claims (danger from `safe_context`,
   loading, a game menu or text entry, the title screen, multiplayer, its own anti-cheat finding), replaced whole at
   each call; danger and loading end any holder at once, and a game menu keeps `request_input` at not now. EldenNet's
   hud panels are its own to hide on danger: GraceUI ends only the view that holds input.

## 16. The host review's fixes (2026-11-16, D145 to D153)

An independent review of GraceUI's in-game host found three Medium and fourteen Low issues, all fixed the same day.
What EldenNet must know or change; none of it is folded into EldenNet's design yet:

1. **Faces within the atlas's room** (D148): each face counts its estimated area against the shared atlas's room when
   it is registered: the glyphs its font has in its ranges, each as a box of (2 × size + 2) by (size + 2) pixels. The
   mods' faces share 8192 × 4096 estimated pixels; the mods other than EldenNet use at most half of it, so EldenNet's
   faces always have the other half, and all of it when no other mod registers faces. A face past the room answers
   `full` (and leaves the face's count as it was). EldenNet registers only the ranges each face needs: a CJK face of
   the whole unified block at 24 px is about 27 million estimated pixels on its own, which fits only while the other
   mods use little; its language's own kana and common kanji are a fraction of that. Should Dear ImGui still run out of
   room, the bake is made again without the later faces, and their `glyph_table` answers refused: EldenNet's text then
   falls back to GraceUI's face 1 or its own atlas texture, as for a face that came too late.
2. **The bake takes a few frames** (D147, D148): warm-up bakes on GraceUI's own thread, so `glyph_table` answers
   `not_ready` for some frames after the seal rather than for none; EldenNet already asks until it is ready.
3. **A command stride of at most 256 bytes** (D152, `GRACEUI_DRAW_COMMAND_SIZE_MAX`): EldenNet's lists use ABI 1.0's
   40-byte command, well within it.
4. **World-state claims** (D145): nothing changes in what EldenNet calls; a request made just as a provider claims
   danger is now answered not now (danger) rather than granted, and the last claim any provider made decides.
5. **The first request** (D153): a request made while another thread's first request is still running waits 2 s at
   most, then answers null; EldenNet asks from a thread of its own (D70), never from `DllMain`, and `graceui-sys`'s
   look every 250 ms asks again.
6. **The game's window** (D146): GraceUI now filters the bound swap chain's window, whatever window appeared first in
   the game's process (a mod loader's console is never taken for it); nothing for EldenNet to change.
7. **When the overlay is off** (D149, D150): a removed or replaced device, or a fault, turns the render path off for
   the session, and input with it; `connect` and the slots then answer inactive (render dead), and F5 reaches the game.
   EldenNet shows its own fallback (its launcher's notice, its log) as for any inactive session.

## 17. Hotkeys and a first `poll` for F11 (S10.4.d, 2026-11-17, D155)

Agreed with EldenNet's session on 2026-11-17: F11's toggle goes through GraceUI's hotkeys, brought forward into 0.1.
None of it is folded into EldenNet's design yet.

1. **Registering F11** (D155): after `connect` with the cap `hotkeys` among its caps (checked with `has_cap`), T1 calls
   `register_hotkey` once: action `debug-view` (1 to 32 of a-z, 0-9 and '-'), a display name of its own, key `VK_F11`
   (0x7A), modifiers 0, pad chord 0. The answer is `ok` with the hotkey's id, bound or, if another mod or GraceUI's
   menu key took F11 first, unbound (GraceUI logs which); `refused` for Escape, a modifier, Alt+F4 or a bad action;
   `not_yet` for a pad chord; `full` past 8 a mod. With `graceui_sys.hpp`: `api.register_hotkey(me, "debug-view",
   name, VK_F11, 0, id)`.
2. **Taking its presses** (D155): `poll(me, &event)` on EldenNet's own thread (T6, or T1's loop until T6 exists),
   called until its kind is `GRACEUI_EVENT_NONE`; each `GRACEUI_EVENT_HOTKEY` names the hotkey's id and its presses
   since the last event (1 as a rule); an odd count toggles the view. `poll` never waits; once per frame of EldenNet's
   loop is enough. With `graceui_sys.hpp`: `api.poll(me, event)`.
3. **What the game sees**: nothing of F11 while EldenNet's hotkey binds it, through DirectInput or as messages, as for
   the menu key; while a holder (the menu, the chat box, a modal) has input, F11 fires nothing. EldenNet reads no key
   itself and hooks nothing for it (invariants 8 and 16).
4. **ARCH**: F11's row moves from EldenNet's own input to GraceUI's hotkey (`hotkeys.debug_view`'s home is GraceUI's
   Hotkeys page from GraceUI 0.6, D115); EldenNet's §14 item 4 above (F11 waiting on `register_hotkey`) is settled by
   this.

## 18. The banner and the F11 view as part 12 builds them (S10.4.d, 2026-11-20, D156)

What S10.4.d gives EldenNet's part 12; none of it is folded into EldenNet's design yet. `examples/hello-debug-view` is
the whole of it in miniature, with invented content, and the harness runs it beside a second mod, each loaded before and
after GraceUI.

1. **The banner** (D134): one notice under EldenNet's own key with a duration of 0, its text replaced in place by a
   later `notice` under the same key and withdrawn by an empty text. It needs only the cap `notices`, and it works in
   the anti-cheat's notice-only mode, after `connect` answered inactive (anti-cheat) with a handle.
2. **The F11 view on Dear ImGui's core** (D156): EldenNet compiles Dear ImGui 1.93.0's core with no backend (as
   `gu_imgui_core` does) and includes `graceui_imgui.hpp` from `graceui-sys`. On T1, after `connect` with the caps
   `notices panels textures hotkeys`: `ImGui::CreateContext()`, `graceui::imgui::prepare(io)`, its fonts added, then
   `graceui::imgui::register_atlas(api, me, io)` before its first `submit` (its registration window); a panel of its
   own, hidden, made with `create_panel`; F11 registered (§17). Each frame of its own, on its own thread: build the
   view with `io.DisplaySize` set to the panel's size, `ImGui::Render()`, then `list.build(*ImGui::GetDrawData(),
   width, height)` and, if it answered true, `api.submit(me, panel, list.view())`. A frame past ABI 1's bounds (256 KiB
   of vertices or indices, 64 commands) is not built; the view then keeps its last list.
3. **Opening and closing**: on each `GRACEUI_EVENT_HOTKEY` for F11 with an odd count, the view toggles: to open, one
   list submitted and then `show_panel`, so it never shows empty; to close, `hide_panel`. The F11 view takes no input
   in 0.1 (no `request_input`): its keys and pointer reach a holder only with §6's protocol (D142).

## 19. EldenNet's pages in GraceUI's page format (S11.2.h, 2026-11-20, D160)

What S11.2.h settles for EldenNet's pages, which are EldenNet's since G20 (S11.4.a builds them). None of it is folded
into EldenNet's design yet.

1. **One file per space**: EldenNet's page data is a TOML file in GraceUI's format, as GraceUI's own space and the demo
   space are (`src/grace-pages/spaces/*/space.toml` in GraceUI): `[space]` (its id `eldennet`, name, settings schema
   number, commands), `[[topic]]` (each topic of §6.5 with its shape, a stream with its ring), `[[category]]`,
   `[[setting]]` (§6.9's fields) and `[[page]]` with its sections or tabs, blocks and actions. GraceUI's demo space shows
   every part of it.
2. **The lint**: EldenNet's CI runs `python3 -I tools/pages/build.py --lint <EldenNet's space.toml>` from a pinned
   GraceUI. It checks, among the rest, that every setting appears on exactly one settings block on its home page
   (invariant 12), that a secret's default is empty, that each component draws a declared topic of its shape, and that
   each action's command is one EldenNet declares. A link to one of GraceUI's own pages is written `graceui/<page id>`
   (`graceui/graceui.menu`, D115).
3. **Into the DLL**: EldenNet turns its TOML into the protocol's JSON (`space_pages` and `settings_description`), as the
   tool does for GraceUI's built-in spaces, and hands both over at `register_pages` (S11.2.b types the slot). Its
   settings description's hash is the first 16 hex digits of the SHA-256 of its canonical JSON.
4. **Availability and tri-states**: a setting's availability is `live`, `later`, `inert`, `check:<id>`,
   `version:<x.y>` or `requires:<key>`, a later one locked; a tri-state's values are `on`, `off` and `auto`. *Later
   (2026-11-20, D163, at EldenNet's request):* a tri-state names its third value, `third = "auto"` or
   `third = "inherit"`; EldenNet's allowances and per-reaction switches are `inherit` tri-states (on, off, inherit).
   For each one on `inherit`, EldenNet's `settings` topic body lists in `inherited` the key, whether it resolves on or
   off, its source in words (the chain's link that decides: the named slot, the type, the live kind, the base kind or
   the shipped default) and, if it has one, the page that sets it. Both views show it faint beside the row, the
   browser with the source as a link.

## 20. From part 12's run (2026-11-20, D164)

What GraceUI changed after EldenNet's part 12 ran end to end against f51bcc7. None of it is folded into EldenNet's design
yet.

1. **An unbound hotkey answers so**: `register_hotkey` answers `GRACEUI_UNBOUND` (11) with the hotkey's id when the
   hotkey is registered but unbound (its default taken, or none), and `GRACEUI_OK` only when it is bound. EldenNet's
   launcher keeps its own Pause key while EldenNet's GraceUI Pause is unbound; F11 likewise. With `graceui_sys.hpp`, the
   wrapper returns the status as it comes. §17 item 1 reads accordingly.
2. **Not installed, stop at once**: `GraceUiSysFound` has `installed` (1 if a module named `graceui.dll` was loaded or
   `GraceUI\graceui.dll` was in the game's folder at the last look, else 0), and `graceui_sys_find` takes the flag
   `GRACEUI_SYS_STOP_IF_ABSENT`, which ends the looking at the first look that finds GraceUI not installed. EldenNet's
   own rule for this can go, using the flag instead.
3. **The vendored headers**: EldenNet's copy of `graceui-sys` (with `graceui_test.h`) is updated to the commit that
   brings these; every struct keeps its size field.
4. **Imports of a consumer's Dear ImGui core**: built with `/Zc:threadSafeInit-`, the core does not import the
   runtime's thread-safe statics; `ReadConsoleW` comes with `sscanf` either way.
5. **GraceUI's build writes `graceui.toml`** (D165): every Windows build of GraceUI lays out `GraceUI/` beside `bin/`
   (`graceui.dll`, `LICENSE.txt`, `NOTICE.txt` and the `graceui.toml` listing them), so EldenNet's harness installs
   that folder and drops its own writer. The format is D52's: `graceui`, `abi`, `protocol`, `caps` (empty for now),
   `released` and `[files]`, names relative with `/`. EldenNet's release job checks the carried folder with
   `python3 -I tools/release/manifest.py check FOLDER` from GraceUI's tag, or its own reader of the same subset.

## 21. The in-game menu (2026-11-20, D167, S11.2.b)

GraceUI's menu now opens on F5, drawn by the host: GraceUI's own space, with GraceUI › Menu (`ui.scale`,
`ui.menu.theme`) as S4's page. What it means for EldenNet:

1. **F5 is GraceUI's.** With no holder, F5 opens GraceUI's menu (the holder of kind menu that GraceUI asks for, owner
   0). EldenNet's launcher, harness and documentation should expect that: a test that presses F5 with no holder now
   finds the menu holding input until a second F5 or Esc from the rail.
2. **The fonts folder holds eight faces** (`Game\GraceUI\fonts\`): Noto Sans Regular and SemiBold, Noto Sans Mono
   Regular, Cinzel Regular and Bold, EB Garamond Regular, Medium and SemiBold, with `OFL.txt` (Noto's),
   `Cinzel-OFL.txt` and `EBGaramond-OFL.txt`. GraceUI's build lays them out in `GraceUI/fonts/`, listed in
   `graceui.toml` (D165): install that folder as it is.
3. **The atlas is larger**: GraceUI's own faces take more of it (1024 × 2048 at 1080p with no mod's faces, measured
   under Wine). EldenNet's eight faces (D139) still fit; should a bake run out of room, the mods' faces go first, then
   the menu's, so notices keep their face.
4. **S4's measurements** (EldenNet's roadmap S11.2.c) come from the harness's "menu" scenario until the game: frame cost,
   atlas size and screenshots (with `GRACE_SHOTS` set). The owner's look review (S11.2.d) has the two screenshots.

## 22. grace-serve for EldenNet's launcher (2026-11-20, D161, G29 (a))

The server library is built (S11.2.i). What EldenNet's launcher does with it, off game:

1. It links `grace_serve` and makes its own `Server`, from ordinary code, never DllMain; `start` and `stop` are plain
   calls, and `stop` joins both of the server's threads.
2. It hands EldenNet's space over with `add_space(describe_space(pages, settings, version, increment))`, from EldenNet's
   page data and settings description (GraceUI's format, D160), and serves the web files of GraceUI's folder
   (`graceui.toml`, checked, D165) or files it hands in from memory.
3. On its `SpacePort` it answers each command exactly once (ok or an error); on `watched` it publishes the state, or a
   stream from its oldest row, since the server keeps nothing for a topic nobody watches.
4. It uses only the routes `/api/ticket` and `/api/ws`, and loopback only (§7.2 item 26).
5. The page paces bursts: every message counts against 20 a second before it is parsed, subscriptions included.

## 23. The ABI's protocol slots (2026-11-20, D169, agreed with EldenNet's session)

GraceUI 0.2's table provides `register_pages`, `publish` and `answer`, and `poll` gives the space's events
(`GRACEUI_CAP_PAGES`). What EldenNet does with them, on T6:

1. **Register once**, after `connect`: `register_pages` with `GRACEUI_VIEW_GAME | GRACEUI_VIEW_BROWSER`, its version,
   its increment, and its page data and settings description as JSON (GraceUI's format, D160), its `space` "eldennet".
   GraceUI's server starts for it (the browser view, G4), on `dashboard.port`, only while `dashboard.enabled` is on and
   never in the notice-only mode. With `graceui_sys.hpp`: `api.register_pages(consumer, views, version, pages,
   settings, increment)`.
2. **Poll with a buffer**: `api.poll(consumer, event, buffer, GRACEUI_FRAME_FROM_PAGE_MAX)`. Hotkeys come first, then
   `GRACEUI_EVENT_WATCHED` / `_UNWATCHED` (a sub or unsub frame: start or stop building that topic) and
   `GRACEUI_EVENT_COMMAND` (a cmd frame and `event.reply`). The old `poll(consumer, event)` still gives hotkeys alone.
3. **Publish** state and rows frames of its own declared topics (at most 256 KiB each; EldenNet's kinds go as pages of
   rows); a topic nobody watches is dropped.
4. **Answer** every command exactly once with `api.answer(consumer, event.reply, result_frame)`: ok, or an error with
   `body.detail` in words. A command left 10 s is answered `internal` by GraceUI with a log line naming the space: slow
   work (self-test, finding servers, preparing lines) answers at once and publishes its progress.
5. **The vendored headers** (graceui.h, graceui_sys.hpp, graceui_test.h) move to the commit that brings these; the
   event's struct grew at its end, so code built against 0.1's headers keeps working, with hotkeys alone.
6. **In game**, EldenNet's pages appear in GraceUI's menu once the menu shows a mod's space (D169's "not yet"), through
   the same events: nothing more to do on EldenNet's side.

## 24. The browser view's web shell (2026-11-20, D166)

1. **The status lamp** shows only when a space publishes a state topic `status`, whose body is
   `{"word": …, "tone": …, "reason": …, "page": …}` (the word in the top bar, its tone, the reason on hover and focus,
   the page a click opens). EldenNet's L1 to L4 words and its Paused and Dormant (§4.2.3) map onto it.
2. **Its pages and settings** reach the browser as registered (D169): GraceUI's server answers each space's `pages`
   topic itself; EldenNet publishes its `settings` topic (a settings body: description, values, changed, inherited)
   and answers `settings.set` and `settings.reset` at its one entry point.
3. **Destructive commands** will carry `confirm: true` from both views (§7.3), and GraceUI will refuse one without it
   `confirm_required`: EldenNet's destructive commands need not check it themselves, but may.

## 25. From part 12's second run (2026-11-20, D170)

1. **installed on the by-name path**: graceui-sys's find sets `installed` to 1 when it finds a loaded graceui.dll by
   name (D164's rule; the branch returned before it). EldenNet's work-around (installed, or a find by name) can go once
   it vendors graceui-sys from the commit that brings this.
2. **No Direct3D DLL at load**: graceui.dll no longer imports dxgi.dll, d3d12.dll or d3dcompiler_47.dll; it binds the
   three functions it needs on its init thread, after the game's window, from System32 alone. A mod (or EldenNet's
   launcher with --also) that loads graceui.dll early no longer meets Wine's loader hang through GraceUI. Nothing for
   EldenNet to change.

## 26. grace-dev, for EldenNet's pages (2026-11-20, D168)

EldenNet's pages can be previewed without the game: `grace-dev --space-dir DIR --web web/dist`, with DIR holding the
pages.json and settings.json EldenNet's page build writes (and, if it likes, a topics.json of fixture states and a
scenario.json of timed steps, invented content only). grace-dev checks them as `register_pages` does, and
`grace-dev --dump eldennet <page> --size 1920x1080` prints a page's in-game structure.


## 27. The menu shows mods' spaces (2026-11-20, D171)

1. **EldenNet's pages in game, through GraceUI's menu**: a space registered with `GRACEUI_VIEW_GAME` is shown by F5's
   menu, first on the session's first open, with GraceUI's own categories at its rail's foot. EldenNet's space needs
   nothing new for it: the same `register_pages`, `poll`, `publish` and `answer` as for the browser.
2. **The menu is a view of the `settings` topic**: while it shows EldenNet's space it watches `settings`, so EldenNet
   hears WATCHED when the menu or a browser starts watching and UNWATCHED when neither does (the menu's close included).
   Its state's `values` (and `inherited`, for tri-states that inherit) set the rows; publish every value, or the menu
   shows a value missing from it as its default.
3. **Commands from the menu** come as any view's do, with a reply handle and the same 10 s deadline; a refusal's words
   (the result's own `detail`, protocol 1.1, §28) are shown under the row, and the row shows the last published value
   again.
4. **Component blocks** (status tiles, feeds) read "Shown in the browser view." in game (G16); only their settings are
   drawn there in 0.2.

## 28. Protocol 1.1 (2026-11-20, D172)

1. **A refusal's words in `result.detail`**: optional, at most 256 bytes, a sentence a player reads ("Not now: the
   ritual is under way."), shown under the row in both views in place of the error code's words. Put EldenNet's in
   `detail`, not in `body`; nothing personal, no value, no path (PV rules, §6.7).
2. **Clearing a stream**: publish an empty `rows` batch whose `from_seq` is past the next row (skip one number), and
   every view clears that stream at once; the next rows follow on from that `from_seq`. A batch past a gap with rows in
   it starts the stream over there, as before.
3. **Nothing breaks**: 1.0 frames are still taken (GraceUI checks the major only); `hello` may say "1.1".

## 29. D166's follow-ups and GraceUI 0.2.0 (2026-11-20, D173, D174)

1. **Destructive commands arrive confirmed**: a command one of EldenNet's destructive actions sends (tone
   `destructive`) reaches EldenNet's space only with `"confirm": true` in its args; GraceUI refuses one without it
   `confirm_required`, from the browser and the in-game menu alike. EldenNet may rely on it, and keeps its own checks.
2. **Pages by increment**: a page whose `increment` is later than the increment EldenNet registers is absent from both
   views (rail, Page Up and Page Down, search, related links, shortcuts). Register the increment EldenNet's release
   is, and give each page the increment it ships in; a page with none (a later-version statement page) always shows.
3. **Words**: a secret reads Set or Missing; availability badges read "Waits on a game check", "Takes effect in
   v1.5", "Off because … is off" (for `requires:<key>` while that key is off) and "No effect in this version" (inert).
4. **GraceUI 0.2.0**: the `pages` capability and protocol 1.1 come with it; a floor of 0.2.0 asks for both.

## 30. GraceUI's side of the in-game spikes S0-E and S4 (2026-11-20, D175)

What we agreed, from GraceUI's side; the launcher and the DLL's side are EldenNet's (its message of 2026-11-20).

1. **The package**: from GraceUI's commit, graceui.dll's test build (its GraceUI folder with `graceui.toml`, which
   E12's pre-flight takes as it is), `grace-spike.dll` and `grace-standin.dll`, built with clang-cl;
   `tools/grace-spike/README.md` is the owner's part of the steps (runs A1 to A4, A0 and B), for EldenNet's README.
2. **The launcher**: `--also` with `grace-spike.dll` (and `grace-standin.dll` in the S0-E runs), before or without
   `graceui.dll` as the run says; `ELDENNET_SPIKE_DIR` naming `%APPDATA%\EldenNet\spikes\<word>-<run number>`, with
   the words `s0e`, `s0e_alone` and `s4`; unset outside a spike run, where grace-spike does nothing.
3. **The DLL**: no call into the game in these runs (EldenNet's "spike: no call"); the connect, the banner and F11's
   view as usual.
4. **The harness cases to run against the package on the fake game**: "spike" and "spike-after" (the observer and the
   stand-in before and after GraceUI, the typing test), "menu", "menu-space", "input" and "abi".
5. **The reader**: `python3 -I tools/grace-spike/report.py <spikes folder> --by-hand movement=yes|no`; `--json` for
   EldenNet's spike-report to fold in.

## 31. A holder's events, for the chat box (2026-11-20, D176; proposal 0003)

What EldenNet's chat box uses once GraceUI 0.2.1 is out (ABI 1.1):

1. **Connect** with floor 0.2.1 and check `has_cap("holder-events")`; without it the box cannot take typing.
2. **Ask with the flag**: `request_input` with `flags = GRACEUI_INPUT_EVENTS` (a field appended to
   `GraceUiInputRequest`, which is 32 bytes in ABI 1.1), for the box's own panel (made with
   `GRACEUI_PANEL_TAKES_INPUT`, shown), wanting the keyboard, the mouse and the pad.
3. **Poll** with ABI 1.1's event size (104 bytes; graceui-sys's `Api::poll` asks with it where GraceUI has the
   capability, with or without a buffer): `KEY`, `CHARACTER`, `POINTER` (in the panel's pixels; `button_event` 1 for a
   button, else a move or the wheel), `PAD` (B as cancel, A as send) and, once and last, `INPUT_ENDED` with its reason
   (released after the box's own `release_input`; danger, which keeps the draft; no render; the player's exit). Feed
   them to the box's headless Dear ImGui with `graceui::imgui::feed(io, event)` before its `ImGui::NewFrame()`; it
   sets `ImGuiBackendFlags_HasGamepad` at the first pad event, and the box sets `ImGuiConfigFlags_NavEnableGamepad`
   if it wants pad navigation.
4. **The caret**: `set_caret` with the caret's rectangle in the panel's pixels, whenever it moves, for the IME, while
   the box holds input: one set while it does not is ignored, and the caret ends with the holder.
5. **Yours, not GraceUI's**: opening the Steam floating keyboard, and the clipboard if Ctrl+V is to paste.
6. **Later, by D-entry**: the pad chord that opens the box (View + D-pad Up), as a hotkey with its masking.

## 32. The menu's default size is 1.25 (2026-11-20, D177)

From the owner's look review: `ui.scale` (Menu size) now defaults to 1.25 instead of 1.0, its bounds (0.75 to 1.5)
unchanged. The effective scale that GraceUI's display-changed event carries is `(H / 1080) × ui.scale`, so at the
default EldenNet's text that follows it (subtitles, the chat box, the call-out box: NPC §10.7) is a quarter larger than
its sizes at 1.0. Check those sizes against the new default; if any was tuned to look right at 1.0, divide it by 1.25
rather than ignore the scale. EldenNet's pages in the browser are a quarter larger by default too (one setting for both
views, §3.13). The Steam Deck's first-run 1.35 (G6) is unchanged.

## 33. The display, and GraceUI's settings kept (2026-11-20, D179, D180; proposal 0004)

In GraceUI 0.2.2 (ABI 1.2):

1. **Connect** with floor 0.2.2 and check `has_cap("display")`. The answer now says `ui_scale` (the player's, kept
   from the last session) and `ui_scale_default`, and the display's size once the game's swap chain is bound (usually
   0 at your connect).
2. **Faces**: register the subtitles', the chat box's and the call-out box's faces with `GRACEUI_FACE_SCALED` and
   their design size at 1080 lines and the default Menu size. GraceUI bakes each at warm-up at that size times the
   content scale, `(H / 1080) × ui.scale / its default`, rounded to a whole pixel: your O93 rule, with no constant.
   The glyph table gives the baked size.
3. **Poll** with ABI 1.2's event size: `GRACEUI_EVENT_DISPLAY` comes once after warm-up and after each change of the
   size or `ui.scale`, with `content_scale` (your NPC §10.7 scale) and `faces_scale` (what your scaled faces were baked
   at). Lay your text out at `content_scale`; after a change, draw your scaled faces' glyphs scaled by
   `content_scale / faces_scale` until the next session (the atlas is baked once, invariant 11). graceui-sys's
   `Api::poll` asks at the full size already.
4. **Settings**: Menu size, the theme and GraceUI's other settings are kept in `Game\GraceUI\user\settings.json`
   between sessions; nothing for EldenNet to do, but a release must not carry a `user\` folder (the manifest skips it).

## 34. A mod's notices may be quiet (2026-11-20, G32, D181)

In GraceUI 0.2.3 (no change of the ABI):

1. **The player may quiet EldenNet's notices** on GraceUI's Installed mods page, as any mod's. A quiet mod's notice is
   still answered `ok` (EldenNet learns nothing of the switch, by design) and is kept in a list on that page, not drawn
   over the game; a withdrawal goes on as usual. So never rely on a notice being seen: what the player must know (the
   banner of EldenNet's F11 view, a failed start) should also be in EldenNet's own pages or its log. Nothing to change
   in code.
2. **GraceUI's own notices always show** (the player's exit, why the menu cannot open, a too-old mod).
3. **The switch is kept** between sessions in `Game\GraceUI\user\settings.json` (`"quiet_mods"`, mods' ids only); a
   release must still not carry a `user\` folder.

## 35. The menu by mouse (2026-11-20, D182)

In GraceUI 0.2.4 (no change of the ABI): the in-game menu takes the mouse on every space's pages, EldenNet's included:
a click does what Enter does, a slider follows a held button, the wheel scrolls. Nothing for EldenNet to do; its
pages' settings and actions arrive as the same commands as from the keys.

## 36. The security review's fixes (2026-11-20, D183)

In GraceUI 0.2.5 (no change of the ABI's shape; `register_pages` and `publish` refuse more):

1. **A destructive action needs its `confirm` text**: `register_pages` refuses page data with an action of tone
   `destructive` and no `confirm` (its words are what the second press shows). Check EldenNet's pages before your slice
   registers them.
2. **The space id `graceui` is reserved**; and a space may not declare a topic named `pages` (the server's own).
3. **The `settings` topic is held to the registration**: if you publish `schema` or `description`, they must be the
   ones you registered (compared decoded), or the frame is refused; a secret's value is replaced by `"set"` or `""`,
   with one log line. Publish secrets' status only, as GraceUI's own store does.
4. **Your anti-cheat flag now works**: `set_world_state` with `GRACEUI_WORLD_ANTI_CHEAT` closes the browser view's
   server for the rest of the session, whatever any space asks; GraceUI also re-checks the process's modules every
   60 s while the server runs. The menu stays.
5. **Not yet: which view sent a command** (§7.3). Both views are this PC's until pairing; telling a space the view is
   an addition to ABI 1 (a field in the command's event), to agree with you before the home network comes, since your
   refusal of `secrets.set` and `providers.update` from paired devices (§7.2 item 16) needs it.

## 37. Your pages' components in game (2026-11-20, D184)

In GraceUI 0.2.6 (no change of the ABI or the page data), your first request:

1. **Drawn in game**: a key-value list, a check list, stat tiles and a computed sentence (besides D181's tables and a
   statement), from your topic's body as the browser view reads it. The live feed and the log tail stay the browser
   view's for now.
2. **Watched while the page is open**: the menu sends your space a sub for each such topic of the open page (beside
   `settings`), and an unsub when the page, the space or the menu closes; answer it as you do the browser's (publish
   the state on the sub, and on each change). A topic the browser already watches reaches the menu at once.
3. **Until you publish**, the block says "Nothing to show yet." in both views (it used to say "Shown in the browser
   view." in game). Your `grace-dev --dump` goldens change: the components now show your fixtures.
4. Your other five requests (a field prop for one topic feeding several blocks, an empty-state text, newest-first log
   tails, a greyed page state, shortcut rows drawn as their control) change the page data or add a framework feature:
   they go to the owner and you as one write-up.

## 38. Proposal 0006, built (2026-11-20, G33, D185 to D189)

In GraceUI 0.2.7 (no change of the ABI; additions to the page data, checked by `tools/pages/build.py`):

1. **`props.field`** on a state component: the block draws that member of its topic's body (`system.status` can feed
   Status and Compatibility at once). A name of 1 to 32 of `a-z`, `0-9`, `_`.
2. **`props.empty`**: your words (1 to 80 characters) for a block whose topic has published nothing for it, such as
   "No problems". Before the topic's first state the block still says "Nothing to show yet."
3. **`props.newest_first`** on a log tail, for Logs.
4. **`space.state`**: declare it (a state topic, drawn by no component; it needs a fixture like any topic) and
   publish `{"greyed": {"<page id>": "<words>"}, "inactive": "<words>"}`. A page in `greyed` shows its words over it
   and its controls go inert in both views; `inactive` greys every other page; a page with neither is usable. Each text
   1 to 200 bytes. At L1, list the pages PAGES §4.4 greys, each with "EldenNet is inactive: reason"; at the title
   screen, Playthrough, History and Event monitor with "Load a character to see this". Commands still reach you: keep
   refusing what you cannot do.
5. **Shortcut rows** show their home's control in both views; ← and → change it, Enter follows its link.

Your `grace-dev --dump` goldens change where a page has a shortcut row (its control is drawn now).

## 39. §6.6's commands as you built them (2026-11-21, D190; proposal 0007)

1. **Recorded**: EldenNet sets and clears keys with `settings.set` and `settings.reset` on its secret settings, and adds
   and removes providers with `providers.add` and `providers.remove`; it uses no `secrets.set`, `secrets.clear` or
   `providers.update`. DESIGN §6.6 keeps its rows with a dated note (D190). When pairing comes, these are the commands
   to refuse from a paired device.
2. **Your AI pages' four requests** (a row action, a picker from a topic, a secret a row, a small form) are proposal
   0007 (G34), for the owner and you.

## 40. Proposals 0005 and 0007 approved (2026-11-21, G34, G35)

1. **G34 (proposal 0007)**, all four, with the picker's choices supplied by you through `choices_from` and checked at
   your one entry point. GraceUI builds them as 0.2.8 and tells you its version and ABI (unchanged, 1.2, as the
   proposal expects); until then your stand-ins stay. Then: a Remove on each provider row (`providers.remove` with
   `{id}` and `"confirm": true`), a model picker per job from a state topic you publish, a key field on each provider
   row (status only in the cell, the value never echoed), and the add-your-own form (`providers.add` with `{name,
   base_url}`).
2. **G35 (proposal 0005)**, as proposed: GraceUI resolves your chord, D-pad Up then View, at ABI 1.3 behind
   `has_cap("pad-chords")`. It is built for S4's second round, after your R1; until then `register_hotkey` answers
   `not_yet` for a chord, as today.

## 41. G34 built: GraceUI 0.2.8, protocol 1.2 (2026-11-21, D192 to D196)

ABI 1.2, unchanged. What your AI pages can now use, all optional, each checked at registration (a refusal names it):

1. **A button on each row** (D192): on the table's component block in your page data, not in the topic's body as
   proposal 0007 sketched (so that registration and the server know them): `"row_actions": [{"id": "remove", "label":
   "Remove", "command": "providers.remove", "key": "id", "tone": "destructive", "confirm": "<the words its
   confirmation shows>"}]`, 1 to 3 of them. A press sends `{"id": <the row's id>}`, with `"confirm": true` for a
   destructive one; the server refuses a destructive row command without it. The command must be in your `commands`.
2. **A picker** (D193): on a `text` setting, `"choices_from": "<a state topic you declare>"`, whose body is
   `{"choices": [{"value": "...", "label": "..."}]}` (values 1 to 200 bytes). Both views watch it while the page shows
   the setting; check at your entry point that a value is one of your choices. No `choices` beside it.
3. **A key on each row** (D194): on the table's block, `"row_secret": {"column": "key", "command": "providers.key",
   "key": "id"}`. The column's cells are its status, `"set"` or `""`, never the value. Saving sends `{"id": ...,
   "value": "..."}` (empty clears it). The server drops any body from that command's result: answer ok, or refuse with
   words that do not echo the value.
4. **A small form** (D195): a block `{"kind": "form", "command": "providers.add", "fields": [{"id": "name", "label":
   "Name", "max": 32}, {"id": "base_url", "label": "Base URL", "max": 200}], "submit": "Add"}`, 1 to 4 fields. It
   sends every field as text; the browser clears them when you answer ok and keeps them under your words when you
   refuse.

In game: row buttons are focus stops after a row's switch (a destructive one asks for a second press); a row key's
cell reads "Key: Set" or "Missing" with one line saying it is set in the browser view; a form reads "Filled in the
browser view:" and its labels; a picker steps with ← and →. Your page data that uses them should say `"minor": 2`.
When pairing comes, the commands of 1, 3 and 4 are the candidates to refuse from a paired device (proposal 0007 §5).

## 42. Your three display gaps (2026-11-21, D197, D198; GraceUI 0.2.9)

No change of the protocol (1.2) or the ABI (1.2).

1. and 2. **A row secret's cell** may now also say `"refused"` (shown "Refused", in red, the field kept to type a new
   one) or `"none"` ("Not needed", no field: a local server), beside `"set"` and `""` (D197). Still the status only.
3. **The changed mark** follows your settings body's `changed` list in both views (D198): list a picker's key there only
   when the player picked a model that differs from the preset's. A default that formats to nothing is no longer
   printed ("Default: ." is gone); say "Default: the preset's model." in the setting's help if you want it said.

## 43. A picker's empty default (2026-11-21, D199; GraceUI 0.2.10)

Your per-job pickers default to `""` (the preset's model). Both views drew that blank; they now show it as "Default"
and offer it first, so choosing it sends `""` back to you. Say in the setting's help what Default means (D198). Nothing
to change on your side.

## 44. A row secret's line (2026-11-21, D200; GraceUI 0.2.11)

In game, the line under a table with a row secret ("Key: set in the browser view.") is now left out when every row's
cell is `"none"`. Nothing to change on your side.

## 45. Sections as cards (2026-11-21, G36, D202; GraceUI 0.2.12, protocol 1.3)

ABI 1.2, unchanged. For AI › Models' per-job cards, a section in your page data may now carry, all optional:

- `"chip": "dialogue"`: 1 to 24 characters after the title, faint.
- `"status": {"topic": "health", "field": "dialogue"}`: a state topic you declare (not `settings` or `space.state`),
  its body or the member `field` names holding `{"word": "Ready", "tone": "moss"}` (moss, ember, glint or rot; any
  other tone is drawn glint). Nothing is drawn until you publish it.
- `"actions": [{"id": "prebake", "label": "Prepare lines now", "command": "pools.prebake", "tone": "secondary"}]`: 1 or
  2, a header action's shape and rules (a declared command; a destructive one with `confirm` words, sent with
  `"confirm": true`). Refuse it in words at your entry point when it cannot run now, as you planned.

Page data that uses them should say `"minor": 3`. The browser draws the actions and the status at the head's right; in
game they sit in the head, the actions as focus stops after the section's head. When pairing comes, `pools.prebake`
joins the commands to refuse from a paired device (proposal 0008 §7).

## 46. The browser view paces its subs (2026-11-21, D203; GraceUI 0.2.13)

The web shell now sends subs, unsubs and commands through one queue at 16 a second (the server still allows 20 and
drops the rest), so AI › Models' 14 topics arrive in about a second after connecting and none is lost. No change of
the protocol or the ABI, nothing to change on your side; your preview's own browser may keep its 16 a second.

## 47. Pad chords (2026-11-22, G35, D204, D205; GraceUI 0.2.14, ABI 1.3)

Proposal 0005 is built, as decided. For the chat box's gesture (your NPC §5.6, O11):

- Check `has_cap("pad-chords")`, then register the talk hotkey with
  `pad_chord = GRACEUI_PAD_CHORD(XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_BACK)` (the anchor in the high 16 bits, the key
  in the low). The hotkey may keep its key too: the key and the chord are bound each on its own and either fires it;
  `GRACEUI_UNBOUND` means a default you named is taken (the log line says which).
- A mod connected with an ABI minor below 3 still gets `GRACEUI_NOT_YET` for a chord, as before, so re-vendor
  `graceui.h` at 0.2.14 (`GRACEUI_ABI1_MINOR` 3) and raise your GraceUI floor to 0.2.14 to use it.
- On the chord: one `GRACEUI_EVENT_HOTKEY`, as for a key; you ask for input on it with D176's events as on your talk
  key. View never reaches the game when the chord completes, and both buttons stay hidden until each is released;
  a chord broken before 0.4 s gives the game View as one tap. Nothing fires, and View reaches the game at once,
  whenever `request_input` would answer `not_now` (your `safe_context` failing among them), and while any holder has
  input.
- The owner tests with keyboard and mouse only (2026-11-22: no pad, VR set or handheld). GraceUI checks chords with a
  simulated pad, in unit tests and the harness on both toolchains (D205). What only a real pad shows (the 150 ms, 0.4 s
  and tap estimates, that D-pad Up then View is free in the game, that the late View opens the map, Steam's floating
  keyboard on a Deck) is for testers' machines in S4's second round and your matrix, not for the owner.

## 48. The Hotkeys page lists every hotkey; rebinding asked (2026-11-22, D206, G37; GraceUI 0.2.15)

- GraceUI › Hotkeys ships in 0.2 now, its table live: the menu's key, then every mod's hotkeys with its mod, the name
  you register it with, its key, its pad chord and its state (who took a default first). Nothing to change on your
  side: the name in `GraceUiHotkeyDesc.name` is what it shows, so give each hotkey a player's name ("Talk").
- `hotkeys.menu` is shown locked ("Later version") until a rebinding is decided: GraceUI 0.2 reads F5 alone.
- [Proposal 0009](proposals/0009-rebinding-hotkeys.md) (G37) asks where a hotkey is rebound. Its recommendation moves
  every binding to GraceUI › Hotkeys, which would retire `hotkeys.talk`, `hotkeys.skip_page` and `hotkeys.debug` as
  EldenNet settings (they become the defaults you register) and turn your Settings › Hotkeys into a link or a
  read-only list; its §8 item 5 asks your view. With its §5 (a), an ABI 1.4 `describe_hotkey` gives your call-out
  the key as the player bound it.
- A fix you rely on (D207): when your box of a holder's events was full, the holder's end could, rarely, be lost, so
  your chat box would not hear that it lost input. Since 0.2.15 the end always comes. Nothing to change on your side.

## 49. Rebinding hotkeys (2026-11-22, G37, D208 to D210; GraceUI 0.2.16, protocol 1.4, ABI 1.4)

The owner approved proposal 0009 as recommended, and it is built:

- **Every hotkey is changed on GraceUI › Hotkeys**, yours among them: a key, a pad chord, a reset, a key another has
  asked about and moved. The player's choices are kept in GraceUI's settings file under your mod's id and the action's
  id, and applied whenever you register the hotkey, so keep your action ids stable across releases.
- **Your key settings** (`hotkeys.talk`, `hotkeys.skip_page`, `hotkeys.debug`) would become the defaults you register
  (T, your skip key, F11), and your Settings › Hotkeys a link to GraceUI › Hotkeys, or a read-only list of yours. That is
  proposal 0009's §8 item 5, still yours to answer; GraceUI changes nothing of yours until you do.
- **Your words for a key** ("Press T to talk"): read it with `describe_hotkey` (ABI 1.4, `has_cap("hotkey-bindings")`),
  `key_text` and `chord_text` ready to show, and read it again on `GRACEUI_EVENT_HOTKEY_CHANGED`, which GraceUI sends only
  to a mod connected with `GRACEUI_ABI1_MINOR` 4 or more. Re-vendor `graceui.h` at 0.2.16 and raise your floor to use
  it; nothing changes for you until then.
- **The menu's key** (`hotkeys.menu`) is live: a player may move it from F5. A hotkey of yours on the new key loses it.
- **Protocol 1.4** adds `capture` (`key` or `chord`) to a row action, for any space's tables: the view takes it first
  and sends it as `value`; answer `confirm_required` with words to ask before a change, and the view sends it again with
  `confirm`. A row whose key cell is empty takes no row actions.

## 50. A wait handle for your events (2026-11-22, D212; GraceUI 0.2.17, ABI 1.5)

As answered on 2026-11-22, for your chat box's opening within 50 ms (your NPC §5.1):

- `wake_event(consumer, &event)` (`has_cap("wake")`) gives you a Win32 handle of your own to an auto-reset event that
  GraceUI sets whenever something waits for your poll: a hotkey's press or change, a holder's event, a view's watch,
  unwatch or command, the display event; and once when you ask, so your first wait polls what may already be there.
- T6: wait on it (with your 100 ms as the fallback), poll until `GRACEUI_EVENT_NONE`, then wait again. A wake with
  nothing to poll can happen; it costs one poll. Close the handle when you are done with it; GraceUI keeps its event.
- Under Wine in the harness, the waiting thread woke 1 to 5 ms after a hotkey's key was posted. Re-vendor `graceui.h`
  at 0.2.17 (`GRACEUI_ABI1_MINOR` 5) and raise your floor to use it; nothing changes for you until then.


## 51. The profile editor's needs brought up to date (2026-11-22, D213, G38; your S7.2.a)

- **D2's exception is recorded** (D213): the profile editor is the one content editor in v1, by your O43. Content packs
  stays read-only. The editor's pages, fields, checks, commands and draft helper are yours (S7.2.b; your NPC §4.7), and so
  are the rules the needs list asked GraceUI to change: SP1's `subjects` and the player's own text, SP4's people met and
  PV4's draft text (PAGES §4.8, §4.9).
- **The needs list** (GraceUI's `docs/profile-editor-needs.md`) is brought up to date with G20, O46 and O86 in proposal
  0010 §2. It stays as your input for S7.2.b.
- **What GraceUI would add**, put to the owner as G38 with proposal 0010 (protocol 1.5; no change of the ABI):
  - Topic families: your `profiles/<target>`, and also `named/<id>`, `journal/<id>` and `conversation/<id>`.
  - Master–detail in both views.
  - An editor block: eight field kinds, with your limits read from the member's body; check, save with `rev` and a new
    `conflict` code, reset and restore; a draft shown beside the fields, unsaved.
  - Slow commands, answered within 90 s, for `profile.draft`.
  - GraceUI's own text field in game.
- **Its §6 asks you four things:** whether the field kinds cover your NPC §4.5 and O46 fields, the seeds per key
  included; `doc` as `{field id: value}`, with you writing the TOML; check messages as `{field, item, level, words}` in
  the answer's body; and the draft's answer as `{draft, notice}`.

## 52. GraceUI's own text field in game (2026-11-22, D214, G38 (1) (a); GraceUI 0.2.18)

The first part of proposal 0010. No change of the protocol (1.4) or of the ABI (1.5): your pages work in game as they
are.

- **Typed in game now:** your `text` settings with no `choices_from`, your `secret` settings (masked and write-only:
  the field starts empty, a value is sent once, and the menu keeps only whether it is set), your forms (D195) and your
  tables' row secrets (D194). Your page data needs nothing new for any of them. The "Filled in the browser view" and
  "set in the browser view" lines went.
- **Search** in game opens with `/` or Ctrl+F. It finds the open space's settings, and Enter opens the row on its tab,
  its section and Advanced disclosure opened.
- **A shortcut row's link fixed:** on a page with tabs it now opens the row's own tab. Before, it opened the first tab.
- **Your forms' commands** are answered as before. On `ok`, the menu empties the form. On a refusal, it keeps the text
  and shows your words under the form.
- **Not yet:** Steam's floating keyboard for a pad (testers, D205), and the IME's composition drawn inside the field.

## 53. Topic families and master–detail (2026-11-22, D215, G38 (1) (a); GraceUI 0.2.19, protocol 1.5)

The second part of proposal 0010. No change of the ABI.

- **A family** is a state topic you declare with `family = true` in your page data. Its members, `<family>/<key>`, are
  topics of their own: you hear each watched and unwatched through `GRACEUI_EVENT_WATCHED` and `_UNWATCHED`, with its
  own id, as for any topic. You publish a member while it is watched; a state for an unwatched member is taken and
  dropped. Publish a member, never the family itself, which is refused. A key is a-z, 0-9, '.', '_' and '-', and the
  member's whole id is at most 64 bytes. Your `named/<id>`, `journal/<id>` and `conversation/<id>` fit as families
  `named`, `journal` and `conversation`, and `profiles/<target>` for the editor.
- **A page's `master`**: `{topic, key, detail, label, empty?, none?}`. The `topic` is a state topic whose body is a
  table's (`columns`, `rows`), `key` is the column holding each row's key, and `detail` is the family. The list is
  drawn at the left, its rows by their first column's words and their second's. Your page's own tabs and sections are
  the detail, and a component block whose `topic` is the family draws the chosen row's member. This is the shape for
  Named NPCs (the roster, then Profile, Allowances and Reactions), NPC types and Playthrough.
- **The first row is chosen** as soon as the list has one, and a page keeps its chosen row for the session. A row's
  key never goes into an address, browser storage or a log. A row whose key is not a member's is not listed.
- **Where proposal 0010 differs:** its §3.2 had master–detail as a block. It is the page's, so that your tabs are the
  detail.

## 54. Slow commands (2026-11-22, D216, G38 (1) (a); GraceUI 0.2.20, protocol 1.6)

The third part of proposal 0010. No change of the ABI but a constant in `graceui.h`.

- **Name `profile.draft` in your page data's `slow_commands`** (at most 8 of your own commands; never `settings.set` or
  `settings.reset`). GraceUI then waits up to `GRACEUI_SLOW_ANSWER_DEADLINE_MS` (90 s) for your answer, not 10 s, from
  the browser and from the menu alike. Past that it answers `internal`, as for any command.
- **While it waits,** in game the control that sent it reads "Working…" and takes no press; in the browser a form's
  button and a row's actions wait as before. The editor's draft button (part d) will show its own words.
- **Your other commands keep their 10 s.** A slow one still counts in a view's 20 a second and your queue of 32.

## 55. The editor block (2026-11-22, D217, G38 (1) (a); GraceUI 0.2.21, protocol 1.7)

The fourth and last part of proposal 0010, with your five additions (its §7). No change of the ABI.

- **Your Profile tab is an editor block**: `{kind = "editor", topic = "profiles", editor = {save = "profile.save",
  check = "profile.check", reset = "profile.reset", restore = "profile.restore", draft = "profile.draft",
  notes_label = ..., notes_max = 400}}`, save alone required, every command one of your own, and `profile.draft` in your
  `slow_commands` (the lint refuses a draft that is not slow). On a master page a block bound to any family of yours
  draws that family's member for the chosen row's key, so `profiles` with `key = "id"` on Named NPCs' list draws
  `profiles/<id>`; if a profile's target is ever not the row's key, tell me and I'll add a column for it.
- **Publish the document on the member**: `{target, rev, state: {word, tone, line}, draft_blocked, draft_help,
  fields: [...]}`. A field is `{id, label, kind, help, value, reference, reference_label, max_words, max_chars,
  max_items, min, max, choices, choices_from, locked, nullable}`; dotted ids (`public.summary`) are fine. Kinds: `line`,
  `prose` (one paragraph), `list` (items of words, or `{text, choice, key, locked}`), `choice`, `pick` (choices from a
  topic of yours, as D193), `number`, `switch` and `shown` (never sent back). `null` is "not set here", which is not
  empty, where `nullable` is true. A field GraceUI can't read (no id or label, an unknown kind, a value of another kind,
  a second of one id) is left out, never guessed at: check your documents with the lint's fixture and both views.
- **What you receive**: check and save `{target, rev, doc}`, where `rev` is the revision the player's first unsaved edit
  was made on and `doc` is every field but the shown ones, locked fields and locked items as you published them and each
  item's `key` sent back; reset `{target, all: true, confirm: true}`, after the player confirms; restore `{target}`;
  draft `{target, notes}`. Check everything at your one entry point: GraceUI holds only the caps it shows (`max_chars`,
  `max_items`, `min`, `max`, the notes' `notes_max`) and counts words.
- **What you answer**: a body with `messages: [{field, item, level: "error" | "warning", words}]` (no `field` for the
  whole file), shown under the field or the item in your words, never the offending text; a stale save as the new
  error code `conflict` (the view offers Reload); a draft as `{draft: {field: value}, notice}`, each proposal shown beside
  its field with Use this and Keep mine (one for a shown or locked field, or of another kind, is dropped). After a save,
  reset or restore, publish the document again with its next revision.
- **Unsaved edits** stay in the view's memory for the session, by the document's topic, through the menu's close and
  across pages; nothing is written until Save. A draft lives while its page is open.

## 56. A save's chosen row (2026-11-22, D218, your WHY§387; GraceUI 0.2.22)

No change of the ABI or the protocol.

- **When a save turns a `named.<param>` row into `mine.<id>`,** put `chosen: "mine.<id>"` in the save's answer body
  (`{"ok": true, "body": {"chosen": "mine.<id>"}}`). The view that saved chooses that row as soon as your list has it,
  whether your list's new state comes before the answer or after it; if the player chooses a row by hand in between, their
  choice stands.
- **The key** is a member's key of the page's master family, as D215's: `a-z`, `0-9`, `.`, `_` and `-`, the member's
  whole id at most 64 bytes. Anything else is ignored, as is `chosen` on any answer but a save's that is ok.
- **Focus** stays on Save, in game and in the browser, once the new row's profile is drawn; publish it when its member is
  watched, as for any row.

## 57. No second Esc after the box closes (2026-11-23, G39 (c))

The owner declined proposal 0011. Nothing changes in GraceUI, and nothing is asked of you but this:

- **NPC §5.7's second Esc goes.** Esc and the pad act on the chat box only while it is open: Enter sends the line, Esc
  (or the pad's B, as NPC §5.6 has it) cancels and closes the box. Once the box has closed, or the reply is out, Esc and
  the pad are the game's again at once. A reply still pacing after its box has closed finishes; no press stops it.
- **S11.4.d needs nothing of GraceUI's**: it can be marked dropped (G39), and WHY§437's third point answered by it.
- **The pad while the box is open** may work as the keys do (the owner: "You can implement gamepad buttons to work as
  such too, but we only care about while the chatbox is open"). Your box already takes the pad as a holder (D176's PAD
  events), so this is yours to map.

## 58. Search across spaces in game (2026-11-23, D219; GraceUI 0.2.23)

Nothing to change on your side. The menu's search in game now finds every space's settings, as the browser view's
already did: from EldenNet's pages a player finds GraceUI's Menu size, and from GraceUI's pages your settings, each named
by its space ("EldenNet › Pacing") and opened on its home page with its row focused. Your settings are found by their
labels, synonyms and keys, on the pages present in your increment, as before; give them the synonyms players would type.

## 59. The init's step, readable from outside (2026-11-23, D220; GraceUI 0.2.24)

From your package case A2's stall (GraceUI injected late, `install_runs` 0 for 240 s under load). Nothing to change; two
things you can use:

- **`GraceUI_Test_InitStep()`** in the test build: 0 before the init thread starts; 1 its fonts, 2 waiting for the
  game's window, 3 binding Direct3D, 4 reading the hook targets from a swap chain of GraceUI's own, 5 the input targets,
  6 the install, 7 hooked. Read it from any thread. `GraceUiTestState` does not grow, so your vendored `graceui_test.h`
  needs only the new declaration, if you want to call it, for example in a harness case that times out.
- **The spike observer** writes it in each state line as `init_step`, and `report.py` names the step in the hooks row
  when the host never hooked. An older test build leaves it out.

Also: `report.py`'s movement row now reads "once EldenNet's start is ready (S10.1.m)", as you asked, since R1 is retired
(O106).


## 60. Other devices at home: mark your sensitive commands (2026-11-23, G9, D221–D223; GraceUI 0.2.25, protocol 1.8)

G9 is built: with GraceUI › Browser view's Other devices switched on, a phone at home pairs once with a code shown in
game, then opens every space's pages as this PC's browser does. GraceUI refuses a paired device, with `not_now` and
"Only on the PC the game runs on.", every command its space marks sensitive. What EldenNet should do:

- **Mark them in your pages** (protocol 1.8, D81): `sensitive_commands = ["secrets.set", "secrets.clear",
  "providers.update"]` beside `commands` in your space's page data, and `sensitive = true` on any setting a device must
  never change (a provider's address, for example). Your secret settings and row-secret commands are sensitive already,
  without a mark. The page lint checks that each listed command is one of your space's and not a settings command.
- **Nothing reaches you** for a refused command: it is answered before your space sees it, before any confirmation is
  asked. So your one validating entry point (invariant 3) needs no device check of its own.
- **A paired device's view is otherwise this PC's**: the same topics, the same pages, the same commands. If a page of
  yours should not be read on a phone (PAGES' privacy rules), say so and we can add a mark for topics later; nothing
  in G9 asks for it.
- **Which view sent a command** (§7.3, D224): the `cmd` frame you get in `GRACEUI_EVENT_COMMAND` now carries `view`,
  `"game"`, `"browser"` (this PC) or `"device"` (paired at home), set by GraceUI from the channel and never taken from
  the view. Use it for rules of your own if you need any; the refusals above need nothing from you.
- **Your launcher's host** (G29 (a)) serves loopback only, as before: the home network stays in game at first (§7.2
  item 26). `ServerConfig.network_addresses` exists for tests only; leave it empty.

The test build adds `GraceUI_Test_GetPairingCode` and `GraceUI_Test_SetNetworkAddresses` (a test listens on 127.0.0.2,
never on a real address); your vendored `graceui_test.h` needs them only if your harness pairs a device.

## 61. Taking a file: `plugins.install` (2026-11-23, G28, D226–D229; GraceUI 0.2.26, protocol 1.9, ABI 1.6)

G28 is built: a space may take one file from the browser view on this PC, by `POST /upload` on the loopback listener
alone, with a one-use ticket bound to the session, the command and the file's size and SHA-256 (§7.2 item 18). GraceUI
checks the bytes on arrival and hands them to your space with the command; it keeps nothing afterwards. What EldenNet
should do for item 3 of the Plugins list above:

- **Declare the command** in your space's page data (D226): `file_commands = ["plugins.install"]`, and the same name in
  `sensitive_commands`. The lint refuses a file command that is not one of your commands, not sensitive, slow, or a
  settings command, and more than four of them.
- **Put the drop zone on the Plugins page** (D227): a section's block `{ kind = "upload", command = "plugins.install",
  submit = "Add file", accept = "…" }`, `accept` listing the extensions your plugins come in (such as ".zip"). The
  browser view draws a drop zone with that button on this PC; a paired device and the in-game menu show a line saying
  where to add the file instead. The file's name never leaves the page, so if you need a name, read it from inside the
  package.
- **Read the file through ABI 1.6** (D228): build against `graceui.h` with `GRACEUI_ABI1_MINOR` 6, and give `poll` a
  file buffer in the event's `file` and `file_capacity`, either `GRACEUI_UPLOAD_MAX` bytes (32 MiB) kept for it or one
  grown when `poll` answers `GRACEUI_FULL`: then `frame_length` and `file_length` say how much each buffer needs, the event is kept, and
  the next poll with buffers that large takes it whole. A command with a file arrives as `GRACEUI_EVENT_COMMAND` with
  `file_length` above zero; its args are those the page sent (`{}` from the upload block). A mod built before ABI 1.6
  may not register file commands, and if your event has no room for a file GraceUI answers the command `not_now` itself.
- **Answer as for any command**: your `result` (ok, or an error with its words in `detail`) is shown under the drop
  zone. Run your validator before anything installs (your O67): GraceUI checks only the size and the hash against the
  ticket, never what the file holds. Zero your buffer once you are done with it.
- **Your launcher's host** (G29 (a)) links `grace-serve`, which serves the same route on loopback: there a file
  command's `SpaceEvent` carries `file`, the bytes behind a shared pointer, zeroed when its last holder lets it go.
- **Nothing reaches you** from a paired device, or from a `cmd` that names `plugins.install` without a file: GraceUI
  refuses both before your space sees them. The menu never sends it either, so name it in no action, row action, form
  or editor (the lint refuses that, D229): the upload block alone sends it.
- **Take the file promptly** (D229): one file waits for your space at a time, so a second gets `busy` ("Another file is
  waiting for this mod") until you have polled the first; and an event left on `GRACEUI_FULL` past its answer's
  deadline is dropped with its file, the view told it failed, so poll again with room at once.

## 62. The spike package: the first frames' step and GraceUI's log (2026-11-23, D230; GraceUI 0.2.27, test build)

For A1's next run, after its two crashes between GraceUI binding the swap chain and its first frame:

- **Repack with 0.2.27's test build and its grace-spike.dll.** Each state line carries `frame_step` (1 to 11, the steps
  of GraceUI's first frames, D230), and the observer writes a state line at once whenever it changes. `report.py` names
  the step in the drawing row of a run that drew nothing.
- **A second file in the run's folder, `graceui-log.txt`**: GraceUI's own log lines, copied by the observer as they
  come, numbered, and kept apart from `graceui-spike.jsonl`, whose words stay the observer's own. It holds GraceUI's
  text, numbers and mods' ids, never a path or a name (PV3). Your README's "send the files in the spikes folder"
  already covers it; say that it is there and may be read first.
- **`GRACE_SPIKE_STATE_MS=200`** in the run's environment puts a state line every 200 ms (200 to 60000; 5000 by
  default), so the last one before a crash is close to it.
- Your vendored `graceui_test.h` gains `GraceUI_Test_FrameStep`, `GraceUI_Test_LogCount` and
  `GraceUI_Test_GetLogNumber`; it needs them only if your own harness reads them.

## 63. The spike package: the fault recorder (2026-11-23, D231; GraceUI 0.2.28, test-only)

A1's and A0's four crashes faulted at address 0 in no module (Windows' Application Error events), which names no
caller. 0.2.28's grace-spike.dll records each fault a run meets, without handling it, so the next crash names the call
that went to 0:

- **Repack with 0.2.28's test build and its grace-spike.dll** (graceui.dll itself is unchanged since 0.2.27 but for its
  version). Each fault gives a `fault` line in `graceui-spike.jsonl` (the code, the access, whether the target was the
  null page, `frame_step`, the thread and the frames as fixed words with offsets) and its frames by module file name in
  a third file beside the report, `graceui-faults.txt`. Your README's "send the files in the spikes folder" covers it;
  it names third-party modules by file name, so the owner may read it first.
- **Routine faults appear too**, once each site and at most 64 a run: the game, its anti-tamper or a driver may raise
  and handle their own. `report.py` lists them under each run and judges none; a run that fell has its last one there.

## 64. A free port for your test runs, and grace-dev's dump of a master's detail (2026-11-23, D234; GraceUI 0.2.31)

Two small things you asked for (your WHY§524 and §526):

- **`GRACEUI_TEST_PORT=free`** in the environment of a process that loads GraceUI's test build serves the browser view
  on a loopback port the system has free, instead of the settings' 8642; a number from 1024 to 65535 names one. The
  settings file is never changed, so your editor's case need not write a port into GraceUI's `settings.json` any more.
  Read the port in use from `GraceUI_Test_GetServer` (its `port`). A release build ignores the variable.
- **grace-dev `--dump`** now draws a master list's chosen row's detail: it gives the page each topic it asks for until
  none is new, so a block bound to the master's family shows the first row's member, as the menu does. Your Named
  NPCs goldens should lose their "Nothing to show yet." lines once regenerated with this grace-dev.

## 65. A heap guard in the test build, for the quit's crashes (2026-11-23, D235; GraceUI 0.2.32)

You asked for it for spikes.6 (your WHY§539). Nothing changes for EldenNet's code; for the spike package:

- **Ship `graceui.pdb` with the test build's `graceui.dll`**, or keep it beside the build you package: a breach names
  the code that made the damaged block as offsets in graceui.dll, and the PDB of that very build places them
  (`llvm-symbolizer --obj=graceui.dll --relative-address 0x…`). It holds no machine path (`/pdbaltpath`, sources under
  `/src`).
- **A breach ends the run** with `0xE0475548` (GraceUI's own fail-fast; the launcher's crash record shows that code or
  a fail-fast's). The observer's `graceui-spike.jsonl` then has a `fault` line with `heap_guard` (the breach, the
  block's size, `made_at`), and `graceui-faults.txt` the same with modules' file names. With no `heap_guard` line, a
  crash at the quit was not a block of GraceUI's own found damaged.
- `GRACEUI_TEST_HEAP_CHECK_MS` (100 to 600000; 5000 by default) sets how often GraceUI's thread checks every block.
  The guard costs memory (up to 64 MiB of freed blocks kept, and 96 bytes a block) and some time at each allocation;
  the present thread allocates nothing after warm-up, so frames are not slowed, but GraceUI's warm-up takes longer
  (under Wine a notice came 30 to 80 frames later). If one of your GraceUI cases counts warm-up frames, set
  `GRACEUI_TEST_HEAP_GUARD=off` in its environment (read at graceui.dll's first allocation); leave the guard on for the
  spike runs.

## 66. The owner's spikes.5 findings: the pointer, and the typing test's mouse (2026-11-23, D236; GraceUI 0.2.33)

From the three things the owner saw by hand in spikes.5 (your message of 2026-11-23):

- **The pointer no longer snaps back to the centre** a second after the mouse stops, in the menu and in a panel that
  holds the mouse. Nothing for EldenNet to change: a panel of yours that takes the mouse gets the same pointer.
- **The observer's typing test (F9) takes the keyboard and the pad only**, as your chat box does (O113); the mouse
  stays the game's, so the camera may turn during it. The owner's question in GraceUI's `tools/grace-spike/README.md`
  is now only whether the Tarnished stayed still. If EldenNet's spike package carries its own copy of that checklist,
  bring it into line.
- **The highlight border was the focus ring**, drawn from the menu's open and while the mouse was used. The owner
  decided (GraceUI's G42) that in game it shows only once a key or the pad is used and goes with the mouse, as in
  the browser view (D237). Your space's pages in GraceUI's menu follow it; nothing for EldenNet to change.

## 67. The window's focus and the presses that reach the game, in the spike report (2026-11-24, D238; GraceUI 0.2.34)

For A1's missed presses (your spikes.6 report): nothing in EldenNet changes. The test build's state lines gain
`window_focus` (foreground, active, focused; focus gained, lost; the game activated, deactivated) and `free_downs` (the
presses that reached the game with no holder: DirectInput keys and buttons, window keys and buttons), and `report.py`
lists them. If the launcher's run notes ask the owner what happened, asking roughly when a press did not register lets a
state line place it.

Then, from the owner's answers (a missed press worked when pressed again, so the window had the keyboard), D239 adds to
the same state lines each input detour's longest call and its calls over 2 ms and 8 ms (`stall_max_us`,
`stalls_over`), the longest gaps between the game's DirectInput reads and those over 100 ms (`poll_gaps`), the heap
guard's longest time in a detour and its longest check (`guard_in_detour_us`, `heap_check_us`). For the next A1, the
owner's rough time of a missed press, set against the state line near it, says whether a read came late.


## 68. One hook holder, shown at run time, and a mod beside a second copy (2026-11-24, D240; GraceUI 0.2.35)

For v0.1's exit check S10.3.d (your request of 2026-11-24): nothing in EldenNet's code changes; your harness's case
reads these from GraceUI's test build.

1. **A second copy** from another folder, asked as a mod carrying a private copy would ask it
   (`GraceUI_RequestApi(1)`), answers null every time. Its `GraceUI_Test_GetState` says `host` 4
   (`GRACEUI_TEST_HOST_IDLE_COPY`) with no init thread, install run or hook; the active copy's says `copies_named` 1,
   its hooks unchanged, and shows one notice of its own (owner 0, `GRACEUI_TONE_WARNING`): "A second copy of GraceUI was
   loaded from another folder and is idle: " and its name relative to the game's folder, never a path; its log has one
   line, "a second copy of GraceUI is loaded, and idle: " and the same name. A copy is named at the active copy's next
   request, or when it is asked first itself. The first `GraceUI_RequestApi` in the process takes the mutex: the copy
   asked second is the one that idles, whichever folder it is in.
2. **Where each hook leads**: `GraceUI_Test_GetHookLead(index, &lead)`, in `GraceUI_Test_GetHookName`'s order, says
   whether the hooked function's entry leads now to that copy's own detour (`leads_here`), the jumps followed
   (MinHook's are two), and the module it leads into (`module_base`, its `HMODULE`; `module`, its base name). Every
   function on D102's list leading into the active copy, and none read in the idle copy, is "one hook library in the
   process" at run time; a hook laid over one of GraceUI's shows as leading into its own module.
3. **GraceUI's harness** now runs a mod of your kind beside a second copy, each way round (copy-beside-mod and
   copy-beside-mod-reversed): its panel, hotkey, input holder and danger close carry on while the other copy idles, and
   `graceui_sys_find` asked afresh answers the active copy's table, as does its scan past an idle copy that holds the
   name graceui.dll. Under Wine `GetModuleHandleW(L"graceui.dll")` answered the active copy each time; Windows may
   answer the one loaded first, which is why the scan's case is checked by itself.

## 69. EB Garamond Italic in GraceUI's folder, for the Tarnished's lines (2026-11-24, D241; GraceUI 0.2.36)

For S11.4.c's italic lines: GraceUI 0.2.36 ships `fonts\EBGaramond-Italic.ttf` beside the Regular you read now, from
the same pinned commit and under the same `EBGaramond-OFL.txt`. Register it as a third `GRACEUI_FACE_SCALED` face (at
your lower third's size), read from GraceUI's folder in your registration window as you read Regular; you register two
of your eight faces, so nothing else changes. An older GraceUI has no such file: then register none and draw the
Tarnished's lines upright, or set your floor to 0.2.36. The panels' rounded corners need nothing of GraceUI's: the
checker takes any triangles on your own texture, so corners made as Dear ImGui makes them (a fan of points per corner
and a half-pixel fringe fading to nothing by vertex alpha, on your white texel) pass as they are.

## 70. The menu's pad mode (2026-11-24, D242; GraceUI 0.2.37)

Nothing for EldenNet to change. In game, GraceUI's menu now follows the last device used: after a pad press its key
hints are the pad's buttons, in GraceUI's own glyphs, for your space's pages as for GraceUI's; Y opens search, View
hides and shows the hints, and the right stick scrolls the page. Your own panels (the chat box, the lower third, F11)
draw their own hints, as before. For your harness: `GraceUiTestMenu` gains `pad_mode`, `hints_shown` and `footer` (the
footer's words as drawn), and `GraceUI_Test_GetMenu` now fills as far as your struct's size reaches, so a harness built
against an older `graceui_test.h` still reads the menu; `GraceUI_Test_PadState` gives a whole pad, sticks and triggers
included, should a case of yours need the right stick or a trigger.

## 71. What pressed a hotkey, and the notice when GraceUI's own copy idles (2026-11-24, D243, D244; GraceUI 0.2.38)

Your three points of 2026-11-24.

1. **What pressed a hotkey (ABI 1.7).** `GraceUiEvent.hotkey_by` (the word that was `reserved3`, so your event of 1.2's
   size or more holds it) says what made the presses a `GRACEUI_EVENT_HOTKEY` counts: `GRACEUI_HOTKEY_BY_KEY`,
   `GRACEUI_HOTKEY_BY_CHORD`, or both if both came before your poll. Check `has_cap("hotkey-by")`; an older GraceUI
   leaves it 0. Your chat box can log `via=pad_chord` for the chord and arm the swallow of T's character only when the
   key's bit is set. Re-vendor `graceui.h` (GRACEUI_ABI1_MINOR 7).
2. **Wine's lookup by name.** Your case's result is recorded in D240: Wine answered the copy loaded first there, the
   active copy in GraceUI's own scenarios; nothing relies on either, and graceui-sys's scan covers both.
3. **The notice's words.** When GraceUI's own copy (`GraceUI\graceui.dll`) is the one that idles, its notice now
   reads "GraceUI's own copy is idle: another copy was started first and is in use, <its name>. Remove that copy to use
   GraceUI's own.", naming the copy in use, the one to remove; the log line says the same. Your -second-copy-first case
   reads the new words there; the other order's are unchanged.

## 72. Your preview's three findings in the menu (2026-11-24, D245; GraceUI 0.2.39)

1. **The links back follow your increment.** On GraceUI's own pages, your categories at the rail's head now leave out
   a category with no page in your space's increment, as your own rail does, and each links to its first page present.
2. **The rail's ring shows whole**, inside the rail at the display's left edge.
3. **A related row's words clear its ring**: they start 12 px (at scale) inside the row, centred in its height.

For your goldens: any of your pages with a Related card moves by those rows' words alone (each label, description and
arrow), in grace-dev's dumps as in GraceUI's own; regenerate them at 0.2.39. Nothing else moves.

## 73. The menu's key hints as key caps (2026-11-24, D246; GraceUI 0.2.40)

GraceUI's menu now draws its keyboard hints as §3.8's key caps: each key in a cap, its label beside it. So:

1. **Your goldens' footers move** (grace-dev's dumps of your pages, and any of GraceUI's): regenerate them once, at
   0.2.40, rather than at 0.2.39; with 0.2.39's Related rows, nothing else moves.
2. **A test that looks for a whole hints line** on the screen (`GraceUI_Test_MenuShows("Enter  Save    Esc  Cancel")`)
   no longer finds it in one text: read `GraceUiTestMenu.footer` instead, which holds the line's words as before. A
   label alone ("Cancel") is still drawn as its own text.

## 74. The key caps' arrows drawn (2026-11-24, D247; GraceUI 0.2.41)

A key cap in the menu's footer now draws its arrows (← ↑ → ↓) as shapes, a shaft and a head centred in the cap,
instead of the mono face's small, low glyphs. So:

1. **Your goldens' footers move once more**, where a footer has an arrow in it ("← →  Change", "↑ ↓  Move"): each
   arrow is now a `fill` and a `tri` line instead of part of a `text` line, and those caps are wider. Pin 0.2.41
   rather than 0.2.40, and regenerate them once.
2. **Nothing else changes**: `GraceUiTestMenu.footer` and the view's `footer()` hold the keyboard's hints line as
   before, and the Related card's arrow and every other arrow in the menu's words stay text.

## 75. The rail's foot, and empty words without "yet" (2026-11-24, D248, D249; GraceUI 0.2.42)

Your two questions from Content packs. So:

1. **Your foot categories** (Settings, Docs, Content packs) can say `"foot": true` in their `[[category]]`, at
   protocol 1.10. They must come after every other category: the lint and registration refuse a foot category before
   one that is not. Both views then pin them to the rail's bottom under a rule, above GraceUI's own link, and with
   GraceUI's page open they are links at GraceUI's foot. Your goldens' rails move where you mark them.
2. **The default empty words** are now "Nothing to show.", without "yet", in both views. Your blocks with words of
   their own (`props.empty`) don't change.
3. **The protocol is 1.10**: the welcome and hello say "1.10". A space built for 1.9 runs unchanged.

## 76. A filter bar on a table or a master list (2026-11-24, G43, D250; GraceUI 0.2.43, protocol 1.11)

The owner chose G43 (a). So, for Requests (0.2), Named NPCs and Memories (0.3):

1. **Give the table block, or the page's `master`, a `filter_bar`**: `filters` (up to four, each `{column, label}`),
   `sort` (up to four, each `{key, label, direction}`, `direction` "descending" or left out for ascending; the first is
   the default), and `search` (`{columns, label}`, one to four columns; never one holding chat or story text). Column
   and key names are lowercase names (`a-z`, `0-9`, `_`). A `filter_bar` block of its own is refused.
2. **Put your hidden sort keys in each row** as numbers (`last_met_s`, `first_met_s`, `at_ms`, `first_token_ms`,
   `total_ms`, `tokens`, a memory's time and weight). A row without the key sorts last in either direction.
3. **Nothing comes to your space**: filtering and sorting happen in the view, so no command needs handling. Your row
   actions still receive their own row's key.
4. **Your goldens move only where a page carries a bar.** The protocol is 1.11.

## 77. Documents, a prose form field and the report preview (2026-11-24, G44, D251; GraceUI 0.2.44, protocol 1.12)

The owner chose G44 (a). So, for Docs and Tools › Bug report (0.2), both drawn in game too from now:

1. **Docs is a master page.** Its `master` topic's rows are `{id, title, headings}`, `headings` one string of the
   document's headings (joined as you like, such as " · "), and each document is the family member `<detail>/<id>`,
   `{"title": "...", "blocks": [...]}`. Its one section holds one block, `{ kind = "document", topic = "<detail>" }`,
   alone: the lint and registration refuse a document block anywhere else or beside another block.
2. **Blocks**: `{"heading": "...", "level": 2}` (or 3), `{"paragraph": spans}`, `{"list": [spans, ...], "ordered":
   false}`, `{"note": spans}`, `{"code": "..."}` and `{"table": {"columns": [{id, label}], "rows": [...]}}`. Spans are one
   string, or a list of `{"text": "..."}`, each with at most one of `"strong": true`, `"code": true`, or `"doc": "<id>"` with an optional
   `"heading": "<its words>"`. Never a URL. A link to a document your list does not hold is drawn as plain words; a
   heading the document lacks opens it at its top.
3. **Limits**: 64 KiB of JSON a document (beyond it the reader says the document is too large to show), 400 blocks,
   2000 characters a paragraph, item or note, 120 a heading, 16 KiB a code block (your licence texts fit), 100 items, 8
   columns, 200 rows. A block that breaks a rule is left out, the rest drawn.
4. **Search** is the list's own: give the master `filter_bar = { search = { columns = ["title", "headings"], label =
   "Find a page" } }` (D250), and a document is found by its title or a heading, never by its body. The global search
   (D219) is unchanged: it indexes page data and never a topic, so it does not reach documents.
5. **"What happened" is a form field with `prose = true`**, its `max` up to 2000; the caution goes in the section's
   `description`. A form sends every field, an empty one as `""`, in both views.
6. **The bundle's preview is `{ kind = "component", component = "report_preview", topic = "...", command = "..." }`.**
   Its topic's body is `{"parts": [{"id", "label", "optional", "included", "fields": [{"label", "value", "code"}]}]}`;
   `"code": true` draws a field in the mono face with its line breaks (your recent log lines). An optional part's switch
   sends your command with `{"id": "<part>", "on": true|false}`: declare it in your commands, never as a file command.
   The view changes nothing itself: publish the body again with the part's `included`, and a part left out is then drawn
   in the muted ink with its fields hidden. Your scan before copying or saving stays a check list beside it.
7. **Your goldens move only where a page carries these.** The protocol is 1.12; no change of the ABI.

## 78. Quick switches and Live updates (2026-11-24, G45, D252; GraceUI 0.2.45, protocol 1.13)

The owner chose G45 (a); this is its first step. So, for Home, Requests and Pause AI (0.2):

1. **List your quick switches in `[space]`**: `quick = [...]`, up to 12, in the order shown. A setting's is
   `{ key = "features.chat" }` alone: a switch or a choice of yours, shown with its own label and "Lives in" its home.
   Pause AI is a session switch: `{ id = "pause", label = "Pause AI for this session", command = "<yours>",
   state = { topic = "<a state topic of yours>", field = "<a boolean member>" }, home = "home" }`, all on one line in
   TOML. Its command is neither a settings, a file nor a slow command.
2. **Answer the switch's command** `{"on": true|false}` and publish the state topic with the field changed: the view
   shows only what you publish (invariant 1), and "Not known yet" until it has come. In game that topic is watched on
   every page, so publish it on watch.
3. **Say `live = true` on Home and Requests** (and later Events): pages whose topics move by themselves. The player may
   then pause them or slow them to once every 5 s; nothing changes on your side, and your settings, editors, choice
   lists and the answers to the player's own commands are never held.
4. **In game, Start opens the quick switches** from anywhere and never reaches the game; in the browser they are under
   the top bar's power button. Your goldens move where a page is live or your space has quick switches (the top bar's
   pills). The protocol is 1.13; no change of the ABI.

## 79. Two fixes your 0.2.44 pin found (2026-11-24, D253; GraceUI 0.2.46)

1. **In game, a document's member is watched now**: Docs shows each document. Your docs golden from grace-dev's dump
   follows; nothing in your page data changes.
2. **A report preview's fields wrap in game and keep their line breaks in both views**, so "What happened" shows whole.
   Nothing changes on your side. The protocol stays 1.13.


## 80. A space's scope, and tables read a page at a time (2026-11-24, G45, D254; GraceUI 0.2.47, protocol 1.14)

G45's second step, in the formats your session gave (proposal 0014 §7). So, for the Journey and History (0.3):

1. **Name your scope in `[space]`**: `scope = { label = "Journey", topic = "journeys" }`. With no `field` the topic's
   whole body is the scope: `{"choices": [{"id": "j1", "label": "<the character's name> · Journey 1"}, {"id": "j0",
   "label": "…", "read_only": true}], "current": "j1"}`. An id names a member: not empty, at most 64 bytes, no slash;
   at most 32 choices are read. Its labels show only in the chooser and in a read-only journey's line (below), never in
   a title, an address or a log, as PV2 wants.
2. **Say `scoped = true` on History's pages, Playthrough and Named NPCs.** On such a page a block bound to a family
   draws its member for the journey in view (`playthrough/<journey>`, `memories/<journey>`), watched like any member,
   so publish an earlier journey's only while it is watched. Named NPCs' master list may be the family itself
   (`topic = "characters.named"`): the view reads `characters.named/<journey>`, and leave Nearby out of an earlier
   journey's member, as you planned. The master's detail family still follows the chosen row.
3. **A choice marked `read_only`** greys the page, as your `space.state` words would, with "<its label> is read only."
   Your own `space.state` words, where you give them, come first.
4. **A paged table**: a table block with no topic and `page = { command = "history.page", args = { view = "events" },
   changes = "<a state topic of yours>" }`; or a page's master list with `page` in place of its `topic`. The command
   is declared as any of yours, not a settings, file or slow command. You receive your args and `filters` (each filter's
   value by its column), `sort` (`key`, `direction`), `search` (trimmed, only if it holds words), `scope` (the journey,
   on a scoped page) and `cursor` (Show more's, as you gave it in `next`). Answer in the result's body `{"columns",
   "rows", "next", "total"}`; the view reads at most 200 rows, shows "1,234 in all" from `total`, and offers Show more
   while there is a `next`. Refuse with your words in the detail ("Load a character to see this.") and they stand in for
   the rows.
5. **Nothing is asked before your scope's topic has come** on a scoped page, so publish `journeys` on watch. Any change
   of filter, sort, search or journey asks for the first page again; a late answer is dropped by the view. When the
   `changes` topic's body changes, a table showing its first page asks for it again; one showing more says "New rows ·
   Show". Live updates holds that topic while it holds (D252).
6. **A filter on a paged bar names its choices**: `choices = [{ value = "…", label = "…" }]`, or `choices_from =
   { topic = "<yours>", field = "<a list>" }` (strings, or `{value, label}`), for your regions entered. The bar shows no
   "N of M shown" line, since you filter. The browser sends a search 300 ms after the typing stops; the game when the
   field closes.
7. **grace-dev** can preview a paged page: a scenario reaction may answer `{"body": {...}}`, the same page whatever is
   asked. The protocol is 1.14; no change of the ABI. Your goldens move on scoped and paged pages (the chooser's pill,
   "Loading…" until a page comes).

## 81. Four fixes to the scope and paged tables (2026-11-24, D255; GraceUI 0.2.48)

Found by a review of 0.2.47's code; nothing changes on your side, and the protocol stays 1.14.

1. In game a late answer to a page asked before the menu closed is never drawn as the page asked now.
2. In the browser a refused page (History's "Load a character to see this.") asks again when your `changes` topic
   moves, as the game does, so publish that topic when a character loads.
3. A refused Show more shows your words at the list's foot, the rows kept.
4. `total` must be a whole number (at most 2^53 − 1) to show as "N in all"; anything else shows no count.

## 82. Your four points from the 0.2.46 pin (2026-11-24, D256; GraceUI 0.2.49, protocol 1.15)

1. **Docs within the limits.** What is out of view costs no vertex now, so grace-dev's dump of Docs passes its check;
   drop the exit you took for Docs. In game Docs drew already (the menu's own list is not checked as a mod's is), and
   now costs only what shows.
2. **A page opens at its top.** Requests and Home open on their first card's label, the header in view, rather than
   on Related; focus does not jump when the filter bar comes. Your goldens of ai.requests and home move by their focus.
3. **A row drawer**: mark a column of a table's body `"drawer": true` (Why, Text shown) and it leaves the row for the
   row's drawer, label above words, line breaks kept. In game the row is a stop; Enter opens its drawer, Esc closes it.
   In the browser a mark at the row's start opens it. Give each row an `id` so its drawer follows it. No minor: an
   older GraceUI shows the column on the row.
4. **A header switch**: in a page's `actions`, `{ id = "pause", label = "Pause AI", command = "ai.pause",
   tone = "secondary", state = { topic = "status", field = "ai_paused" } }` (TOML on one line). It shows your
   `status.ai_paused`, "Not known yet" until it comes, and sends `{"on": ...}` flipped; not destructive, not
   confirmed, and not on a section. Page data using it is minor 15.

## 83. Your streams in game, and a filter bar on a stream (2026-11-24, D257; GraceUI 0.2.50, protocol 1.16)

1. **Logs, and any live feed or log tail of yours, show in game now**, where they said "Shown in the browser view.": each
   row's `time` and `line`, the newest 200 rows. Nothing changes on your side: the topic is watched with its page, and
   a new watcher gets the rows you send it as now. Your goldens of pages with a stream move.
2. **A filter bar on a stream**: `filter_bar = { filters = [{ column = "level", label = "Level" }], search = { columns =
   ["line"], label = "Find in the log" } }` on Logs' `log_tail`, with a `level` field on each row (`error`, `warning`,
   `info`, `debug`, `trace`); no sort (the rows come in time's order). Both views filter what they hold. Page data with
   it is minor 16.

## 84. Workshop's gate: a page in the browser alone, shown with a setting, gated, with a ribbon (2026-11-24, D258; GraceUI 0.2.51, protocol 1.17)

For Tools › Workshop (PAGES §4.6.8, SP8), four fields on its page:

```toml
browser_only = true                 # absent in game: rail, search, Related
shown_with = "debug.panel"          # present in both views only while the switch is on
gate = { words = "Workshop shows unfiltered data and contains spoilers. Open it for this session?", confirm = "Open Workshop" }
ribbon = "Spoilers"                 # a band at the page's top while it is open
```

The gate stands in for the page once a browser session, and nothing of the page is drawn or watched until it is
confirmed. No setting and no quick switch may have a browser-only page as its home. Page data with these fields is
minor 17. The words above are only an example: they are yours to write.

## 85. A panel drawn in each frame: `on_present` (2026-11-24, D259; GraceUI 0.2.52, ABI 1.8)

Your subtitles' fades and world labels can now follow every frame, rather than your T6's ten submits a second (your
WHY§364 (3)). Opt in per panel, from your own thread, once `has_cap("on-present")` says yes:

```c
GraceUiPresentHook hook = {sizeof hook};
hook.vertex_capacity = 4096;  /* the most your per-frame list holds: 1 to a submission's bounds */
hook.index_capacity = 6144;
hook.command_capacity = 16;
hook.callback = &subtitles_on_present;  /* uint32_t (void* user, const GraceUiPresentFrame*, GraceUiDrawList*) */
hook.user = &subtitles;
api->set_on_present(me, subtitles_panel, &hook);
```

- **Each frame the panel is shown**, on the game's present thread, the callback is told the frame's number (it rises
  by one in each frame a callback can be called in), a steady time in µs, the delta (1/60 at first, at most 0.25 s
  after a pause; the time is the clock), the panel's place and size, the display's size, `flags`
  (`GRACEUI_PRESENT_MENU_OPEN` while GraceUI's menu is open), and `spent_us`: what your callbacks took together in the
  frame before, in microseconds on GraceUI's clock, as your budget weighs them (0 if none ran then). So you need not
  time yourselves: keep `spent_us` by `flags` for PAGES' frame cost with the menu open and closed, your own
  callbacks' alone. It answers `GRACEUI_PRESENT_KEEP` (your
  last submitted list is drawn) or `GRACEUI_PRESENT_DRAW`, having filled `list` with arrays of your own that hold until
  it returns. GraceUI copies that list into the room it reserved, checks it as `submit` does and draws it in that frame
  alone. A list that does not fit or fails the check is not drawn (your submitted one is), and is logged once.
- **What the callback must not do**: wait, lock, allocate per frame after its first frames, or let an exception out.
  It may not call GraceUI either: every slot but `has_cap` answers `GRACEUI_REFUSED` from inside it. So it reads only
  what your T6 has already left for it (your newest snapshot, by an atomic pointer), and lays out from glyph tables
  fetched beforehand.
- **Budget**: your callbacks together get 0.5 ms a frame (EST). Over that for 120 frames running, or an exception, and
  all of them are suspended for the session. GraceUI names EldenNet in one notice, your panels draw their submitted
  lists, and `set_on_present` answers refused for a callback, even after you connect again (clearing one is taken). So keep submitting a sensible list: it is
  what players see if the callback is ever suspended.
- **Clearing**: `set_on_present` with a null callback, and `disconnect`, return only once the callback is not running
  and can never be called again. After that you may free `user`. Each waits for the frame being drawn to end, however
  long it takes, so never call either from DllMain; GraceUI's present thread never waits for you.
- **Setting a callback closes your registration window**, as your first `submit` does: register your textures before.
  Up to 4 callbacks a mod and 8 in all (EST).
- **Nothing changes for a mod that does not opt in**, and nothing in your page data. A mod built against ABI 1.7 sees
  the same table, one slot shorter.

## 86. Your 0.2.46 build's in-game points: a table's columns, wrapped sentences, a tile's unit (2026-11-25, D260; GraceUI 0.2.53)

Points 1, 2 and 4 of your message of 2026-11-24; point 3 is §85's `spent_us` and `flags`. Nothing changes in your page
data or the ABI.

- **A table in game** now has one header line of its columns' labels. Each column after the first has a slot of its
  own, and a `numeric = true` column is right-aligned in it, as in the browser. A row missing a cell leaves that slot
  empty, so Traces' four times stay under their own labels. With too many columns for slots of a readable width (64 px
  at the effective scale), there is no header and each value follows its column's label: "First token 0.04 s  ·
  Reply 0.64 s". A row secret's status in its slot is its words alone; its label is in the header.
- **A `computed_sentence` and a section's description** wrap to their card, as the report preview's fields do.
- **A stat tile's `unit`** is drawn after its value in the small face, muted, as the browser's `tile__unit` draws it:
  "21 of 24" large, "shown" small.

## 87. The rest of the in-game texts wrap (2026-11-25, D261; GraceUI 0.2.54)

After §86, four texts were still cut at one line in game. Each now wraps: a page's description, a `statement`, a
`key_value_list`'s value (in its two thirds of the row, the row growing to hold it) and a focused setting's help line.
A check list's detail stays on its label's line. Nothing changes in your page data.

## 88. Your 0.2.53 pin's six points (2026-11-25, D262; GraceUI 0.2.55, protocol 1.18)

Each point as I answered it on 2026-11-25. Only point 2 adds anything to your page data. A space that uses it asks
for GraceUI 0.2.55 (protocol 1.18): an older GraceUI refuses `choices` on such a bar when you register.

1. **grace-dev's dump shows a stream as the game draws it.** A topic fixture that is a list goes to the view as the
   menu's copy, `{"rows": [...]}`, with its newest 200 rows alone, so your goldens of Logs show its rows.
2. **A filter's own choices on any bar.** A filter on a bar that GraceUI filters itself (a table or a master list
   read whole, a log tail, a live feed) may give `choices = [{ value = "warning", label = "Warnings" }, ...]`. It
   then offers those values in that order, in your words, and filters by value as before. A row whose value is not
   listed shows under All alone. GraceUI never words your levels itself. `choices_from`, a state topic's list, is
   still only for rows read a page at a time, where a filter names one or the other; the lint and registration
   refuse it on any other bar.
3. **No drawer on an empty row.** A row whose drawer columns are all empty or missing has no mark and no focus stop in
   game, and no Details button in the browser. The mark's room stays on every row, so the columns still line up.
4. **A stream's bar comes before its rows.** A log tail's or live feed's filter bar is drawn before any row has come,
   over its empty words, in both views, as a paged table's is. A page's first focus no longer depends on whether the
   browser has fed the stream.
5. **First focus goes to the page's body.** In game, a page's first focus is its body's first control, never a header
   action, so a stray Enter after F5 cannot pause AI. ↑ and the pointer reach the header's actions. On a page whose
   controls come with a topic, focus starts on its first section's label and stays there when they come (D256), so it
   never rests on a header action. A header action takes the first focus only on a page with no section or tab. This
   amends D256's (2). It also changes what I told you: I said a page whose only controls are its header actions would
   focus the first of them. But when a page opens, GraceUI cannot tell such a page from one whose controls have not
   come yet.
6. **Slots are sized by need.** Each column's slot in an in-game table is as wide as its widest words, label or cell,
   where the room allows. Narrow columns take what they need and the wide ones share the rest; the first column keeps
   its third. A cell still too long ends in "…". The slots follow the rows held, so in a live table a slot can widen
   when a wider value comes; I took that over cutting your addresses.

## 89. Your Bug report's Copy and Save (2026-11-25, D263; GraceUI 0.2.56, protocol 1.19)

This is proposal 0015, as you answered it. Your floor for it is GraceUI 0.2.56: an older GraceUI refuses `share` on a
block when you register.

- **Your block**: `{ kind = "component", component = "report_preview", topic = "report", command = "report.part",
  share = { copy = true, save = "report.save" } }`. `report.save` must be one of your declared
  `sensitive_commands`, and must not be a file command. The block's topic must be a topic of its own, not a family,
  since Save sends `{}`. It may draw a field of the topic's body with `props.field`. Drop `report.copy`: GraceUI copies in both views and sends
  you nothing after a copy.
- **Your body**: give `share` only once your first `report.build` has passed its scan. It is either
  `{"text": "<the whole report, LF line ends>", "file": "eldennet-report-2026-11-25.txt"}` or
  `{"blocked": "Remove the path in What happened before sharing."}`. The text is yours alone: GraceUI never composes,
  changes, stores or logs it. It may be at most 256 KiB. The file name is 1 to 64 of a-z, 0-9, -, _ and ., ending
  .txt. A share whose text or name breaks these rules is dropped whole. Without a share, or with a blocked one, Copy
  and Save are greyed, with your blocked words under them.
- **In game**: Copy puts your text on Windows' clipboard on the press alone and reads "Copied" for 2 s. Save sends
  `report.save` with `{}`. You write the file where you keep your reports and answer with a `detail` that names it
  relative to the game's folder, for example "Saved as EldenNet\Reports\eldennet-report-2026-11-25.txt." GraceUI shows
  those words under the button. A refusal's words show there instead and stay until Save is pressed again, even if you
  publish the report meanwhile; focus returns to Save. A click on Copy copies as Enter does.
- **In the browser on this PC**: Copy uses the browser's clipboard. Save downloads your text under your file name.
  Neither sends you anything, so you never see `report.save` from the browser.
- **On a paired device**: neither button is drawn. GraceUI's server leaves `share` out of every body it sends a device for
  any topic a report preview draws, at the field the block names, so your text never reaches one. The parts and their switches still go to a device, as now.

## 90. This-PC-only pages and topics, and page data for a newer GraceUI (2026-11-25, D264; GraceUI 0.2.57, protocol 1.20)

Your G-1 and G-6 from the Workshop survey. Workshop needs GraceUI 0.2.57 or later once it uses `pc_only`. A space that
gives `pc_only` to an older GraceUI is not refused; the mark is simply not read, so give the floor.

- **`pc_only = true` on a page** keeps it out of a paired device's view. GraceUI's server leaves the page out of the
  page data it sends a device, with every Related link to it and any category it leaves empty. In game, on this PC, the
  page is as any other.
- **`pc_only = true` on a topic** (beside its `shape`) makes the server refuse a device's sub to it, or to a member of a
  family so marked. The refusal is not_now with "Only on the PC the game runs on.". You never hear of the watch.
- **A pc_only page's own commands** (any command it alone names: an action, a form, an editor's, a page source, a
  report's Save) are refused from a device as sensitive ones are, so a device cannot ask for the data by a command.
  A command also named on a page a device sees stays a device's to send.
- **Settings homed on a pc_only page** are left out of your settings topic as a device gets it: its description,
  values, changed keys and inherited values. A device's page data names no pc_only topic.
- **The lint and registration check three things.** A pc_only topic may be drawn only on pc_only pages: not on a page
  a device sees, not in the scope, not by a quick switch, and not as a setting's choices homed elsewhere. A setting
  homed on a pc_only page may be shown on no page, and by no quick switch, that a device sees. It must be `sensitive`
  (or a secret), so a device cannot change it.
- **For Workshop**: mark the page `pc_only = true`, and mark every topic that only Workshop draws. A topic Workshop
  shares with another page stays readable by a device, as now.
- **Page data for a newer GraceUI**: register_pages now refuses page data whose minor is above the GraceUI's own, read
  before the rest is decoded. You get GRACEUI_REFUSED. GraceUI's log and one notice of its own name both minors:
  "EldenNet needs a newer GraceUI for its pages: page data for protocol 1.21; this GraceUI reads up to 1.20.". If you
  register again, that notice is replaced rather than repeated. GraceUIs before 0.2.57 do not check this.
- **So give your page data the minor of your floor**, the oldest GraceUI you support, not the newest GraceUI's.
  With Workshop's pc_only that is 20 (GraceUI 0.2.57).

## 91. A check of your pages in the browser view (2026-11-25, D265; GraceUI 0.2.58)

Your G-4. In GraceUI's `web/` folder, with the view built (`npm ci --ignore-scripts`, then `npm run build`), run:

```sh
npm run -s check:space -- <your space folder> --golden <your golden file>
```

- **The folder** is the one grace-dev takes: `pages.json` and `settings.json`, and optionally `topics.json` and
  `paged.json`.
- **What it does**: it opens GraceUI's real browser view in Chromium (the Playwright your machine has), over GraceUI's
  fake host with your fixtures and GraceUI's own space beside them.
- **What it prints** is a text dump for this PC's browser, for a paired device, and for each switch a page is shown
  with, turned the other way. Workshop's `shown_with` is one such switch. For each of these it lists:
  - the rail: each category's pages, in order;
  - each page's text as the view shows it. A gated page shows its gate's words and buttons first, then the page
    behind it. A page that is absent is named as absent, as Workshop is on a device.
- **It fails** on a console or page error, a CSP violation, a request outside the page's origin, or a text under 4.5:1
  in either theme. With `--golden` it also fails when the dump differs from your file; `--update` writes the file.
- **Exit codes**: 0 passes, 1 fails (each problem is named), 2 means the folder or the build is missing.
- It names no path, so its output can go in your logs.

## 92. A slider's typed entry (2026-11-25, D266; GraceUI 0.2.59)

Nothing to change on your side: every integer, number and duration setting on your pages gains it.

- **In game**: Enter on a focused slider, from the keyboard, or a press on its value, opens GraceUI's own field with
  the value at its step's decimals and no unit, all of it chosen. Enter sets what is typed, Escape drops it, and a
  press elsewhere keeps it. A pad's A opens nothing; Steam's floating keyboard is not built yet.
- **In the browser**: Enter on the track or a click on the value opens a text field in its place, and leaving it keeps
  what was typed.
- **What a text becomes**: spaces and the unit at the end are ignored ("×" also as x). A full stop or a comma is the
  decimal point, so "1,024" is 1.024. The value is snapped to the nearest step from your minimum (or 0), within your
  bounds. A text that is no number, or one outside your bounds, sends nothing: the bounds show under the row in words,
  as your refusals do: "Type a number from 0.75× to 1.50×." A number of 16 digits or more is refused too. Both views
  follow `fixtures/typed-cases.json`.
- **Rounding**: a value now rounds to the finer of your step's and your minimum's decimals, from the arrows and a
  drag too, so a minimum of 0.25 with a step of 0.5 keeps 0.75. The page lint now refuses a min, max or step of more
  than four decimals. Your settings on main hold no numbers yet, so nothing of yours moves.
- **Shortcut rows** keep a plain slider: no typed entry, in either view.
- **Your goldens**: a grace-dev dump with a slider focused gains "Enter  Type" in its footer, before "R  Reset". In the
  browser the value is now a button, `[data-value='<key>']`, and its field is `[data-entry='<key>']`; the value's text
  is as before.

## 93. Plain text in game (2026-11-27, D267; GraceUI 0.2.60)

Nothing to change on your side.

- **Your pages in game** now follow GraceUI's Plain text (GraceUI › Menu), as they did in the browser: titles and
  prose in Noto Sans in place of Cinzel and EB Garamond. Turned on in a session it applies at once, from GraceUI's
  nearest sans faces, scaled; from the next launch those faces are baked at their own sizes.
- **Your own panels** (the subtitles, the chat box, F11) draw with your own faces and are not changed. If you want
  them to follow Plain text, say so: GraceUI can publish it to mods as it publishes `ui.scale` (D180).
- **The shared atlas** grows while Plain text is on at launch (2048 by 2048 at 1080p and a scale of 1.3 with it;
  1024 by 2048 at 1.25 without), up to 4096 by 4096. Past that, or if it cannot hold everything, Plain text's faces
  are left out before any of yours.

## 94. Plain text for mods (2026-11-27, D268; GraceUI 0.2.61, ABI 1.9)

Your request after §93, for the subtitles and the chat box if your O134 decides they follow Plain text.

- **The value**: `GraceUiConnectAnswer.plain_text` at connect (1 or 0, the player's kept setting or its default, as
  `ui_scale` is), and `GraceUiEvent.plain_text` with `GRACEUI_EVENT_DISPLAY`. The display event now also comes after
  each change of Plain text. Both fields are appended; `has_cap("plain-text")` says a GraceUI has them. Build against
  ABI 1.9's header to read them, and zero `plain_text` before connect or check the cap: an older GraceUI leaves it as
  you set it. A mod built for 1.8 reads what it read, with one more display event, its values unchanged, when only
  Plain text changes.
- **The faces are yours**: GraceUI does not lend its menu's. Register `fonts\NotoSans-Regular.ttf` and
  `fonts\NotoSans-SemiBold.ttf` from GraceUI's folder (OFL, credited in its `NOTICE`) with `register_face` in your
  registration window, as you register EB Garamond Italic (§69, D241); `GRACEUI_FACE_SCALED` works on them as on any
  face.
- **A change in session** reaches you at once. With both your serif and your sans faces registered you can switch at
  once; with only those connect's `plain_text` asks for, you follow it from the next launch. Both keep invariant 11.
  For the subtitles and the chat box, the second is what we recommend: players set this once, and nobody pays atlas
  room for faces they do not use.

## 95. Your five points from the Workshop build (2026-11-27, D269; GraceUI 0.2.62)

Nothing to change on your side; three things you may take up.

1. **check:space's dump through a pipe** now arrives whole: it exits only once its output has been handed on. You can
   read stdout again rather than the `--golden` work file.
2. **Pages read before their topics came**: the check now waits until the page has sent nothing for 1.2 s, which
   covers the browser's paced subscriptions (a batch a second, D203), then for 400 ms of stillness. Home's and AI's
   dumps should hold still from run to run, so your golden can take their text again. Each page takes about 1.2 s
   more, so a long space takes some minutes.
3. **A page's presence in game**, in the test build: `GraceUI_Test_MenuPage("eldennet", "tools.workshop")` answers 1
   present, 0 absent, 2 no such space or page, from the menu's last layout. One rule decides the space's rail, its
   Related links and whether search finds the page's settings (the game's search finds settings, not page names), so
   one answer covers them, whatever another page's help says. A space's switches are as the menu last took its values,
   while it was the open space.
4. **pc_only and search**: a device's search does not find the page, nor any setting homed on it. A DOM test pins
   the page; the demo's pc_only page has no settings, so the settings half rests on the same shown pages.
5. **shown_with before the settings come**: the page is absent until the settings topic's values have come, then
   there at once. A DOM test now pins it.

## 96. A held direction repeats in the menu (2026-11-27, D270; GraceUI 0.2.63)

Nothing to change on your side.

1. **Your pages in game**: a held d-pad or left stick now repeats, as a held arrow key does: along the rail, a page's
   rows, a master's list or the top bar, and a slider's or an editor's number field steps on. A repeat never turns one
   of your switches, moves a choice on, follows a link, turns a tab or a page, or goes from the rail into a page; a held
   Enter or A acts once, and a step at a slider's bound sends nothing, so your space sees no stream of equal
   `settings.set`s while a stick is held at the end. The browser view drops the same repeats.
2. **Your own holders** (the chat box, the F11 view) are as before: their key events still carry `repeat`, and a pad's
   buttons come as changes, never repeated. A panel of yours that wants a held direction to repeat times it itself.

## 97. The in-game menu fades (2026-11-27, D271; GraceUI 0.2.64)

Nothing to change on your side.

1. **The menu** fades in over 120 ms when it opens and, when the player closes it (F5, Esc or B on the top level, the
   close button), fades out over 200 ms while input is already the game's again. Danger, a load, focus lost, the
   watchdog, a fault and the player's exit still take it away at once; Reduce motion turns both fades off.
2. **Your panels and notices** are drawn as before: the fade is the menu's alone. A panel of yours that opens as the
   menu closes is drawn at once above or below the fading menu by its layer.

## 98. A page's body fades in at a change of page (2026-11-27, D272; GraceUI 0.2.65)

Nothing to change on your side. In game, when the player goes to another page or space (the rail, LT and RT, Page Up
and Page Down, a link), the new page's body fades in over 120 ms; the rail, the top bar and the key hints stay still,
and Reduce motion turns it off. A test that reads a page's pixels right after a page change waits about 150 ms first.

## 99. Your question on Plain text before the menu opens (2026-11-27, D273; GraceUI 0.2.66)

Not intended, and fixed: GraceUI's thread now follows its own settings from the store at its first wake and at every
change, whether the menu is open or not. `GraceUI_Test_GetMenu`'s `plain_text` reads 1 with Plain text kept from the
last session before the menu has ever opened, and follows a browser's change with the menu closed at once, as the
mods' display event already did. Your case may read the field again rather than `menu_plain_baked` and the settings
topic, though both stay right.

## 100. A confirmation that waits, and private topics (2026-11-27, D274; GraceUI 0.2.67, protocol 1.21)

Built as you asked. **`confirm_after_ms`** (1 to 3000) on a header action, a section's own action or a row action that
asks first (destructive, or with confirm words): its second press counts only that long after the first. In game the
button reads "Press again to confirm" at once, greyed until the wait is over; a press before then does nothing and lets
nothing go, and moving away lets the confirmation go as before. In the browser the dialog shows your words at once and
its action's button is disabled for the wait. The command still carries `"confirm": true`. So Forget declares
`confirm_after_ms = 1000` on its row action. The lint refuses a value outside 1 to 3000, one on an action that does not
ask first, one on a header switch and one on a capture row action; both views draw a larger value as 3000.

**`private = true`** on a topic (a chat's lines, a model's text): the lint, run by your CI over your `space.toml`, then
refuses that topic as a section's status, a header switch's state, a filter's or a setting's choices, the scope's topic
and a quick switch's state, so that it is drawn in its own blocks alone. Neither view reads the mark: nothing of any
topic reaches search, a title, a URL or storage already. Your page data names minor 21 once it uses either.

## 101. Steam's floating keyboard for GraceUI's own fields (2026-11-27, D275; GraceUI 0.2.68)

GraceUI's menu now opens Steam's floating keyboard when a text field begins editing from a pad: search by Y, a
setting's text, a form's fields, a row secret, an editor's text or notes, a filter's search. It calls
`SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput` through the newest `SteamAPI_SteamUtils_v0NN` accessor of the
`steam_api64.dll` the game has loaded, on GraceUI's thread, beside the field (one line), and dismisses it when the
field is let go or the menu closes by any exit. An edit that outlives an Alt+Tab does not reopen it on the return:
S4 may see whether Steam's keyboard takes the game window's focus, which would make GraceUI's menu let go of input as
an Alt+Tab does (G40), and that is worth a line in its round. It never loads Steam's library, links no SDK and calls
no game code. Without the library, the exports or Steam's yes (it opens on a Deck or in Big Picture) nothing shows,
and GraceUI's log says which in one line: "Steam's floating keyboard: ..." is the line for S4's round to read.

Nothing changes for EldenNet. If you would rather your chat box opened the keyboard through GraceUI than make the call
itself (R2's one third-party call), GraceUI can offer the input holder an ABI slot for it (a mode and the field's
rectangle in the mod's panel); say so and it is the next ABI minor.
