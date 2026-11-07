# GraceUI mock: the framework layout in the Grace theme

A static HTML and CSS mock of EldenNet's main menu for the owner's review. Version 2 follows the framework
categories (the information architecture rework of 2026-11-07): a collapsible sidebar of categories with sub-pages
(Home, AI, Voice, Characters, History, Behaviour, System, Tools; Settings, Docs and Content packs at the foot), a slim
top bar, and pages built from the same few parts (a header with a help mark and a one-line description, tabs,
collapsible sections with uppercase gold labels, stat tiles, filter bars, data tables and a Related card). It is
drawn in the Grace theme (DESIGN §3). It is not product code: the web app is a separate build (DESIGN §2, G3), and in
game the menu is drawn with the same tokens through EldenNet's one overlay (G1).

Open `index.html` in any browser. No server is needed and nothing is fetched from the network.

## The pages

| File | Page | What it shows |
|---|---|---|
| `index.html` | **Home** | Header actions Pause AI, Run self-test and Customise. **Setup** (collapsible, full width): Model (provider and key status, preset, Test with its verdict), Voice and Microphone (both "arrives in v1.5", with a link). A grid of cards: **Status** (state word, "you can talk now", one dot per model slot, version, game build, session time, the dashboard's address), **Nearby** (perceived characters, state words, wounds in words, a met NPC's disposition, one veiled character), **Happening now** (short-lived events with a time-left bar), **Problems** (one warning with its fix and a link), **Lines** (displayed lines and conversation notices), **Recent events** (perceived events in our words, with a family pill), **Model calls** (slot, purpose, speaker, model, timings, outcome, in AI › Requests' words; this hour's own-initiative calls against the bound). Cards are placed in this order with no packing, so the visual order and the focus order agree |
| `ai-providers.html` | **AI › Providers** | Tabs Providers and Health. The advised preset with "Use this"; one card per provider (cloud or local and its flavour, address, a write-only key with its status in a masked text field, requests at once with its default, JSON answers as Off · Object · Schema, Advanced); the local servers found; Health per provider with its error class in words |
| `ai-models.html` | **AI › Models** | Preset select with Apply and Reset to preset; advice for this machine with the free-model warning; a notice for a slot that differs from the preset; one card per job slot (Conversation, Quick lines, Prepared lines, Memory, Self-test) with provider, model, fallback, health and the fixed budgets; prepared-line batches and "Prepare lines now"; Decisions as a dimmed card tagged "WIP" (G7) |
| `characters-types.html` | **Characters › NPC types** | Search (no "may spoil" switch: every type is listed from the start, G5), Reset page and kind chips; the master table in EldenNet DESIGN §7.6's order (each kind's own row, such as "Named, every NPC", followed by its slots or types; boss, invader and mount, whose only type is the kind, one row each; then the locked Unknown row); the chosen type's allowances as tri-states with the inherited value and its source, linked to the source's own row, availability badges, changed lozenges with reset, and Acts locked "later version" |
| `characters-named.html` | **Characters › Named NPCs** | A roster (Met; Not met, by first-meeting name with a short first-meeting description; Seen with "no lines"), then one NPC with tabs **Profile** (the general profile, their way with news, a line linking to Playthrough), **Allowances** and **Reactions** (the slot's tri-states, shortcuts into NPC types marked "lives in …", each inherited source linked to the Named kind's row; no cooldowns: those are the same for everyone and live on Behaviour › Reactions). The journey selector shows in the top bar |
| `characters-playthrough.html` | **Characters › Playthrough** | Tiles (people met, stories seen, spoken with this session); World now; filters (names, status, region, sort); the people met with status and last met, their story as last seen and, on screens 1600 px wide and more, the first "since you last spoke" note; the chosen NPC's status, story, related story, what they have heard and notes, with links to the general profile and Memories (G5, G8) |
| `history-memories.html` | **History › Memories** | The page switch "Named NPCs remember"; tiles (NPCs met, keepsakes, memory work waiting); filters (NPC, basis, sort, direction; no free-text search, D21); the chosen NPC's journal card with the facts EldenNet counts, in words; Forget Idrel and Forget this journey at the right in red; the keepsake table (when, NPC, keepsake, weight in number and word, basis, about, and the one protected keepsake); memory work and backups (no paths); "Diaries and scene memories: later version" |
| `behaviour-speaking.html` | **Behaviour › Speaking up** | Who speaks unasked; greetings; quest business with a readiness word per met named NPC; news; between NPCs; autonomy with its period and bound; calm remarks. Reaction rows are shortcuts marked "lives in Behaviour › Reactions", drawn as there (a switch and a cooldown slider at EldenNet's defaults); talk per window and the overheard-lines range are shortcuts to Pacing |
| `behaviour-pacing.html` | **Behaviour › Pacing** | Presets Quiet, Default and Lively; "What this means" in words; Gaps, Window and Reach as full-width sliders with the value in gold and the default in the help line |
| `voice-tts.html` | **Voice › Text-to-speech** (WIP: v1.5) | A statement page with no controls (G7): what v1.5 adds, what never comes, and a read-only summary of each kind's voice allowance linking to NPC types |

Every other sidebar entry is a placeholder ("Not in this mock" on hover). Later-version entries (Director, Speech-to-
text, Acts) are dimmed with a "WIP" tag whose tooltip says when (G7).

Review states can be opened by hash: `index.html#quick` (the quick switches), `#theme`, `#live`, `#help` (the help
dialog of any page), `#collapsed` (the icon-only sidebar), `ai-providers.html#health`,
`characters-named.html#allowances` and `#reactions`.

## Placeholder data

Everything shown is invented and written for this mock: the character Brannoc, the region Mistfold Downs, the named
NPCs (Idrel, Wenna, the hooded pilgrim, Old Tamsin and the rest), the places, lines, keepsakes, world lines, type
labels and model names (`chat-medium-it` and the like). OpenRouter and the four local servers are named because
EldenNet's design names them (its Cloud budget preset and its local probes). No game text, names, icons, fonts, images
or button art are used. The state is early game: one region, two named NPCs met, two world lines that hold.

The mock follows the spoiler and privacy rules (DESIGN §4.8, §4.9): only what the player has reached is shown or
counted; absence is silent; the veiled look appears only for a perceived character whose name the game has not shown;
every type and every named NPC's general profile is listed from the start in first-meeting words (G5, G8); story
lines appear only on Playthrough, for the stage the player last saw; quest business shows a readiness word only. A
provider's address appears on AI › Providers only; keys are write-only. Chat and model text appear only on Home's
Lines and on Memories (Model calls shows no text in this mock).

## Fonts

The intended faces are OFL fonts, to be bundled later from their publishers with their `OFL.txt` (DESIGN §3.4):
Cinzel for display, EB Garamond for sidebar items, titles and prose, Noto Sans for UI text, and Noto Sans Mono for
numbers and key caps. The mock bundles none and requests none: `grace.css` names each intended face first, and it is
used only if installed on the reviewer's machine. Otherwise a local fallback is used. Display text is set in small
capitals to stand in for Cinzel's look.

## Screens

- **1280 × 720**: scale 1, a 232 px sidebar, Home in three columns, two-column sections stacked. On screens under 800
  px tall the sidebar uses 36 px categories and 28 px pages, so every category shows with the longest one (Behaviour,
  seven pages) open.
- **1600 px wide and more** (1920 × 1080): scale 1, a 260 px sidebar, Home in five columns, two-column sections side
  by side, content capped at 1600 px and centred.
- **2560 × 1440**: scale 1.333, the in-game effective scale (screen height ÷ 1080), so it lays out as 1920 × 1080 does.
  3840 × 2160: scale 2.
- Under 1100 px the sidebar collapses to icons, Home drops to two columns and master–detail pages stack. The whole menu
  is capped at 1.9 × the screen height and centred on ultrawide screens.

## How it is built

- `grace.css`: the tokens of DESIGN §3.2–§3.6 as custom properties, then the frame and components. Every size is in
  rem from one root size, so the whole menu scales together. The lozenge rule, the grace glow and the ash grain are
  drawn by CSS and an inline SVG noise filter of ours; the icons are our own line drawings in an inline SVG sprite.
- `mock.js`: the sidebar accordion (one category open at a time) and its collapse button, the top bar's menus, tabs
  (arrow keys, Home and End), collapsible sections and "Advanced", switches, segmented choices, filter chips, sliders
  (reaction cooldowns step through 0 to 6 h of play) and the help dialog. It saves nothing, sets no timer and makes no
  request.
- Each page carries a Content Security Policy (`default-src 'none'`; scripts, styles and fonts from its own origin
  only; images from its own origin and `data:` URIs; `connect-src 'none'`) and no inline style. Motion is one-shot
  only: a 120 ms fade on a page or tab change and a 1 s tint on the newest line, both off under
  `prefers-reduced-motion`.
- The pages are written by `mock/build/build.py` (`python3 -I mock/build/build.py mock`, from the repository root),
  so the sidebar, top bar and help dialog are the same on every page; edit the generator, not the HTML. `grace.css`
  and `mock.js` are written by hand. `mock/build/shoot.js` takes the review screenshots at 1280 × 720 and 2560 × 1440
  from `file://` with Playwright's Chromium, aborts every other request and reports overflow; its output goes outside
  the repository.

## What it does not show

The in-game ImGui drawing (G1), pad glyphs and pad mode, Grace Clear, the plain-text option, search results,
confirmations, the stale state, the title-screen and inactive states, First light, the Customise drawer, the
browser-only Workshop, and every page other than these ten.

## Licence

MIT, as the rest of GraceUI. Written for this repository; nothing is copied from SeverActions, Magelight UI, SkyrimNet
or the game. SkyrimNet's dashboard informed only how the pages are grouped and laid out.
