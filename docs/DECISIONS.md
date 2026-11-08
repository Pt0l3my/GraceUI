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
- **2026-11-08.** G13 (an interactive showcase of the menu: wait for Min's blueprint; later the same day, it is
  EldenNet's). G14 (GraceUI's scope: a UI framework other modders can use; its consequences to be put as G15
  onwards).
- **2026-11-08.** G15–G20 put to the owner with proposal 0001 (GraceUI as a UI framework for any mod); G15–G17
  decided the same day, as recommended (GraceUI owns the overlay; page data and panels; the browser view for every mod
  that opts in), which supersede G10.
- **Open.** G18–G20.

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

**Later changes.** 2026-11-08: superseded by G15 (a), decided by the owner: GraceUI owns the one overlay, so that any mod can use it without EldenNet; hudhook, its MinHook, the Present hook and the input hooks move into `graceui.dll` with every rule of EldenNet's O25, and EldenNet draws its surfaces through GraceUI. The design is folded in S7.8.c of EldenNet's roadmap (proposal 0001).

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

**An interactive showcase of the menu** · *2026-11-08 · Decided by the owner*

**Question.** The owner asked on 2026-11-08 for an interactive showcase of the menu, a page to click through in the browser like Skyrizz's, beside EldenNet's static showcase. How is it built? **Options:** (a) **From the page descriptions**: write GraceUI's page descriptions (§2.5) now as data, with a settings description in the shape EldenNet will export (D8) and invented sample data, and draw them with a small renderer of the shared components (§2.5) in plain script, into one self-contained page with GraceUI's fonts bundled (§3.4); tested in a headless browser (no request outside the page, every page at two widths, no script error, no name of the game's characters or places, the spoiler and privacy rules, contrast); published privately on claude.ai and opened from file, with no server. The page descriptions and the sample data carry over into GraceUI 0.1 whatever its code becomes; the renderer is replaced by the web app. (b) **The real web app now**: start GraceUI 0.1's browser app (Preact, TypeScript and signals with Vite, G3) against `grace-dev`'s fixtures, and publish its build as the showcase. Nothing to keep in step, but the code starts before Min's blueprint shows how much of SkyrimNet's dashboard GraceUI adopts (G12). (c) **The static mock made clickable by hand**, as Skyrizz's showcase is drawn. The quickest, but it drifts from the design wherever no test pins it.

**Recommendation.** (a). It follows the Skyrizz session's advice of 2026-11-08 (one source for the pages, fonts bundled from the start, tests as plain pass and fail lines, one file per page), keeps §2.5's rule that the page descriptions are the one source of the pages, and commits no code base before the blueprint arrives. It would live in this repository (`showcase/`), linked from EldenNet's showcase; the static mock stays until the showcase covers its pages.

**As put to the owner.** The three options above, as "From page data", "Real web app now" and "Clickable mock", and a fourth: (d) **Wait for the blueprint**: "Keep the static showcase for now and decide after Min's engine blueprint arrives."

**Decision.** (d). The owner's answer: "Wait for blueprint". EldenNet's static showcase stays as it is; how the interactive showcase is built is asked again, as a new question, once Min's blueprint has arrived and what GraceUI adopts of SkyrimNet's dashboard is known (G12).

**Follows from.** G3, G12; §2.5; DESIGN §10's GraceUI 0.1 (page descriptions, fixtures and the mock).

**Later changes.** 2026-11-08, the same day: the owner decided that the interactive showcase is part of EldenNet, not GraceUI. The owner's words: "also, I intent to make GraceUI as a standalone just a UI that is capable to present EldenNet in-game, so the interactive UI should be part of EldenNet and not GraceUI." The recommendation's "in this repository (`showcase/`)" no longer applies; when the question is asked again after the blueprint, it is asked in EldenNet. What the same words mean for GraceUI's browser view is G14.

## G14

**GraceUI's scope: in game, and the browser view?** · *2026-11-08 · Decided by the owner (the scope); its consequences are open*

**Question.** The owner said on 2026-11-08 that GraceUI is to be "a standalone just a UI that is capable to present EldenNet in-game" (G13's later change). GraceUI's design today has two views of one set of pages: the menu in game on F5, and the same pages in the player's browser at the dashboard's address while the game runs, served by EldenNet (G3, G4, G9; §2.1). Does the browser view stay part of GraceUI? **Options:** (a) **Both views stay in GraceUI**: GraceUI is the one UI that presents EldenNet, in game and in the browser while the game runs; showcases and demos with sample data are EldenNet's. (b) **In game only**: GraceUI draws only the in-game menu; the browser dashboard becomes EldenNet's own, with its own web app, and G3, G4 and G9 move to EldenNet. (c) **In game only, and no browser dashboard in v1.**

**Recommendation.** (a). One set of page descriptions drives both views (§2.5, invariant 12), so splitting the views across two repositories would write every component twice in different places and let them drift; the server is already EldenNet's (`graceui.dll` opens no socket, G10), and GraceUI already depends on nothing of EldenNet.

**As put to the owner.** "Does the browser view stay part of GraceUI?", with (a) "Both stay in GraceUI (Recommended)", (b) "In game only" and (c) "No browser view in v1".

**Decision.** None of the options as put: the owner widened the question. GraceUI is to be a UI framework that other modders can use, as Magelight UI and PrismaUI are for Skyrim; EldenNet requires it as a dependency and always ships with it. The owner's words: "I want GraceUI to be something other modders can use, like Magelight or prismaUI for SKyrim. EldenNet will require it as dependency, but it will always ship with GraceUI anyway." What this changes (whose overlay draws in game, G10; what draws another mod's pages, G1; the browser view; the interface other mods use; which pages are EldenNet's own) is worked out in a proposal and put to the owner as new questions, G15 onwards. Until they are answered, the decided design stands.

**Follows from.** G3, G4, G9, G10, G13.

**Amends.** G11 (GraceUI as EldenNet's menu and dashboard): GraceUI becomes a framework for any mod, and EldenNet's menu and dashboard become its first user.

## G15

**Who owns the one overlay** · *2026-11-08 · Decided by the owner, as recommended*

**Question.** Now that GraceUI is a framework other mods can use (G14), which module owns the one hudhook overlay, its MinHook, the Present hook and the input hooks? **(a) GraceUI** (recommended): they move from EldenNet into `graceui.dll`, with every rule of EldenNet's O25; EldenNet draws its subtitles, chat box, call-outs, labels, toasts, banner and F11 view through GraceUI like any mod, and links no hook library. **(b) EldenNet, as G10 decided**: GraceUI keeps drawing through EldenNet's overlay, and other mods can use GraceUI only while EldenNet is loaded and active, through a host API EldenNet grows later. **In plain words:** Only one copy of the overlay library GraceUI and EldenNet use can run safely in the game. Should that copy be GraceUI's, so any mod can use it on its own, or stay EldenNet's, so GraceUI only works for other mods while EldenNet runs?

**Recommendation.** (a). One process holds one hudhook and one MinHook safely, and every shared host that works in this game is one module that others call (FW F1, F2); a framework other modders use cannot depend on an AI mod that starts offline through its own launcher; your words "EldenNet will require it" then hold literally; nothing is built, so moving now costs a design pass, while moving later would rewrite EldenNet's surfaces twice. *What (a) gives up:* G10's isolation (a fault in GraceUI's host stops EldenNet's subtitles and chat as well as the menu); EldenNet cannot show anything, not even its banner, without GraceUI, so its launcher checks GraceUI first; the hook code ships in the releases players replace by hand; GraceUI's first increment must be ready for EldenNet v0.1.

**What each answer changes.** (a) amends G10 and EldenNet O25 (the pin and its rules move to GraceUI; both CLAUDE.md files change), rewrites invariant 16 as "one host, one hook set", replaces invariant 15's "EldenNet keeps playing" and amends G2's "keeps playing whatever it finds" (D88); DESIGN §2, §3.16, §5 and §8 are rewritten around the host; D69 to D72, D77, D86 and D88 apply; §5 of this proposal's items 1 to 7 and 12 go to eldennet-changes; GraceUI gains a 0.1 Host increment with EldenNet v0.1. (b) keeps G10, O25 and invariants 15 and 16 as they are; the framework is limited to EldenNet's presence; G17's recommendation becomes (b).

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.

**As put to the owner.** In plain words, with the recommended option first.

**Decision.** (a), as recommended: GraceUI owns the one hudhook overlay, its MinHook, the Present hook and the input hooks, with every rule of EldenNet's O25; EldenNet draws its subtitles, chat box, call-outs, labels, toasts, banner and F11 view through GraceUI like any mod, and links no hook library.

**Owner's words.** "GraceUI (Recommended)".

**Amends.** G10 (superseded), EldenNet O25 (the pin and its rules move with the overlay), G2 ("keeps playing whatever it finds", D88) and invariants 15 and 16, all rewritten when the design is folded (S7.8.c).

**Later changes.** 2026-11-08: by EldenNet's O60 (the owner's direction), EldenNet and GraceUI's in-game parts are expected to be written in C++, settled when Min's blueprint is adopted; hudhook (Rust) would then give way to a C++ overlay that GraceUI owns, with every rule of O25 restated for it and the owner's approval. The same day Min confirmed the blueprint is C++, so C++ is settled (O60's later change); the overlay itself is chosen at adoption.

## G16

**What other mods write, and what draws it in game** · *2026-11-08 · Decided by the owner, as recommended*

**Question.** What does a mod that uses GraceUI provide? **(a) Page data and panels** (recommended): its pages and settings described as data in GraceUI's components (drawn in game with the Grace theme and, if it opts in, in the browser), topics and commands in the bridge protocol, and, for anything else, in-game panels it draws itself as checked vertex lists, with GraceUI's drawing kit or its own headless Dear ImGui with the default vertex layout and 16-bit indices (checked in 1.89.2 and 1.92.6; other builds convert before submitting, and the checker refuses anything else); no HTML in game. **(b) Page data only**: no panels for other mods (EldenNet's own surfaces still use them). **(c) HTML pages in game**, as Magelight UI and PrismaUI offer: this reopens G1 and needs a hudhook patch (an O25 change), Ultralight's licence and about 57 MB, or another engine. **In plain words:** Magelight and PrismaUI let modders write web pages. GraceUI would instead let them describe their pages as data, which GraceUI draws in the game's style and in the browser, plus panels they draw themselves. Is that the right offer, or should mods get web pages in game?

**Recommendation.** (a). G1's reasons apply to every mod: the pinned hudhook cannot update an image without leaking memory and stalling the frame (REN §2.1). Page data shows in both views from one source and keeps other mods' code out of the browser; panels let a mod draw anything without sharing ImGui's internals, which differ between versions (FW §4); EldenNet needs panels anyway. *What (a) gives up:* modders cannot bring HTML, CSS or React; pages are limited to GraceUI's components; panels show only in game; pictures are static, so no video or live images.

**What each answer changes.** (a): D75 and D76 apply; G1 extends to every mod unchanged. (b): D76 serves EldenNet only and the guide drops panels; mods with needs beyond the vocabulary cannot use GraceUI. (c): G1 is reopened with its three approvals (licence, hudhook patch, size), invariant 11 and REN §2's findings are revisited, and S4 gains an HTML spike.

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.

**As put to the owner.** In plain words, with the recommended option first.

**Decision.** (a), as recommended: a mod provides its pages and settings as data in GraceUI's components (drawn in game in the Grace theme and, if it opts in, in the browser), topics and commands in the bridge protocol, and in-game panels it draws itself as checked vertex lists (GraceUI's drawing kit, or its own headless Dear ImGui in the default vertex layout); no HTML in game.

**Owner's words.** "Page data + panels (Recommended)".

**Amends.** G1 extends to every mod unchanged; D75 and D76 apply.

## G17

**The browser view: a GraceUI feature for every mod?** · *2026-11-08 · Decided by the owner, as recommended*

**Question.** The browser dashboard (G4, G9) shows the same pages as the menu. Who serves it? **(a) GraceUI, for every mod that opts in** (recommended if G15 is (a)): the local server and the web app move into `graceui.dll` with every rule of §7.2 and pairing; EldenNet's dashboard is its space there, always on as G4 decided. **(b) EldenNet, for its own pages only** (recommended if G15 is (b)): GraceUI keeps the web app and the server as a kit that EldenNet runs, as designed now; other mods have no browser view. **(c) EldenNet's own web app**: GraceUI is in game only, and the browser dashboard, G3, G4 and G9 move to EldenNet. **In plain words:** Should the "open it in your browser" dashboard be something GraceUI offers any mod, or stay EldenNet's alone?

**Recommendation.** (a), given G15 (a). One hub: each mod publishes once and both views show it, from one page source; no other Elden Ring host offers a browser view (FW F7). Your words ("a UI that is capable to present EldenNet in-game", "like Magelight or prismaUI") put the game first, and (a) keeps that: the browser shows a mod's pages only when the mod asks. (c) would write every component twice, in two repositories (G14's recommendation). The study behind this proposal leaned the other way, towards (b), for your in-game words and no framework-wide port; this recommendation weighs the one hub higher. *What (a) gives up:* `graceui.dll` opens a network port on the PC and runs threads, and a mod's command bug becomes reachable from the browser as it is from the game. (b) keeps, even with G15 (a), a `graceui.dll` that opens no socket, and G4's port as EldenNet's alone behind its gate; other mods' authors still get a browser test bench under either answer (`grace-dev`, D85).

