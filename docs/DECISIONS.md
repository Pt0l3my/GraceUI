# Owner decisions

The project owner's decisions on GraceUI, binding, one entry each, numbered G1 onwards. This file is the one record
of them: [DESIGN §12](DESIGN.md#12-decisions-for-the-owner) is its index, and every G-number GraceUI's documents (and
EldenNet's) cite is an entry here. What the designers settle themselves stays in DESIGN §13, the decision log (D1
onwards). EldenNet's own decisions (O1 onwards) are in EldenNet's `docs/DECISIONS.md`; what each G-answer changes in
EldenNet is in [eldennet-changes.md](eldennet-changes.md).

- **Ids.** An id is never renumbered or reused. G1–G10 are the questions DESIGN §12 put to the owner, in its order.
  G11 and G12 are owner decisions recorded outside §12's table (in DESIGN §14 and in CLAUDE.md); they take the next
  ids. G13 onwards are later questions.
- **An entry** gives the date and status, the question as it was put (with its options), what was decided, the
  owner's own words, and what it amends. The recommendation as it was written and its reasons stay inside the
  decision, under their own italic labels. A bare section sign (§2.9, §7.2) means a section of GraceUI's DESIGN.md;
  "EldenNet O25" or "EldenNet DESIGN §7.8" points into EldenNet; the research notes are cited by their short names
  (REN, ML, IA, LOOK, SND).
- **Changes.** A later decision may widen or supersede an earlier one, and says so under **Amends**. The earlier
  entry keeps its text as decided, with a dated note under **Later changes**; the question stays as it was put.
- **A new question** takes the next free id: an entry here marked **Open**, with the question, its options and a
  recommendation, and a row in DESIGN §12's index. The owner's answer is recorded in the entry with its date and
  their words, and the index row is updated. When it changes what EldenNet must do, it is added to
  eldennet-changes.md too.

## By date

- **2026-11-07.** G11, the request that started GraceUI. G1 (the in-game renderer). G2 and G4–G9 (a standalone mod,
  the dashboard always on, the roster from the start, the Steam Deck's scale, the WIP tag, the Playthrough page,
  pairing for other devices). G3 and G10 (Preact; the overlay stays in EldenNet), with the roster's widening by
  EldenNet's O43 as updated and O45. G12 (building on SkyrimNet). G1–G6 were put with the first proposal, G7 with
  the framework categories (D38), G8 after their review, and G9 and G10 with the owner's answers to G4 and G2.
- **2026-11-08.** G13 put to the owner (an interactive showcase of the menu).
- **Open.** G13.

## G1

**The in-game renderer** · *2026-11-07 · Decided by the owner, as recommended*

**Question.** Which renderer draws the menu in game? **Options:** (a) **Dear ImGui with a native Grace theme** in EldenNet's one hudhook overlay; the full web UI in the browser. (b) **Ultralight in CPU mode** (the engine Magelight uses): the same HTML pages in game, uploaded as a texture through a reviewed patch to hudhook. (c) Ultralight with a Direct3D 12 GPU driver of our own. (d) CEF (Chromium) off-screen. (e) RmlUi, drawn as ImGui geometry. (f) Blitz or Servo.

**Decision.** (a). The browser keeps the full HTML pages. *Recommendation as written:* (a) for v1, with (b) kept as a throwaway spike after v1.0 if the owner wants the same HTML pages in game. *Why:* hudhook 0.9.3, pinned by O25, can show a changing web page only by leaking a new upload buffer on every update (about 15 MB at 1440p) and stalling the present thread, and it gives no access to the GPU, so (b), (c), (d) and (f) all need a change to the pinned library (REN §2). (b) also needs Ultralight's proprietary licence (a public free mod rests on its limited commercial grant), about 57 MB of runtime, an engine with a 60 FPS cap and no evidence under Proton; Magelight, the closest precedent, spent 17 of its 27 written invariants on its web engine (ML §2). (c) adds a GPU driver of our own for a still menu. (d) is about 250–300 MB with helper processes and a mixed Wine record. (f) are not ready (beta, monthly breaking changes). (e) fits O25 and is the fallback if ImGui proves too plain, but its pages are not the browser's. (a) adds no licence, binary or hook, uses the Proton path EldenNet must make work anyway and needs no texture updates. *What (a) gives up:* the in-game view is not the browser's HTML; it is drawn from the same page descriptions, tokens and protocol (§2.5), so it shows the same pages, but typography is simpler (no kerning or OpenType features, §3.16) and each component is written twice. The owner can judge the look before deciding: S4 produces in-game screenshots of a Grace page beside the browser mock. Choosing (b) later needs three approvals: the licence, the hudhook patch (an O25 change) and the size.

