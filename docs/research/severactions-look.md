# Study: the SeverActions look, and the proposed "Grace" theme for GraceUI

Research note for the GraceUI design, 2026-11-07, cited as **LOOK**. Read-only study; nothing here is copied. The theme
it proposes is adopted in [GraceUI DESIGN](../DESIGN.md) §3, and its points for the design (§4) are settled there.

## 0. Sources, method and limits

- **Screenshot**: a capture of SeverActions' menu ("the hearth ledger", its Inventory page), 2000 × 1125, supplied by
  the project owner on 2026-11-07. It is not committed.
  Colours were sampled from it with a small Pillow script of our own (antialiased peaks, so they are approximate).
  Its rail measures about 248 px and its header strip about 54 px, against design sizes of about 236 and 52, so
  the screenshot shows a UI scale of about 1.05.
- **Built views** in SeverActions' public repository (`github.com/Severause/SeverActions`, cloned on 2026-11-07 into an
  isolated folder; untrusted, read only), under `00 Core/Magelight/SeverActions/views/`: the main menu's minified stylesheet (about 0.5 MB, about 3,900 rules), plus the small prompt, diary
  and letter sheets. They were read only through helper scripts of our own, kept outside the copy and outside this
  repository and run with `python3 -I`. Those scripts count tokens and print rules. Nothing was built or run, and no
  image was opened apart from the owner's screenshot.
