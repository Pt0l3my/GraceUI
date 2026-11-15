# Changes EldenNet's design needs for GraceUI

A short list for EldenNet's design (branch `design/v1-proposal`), from [GraceUI's design](DESIGN.md). EldenNet's codes
(DESIGN, ARCH, NPC, MEM, ER02) refer to its documents; "GraceUI §n", G1–G27 and D1–D101 to GraceUI's. Nothing in §1 to
§10 is changed in EldenNet yet, apart from the roster, which EldenNet's design now records (its O43 as updated, O45 and
O46; §9 item 1); §11, the framework's changes, was folded into EldenNet's design on 2026-11-15 (roadmap S7.8.c), and
where it and an earlier item disagree, §11 holds. Items marked **(G*n*)** come from that entry of GraceUI's
[DECISIONS.md](DECISIONS.md) (indexed in GraceUI §12), every one of which the owner has decided (G3 and G10 on
2026-11-07, as recommended; G10 superseded by G15 on 2026-11-08). All of it stays minimal: one overlay, GraceUI's since
G15, and read-only towards the game; EldenNet starts no new thread for the UI, while GraceUI starts its own init thread
and the server's (GraceUI D71, D81).

## 1. The main UI on F5 through GraceUI

1. **DESIGN §12 and NPC §10.1**: replace "settings (F10, safe moments only)" with "the main menu (F5, GraceUI; safe
   moments and the title screen)". The menu is drawn in EldenNet's one hudhook overlay with Dear ImGui and the Grace
   theme **(G1)**, after the world surfaces and below the toasts and the banner: GraceUI's `graceui.dll` lays it out
   and tessellates it, and EldenNet's overlay copies the vertex lists into ImGui's draw list **(G10 (b))**.
   *Later (2026-11-15): G15 (a) supersedes G10 (b); the one overlay is GraceUI's own, and the menu draws in it (§11).*
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
   and a migration moves any it already holds (§11 item 7).*
4. **S4** (DESIGN §16.1) adds: F5 is free in the default layout; one Grace settings page drawn in game, with its frame
   cost at 1440p and on the Deck, the atlas size, and screenshots for the owner's look review **(G1)**; buffered
   DirectInput reads; the `WM_CHAR` panic reproduced; the Steam overlay browser reaching 127.0.0.1 (a hand test,
   D30); opening a URL from the game under Proton; the ABI round trip with `graceui.dll` and the cost of copying its
   vertex lists, and a menu page of more than 65,536 vertices drawing correctly through hudhook 0.9.3's per-command
   vertex offsets **(G10 (b))**. *Later (2026-11-15): the same checks run on GraceUI's host, which draws S4's page
   itself; EldenNet's part is its panels and the connect (§11).*
5. **The wizard** (DESIGN §11.5) becomes GraceUI's First light in v0.6 (D16); the rollback modal and F11's debug view
   stay in EldenNet's overlay. *Later (2026-11-15): they stay EldenNet's own, drawn as its panels through GraceUI's
   host (§11 item 2).*
6. **The v0.6 increment** (DESIGN §16.2): "F10 pages" becomes "the GraceUI menu on F5 (every page but Workshop)".

## 2. The bridge (a `UiPort` in the core)

1. A **player-safe projection layer** in `en-core` (GraceUI §4.8, SP1–SP9): topics built only while watched, at most
   4 Hz each, display strings formatted in the core, published with `arc-swap`.
2. **One validating command entry point** for both views (a new bounded channel into T6; `try_send` from T3 and from
   the server). *Later (2026-11-15): EldenNet pulls its commands from GraceUI with `poll` on T6 (§11 items 4 and 7).*
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
   `grace-serve` (§11 items 1, 2 and 7).*
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
in GraceUI. §11 item 7 lists what EldenNet keeps.*

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
(D29, D76); §11 lists what EldenNet keeps.*

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
   on hudhook.
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
7. **Terrain** (G27 (a), decided 2026-11-14): the terrain's heights read from the player's install (the source to be
   found by S7.9.c: map pieces, collision or a height texture, each structure verified, O48), built once on the PC
   with the drawn base and cached; projected for the browser's 3D relief and rendered once as the in-game relief
   image; masked to what is reached.
   *Later (2026-11-15):* the relief is part of the base, so it shows only what the player's own game map shows for the
   pieces held (D90), and in game it is composed with the map's texture (D98).
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
   a renderer backend; its greps check that no hook library is linked.
2. **`en-overlay` becomes EldenNet's display layer**: its surfaces (subtitles, the chat box, the call-out box, toasts,
   world labels) are panels, built with GraceUI's `grace-draw` and `grace-view` crates (pinned by tag) and drawn
   through an `on_present` callback within its budget; its faces are registered with `register_face` before warm-up,
   and it follows the atlas's generation; its HDR handling is GraceUI's. The F11 debug view keeps its imgui-rs code on
   a headless context of EldenNet's own (`imgui` 0.12, the Dear ImGui 1.89.2 that hudhook 0.9.3 bundles, with no
   backend), its draw data submitted as a panel while it is open, its font atlas registered once as a texture and its
   input taken through `request_input`; only F11's frame glue is designed anew for v0.1.
3. **Input**: the DInput and XInput detours, the window-procedure filter, the `WM_CHAR` fix and IME placement move to
   GraceUI, which finds the XInput DLLs by module name, not through the game's import table; the chat box and call-out
   box take input through `request_input`; EldenNet's keys (the chat key, F11, the talk gesture's chord) are
   registered hotkeys; the Steam on-screen keyboard is opened by GraceUI for its own fields and by EldenNet for its chat
   box, as now.
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
   the server's threads are GraceUI's.
8. **The world-state provider**: EldenNet publishes danger, loading, a game menu or text entry open, the title screen,
   multiplayer and an anti-cheat hit from `safe_context`, its `ui` group and T10 (D78), as one provider among any
   others, and keeps gating its own space.
9. **EldenNet's pages** wait for G20 (Open). Under its recommended answer, (a), its page data, fixtures and goldens,
   SP1–SP9, its PV rules and D61 join EldenNet's documents and CI, and GraceUI's page lint runs there (D82).
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