**Owner's words.** "let's use what's best compatibility with Elden Ring game engine", after weighing the lack of HTML in game.

**See also.** EldenNet O25 (the hudhook pin), which rules out (b)–(d) and (f) without a change; G10 keeps the one overlay in EldenNet.

## G2

**GraceUI as a standalone mod** · *2026-11-07 · Decided by the owner; the recommendation was (a), a library*

**Question.** Is GraceUI a library inside EldenNet's DLL, or a standalone host DLL other Elden Ring mods can use? **Options:** (a) **A library**: GraceUI's crates and web bundle linked into `eldennet.dll`. (b) **A standalone host DLL** with an API for other mods, now. (c) (a) now, and a host later if other mods ask, grown from the bridge.

**Decision.** A standalone mod. So GraceUI has its own releases and one folder of its own in the game's `Game` folder, with a version file (§2.7, D51–D53); each EldenNet release carries the GraceUI release current at its release date; a player updates GraceUI on its own by copying a newer release's folder over the old one with the game closed; EldenNet checks what it finds at every start and keeps playing whatever it finds (§2.9, D54). The answer leaves two things to the design: which module owns the one overlay (G10, since decided: (b)), and whether other mods may use GraceUI, which v1 still leaves out (§1.3). *Recommendation as written:* (a), with (c)'s path noted, because one process holds only one hudhook overlay and one MinHook safely (ER02 §7.1) and being a host cost Magelight a versioned C ABI, a registry per mod, input-mode ownership, a sandbox, version gates and handling for mixed copies (ML §7). *What the decision costs:* a versioned C ABI with a handshake, the bridge's frames carried in process as they are to the browser (D55), the version file, and handling for a missing, older, newer or mixed copy: the ABI and version parts of Magelight's list, without its registry, sandbox or input arbitration, because GraceUI serves EldenNet alone.

**Owner's words.** "GraceUI will be a standalone mod, but EldenNet will feature the latest version with each release (at the release date) as built in, but users will be able to update GraceUI separately (by dragging files into Elden Ring/Game directory and overwriting old files)."

**Later changes.** G10 (2026-11-07) settles which module owns the one overlay: (b), EldenNet.

## G3

**The browser app's framework** · *2026-11-07 · Decided by the owner, as recommended*

**Question.** Which framework for the browser app? **Options:** (a) React. (b) **Preact** with TypeScript and signals, built with Vite. (c) Svelte 5. (d) Solid. (e) Plain web components (or Lit)

**Decision.** (b), as recommended. The browser app is Preact with TypeScript and signals, built with Vite (§7.5). *Why (as recommended):* under G1 the web app runs only in desktop browsers, so the choice is about authoring and upkeep. Preact keeps React's component model and hooks, which people and AI assistants know best, at about a tenth of React's size; signals suit live topics that change four times a second without re-rendering pages. Its small parse cost and an es2022 target keep the door open to Ultralight's interpreter-only JavaScript if G1 ever changes (REN §4.2). Svelte 5 changed its syntax recently, so examples and generated code mix two dialects; Solid has a smaller community; web components need hand-written state handling. Licences (Preact MIT, Vite MIT) are read again at adoption and listed in NOTICE.

**Owner's words.** "G3: yes".

## G4

**The browser dashboard, always on** · *2026-11-07 · Decided by the owner; the recommendation was (a), opt-in*

**Question.** Is the browser dashboard on by default? **Options:** (a) **Off by default; opt-in** in Settings › Browser view (from v0.6), in First light, or with `dashboard.enabled = true` in `settings.toml` before v0.6. (b) On by default.

