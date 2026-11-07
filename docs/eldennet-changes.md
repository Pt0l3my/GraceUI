# Changes EldenNet's design needs for GraceUI

A short list for EldenNet's design (branch `design/v1-proposal`), from [GraceUI's design](DESIGN.md). EldenNet's codes
(DESIGN, ARCH, NPC, MEM, ER02) refer to its documents; "GraceUI §n", G1–G12 and D1–D68 to GraceUI's. Nothing here is
changed in EldenNet yet, apart from the roster, which EldenNet's design now records (its O43 as updated, O45 and O46; §9
item 1). Items marked **(G*n*)** come from that entry of GraceUI's [DECISIONS.md](DECISIONS.md) (indexed in GraceUI §12),
every one of which the owner has decided (G3 and G10 on 2026-11-07, as recommended). All of it stays minimal: one overlay, read-only towards the game, no new thread.

## 1. The main UI on F5 through GraceUI

1. **DESIGN §12 and NPC §10.1**: replace "settings (F10, safe moments only)" with "the main menu (F5, GraceUI; safe
   moments and the title screen)". The menu is drawn in EldenNet's one hudhook overlay with Dear ImGui and the Grace
   theme **(G1)**, after the world surfaces and below the toasts and the banner: GraceUI's `graceui.dll` lays it out
   and tessellates it, and EldenNet's overlay copies the vertex lists into ImGui's draw list **(G10 (b))**.
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
   first proposal's `ui.menu.names` is dropped: names are plain only (GraceUI D39).
4. **S4** (DESIGN §16.1) adds: F5 is free in the default layout; one Grace settings page drawn in game, with its frame
   cost at 1440p and on the Deck, the atlas size, and screenshots for the owner's look review **(G1)**; buffered
   DirectInput reads; the `WM_CHAR` panic reproduced; the Steam overlay browser reaching 127.0.0.1 (a hand test,
   D30); opening a URL from the game under Proton; the ABI round trip with `graceui.dll` and the cost of copying its
   vertex lists, and a menu page of more than 65,536 vertices drawing correctly through hudhook 0.9.3's per-command
   vertex offsets **(G10 (b))**.
5. **The wizard** (DESIGN §11.5) becomes GraceUI's First light in v0.6 (D16); the rollback modal and F11's debug view
   stay in EldenNet's overlay.
6. **The v0.6 increment** (DESIGN §16.2): "F10 pages" becomes "the GraceUI menu on F5 (every page but Workshop)".

## 2. The bridge (a `UiPort` in the core)

1. A **player-safe projection layer** in `en-core` (GraceUI §4.8, SP1–SP9): topics built only while watched, at most
   4 Hz each, display strings formatted in the core, published with `arc-swap`.
2. **One validating command entry point** for both views (a new bounded channel into T6; `try_send` from T3 and from
   the server).
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
   source, pinned by tag and commit **(G2)**.
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
   without blocking. Budget: encoding and parsing ≤ 0.5 ms each for the largest payload (GraceUI §8).

## 3. The local server (always on, G4)

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
7. **Deck scale (G6, decided by the owner)**: the first run on a Steam Deck writes `ui.scale = 1.35`.

## 6. Compatibility matrix (ARCH §12.4)

Add to "Other mods", each run with the menu open and closed and the dashboard on and off:

| Entry | Expected |
|---|---|
| ReShade, with and without its own overlay open | The menu draws and takes input; no input reaches ReShade's overlay while the menu holds it; any conflict documented |
| RTSS / MSI Afterburner on-screen display | The menu draws; any conflict documented (the practice tool lists RTSS as unsupported, ER02 §7.1) |
| A frame-generation or upscaler mod (DLSS or FSR frame generation) | The menu is not dropped or blacked out on generated frames (Magelight's field reports, ML §5 A); otherwise documented, with capture released after 2 s without a render |
| Driver-level frame generation (NVIDIA Smooth Motion, AMD Fluid Motion Frames) | As above |
| Another hudhook-based overlay (for example the practice tool) | Two MinHook copies in one process (ER02 §7.1): EldenNet detects the module, logs it and documents the pair as incompatible |
| Discord, Xbox Game Bar and NVIDIA app overlays | No input leak, no lost capture |
| The Steam overlay (already listed) | Shift+Tab with the menu open releases capture cleanly; the hand test of D30 |
| GraceUI missing, half-copied, older or newer than the one EldenNet came with, or for another ABI or protocol major | EldenNet plays on; the menu works or stays off with GraceUI §2.9's notice |
| `graceui.dll` listed as a native by another loader | Nothing starts |

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
   Preact, "yes") and G7 (the WIP tag) are GraceUI's alone and change nothing in EldenNet.

## 8. GraceUI as its own mod (G2)

1. **Install layout** (DESIGN §11.4): add `<game>\Game\GraceUI\` (GraceUI's folder: `graceui.dll`, `graceui.toml`,
   `web\`, `fonts\`), which EldenNet reads and never writes.
2. **Release** (DESIGN §16; ARCH §6.10): each EldenNet release zip carries the `GraceUI` folder of the GraceUI release
   current at its release date, byte for byte; the release job checks it against GraceUI's published `graceui.toml`
   and runs the handshake; the release notes name its version; a build constant records it, for the "older than the
   one this EldenNet came with" notice (GraceUI §2.7, D53).
3. **Failure contract** (DESIGN §2.7): add GraceUI §2.9's rows (GraceUI missing, mixed or for another EldenNet: the
   menu off with a notice), with EldenNet's own surfaces unaffected.
4. **Safety rules** (DESIGN §2.6): R12 unchanged, since `graceui.dll` hooks nothing; R3 for `GraceHost`'s `send` and
   `log`; R14, it is never unloaded. **R9 and ARCH §2.4**: `grace-host` needs `unsafe` in the game process (loading the
   DLL by full path, `GetProcAddress`, calls through `GraceUiApi`'s pointers, slices over borrowed vertex buffers, the
   `extern "C"` `GraceHost` callbacks, writes into ImGui's draw list), and so does `graceui::abi`. R9's in-process list
   gains `grace_host::{abi, draw}` and `graceui::abi`; `grace-host` takes `en-overlay`'s row of ARCH §2.4
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
