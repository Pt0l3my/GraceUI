# Changes EldenNet's design needs for GraceUI

A short list for EldenNet's design (branch `design/v1-proposal`), from [GraceUI's design](DESIGN.md). EldenNet's codes
(DESIGN, ARCH, NPC, MEM, ER02) refer to its documents; "GraceUI §n", G1–G8 and D1–D50 to GraceUI's. Nothing here is
changed in EldenNet yet. Items marked **(G*n*)** depend on that Open row's answer; the rest follow from decisions
already settled. All of it stays minimal: one overlay, read-only towards the game, no new thread.

## 1. The main UI on F5 through GraceUI

1. **DESIGN §12 and NPC §10.1**: replace "settings (F10, safe moments only)" with "the main menu (F5, GraceUI; safe
   moments and the title screen)". The menu is drawn in EldenNet's one hudhook overlay with Dear ImGui and the Grace
   theme **(G1)**, after the world surfaces and below the toasts and the banner.
2. **NPC §10.6** (the F10 panel) is superseded by GraceUI §4 (information architecture): a framework's categories,
   Home, AI, Voice, Characters, History, Behaviour, System and Tools, with Settings, Docs and Content packs at the foot
   (GraceUI D38), which replace the first proposal's flavour-named sections. Point NPC §10.6 and DESIGN §12's
   "Settings" bullet to GraceUI §4.2 and §4.5, and drop NPC §10.6's out-of-date "ambient lines" and bark-density
   rows.
3. **Settings schema** (ARCH §11.5): add `hotkeys.menu = "F5"` and remove `hotkeys.settings`, with a migration (schema
   3, or an amendment to schema 2 before v0.6, because `deny_unknown_fields` refuses unknown keys). Add
   `ui.menu.{theme, plain_text, reduce_motion, key_hints, pad_glyphs, backdrop, compact_rows, show_world,
   home_cards}`, `ui.menu.show_unseen_types` **(G5)** and `dashboard.{enabled = false, port}` **(G4)** in the same
   amendment. The first proposal's `ui.menu.names` is dropped: names are plain only (GraceUI D39).
4. **S4** (DESIGN §16.1) adds: F5 is free in the default layout; one Grace settings page drawn in game, with its frame
   cost at 1440p and on the Deck, the atlas size, and screenshots for the owner's look review **(G1)**; buffered
   DirectInput reads; the `WM_CHAR` panic reproduced; the Steam overlay browser reaching 127.0.0.1 (a hand test,
   D30); opening a URL from the game under Proton.
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
   a bounded **event tap** on Augur's bus (500, topic `monitor.events`), **`types_seen`** per playthrough **(G5)**,
   the **model call log** as a projection, a session-only **Pause AI** command (D15), a **subtitle preview** command.
   For GraceUI's framework categories (GraceUI §6.5): projections `selftest` (the self-test's state and figures),
   `usage` (this session's calls, tokens and own-initiative calls against the bound), `shortlived` (perceived TTL
   events with time left), `errors` (the last 20 failures in words, scrubbed), `memories` (met NPCs per journey) and
   `conversation/<id>` (displayed lines); `tidings` renamed `world`; the diagnostics view split into
   `system.status`, `system.performance`, `traces` and `content`; `health` also carries the prepared-line pools'
   state, and `named` each met persona's quest-business readiness word (none for unmet personas, SP5). `kinds`
   carries near-now counts of perceived characters only (SP7): a hidden character, a boss before its bar or an
   unannounced invader changes no count.
5. **Crate rules** (ARCH §2.3): `en-core` may depend on `grace-protocol`; `en-overlay` on `grace-protocol`,
   `grace-view` and `grace-imgui` (which take `imgui` only through `hudhook::imgui`, as O25 requires); `eldennet` on
   `grace-serve`. `cargo deny` allows GraceUI's git source, pinned by tag and commit **(G2)**.
6. **The scheduler**: while the menu is open, lines addressed to the Tarnished wait as they do while the chat box is
   open; barks and NPC-to-NPC lines continue (D34).
7. **A `seen` bit on stored events** (MEM's `events` table): set when the Tarnished perceived the event, by the live
   tap's SP7 rule, so History › Events can list a journey's record without the events the player never saw. The event
   log's retention is unchanged. If EldenNet declines it, History › Events shows only this session's tap.
8. **A read-only, paged `history.page` query** over a journey's events and conversations (100 rows a page, journeys
   of the loaded playthrough only, refused at the title screen), built in the projection layer with SP2, SP3 and
   SP7 applied, on T6 and never on the present thread (GraceUI §6.6).

## 3. The local server (opt-in)

1. `grace-serve` runs as tasks on the Net runtime (T7–T8). ARCH §1.2's "no other thread is created" still holds. Off
   means no socket.
2. **C4 wording** (DESIGN §1.3): serving 127.0.0.1 is not a model request; the dashboard keeps serving while EldenNet
   is dormant, with live views paused.
3. **Privacy** (DESIGN §14): the server's token never reaches a log, a report or a recording; request lines are logged
   without fragments or query strings; PV1–PV8 join the bug-report scan's rules.
4. **F11** gains "Copy dashboard link" and "Open in browser" from v0.2, until the menu ships (G4).

## 4. The overlay (en-overlay)

1. **`WM_CHAR` fix, before v0.2** (it affects the chat box too): hudhook 0.9.3 panics on a lone surrogate. Take
   `WM_CHAR` and `WM_UNICHAR` in `before_wnd_proc`, pair surrogates, feed ImGui in `before_render`.
2. **Pad input**: feed XInput state to ImGui's navigation while the menu holds input.
3. **IME**: position the OS composition window at the focused field through ImGui's platform IME hook and IMM.
4. **Atlas**: rebuild on a scale change only while the menu is closed, at most four times a session, logged (D29).
5. **Capture**: one close routine shared with the chat box; neutralise buffered DirectInput reads; confine the OS
   cursor and restore its previous clip; never force the OS cursor's visibility; a lost-keys fallback that logs where
   focus was; log the present layers recognised at startup, by module name.
6. **Menu fault isolation**: a panic in the menu turns off the menu alone for the session.

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
7. **Deck scale (G6)**: if accepted, the first run on a Steam Deck writes `ui.scale = 1.35`.

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

And: Proton × the menu × IME × the Steam keyboard; the Deck in game mode (menu only) and desktop mode (the dashboard
too); HDR × the menu (the ceiling and the opaque backdrop, D26); a non-ASCII install folder and Windows user name;
binding 127.0.0.1 raises no firewall prompt on Windows.

## 7. Small fixes found on the way

1. ARCH §11.5's example sets `greet_npc = false` under `[allow."card:vulgar_militia"]`, but `greet_npc` is a reaction
   id; the allowance keys are `npc_to_npc` and `reactions = { greet_npc = false }` (IA §12.1).
2. DESIGN §7.6 lists every type whether or not it has been seen; see G5. Its page also lists every persona slot,
   met or not; see G8 (if accepted, unmet personas are hidden until met and the `named` kind row carries their
   allowances).
3. DESIGN §7.6's page line ("named slots first, then kinds and their types") gains each kind's own row ahead of its
   slots or types, the `named` kind's included (`[allow."named"]`, also `merchant` and `summon`), so every inherited
   source an allowance names has a row of its own to open (GraceUI §4.6.4). Under either answer to G8.
4. Record G1–G6 and G8 in DESIGN §19 as Open rows when EldenNet takes these changes up, with GraceUI's recommendations.
   G7 (later-version entries in the sidebar) is GraceUI's alone and changes nothing in EldenNet.