**Decision.** (b), always on. EldenNet serves the dashboard from the end of init's gate and safety steps to exit, unless anti-cheat is found later (§7.2 item 1); the player opens `http://127.0.0.1:8642` (D58) in a browser on the same PC and types or copies nothing else; the address is shown in game (Settings › Browser view, Home › Status, F11) and on the launcher's first screen (§7.2). Host, Origin and Fetch Metadata checks stay; the per-session token in the link gives way to a session cookie granted to any visit of the address (D60), and a changed provider address clears its key (D61). A switch can still turn the server off (D59). Other devices on the home network get in only when the player switches that on, and each only after entering a pairing code shown in game (G9). *Recommendation as written:* (a), because a listening port is attack surface even when hardened (§7.1) and Magelight moved network access to opt-in before its 1.0 (ML §3.16); §7.2's checks are what keep that surface small now.

**Owner's words.** "Browser dashboard will work the same way as SkyrimNet's, open the IP:port of the dashboard, always on."

**Later changes.** G9 (2026-11-07): other devices on the home network only behind a switch, each after pairing with a code shown in game.

## G5

**NPC types listed from the start** · *2026-11-07 · Decided by the owner; the recommendation was (b)*

**Question.** NPC types: are types listed before the player has seen one? **Options:** (a) Every type listed, as EldenNet DESIGN §7.6 and O38 describe. (b) **Base kinds always listed; cards, creature families and other named slots listed once the Tarnished has seen one**, recorded per playthrough (`types_seen`), with a "show types I haven't met (may spoil)" switch.

**Decision.** (a), every type listed from the start. So NPC types lists every kind and type from the start, each with its label, its tags and a general profile: only what anyone would see on a first meeting with one, in our own words, reviewed for spoilers like shipped content; never a boss's name, and no tag names a place or a quest. This is a deliberate exception to invariant 2 and SP5 for the roster only (SP4, SP5). `ui.menu.show_unseen_types` and `types_seen` are dropped. What this playthrough has reached is on the new Characters › Playthrough page (§4.6.4, D63–D65), where invariant 2 holds in full. *Recommendation as written:* (b), because a full list names late-game creatures; the first-meeting rule and the spoiler review answer that now.

**Owner's words.** "we will feature a full roster with character biographies, so they will be part of EldenNet's database already." And, for G5 and G8 together: "G5 and G8: we can have a page that track playthrough, shows met NPCs and their status, their quest stage, etc. that could be useful. Otherwise for general character profiles, we can have them all already shown."

**Amends.** Invariant 2 and SP5, by a deliberate exception for the roster only; `ui.menu.show_unseen_types` and `types_seen` are dropped.

**Later changes.** *Widened the same day (EldenNet O43, as updated):* the roster's named slots include every named boss, by its first-meeting name (D67); type labels and tags still name no boss.

## G6

**A larger text scale on the Steam Deck** · *2026-11-07 · Decided by the owner, as recommended*

**Question.** Should the Steam Deck default to a larger text scale? **Options:** (a) `ui.scale` 1.0 everywhere, as now. (b) **On a Steam Deck, the first run writes `ui.scale = 1.35`**, detected through Steam's own `IsSteamRunningOnSteamDeck` in the already-loaded `steam_api64.dll` (the DLL O11's keyboard uses), shown and changeable in First light. (c) Ask in First light, with no detection.

**Decision.** (b), as recommended. *Why (as recommended):* at 1.0 on a 7-inch 1280 × 800 screen, menu text renders at about 11 device px and subtitles at about 22; at 1.35 menu text is about 15 px and the menu uses its drawer layout. `ui.scale` is one scale for all of EldenNet's text (NPC §10.7), so this changes the subtitles too, which makes it the owner's call.

**Owner's words.** "that's okay."

## G7

**Later-version features in the sidebar: "WIP"** · *2026-11-07 · Decided by the owner, as recommended, with the tag named "WIP"*

**Question.** Do later-version features appear in the sidebar? **Options:** (a) Hidden until they ship. (b) **Dimmed entries with a "v1.5" or "later" tag, each opening a statement page with no controls**: Voice › Text-to-speech and Speech-to-text, AI › Director, Behaviour › Acts; never for features that are not planned. (c) One "Coming later" page.