- **Licence position** (GraceUI's CLAUDE.md): SeverActions has no licence, so all rights are reserved. This note describes the
  look in our own words. Where a value is given for SeverActions it is rounded or sampled, and it is there only to
  calibrate our own palette. No CSS, HTML, JS, text, image, sigil or layout code is reproduced. Its fonts (Marcellus
  SC, IM Fell English and IM Fell English SC, plus Cinzel and Cormorant SC in the build) are OFL, but GraceUI
  takes any font only from its original publisher (§3.4).
- **EldenNet context**, read in EldenNet's documents: DESIGN §12 (presentation), NPC §10.1 (surfaces, fonts and colours),
  NPC §10.6 (F10 settings), NPC §10.7 (scale and HDR), ARCH §11.5 (`ui.scale`, `ui.hdr`, `ui.paper_white_nits`).

---

## 1. SeverActions' visual language, described

### 1.1 Overall impression

The menu is a full-screen, nearly opaque panel (about 97% alpha) over the game. It reads as a warm, candle-lit
**ledger or account book**: dark umber grounds in three or four close steps, parchment-coloured text at four
levels of strength, and one metallic accent. The accent is brass-gold in the default theme. In the screenshot it is
a copper-orange, sampled at about `#E39C5D` from the carry bar. Ornament is very restrained: hairline rules, small
uppercase labels with wide letter-spacing, italic serif for dates and descriptive sub-lines, and monospaced
tabular figures for every number. Most of the character comes from the type, the hairlines and the warm palette,
not from decoration. Corners are nearly square (0–4 px). Surfaces are flat, with very soft drop shadows only on
cards that lift.

It is built as **themes over tokens**. One set of role tokens (four grounds, four ink levels, accent and
accent-hi, a strong and a light rule, status colours) is swapped wholesale by a theme attribute on the root. Seven
variants exist:

| Variant | Description |
|---|---|
| Default | Brass on umber |
| Tallow | Copper on a redder umber, IM Fell hero face, film grain, near-square corners |
| Windhelm | Ice-blue on blue-black, Cinzel hero face, square corners |
| Dwemer | Copper on teal-black, Marcellus SC hero face |
| Soulcairn | Lavender on violet-black |
| Vellum | A light parchment theme with dark ink |
| Ledger | Grain only |

A theme changes the grounds, ink, accent, the hero display face, the corner radii, a grain texture and the
title glow. Category colour families stay fixed across themes, with a darker set for the light theme: item kinds,
schools of magic, vitals, resistances, holds and cities.

### 1.2 Palette by role

The approximate values are from the screenshot, a copper variant. They are a starting point for calibration and
are not to be reused.

| Role | How it is used | Approx. value |
|---|---|---|
| Ground 0 (deepest) | Sidebar rail, and the outer frame on wide screens | ≈ `#18100A` |
| Ground 1 | Main page ground and header strip | ≈ `#211913` |
| Ground 2 | Cards, the detail pane, selected rail item | ≈ `#1E1912` to `#16120D` |
| Ground 3 | Selected table row, buttons, tags, hover on cards | ≈ `#291D13` to `#302313` |
| Well | Search field, meter tracks, inputs: darker than the page | ≈ `#110D0A` |
| Ink high | Names, titles, active items | ≈ `#F0E2C5` |
| Ink | Table text, body | ≈ `#E2D6C2` (cells), ≈ `#CFC1A5` (token) |
| Ink muted | Inactive rail items, inactive tabs, secondary facts, italic date | ≈ `#8E8070` |
| Ink faint | Group labels, column heads, counts, sub-lines, key-hint labels, version line | ≈ `#5E5749` to `#655C4D` |
| Accent | Active markers (rail bar, tab underline), money values, carry bar, "worn" tags, column-head sort | copper ≈ `#E39C5D` here, brass ≈ `#C9A45C` in the default theme |
| Accent high | Active tab text, active child item, enchantment sub-lines | ≈ `#ECC29E` (copper), ≈ `#E8C687` (brass) |
| Accent tints | Hover ≈ 4–5% accent over the ground; selection and glow ≈ 10–22% accent | — |
| Danger | Destructive buttons ("Destroy", "Drop"), negative deltas under values | Muted wine-red text ≈ `#996B6F`; bright "blood" ≈ `#C25B4C` for alerts |
| Success, warning, info | Moss green, rust orange, a grey-teal "active" | ≈ `#7D8A5D`, `#C56A3D`, `#618C84` |
| Rules | Hairlines and borders are the accent at low alpha: about 9% (light), 14% (normal) and 30% (strong, under column heads and mastheads) | — |

Gold or copper is always the one "lit" colour on the page: active, selected, money and focus. Everything else is
warm grey-brown. Status colours are dull, earthy versions (moss, rust, wine), so they never compete with the accent.

### 1.3 Type

- **Families.** Four roles:
  - a **text serif** for rail items, item names, the header name and section titles; Windows' Sitka, falling back
    to Cambria, Palatino and Georgia;
  - a **hero display serif** that changes with the theme (Sitka Banner, IM Fell English, Cinzel, Marcellus SC or
    Cormorant SC) for mastheads and big numbers;
  - a **UI sans** (Segoe UI or the system UI face) for small labels, tabs, child items and buttons;
  - a **monospace** (Cascadia Mono or Consolas) for every number, count, key cap, the search field, filter chips
    and the version line.
  Italic serif is used heavily, about 200 times: dates, enchantment sub-lines, empty-state hints and placeholders.
- **Scale.** Small and dense. The base is about 14 px. Tokens run about 12, 13.5, 15, 17, 22, 28, 40 and 56 px, and
  in practice most text is 10–13 px. The commonest sizes are about 11, 12, 10, 13 and 10.5 px. Rail items are
  about 15.5 px (serif), rail children about 13 px (sans), tab labels about 12 px, column heads about 10.5 px, tags
  and stat labels 9 px, page titles about 40 px.
- **Letter-spacing and caps.** All small labels are uppercase with wide tracking. Section labels and column heads
  use about 0.15–0.17 em (1.6–1.8 px at 10.5 px), tabs about 0.1 em, tags about 0.13 em, and the wordmark about
  0.17 em. Uppercase is used about 200 times. There is no true small-caps feature: the effect comes from uppercase
  sans at small sizes, or from an SC display face in some themes. Numerals are tabular everywhere.
- **Weights.** Mostly 600 for labels, tabs, buttons and titles, 700 for a few headers, and 400 for body text.

### 1.4 Spacing and grid

- **Frame.** Rail about 236 px. Header strip about 52 px. Page gutter about 48 px on the left of the strip and
  24 px on the right. Rail items about 36 px tall, with 22 px side padding. Children about 29 px tall, indented to
  about 40–50 px. Groups about 18 px apart.
- **Rhythm.** Mostly multiples of 2 and 4: 2, 4, 6, 8, 10, 12, 14, 18, 22 and 26. Gaps inside controls are 4–8 px.
- **Tables.** Table rows about 38–42 px tall, column heads about 30 px. Inventory columns are a fixed grid: glyph,
  name (flexible), quantity, damage or armour, value, weight, actions. The grid narrows at three breakpoints.
- **Wide screens.** On wide screens the page is capped at about 2 × the screen height and centred, with the rail
  pinned to its left edge. A "full wide" option removes the cap.
- **Scaling.** The whole menu is one container, sized to the viewport divided by the UI scale and then
  transform-scaled back, so layout is computed at a fixed design size.

### 1.5 Components

- **Sidebar rail.**
  - The top has a wordmark: uppercase serif, about 15 px, wide tracking, in the accent. Under it is a one-line
    sub-title in faint mono, used for the version or a tagline. A hairline separates it.
  - Next come **group labels**: tiny uppercase sans, about 10.5 px, tracked, faint, for "Party", "Goods & Gear"
    and "World".
  - **Items** are serif, about 15.5 px, muted, each with a small line icon.
  - The **active item** gets a 2 px accent bar on its left edge, a slightly lighter ground, high ink, and an icon
    lit in accent-high with a faint glow.
  - **Children** (Items, Stats, Spells) are smaller sans, indented. The active child is accent-high, with a half-
    strength accent bar.
  - Small **count badges** are pill-shaped and mono, with good and bad tints.
  - The **footer** is separated by a hairline and holds Settings and a key-hint line ("Esc closes, ←→ pages") in
    faint sans. Absent modules are dimmed to about 55% with a "not installed" tag.
- **Header strip.** A context line: the character's name in serif semibold, middot separators, facts in muted
  sans, money in accent mono, and the in-game date in muted italic serif. On the right are a bordered "?" help
  button (it pulses until first seen) and a close cross.
- **Party cards.** Compact cards, about 176 px, each with a round portrait, a name and a weight meter. The selected
  card has an accent border, a faint outer glow and a short accent underline bar.
- **Tabs with counts.** Uppercase tracked sans, about 12 px semibold, in muted ink. The count follows in faint
  mono, about 10.5 px, untracked. The active tab has accent-high text and a 2 px accent underline that sits on
  the hairline. Empty categories show at about 45% opacity. The tabs wrap to two rows.
- **Filter chips.** Pill-shaped (fully round), mono, about 11 px, with a small glyph prefix (✦, ♥, ▲, •) and a
  hairline border. Active chips get accent text, a border at about 60% accent and a 6% accent fill. A second,
  rectangular chip style, in the serif with a heavier bottom border, is used for categories elsewhere.
- **Search and sort.** The search field is an inset well (darker than the page) with a hairline border, mono
  text and an italic muted placeholder. On focus it gets an accent border and a 1 px accent glow ring. The sort
  control is a well too, with a tiny uppercase "Sort" label, the value and a chevron. The header buttons beside
  them are wells with mono labels.
- **Tables.**
  - Column heads are uppercase tracked faint sans over a strong hairline. The sorted head turns accent, with a
    tiny arrow.
  - Rows have hairline separators. The first column is a small square glyph tile.
  - The name is in serif, with an optional sub-line in small muted sans, and enchantments in italic accent-high.
  - Numbers are right-aligned tabular mono. Money is in the accent. Negative deltas appear in small muted red
    under the value.
  - Tiny uppercase boxed tags mark "worn" and "quest".
  - Hover is a 4–5% accent tint. Equipped rows get a 2 px accent left bar and a faint tint. The selected row gets
    a lighter ground and a 1 px inset accent outline. Locked (quest) rows are at 65% opacity with a "not allowed"
    cursor.
  - Row actions appear only on hover or focus.
- **Detail pane** (right column, about a quarter of the width).
  - It is a stack of bordered cards: about 4 px radius, ground 2 and 12–14 px padding.
  - A large preview image comes first. Under it are a small boxed "exit live view" toggle, the item name in serif
    and an "unpin" mini-button.
  - A **row of three stat tiles** follows (value, weight, value per weight). Each is a bordered box with a large
    number and a tiny uppercase label underneath.
  - An "Effects" eyebrow in the accent leads the effect rows: a sans name with mono magnitude and duration on the
    right, and a muted description line.
  - Next is a row of small uppercase outlined action buttons (Eat, Favourite, Pin), then a separate right-aligned
    row of destructive ones (To junk, Drop, Destroy) in muted red.
  - Last come a **carry meter** (6 px track in the well, accent fill, red when over the limit) with mono figures,
    and a "Carts" drop zone with an italic hint.
- **Stat tiles** elsewhere: an uppercase tracked label with a small glyph, a large number in the hero face
  (about 28 px, tabular), a unit in small sans, and a foot line. Alerts appear in red semibold.
- **Buttons.** Four kinds:
  - default: ground-3 fill, ink, sentence case, semibold, about 13.5 px;
  - primary: accent fill with dark "on-accent" text;
  - ghost: accent text, no fill;
  - danger: red text, no fill.
  All are about 7 × 14 px padding and 3–4 px radius. Disabled buttons are at 40% opacity. The detail pane's
  action buttons are a smaller uppercase outlined variant.
- **Settings controls.**
  - Rows are about 44 px, with the label on the left and the control on the right, hairlines between them and a
    hover tint. Nested rows are indented with a faint accent left rule. A small accent dot marks a value changed
    from its default, with a reset button.
  - Toggles are bordered **text pills** (an ON/OFF word in mono) rather than sliding switches. On, they turn
    accent with a 10% fill.
  - Sliders have a 6 px well track and a 16 px round accent thumb with a dark ring that grows slightly on hover,
    with a tabular value on the right.
  - Settings have their own inner rail of groups and a search box. Matches are tinted and the other rows dimmed.
- **Key-hint footer.** Under the table, separated by a hairline, is a wrapping line of hints. Each hint is a key
  cap (a bordered mono box, about 10 px, 3 px radius) followed by a faint label: ↑↓ move, Enter equip, R drop,
  F favourite, P pin, Space select. A pad-glyph version replaces it when a controller is in use.
- **Scrollbars.** Thin (about 5 px, 4 px in the rail), with no track, a thumb of muted ink at about 25% alpha and
  a 3 px radius, slightly stronger on hover.
- **Focus.**
  - Keyboard focus anywhere in the shell is a 1 px accent outline with a 1 px offset. That is thin, and easy to
    lose at TV distance.
  - With a gamepad the root is flagged as pad input, and the focused element gets a 2 px accent-high outline, a
    2 px offset and a soft 4 px accent halo.
  - Inputs show focus by an accent border plus a 1 px glow.
- **Motion.**
  - Durations come in three steps: about 120, 200 and 260 ms. Easing is a standard ease-out curve plus a softer
    "settle" curve.
  - Pages fade in while rising 4–6 px. Drawers slide in from 24 px. Toasts rise 8 px. Cards lift 2–3 px with a
    deeper shadow on hover.
  - Colour, background and border changes take 100–180 ms.
  - A few things loop: the help button pulses until seen, plus a "phase" ribbon, a skill glow and ember and rime
    effects on some pages.
  - The reduced-motion query covers only the help-button pulse.
- **Elevation.** Three shadow levels, all black and soft. There is an optional "glass" surface with a 16 px
  backdrop blur, and a faint inner top sheen on parchment cards. Some themes add a soft accent glow behind hero
  titles.

### 1.6 What works, and what we should do differently

What works, and what we keep in spirit:

- one lit colour against warm greys;
- four ink levels;
- tracked uppercase eyebrows and column heads;
- tabular mono figures;
- italic serif for flavour lines;
- hairline structure instead of boxes;
- a 2 px accent bar as the universal "this one" marker (rail, equipped rows, settings rail);
- counts on tabs;
- hover-revealed row actions;
- a detail pane of small cards;
- key hints always visible;
- themes as pure token swaps;
- a wide-screen cap.

What we do differently, as lessons:

1. **Contrast.** The faint ink fails WCAG AA for the small text it carries. Measured on the screenshot: group
   labels 2.6:1, tab counts 2.6:1, row sub-lines 2.7:1, key-hint labels 2.6:1, the brand sub-line 2.6:1, the
   cart hint 2.4:1. The design token for faint ink is about 2.7:1 on its ground. These are exactly the labels
   that orient the player. Grace keeps every readable text at 4.5:1 or better (§3.3).
2. **Bundled fonts.** The fonts depend on Windows system faces (Sitka, Segoe, Cascadia, Cambria). Under Proton
   and on the Steam Deck those are absent, so the look falls back to whatever the system has. Grace bundles
   every face it uses.
3. **Theme every control.** One native control is unthemed: the "Select" checkbox shows the engine's default
   blue-violet tick. Grace draws every control itself.
4. **Focus visibility.** Keyboard focus is a 1 px ring, which is too faint for a couch. Grace uses one strong ring
   for keyboard and pad alike.
5. **Reduced motion.** It covers one animation. Grace covers all of them.
6. **Render cost.** Backdrop blur and looping animations are costly for an off-screen HTML renderer: every
   animated frame re-renders and re-uploads a dirty region. Grace has no blur and no infinite loops.
7. **Density.** At about 10–12 px for most labels, the UI suits a desk monitor, not a TV or a 7-inch Deck. Grace
   starts larger and scales (§3.12).

---

## 2. Design brief for Grace

**Same spirit, our own identity, Elden Ring's mood.** The Lands Between are ash and umber under a golden tree: a
dimmer, greyer and more weathered ground than SeverActions' candle-lit brown; gold that reads as **grace-light**
(Erdtree gold, not brass or copper); text the colour of **muted parchment and ash**; scarlet-rot red for danger;
glintstone blue for information. The interface should feel like a **chart table at a site of grace**: quiet,
legible, a single warm light marking where you are. Hard rules:

- No game assets: no FromSoftware fonts, icons, frames, button glyphs, textures, the Elden Ring logo, or any
  Erdtree or ring silhouette that echoes the logo.
- Nothing from SeverActions beyond the general ideas described above.
- Continuity with EldenNet's in-game surfaces: ash-white `#E8E2D0`, name gold `#C8B27A`, call-out edge
  `#9E3B32`, and the fonts already chosen in DESIGN §12 and NPC §10.1.

---

## 3. The Grace theme (proposal)

### 3.1 Principles

1. **One light.** Gold marks only these: where you are, what is selected, what has focus, what is primary.
   Never decoration.
2. **Ash before umber.** The grounds are low-saturation warm greys (hue about 40°, saturation 15–25%), darker
   and greyer than SeverActions.
3. **Legible first.** Every readable text is 4.5:1 or better. Control edges are 3:1 or better. Bodies are larger
   than SeverActions' (TV, Deck).
4. **Flat and still.** Hairlines, flat layers, fades only (DESIGN §12). No blur, no loops, no parallax.
5. **Tokens, then themes.** Every colour, size and duration is a token. Variants are token swaps.
6. **Spoilers are a state, not an absence.** A veiled thing has its own look (§3.9). Counts never leak what it
   hides.

### 3.2 Palette: Grace (dark, the default)

Every value is ours. Contrast is computed per WCAG 2.x with a small script of our own (not committed); the ratios were
checked again for GraceUI DESIGN §3.3.

| Token | Value | Role |
|---|---|---|
| `ground.well` | `#0B0A08` | Inputs, meter tracks, key caps, code |
| `ground.0` | `#100E0B` | Rail, outer frame, modal scrim base |
| `ground.1` | `#161410` | Page ground, header strip |
| `ground.2` | `#1D1A15` | Cards, detail pane, settings panel |
| `ground.3` | `#26221B` | Selected row, raised button, active chip, tag |
| `ground.4` | `#2F2A21` | Pressed, hover on raised, tooltip |
| `ink.hi` | `#E8E2D0` | Titles, names, active items. This is EldenNet's ash-white |
| `ink.body` | `#CFC6B1` | Table text, prose, values |
| `ink.mute` | `#A69D89` | Inactive rail items and tabs, secondary facts, italic flavour |
| `ink.faint` | `#9A927F` | Eyebrows, column heads, counts, key-hint labels; the floor for readable text |
| `ink.off` | `#5F584B` | Disabled only (WCAG-exempt); never for information |
| `gold` | `#C9A65A` | Erdtree gold: accent bar, underline, primary fill, meters, money and runes |
| `gold.hi` | `#E3C579` | Grace-light: active text, focus ring, active icon |
| `gold.name` | `#C8B27A` | Speaker and NPC names: EldenNet's muted name gold (NPC §10.1) |
| `gold.deep` | `#8C6F35` | Strong rule, the border of the selected card |
| `on.gold` | `#15120C` | Text on gold fills |
| `rule` | gold at 14% (≈ `#2F281A` on ground 1) | Hairlines, row separators (decorative) |
| `rule.strong` | gold at 28% (≈ `#483D25`) | Under column heads and page titles |
| `edge` | `#7E7460` | Borders that identify a control (inputs, toggles, checkboxes, chips): 3:1 on every ground |
| `rot` | `#D9806F` | Danger text and icons (scarlet rot), negative deltas |
| `rot.fill` | `#8F3A2F` | Danger button fill (ink.hi on it is 5.8:1) |
| `rot.edge` | `#9E3B32` | Danger borders and the call-out edge (EldenNet). Never text: it is 2.6:1 |
| `ember` | `#DB9550` | Warnings |
| `moss` | `#A3B37E` | Success, "on", healthy |
| `glint` | `#94B6D2` | Information, links in help, a system or provider event (glintstone) |
| `tint.hover` | gold at 5% over the ground | Hover |
| `tint.select` | gold at 10% over the ground | Selected, with the 2 px bar |

Category families, if needed (for example the Home live feed by source): combat uses `rot`, dialogue `gold.name`,
world and quest `moss`, system and provider `glint`, and spoiler-veiled `ink.faint`. Each is always paired with an
icon or a word, because gold and ember are almost equal in lightness (1.08:1) and hue alone is not enough.

### 3.3 Contrast, checked

Ratios against each ground:

| Text | well | g0 | g1 | g2 | g3 | g4 |
|---|---|---|---|---|---|---|
| ink.hi `#E8E2D0` | 15.3 | 14.9 | 14.2 | 13.4 | 12.2 | 11.0 |
| ink.body `#CFC6B1` | 11.7 | 11.4 | 10.8 | 10.2 | 9.3 | 8.4 |
| ink.mute `#A69D89` | 7.4 | 7.2 | 6.8 | 6.5 | 5.9 | 5.3 |
| ink.faint `#9A927F` | 6.4 | 6.2 | 6.0 | 5.6 | 5.1 | 4.6 |
| gold `#C9A65A` | 8.6 | 8.3 | 8.0 | 7.5 | 6.9 | 6.2 |
| gold.hi `#E3C579` | 11.8 | 11.5 | 11.0 | 10.4 | 9.4 | 8.5 |
| gold.name `#C8B27A` | 9.5 | 9.3 | 8.9 | 8.4 | 7.6 | 6.9 |
| rot `#D9806F` | 6.9 | 6.7 | 6.4 | 6.0 | 5.5 | 4.9 |
| ember `#DB9550` | 7.9 | 7.7 | 7.4 | 7.0 | 6.4 | 5.7 |
| moss `#A3B37E` | 8.8 | 8.5 | 8.2 | 7.7 | 7.0 | 6.3 |
| glint `#94B6D2` | 9.3 | 9.1 | 8.7 | 8.2 | 7.5 | 6.7 |

- **Text levels.** Every text level passes AA (4.5:1) on every ground, and every level but faint passes AAA (7:1)
  on grounds 1–2. By comparison, SeverActions' faint level measures 2.4–2.7:1.
- **Non-text (WCAG 1.4.11).** `edge` measures 3.1–4.3:1 on every ground. `gold` on any ground is 6.2:1 or better.
  The focus ring (`gold.hi`) is 9.4:1 or better on grounds 1–3.
- **Gold surfaces.** `on.gold` on `gold` is 8.1:1 and on `gold.hi` 11.2:1. The ring against a gold surface is
  only 1.4:1, so the ring always sits 2 px off the element, on the ground (§3.9).
- **Grounds.** The ground steps are subtle (1.05–1.14:1). That is deliberate, because a ground is not information:
  every boundary that matters also has a rule or an edge.
- **Grace Clear**, the high-contrast variant, has a black well, ink.hi `#F4EFE2` (18.3:1 on black), ink.mute
  `#C2BAA6` (10.3:1), no faint level, 2 px edges and a 3 px focus ring.

### 3.4 Fonts (OFL only, from their publishers)

| Role | Family | Publisher and source | Use |
|---|---|---|---|
| Display | **Cinzel** (variable, 400–700) | Natanael Gama (NDISCOVER); the publisher's repository or Google Fonts. SIL OFL 1.1 | Wordmark, page titles, the NPC name on its page, big stat numbers. Only at 20 px or more. Inscriptional Roman capitals, the closest OFL fit to the Lands Between's carved-stone mood |
| Text serif | **EB Garamond** (Regular, Italic, Medium or SemiBold) | Georg Duffner, extended by Octavio Pardo; the publisher's repository or Google Fonts. OFL 1.1 | Rail items, card titles, prose (personas, memories, help), italic flavour lines. Lining figures switched on wherever numbers appear (EB Garamond defaults to old-style). Already EldenNet's in-game serif (DESIGN §12) |
| UI sans | **Noto Sans** (Regular, Medium, SemiBold) | Google, Noto project (`notofonts`). OFL 1.1 | Tabs, labels, eyebrows, buttons, settings rows, table cells. Already EldenNet's UI sans |
| Mono | **Noto Sans Mono** (Regular, Medium) | Google, Noto project. OFL 1.1 | Numbers in tables, counts, key caps, IDs in the debug view. Already EldenNet's mono |

Notes on the fonts:

- **Sources.** No font is ever taken from SeverActions or Magelight. Each ships with its `OFL.txt` and the
  publisher's copyright line in `NOTICE`. Subset to EldenNet's atlas ranges (Latin, Latin-1, Latin Extended-A,
  General Punctuation) and grow the ranges with localisation.
- **Alternatives**, if Cinzel feels too close to a known title style: Forum (Denis Masharov, OFL), or EB Garamond's
  own small caps (`smcp`/`c2sc`), which saves a family. Avoid IM Fell and Marcellus SC, the faces most associated
  with SeverActions' themes, so that Grace has its own voice.
- **No system fonts.** The stack is ours on every platform (Proton, Deck), with a generic serif or sans fallback
  only for unexpected glyphs.
- **"Plain text" option.** It swaps EB Garamond prose for Noto Sans and Cinzel titles for Noto Sans SemiBold, for
  players who find the serifs hard to read. It is part of accessibility (§3.11).

### 3.5 Type scale

Sizes are CSS px at 1080p and scale 100%. One rem is 16 px. The ratio is about 1.2.

| Token | Size / line | Family, weight | Tracking, case | Use |
|---|---|---|---|---|
| `display.xl` | 40 / 44 | Cinzel 500 | +0.04 em | Page masthead (Home, NPC Types) |
| `display.lg` | 30 / 36 | Cinzel 500 | +0.03 em | Named NPC's page title, wordmark at 22 |
| `display.md` | 22 / 28 | Cinzel 600 or EB Garamond 600 | +0.02 em | Section titles, big stat values |
| `title` | 19 / 26 | EB Garamond 600 | 0 | Card titles, detail-pane name, header name |
| `prose` | 17 / 26 | EB Garamond 400 and italic | 0 | Persona, memory and help text. At least 16 px (NPC §10.7) |
| `rail` | 17 / 24 | EB Garamond 500 | 0 | Rail items |
| `ui` | 15 / 22 | Noto Sans 400 or 500 | 0 | Settings labels, table cells, buttons (sentence case, 600) |
| `ui.sm` | 13.5 / 20 | Noto Sans 400 | 0 | Sub-lines, descriptions under settings, rail children |
| `tab` | 13 / 18 | Noto Sans 600 | +0.10 em, upper | Tabs |
| `eyebrow` | 12 / 16 | Noto Sans 600 | +0.14 em, upper | Rail group labels, column heads, stat labels, tags (tags 11 px is the floor) |
| `num` | 14 / 20 | Noto Sans Mono 400 | 0, tabular | Table numbers, counts (13), key caps (12) |

- **Minimums.** 11 px for any text, used only for tags in capitals. 12 px for eyebrows. 13.5 px for running UI
  text. 16 px for prose.
- **Small caps.** Eyebrows and tabs are uppercase sans with tracking, never faked small caps. Cinzel supplies real
  inscriptional capitals for titles.
- **Numbers.** Table and stat numbers are tabular and lining in every family. Money and runes are `gold`, other
  numbers `ink.body`.

### 3.6 Spacing, grid, radii and elevation

- **Base unit 4 px.** Steps are 2, 4, 8, 12, 16, 20, 24, 32, 40, 48 and 64.
- **Frame at 1080p and 100%.**
  - Rail 248 px: 24 px side padding, items 40 px tall, children 32 px, groups 20 px apart.
  - Header strip 52 px.
  - Page gutter 32 px.
  - Detail pane 360–420 px.
  - Content width capped at 1.9 × the viewport height, centred on ultrawide screens. The rail stays attached to
    the content's left edge, and the game shows, dimmed, either side.
- **Tables.** Rows 44 px (comfortable, the default) or 36 px (compact, a setting). Pad mode forces 44 px. Column
  heads 32 px.
- **Settings rows.** 48 px tall, with the label and description on the left and the control right-aligned in a
  fixed 280 px column, so controls line up down the page.
- **Radii.** 2 px for controls, chips, tags and key caps; 4 px for cards and panes; 0 for tabs and rows. Pills are
  used only for status badges, never for chips, to set Grace apart from SeverActions' pill filters.
- **Elevation.** Flat by default. Cards are separated by `ground.2` and a `rule` border. Menus, tooltips and modals
  get one soft black shadow (0 8 24 at 50%). The scrim is `ground.0` at 70%. Nothing lifts on hover.
- **Ornament (Grace's own motifs).**
  - **The lozenge rule.** Section dividers and the masthead underline are a single `rule.strong` hairline with a
    small hollow lozenge (6 px, `gold.deep`) at its left end, never a double rule.
  - **The grace glow.** The active rail item gets a soft radial gold glow (about 6% at its centre, fading to 0
    over about 120 px) behind the 2 px bar, like light from a site of grace. It is static.
  - An optional fine **ash grain** at 2–3% on grounds 0–1 hides banding in HDR (§3.13). It is our own noise,
    generated or drawn by us.

### 3.7 Layout frame (where the look meets the SkyrimNet-like layout)

The frame, from top to bottom and left to right:

- **Rail.** The wordmark "ELDENNET" in Cinzel, tracked, in `gold`. The sub-line under it is the build and
  increment ("v0.6 · Threshold") in faint mono. The sections follow.
- **Header strip.** The current context: the character, the region in spoiler-safe form, and a model status dot
  with its word. On the right sit help, a key-hint toggle and close.
- **Page.**
- **Optional detail pane.**
- **Footer key-hint line** at the bottom of the page.

The section list itself is in [information-architecture.md](information-architecture.md) (IA). This note only fixes how
a section looks.

### 3.8 Components

- **Rail item.**
  - Rest: `ink.mute`, a line icon in `ink.faint`.
  - Hover: `ink.body`, `tint.hover`.
  - Active: `ink.hi`, a 2 px `gold` bar, `ground.2`, an icon in `gold.hi`, and the grace glow.
  - Children: `ui.sm` Noto Sans, indented 48 px. The active child is `gold.hi` with a 2 px bar at 50%.
  - Group label: `eyebrow` in `ink.faint`.
  - Badge: a mono count in a 2 px-radius box with an `edge` border. Status badges are pills tinted moss, ember or
    rot.
- **Tabs with counts.**
  - Rest: `tab` in `ink.mute`, with a count in `num` 12 px `ink.faint`, spaced 6 px.
  - Active: `gold.hi`, with a 2 px `gold` underline on the `rule`.
  - Empty: `ink.off` and not focusable.
  - Counts count only what the player has reached (§3.9).
- **Filter chips.**
  - Rectangular, 2 px radius, 28 px tall, Noto Sans 13 px, with an `edge` border and a leading lozenge glyph that
    is hollow when off.
  - On: a filled `gold` lozenge, `gold.hi` text, a `gold` border and `tint.select`.
  - A chip that toggles carries a check state, never colour alone.
- **Search.** A `ground.well` fill, an `edge` border and Noto Sans 15 px with an italic `ink.mute` placeholder. On
  focus it gets a `gold` border and the focus ring. A clear button appears with text. A result count appears in
  `num`.
- **Tables.**
  - The column head is an `eyebrow` over `rule.strong`. The sorted column shows `gold.hi` and an arrow.
  - Rows are separated by `rule`.
  - The name is EB Garamond 17 px in `ink.hi`, with a sub-line in Noto Sans 13.5 px `ink.mute`.
  - Numbers are right-aligned `num`.
  - Tags (11 px capitals on `ground.3`): "SPEAKS" or "MET" in moss, "VEILED" in faint.
  - Hover is `tint.hover`. Selected is `ground.3` plus a 2 px `gold` bar, with `aria-selected` set.
  - Row actions show on hover or focus, and always in pad mode for the focused row.
- **Detail pane.**
  - Cards on `ground.2` with a `rule` border, 4 px radius and 16 px padding.
  - The title is `title` in `gold.name` for NPCs, `ink.hi` for anything else.
  - Then a stat-tile row, sections led by eyebrows, then actions: primary first, destructive last on their own
    row, behind a confirm.
  - No game imagery: portraits are absent in v1, or a lozenge monogram we draw.
- **Stat tiles.** A `ground.2` card with an eyebrow label on top, the value in `display.md` (Cinzel or Garamond,
  lining), a unit in `ui.sm` `ink.mute`, and an optional 3 px meter (`gold` on `ground.well`, `rot` over the
  limit) with its numbers in text. Used on Home for model latency, tokens per minute, NPCs nearby, lines today and
  queue depth.
- **Buttons.** All 36 px tall (44 px in pad mode), 16 px padding, Noto Sans 15 px 600, sentence case.
  - Primary: `gold` fill, `on.gold` text. Hover `gold.hi`.
  - Secondary: `ground.3` fill, `ink.body`, an `edge` border. Hover `ground.4`.
  - Quiet: no fill, `gold` text. Hover `tint.hover`.
  - Danger: an `rot.edge` border, `rot` text. Hover `rot.fill` with `ink.hi`.
  - Pressed is one ground step darker. Disabled is `ink.off` with no fill, and the reason is in a tooltip, not
    just dimmed.
- **Settings controls.**
  - **Switch:** a real two-state track, 40 × 22, with an `edge` border. On, it shows a `gold` fill, a knob in
    `on.gold` and the word "On" beside it, because state is never colour alone.
  - **Slider:** a 4 px `ground.well` track with a `gold` fill, an 18 px `gold.hi` thumb with an `on.gold` ring, and
    a tabular value plus unit. The pad changes the value by its step, and by ten steps with a shoulder button.
  - **Segmented choice** (Off / Sparse / Normal): `edge` boxes. The chosen one is filled `ground.3` with a
    `gold.hi` label and a 2 px underline.
  - **Changed from default:** a small `gold` lozenge before the label, and a reset icon button.
  - **Hotkey capture:** a key-cap field that pulses once (not in a loop) while waiting, and shows conflicts in
    `ember`.
- **Key-hint footer.** A hairline above it. Each hint is a key cap (`ground.well`, `edge` border, 2 px radius,
  Noto Sans Mono 12 px `ink.body`) and a label in `ui.sm` `ink.faint`. Pad hints use **our own** neutral glyphs
  (a drawn face-button diamond with the position marked, plus the letter or word) rather than platform or game
  button art. It switches with the last input device.
- **Scrollbars.** 6 px, no track, a thumb of `ink.mute` at 30%, widening to 10 px and 50% on hover or drag. Hidden
  in pad mode, where focus scrolls the view. In a renderer without scrollbar styling, the same rules apply to our
  own drawn bar.
- **Toasts and banners** inside the menu match EldenNet's in-game toasts: a 3 px left accent in `gold`, `ember`
  or `rot`, Noto Sans 15 px.
- **Modals.** sm 440, md 560 and lg 800 px. `ground.2`, a `rule.strong` border, the one shadow and a `ground.0`
  scrim at 70%. No blur.
- **Tooltips.** `ground.4`, an `edge` border, `ui.sm`. Shown after 400 ms of mouse hover, and at once on focus
  with a pad.

### 3.9 States (one matrix for every interactive element)

| State | Treatment |
|---|---|
| Rest | As specified per component |
| Hover (mouse only) | `tint.hover` and one ink step up. No movement |
| Focus (keyboard or pad, one style) | A 2 px `gold.hi` ring with a 2 px offset onto the ground, plus a 1 px `ground.well` inner ring when the element is gold-filled. 3 px in Grace Clear. Always visible after keyboard or pad input. Hidden after a mouse click, as focus-visible does |
| Pressed | One ground step darker, for 80 ms |
| Selected or current | A 2 px `gold` bar on the leading edge, `tint.select` or `ground.3`, `ink.hi`, and ARIA state |
| Disabled | `ink.off`, no hover, a tooltip with the reason. Still focusable when the reason matters |
| Changed from default | A `gold` lozenge before the label, and a reset button |
| Error or invalid | An `rot.edge` border, a `rot` message under the field, an icon and words |
| Pending or saving | A small static ellipsis or a three-step dot (not a spinner loop), and the word "Saving…". Settings save on change, and the menu's close keeps them (GraceUI DESIGN §5.1) |
| Stale (lost contact with EldenNet) | The page dims to 70%, with a `glint` banner "Reconnecting" and "Retry" |
| **Veiled (spoiler)** | The name is replaced by a neutral phrase ("Someone you have not met"), in `ink.faint` italic EB Garamond with a hollow lozenge. No hover detail, no tooltip, not counted in tab or badge counts, not searchable. Entire unreached pages are absent, not veiled. *Corrected after IA §12.3: veiling is only for a perceived character whose name the game has not shown; a story item not reached is absent (IA SP5)* |

### 3.10 Motion

DESIGN §12 sets fades only, 120 ms in and 300–400 ms out, with no sliding or bouncing. Grace follows it in the
menu:

- **Opening the menu.** The scrim and the panel fade in over 120 ms.
- **Closing it.**
  - Closed by the player: a 200 ms fade. That is shorter than the subtitles' fade, because control returns to the
    game and the player is waiting. This deviates from EldenNet's fade rule and is accepted in GraceUI DESIGN (decision D24).
  - **Closed by danger** (GraceUI DESIGN §5.1): instant, 0 ms. The danger rule outranks the look.
- **Page change.** A 120 ms cross-fade of the page body only. The rail and header stay still.
- **State changes.** Colour, background and border transitions take 100 ms linear. Nothing moves: no lifts, no
  slides, no scale-on-hover.
- **No looping animations.** The live-activity feed updates in place at 4 Hz or less, with new rows tinted for
  1 s and then fading over 300 ms. In an off-screen HTML renderer every animated frame costs a re-render and a
  texture upload.
- **Reduced motion.** A setting, which also follows `prefers-reduced-motion` in the browser. It sets every
  duration to 0 except a fade of 60 ms or less.

### 3.11 Accessibility

- **Contrast.** As checked in §3.3. Grace Clear is offered as a variant.
- **Colour.** Never the only signal. Status always carries an icon and a word. Gold and ember are equal in
  lightness, so a warning always has its icon. The palette was chosen so moss (success) and rot (danger) differ in
  lightness as well as hue.
- **Text.** UI scale runs from 75% to 150% (§3.12). The "Plain text" option is a sans-only reading mode. Prose
  lines are at most 72 characters.
- **Input.** Everything is reachable and operable by keyboard alone and by pad alone, with a visible focus at all
  times in those modes. Hit targets are at least 32 × 32 px with a mouse and 44 px tall in pad mode. Nothing
  depends on hover: the actions a hover reveals are also in the detail pane.
- **Browser dashboard.** It gets semantic HTML with ARIA roles (tablist, grid, switch, slider), so screen readers
  work there. In game no OS screen reader can reach the overlay, so in-game parity is not claimed.
- **Motion.** Reduced-motion setting (§3.10). No flashing.

### 3.12 Scale 75–150% and screens

- **Formula.** The effective scale is `(H / 1080) × ui.scale` (NPC §10.7), applied as one root zoom, so the
  layout is computed in CSS px. One CSS px = `(H / 1080) × ui.scale` device px.
- **Breakpoints**, on the resulting CSS viewport width:

  | Width | Layout |
  |---|---|
  | 1600 or more | Rail, page and detail pane side by side |
  | 1200–1599 | The detail pane narrows to 320 |
  | 960–1199 | The detail pane becomes a drawer over the page, opened with Enter or A on a row |
  | Under 960 | The rail collapses to a 64 px icon rail with labels on focus |

- **Steam Deck (1280 × 800, 7 inches).** At `ui.scale` 1.0, 15 px text lands at about 11 device px, which is too
  small. Recommend a **Deck default of 1.35**. That gives a CSS viewport of about 1280 × 800, so the drawer layout
  applies and 15 px text renders at about 15 device px. Pad mode is on.
- **4K.** It behaves like 1080p at 2× device px. Fonts are vector, so they stay crisp as long as the renderer draws at device
  resolution, which the chosen renderer must confirm (renderers.md). If the in-game renderer is ImGui, atlases are baked per effective size, as EldenNet already
  does.
- **Ultrawide 21:9 and 32:9.** Content is capped at 1.9 × H and centred, with the game visible and dimmed either
  side, as in §3.6.
- **75%** suits desk players at 1440p and above who want SkyrimNet-like density. The compact table rows can be
  turned on with it.

### 3.13 HDR

EldenNet detects HDR from the swapchain. In HDR it linearises the colours and scales them by
`paper_white_nits / 80`, with a default of 203 (NPC §10.7). For GraceUI:

- **Compositing.** The UI is authored and rendered in sRGB (8-bit), then composited in linear light by
  EldenNet's overlay:
  - in scRGB (FP16) swapchains: linearise, then multiply by `paper_white / 80`;
  - in HDR10 (10-bit PQ): linearise, convert BT.709 to BT.2020, scale to nits and encode with PQ.
  The palette is defined once, in sRGB, so there is no second HDR palette to maintain.
- **Ceiling.** The brightest UI colour is `ink.hi` (relative luminance 0.76), which comes out at about 154 nits at
  the default 203 paper white. `gold.hi` is about 117 nits and `gold` about 82. No pure white, no glow and no
  additive blend may exceed paper white. The grace glow is alpha over the ground, never additive.
- **Opaque backdrop.** In HDR the menu's backdrop is **opaque**. In SDR it is 96% `ground.0` over the game. A
  1,000-nit sun behind a 96% backdrop would still show through at about 40 nits as a bright blotch behind text.
  Alternatively, the game image under the menu is darkened first; GraceUI DESIGN §3.14 settles it (decision D26).
- **Banding.** Large dark gradients band in 8-bit and get worse under PQ. Grace uses flat grounds. The only
  gradient (the grace glow) is small and dithered by the ash grain.
- **Saturation.** Gold's saturation is kept moderate, because saturated warm colours look stronger in HDR. Check
  `gold` and `rot` on the HDR test machine (DESIGN §1.3 C1, compat matrix).

### 3.14 Variants and tokens

- **Variants.** v1 ships **Grace** (dark) and **Grace Clear** (high contrast). A light **Parchment** variant
  (dark umber ink on muted parchment) is possible later as a token swap, but it is not needed in v1: the menu is
  used over a dark game.
- **Naming.** Tokens are named by role (`ground.*`, `ink.*`, `gold.*`, `rule*`, `edge`, `rot*`, `ember`, `moss`,
  `glint`, `tint.*`, `font.*`, `size.*`, `space.*`, `radius.*`, `dur.*`), defined once in a token file. They
  compile to CSS custom properties for the HTML renderer, and could compile to an ImGui style if the in-game
  renderer is ImGui. One source means the browser dashboard and the in-game menu cannot drift apart.
- **Icons.** Either our own line icons (1.5 px stroke on an 18 px grid) or a permissively licensed set such as
  Lucide (ISC) or Tabler (MIT), with the licence checked before adoption (EldenNet CLAUDE.md). Elden Ring concepts
  (grace, runes, spirit, flask) are drawn by us as simple abstract marks, never traced from game icons.

### 3.15 What we must not do

- Copy or adapt SeverActions' CSS, class structure, HTML, JS, texts (including taglines such as its rail
  sub-title), images, sigils, map, grain texture or theme names.
- Take any font file out of SeverActions or Magelight.
- Use game fonts, icons, button prompts, UI frames, the Elden Ring logo or ring mark, screenshots or textures, or
  name the theme after a FromSoftware-trademarked term in a way that implies endorsement. "Grace" is an ordinary
  English word, and the repository name the owner chose.
- Copy SkyrimNet's dashboard styling or text. Only its layout idea is used (IA §1).

---

## 4. Points raised for the design, with recommendations (settled in GraceUI DESIGN: 1 → D22, 2 → D24, 3 → D23, 4 → D25, 5 → Open G6, 6 → D26)

1. **Display face.** Recommend **Cinzel** for titles at 20 px or more. Alternatives: Forum, or EB Garamond small
   caps only, which means one family fewer.
2. **Close fade.** 200 ms when the player closes, instant on danger. Recommend accepting both as exceptions to
   DESIGN §12's 300–400 ms out.
3. **The 16 px minimum** (NPC §10.7) applied to the menu. Recommend 16 px or more for prose only, 15 px for UI
   text and 12 px for eyebrows, which is dense enough for settings and tables.
4. **Warning colour.** EldenNet's toast warning `#D2A24C` is as light as Grace gold (1.08:1 apart). Recommend
   `ember #DB9550` for warnings everywhere, always with an icon. This touches NPC §10.1.
5. **Deck default `ui.scale` of 1.35**, with pad mode on.
6. **HDR backdrop opaque**, versus darkening the game under the menu: decide with the renderer choice.

## 5. Provenance

Helper scripts, all ours, kept outside the untrusted copy and outside this repository and not committed (`cssstat.py`,
`blocks.py`, `rules.py`, `contrast.py`). They read the untrusted stylesheets as text with `python3 -I` and print
counts and rules to the terminal only. No SeverActions file was copied anywhere, GraceUI included.