**What each answer changes.** (a): G4, G9, D58 to D60 and D66 move with the server to GraceUI, and D61 stays EldenNet's, with its pages (D82); invariant 4 is GraceUI's server; D81 applies, with §7.2 item 1's gate clause kept as §4.9 of this proposal's four rules; §5 of this proposal's item 7 goes to eldennet-changes. (b): the server stays in EldenNet as designed (eldennet-changes §3 unchanged), and other mods' pages are in game only. (c): the web app, G3, G4 and G9 move to EldenNet, invariants 4 to 6 and 12 become EldenNet's, and the components are written in both repositories.

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.

**As put to the owner.** In plain words, with the recommended option first.

**Decision.** (a), as recommended: GraceUI serves the browser view for every mod that opts in, with every rule of the local server (§7.2) and pairing (G9); EldenNet's dashboard is its space there, always on as G4 decided.

**Owner's words.** "Yes, opt-in (Recommended)".

**Amends.** G4, G9, D58–D60 and D66 move with the server into GraceUI; D61 stays EldenNet's; invariant 4 becomes GraceUI's server's; D81 applies, with §7.2 item 1's gate clause kept as proposal 0001 §4.9's four rules.

## G18

**When the framework opens to other modders** · *Open since 2026-11-08*

**Question.** **(a) After EldenNet v1.0, as GraceUI 1.1** (recommended): the ABI is designed now, EldenNet uses it from GraceUI 0.1, an example mod of ours exercises it in CI, and the guide, the examples and a frozen ABI 1 are published with 1.1. **(b) With GraceUI 1.0**, at EldenNet v1.0. **(c) Early**, as a preview marked unstable from GraceUI 0.2. **In plain words:** Should other modders get GraceUI only once EldenNet 1.0 is out, at the same time, or as early as possible with a warning that it may change?