**Decision.** (b), with the tag "WIP". The four entries (Voice › Text-to-speech and Speech-to-text, AI › Director, Behaviour › Acts) are dimmed, carry a small "WIP" tag whose tooltip and accessible name say when ("Work in progress: arrives in v1.5" or "Work in progress: a later version"), and open their statement pages, which stay; AI › Models' dimmed Decisions card carries the same tag. Settings rows keep their availability badges ("Takes effect in v1.5", "Later version"), which say when a setting starts to work (§4.7, D62). *Why (as recommended):* it keeps the framework's categories recognisable to players who know SkyrimNet's (SND §1.1), tells players what is coming without controls that do nothing (§4.1 principle 3), and costs four rail entries; diaries, thoughts and scene memories stay one-line notes on their pages. A statement names a feature of EldenNet, never a part of the game, so it is no spoiler hint (SP5). (a) leaves Voice with one page and hides why there is no speech; (c) puts unrelated features in one place.

**Owner's words.** "agreed. but call it WIP."

## G8

**Every named NPC's general profile shown from the start, and the Playthrough page** · *2026-11-07 · Decided by the owner; the recommendation was (b)*

**Question.** Are unmet published personas listed? **Options:** (a) Listed by their pre-reveal name, Allowances only, on Named NPCs and NPC types, and counted on Content packs (today's D12). (b) **Hidden on Named NPCs, NPC types, search and counts until met**, unless "Show characters and types I haven't met (may spoil)" (G5's switch, widened) is on; their allowances stay reachable through the `named` kind's own row on NPC types.

**Decision.** Every named NPC's general profile is shown from the start, met or not, and a new page tracks the playthrough. So Characters › Named NPCs lists the whole roster from the start, each by the name the game uses at a first meeting (the pre-reveal fallback, SP3), with its general profile, its Allowances and its Reactions. A general profile holds only first-meeting knowledge (who they appear to be on a first meeting, in our own words, reviewed for spoilers like shipped content): never a stage, fate, later form, reveal, quest step or relation that the story discloses later. This is the same roster exception to invariant 2 and SP5 as G5's. The new Characters › Playthrough page (§4.6.4, D63–D65) shows, for this playthrough only, the NPCs the Tarnished has met, each with their status as the game showed it, where they were last met, the line for the stage of their own story the Tarnished last saw, related stories as reached and the world's stage, with "since you last spoke" notes; invariant 2 and SP1–SP9 apply there in full. Named NPCs without a shipped general profile still appear once seen (SP4), and bosses and invaders stay unnamed until the game names them, because a boss's name is itself a reveal. Putting the right profile into each request is EldenNet's prompt assembly as designed (EldenNet DESIGN §7.7); a page never shows the prompt's private fields. D12 is superseded. *Recommendation as written:* (b), hiding unmet personas until met, because a greyed list of strangers is a hint even when the cast is published; the first-meeting rule answers that now.

**Owner's words.** "Same as G5, we will have them all ready, and the framework will inject correct NPC profile into the request payload prompt." And the follow-up quoted in G5: "G5 and G8: we can have a page that track playthrough, shows met NPCs and their status, their quest stage, etc. that could be useful. Otherwise for general character profiles, we can have them all already shown."

**Amends.** D12 is superseded; the same roster exception to invariant 2 and SP5 as G5's.

**Later changes.** *Widened the same day by the owner's answers to EldenNet's roster questions (EldenNet O43 as updated, O45):* every named boss and every named invader with a shipped bio is in the roster from the start, by its first-meeting name, with a first-meeting general profile (D67); bosses and invaders without a bio still stay unnamed until the game names them.

## G9

**Other devices on the home network: a switch and pairing** · *2026-11-07 · Decided by the owner, as recommended*

**Question.** May other devices on the home network open the dashboard? **Options:** (a) **No**: this PC only (loopback), as G4's answer is built now. (b) **A switch, off by default** (`dashboard.lan`): the server also listens on the PC's home-network address, and each device pairs once with a code shown in game, never without one. (c) **A host field**, as SkyrimNet has: the server listens on whatever address the player types, with no pairing. **As put to the owner:** "the same as SkyrimNet by default, always on and opened at its address on your PC. Opening it from other devices would be a setting you switch on, and then a device must enter a short pairing code shown in game before it gets in."

**Decision.** (b). So the dashboard serves this PC only until the player switches `dashboard.lan` on, a device on the home network gets in only after entering a short pairing code shown in game, and use on this PC never needs a code. *How it works* (§7.2 item 16): `dashboard.lan` (off); when on, the server also listens on the PC's private network addresses only (never a public address, never every interface), and Windows asks once whether the game may accept connections on private networks; Host must be one of those addresses with the port, so DNS rebinding still fails; "Pair a device" in game shows a six-digit code for two minutes, one use, void after five wrong tries; a paired device keeps a random device cookie until the player forgets it, stored only as a hash in a file of its own in EldenNet's config folder (`devices.toml`, never `settings.toml`) and listed with its name and the date it paired; Forget revokes it at once and closes its connections; a paired device cannot set or clear keys or change providers, so a key is never typed over the network; Docs say that the home network carries the page in plain HTTP. *SkyrimNet, for comparison*, in answer to the owner's "G4: how does SN handle that issue?" (from the owner's screenshots and the sandboxed run of its dashboard, SND): its Settings › Dashboard › Web Server has an on switch, a host field that defaults to 127.0.0.1 and a port (8080, with 8081 for a legacy browser UI). Its page opens at that address with no link, sign-in or code. Other devices reach it only if the player changes the host to a network address, and we saw no login or pairing setting. We did not run its game-side server, so whether it checks Host or Origin is not known. *Recommendation as written:* (b), because the owner's "open the IP:port" fits a phone or a second screen beside the game, while on a network address Host and Origin checks no longer tell the player from anyone else on the network, so (c) without a pairing step would let any device on it read chat text and change settings; (a) needs nothing more; (c) is the least work and the least safe.

**Owner's words.** "Yes, approved."

**See also.** G4, whose answer raised it.

## G10

**Which module owns the one overlay** · *2026-11-07 · Decided by the owner, as recommended*

**Question.** Now that GraceUI is a mod of its own (G2), which module owns the one overlay? **Options:** (a) **GraceUI is the UI host**: hudhook, its MinHook and the Present hook move into `graceui.dll`; EldenNet draws its subtitles, chat box, call-outs, labels, toasts, banner and F11 through GraceUI's API, and its input detours go through GraceUI's MinHook. (b) **EldenNet keeps the overlay and loads `graceui.dll`**, which links no hudhook, ImGui or MinHook and hooks nothing: it lays out and tessellates the menu, and EldenNet checks its vertex lists and copies them into ImGui's draw list (§2.9). (c) **GraceUI's code stays linked into `eldennet.dll`**, and only its files (the web app, fonts, Docs) can be replaced by hand.

**Decision.** (b), as recommended. EldenNet keeps hudhook, MinHook, the Present hook and its own surfaces; `graceui.dll` links no hudhook, ImGui or MinHook, installs no hook, starts no thread and draws through EldenNet's overlay as vertex lists that `grace-host` checks (§2.9, D56). *Why (as recommended):* exactly one module may own hudhook, the Present hook and MinHook (EldenNet O25; ER02 §7.1). *The owner's "standalone mod":* (a) and (b) both give GraceUI a DLL and a folder of its own that a player can replace; (c) cannot update the in-game menu without an EldenNet release. *ImGui's one context and allocator:* ImGui keeps its current context and its allocator per copy of the library, so two DLLs drawing into one context need the same version, build and allocator handed across, and a hudhook build inside `graceui.dll` would bring a second MinHook; (b) keeps ImGui out of `graceui.dll`, while (a) makes every EldenNet surface call ImGui through GraceUI. *Rust and the C ABI:* Rust types, allocators and panics do not cross a DLL boundary, so either way the contract is `repr(C)` tables, borrowed buffers and caught panics; (b)'s is frames in and vertex lists out, one call a frame, while (a)'s covers every surface and hook of EldenNet's. *Crash isolation:* one process either way, so a hard fault in either DLL still ends the game; under (b) a caught panic or a malformed vertex list turns the menu off and subtitles, chat and call-outs keep drawing, while under (a) a fault in the host takes them too. *The hudhook pin:* (b) keeps it, R12's detours and every O25 rule in EldenNet; (a) moves them into GraceUI's releases, which players replace by hand. *Updates by file:* (b)'s update can change the menu's layout, drawing and pages but never a hook; (a)'s can change the hooks. *Other mods later:* (a) is the natural host for them; (b) can grow a host API later without moving the overlay; none is in v1 (§1.3). *What (b) costs:* a vertex-list contract that `grace-host` checks, GraceUI's own text field in place of ImGui's (D57), glyph tables handed over from EldenNet's atlas, and topics encoded in process (≤ 0.5 ms of T6 per revision, §8). The design follows (b) throughout.