**Recommendation.** (a). EldenNet v1 is to be "deliberately minimal and stable"; a public ABI is a promise kept for years, and it is better frozen after a real user has driven it through a full release; Magelight broke its API twice before its first public release, in 0.29.0 and 0.30.0 (Magelight `CHANGELOG.md:1135-1210`, `:1290`, via the Skyrizz session, 2026-11-08). *What (a) gives up:* other modders wait about one release, and early feedback from them is lost.

**What each answer changes.** (a): §6's 1.1 Open row; the guide and examples after v1.0. (b): those move into 1.0's exit checks. (c): a preview label, a changelog of breaking changes and support questions from 0.2; the ABI's freeze waits for 1.0 either way.

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.

## G19

**SkyrimNet-derived material and other mods** · *Open since 2026-11-08*

**Question.** G12 lets GraceUI use SkyrimNet's code, dashboard and styling, as Min approved for building on SkyrimNet. If GraceUI is a framework, anything SkyrimNet-derived in it reaches every mod and player that uses it. **(a) Not until confirmed** (recommended): GraceUI keeps to its own code and the Grace theme; whatever is adopted from Min's blueprint for EldenNet's pages lives in EldenNet; once the blueprint arrives, you ask Min, if you wish, whether what GraceUI would adopt may be passed on through a public framework. **(b) Yes**: G12 covers GraceUI as a framework, credited as Min wishes. **In plain words:** Min agreed that EldenNet and GraceUI may build on SkyrimNet. Does that agreement also cover other people's mods getting SkyrimNet-derived parts through GraceUI?