**Owner's words.** "G10: approved".

**See also.** G2, whose answer raised it; EldenNet O25, every rule of which stays in EldenNet.

## G11

**The owner's request that started GraceUI** · *2026-11-07 · The owner's request; outside §12's table*

**Question.** None was put: the owner's request on 2026-11-07, which started this repository, recorded in DESIGN §14 (Sources).

**Decision.** A separate repository named GraceUI; a layout like SkyrimNet's dashboard; the appearance of SeverActions' menu (built on Magelight UI); Elden Ring tabs and settings; earlier the same day, a main EldenNet UI on a hotkey such as F5. Later the same day, on the first proposal: the layout was not yet like SkyrimNet's, and EldenNet is a framework, not a game plugin, so the menu should hold categories of an AI framework, taken from how SkyrimNet groups them, with what Elden Ring's version needs.

**Recorded in.** DESIGN §14 ("The owner's request"); the design answers it in D5, D38 and D39 (D4 is superseded by D39). The look is our own, in SeverActions' spirit (CLAUDE.md, Licences). For the research, the owner also asked that SkyrimNet's dashboard be rendered in a sandbox (EldenNet O52, superseded by G12).

## G12

**SkyrimNet: building on Min's work** · *2026-11-07 · Decided by the owner; outside §12's table*

**Question.** None was put: the owner's decision of 2026-11-07, recorded in EldenNet's CLAUDE.md that day as EldenNet O53, and in this repository's CLAUDE.md as it binds GraceUI.

**Decision.** SkyrimNet's author, Min, gives us the engine blueprint to work with and approves building on SkyrimNet, so its code, dashboard and styling may be used, copied and adapted, credited as Min wishes and with every licence and notice file that comes with it. Never present GraceUI as SkyrimNet itself or claim more endorsement than Min has given.

**Recorded in.** CLAUDE.md, Licences ("SkyrimNet (owner's decision, 2026-11-07, as in EldenNet's CLAUDE.md)"); EldenNet's `docs/DECISIONS.md`, O53, holds the full entry.

**Supersedes.** For GraceUI, the sandbox-only study of SkyrimNet's dashboard (EldenNet O52): structure only, the bundle's source never read.

## G13

**An interactive showcase of the menu** · *Open since 2026-11-08*

**Question.** The owner asked on 2026-11-08 for an interactive showcase of the menu, a page to click through in the browser like Skyrizz's, beside EldenNet's static showcase. How is it built? **Options:** (a) **From the page descriptions**: write GraceUI's page descriptions (§2.5) now as data, with a settings description in the shape EldenNet will export (D8) and invented sample data, and draw them with a small renderer of the shared components (§2.5) in plain script, into one self-contained page with GraceUI's fonts bundled (§3.4); tested in a headless browser (no request outside the page, every page at two widths, no script error, no name of the game's characters or places, the spoiler and privacy rules, contrast); published privately on claude.ai and opened from file, with no server. The page descriptions and the sample data carry over into GraceUI 0.1 whatever its code becomes; the renderer is replaced by the web app. (b) **The real web app now**: start GraceUI 0.1's browser app (Preact, TypeScript and signals with Vite, G3) against `grace-dev`'s fixtures, and publish its build as the showcase. Nothing to keep in step, but the code starts before Min's blueprint shows how much of SkyrimNet's dashboard GraceUI adopts (G12). (c) **The static mock made clickable by hand**, as Skyrizz's showcase is drawn. The quickest, but it drifts from the design wherever no test pins it.

**Recommendation.** (a). It follows the Skyrizz session's advice of 2026-11-08 (one source for the pages, fonts bundled from the start, tests as plain pass and fail lines, one file per page), keeps §2.5's rule that the page descriptions are the one source of the pages, and commits no code base before the blueprint arrives. It would live in this repository (`showcase/`), linked from EldenNet's showcase; the static mock stays until the showcase covers its pages.

**Follows from.** G3, G12; §2.5; DESIGN §10's GraceUI 0.1 (page descriptions, fixtures and the mock).