**Recommendation.** (a). G12 and EldenNet O53 record Min's approval for EldenNet's foundation; neither says whether it reaches third parties, and claiming more endorsement than Min has given is ruled out; GraceUI's look is our own anyway (D3). *What (a) gives up:* the question G13 will ask after the blueprint can place adopted parts only in EldenNet's pages, not in the shared components, so the web app may take longer.

**What each answer changes.** (a): §3.17's last line is aligned with G12 by saying SkyrimNet-derived material stays out of GraceUI until confirmed; NOTICE stays free of SkyrimNet entries. (b): §3.17's line is replaced by G12's rule for GraceUI as a framework, and NOTICE carries SkyrimNet's credits and licences as Min wishes.

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.

## G20

**Where EldenNet's pages live** · *Open since 2026-11-08*

**Question.** G14 leaves open which pages are EldenNet's own once GraceUI is a framework. EldenNet's menu and dashboard content (its categories, pages, settings description, help texts and Home cards), its fixtures, the spoiler, roster and Playthrough goldens, the spoiler rules SP1–SP9, EldenNet's halves of the privacy rules PV1–PV8 and DESIGN §4: **(a) In EldenNet's repository** (recommended), as content EldenNet ships and registers with GraceUI at start; GraceUI keeps the frame, the components, the theme, PV3, PV6 and PV7 as framework rules, and a demo space with invented content (D82). **(b) In GraceUI's repository**, as its first user's bundled pages, as designed now. **In plain words:** EldenNet's own pages, and the rules that keep them free of spoilers, could move to EldenNet, so that GraceUI holds only the parts every mod shares, or stay in GraceUI as now. Which should it be?

**Recommendation.** (a). Your words "the interactive UI should be part of EldenNet and not GraceUI" (G13's later change); the spoiler rules are EldenNet's domain; a framework other modders use should not carry one user's content and its spoiler review. *What (a) gives up:* one repository for the whole information architecture; GraceUI's page lint and parity tests then run on EldenNet's pages in EldenNet's CI against a pinned GraceUI, and a change to a component and to EldenNet's pages can land in two repositories.

**What each answer changes.** (a): D82 applies; DESIGN §4, SP1–SP9, EldenNet's halves of PV1–PV8, D61, its fixtures and goldens move to EldenNet in S7.8.c; invariants 1 and 2 take their framework form in GraceUI and their EldenNet form in EldenNet; §5 of this proposal's item 9 goes to eldennet-changes. (b): DESIGN §4, SP1–SP9, the goldens and invariants 1 and 2 stay in GraceUI as they are, beside the framework rules; GraceUI's release carries EldenNet's page data as a bundled space that EldenNet's `connect` activates, and the framework carries one user's content.

**Follows from.** G14; [proposal 0001](proposals/0001-ui-framework.md) §8, with its reasons in §4 and the evidence in FW.
