# Secrets, privacy and spoilers

GraceUI's views show only what a space publishes, keep none of it, and never show a secret. These are the framework's
rules for every space: invariants 1, 2, 5 and 6, and the privacy rules PV3, PV6 and PV7 (DESIGN §4.9). What a mod's
pages may show of its own story is the mod's own rule (below). Protocol minors are named as (1.8).

## Projections only

A view shows only what its space published (invariant 1): its page data, its settings description and its topics'
bodies.

- A topic's body reaches every view that watches it as the space encoded it: GraceUI gives the same bytes to the
  in-game view and to the browser (§2.9). A member that no block draws still reaches the browser, so a body holds what
  its page shows and nothing more.
- Every word a player reads comes ready from the space, and words are always text: a view never reads markup in them
  (D81).
- Every frame carries its space. `publish` refuses another space's frame, and a view's watches and commands stay
  within the space whose page sent them (D81).
- A view is untrusted input. Every change goes through the space's one entry point, which checks it
  ([Commands](protocol.md#commands), invariant 3).

## Absence is silent

A view shows, counts, greys or hints at nothing its space has not published (invariant 2).

- A component draws only its topic's body. A block whose topic has spoken and has nothing to show says the space's
  `empty` words, or "Nothing to show." ([Components](components.md#props)).
- A quiet mod's notice is answered `GRACEUI_OK` as any other and kept on GraceUI › Installed mods, so the mod learns
  nothing of the player's switch (D181).
- GraceUI's search reads page data and settings alone (§4.3, D274). A page's name, description and help, and a
  setting's label and help, are found by search whatever the player has reached; nothing of any topic reaches search, a
  page's title, a URL or browser storage.

## Secrets are write-only

A key or a password is set, replaced or cleared, and never read back: a view sees whether one is set (invariant 5,
§7.4). A space keeps two kinds.

| Kind | What a view sees | Since |
|---|---|---|
| A `secret` setting | Its value in the settings topic as `"set"` or `""`; its default is empty, never a value | 1.0 |
| A table's `row_secret` | Its column's status, as the space publishes it: `"set"`, empty (Missing), `"refused"` or `"none"` (Not needed) (D194, D197) | 1.2 |

- **The settings topic.** A space publishes its secrets' values as `"set"` or `""`. GraceUI holds the topic to that: a
  value it finds there is sent as `"set"`, and its log says once "space ‹id› published a secret's value: only whether
  it is set was sent".
- **A row secret.** Its command reaches the space with the row's `key` and the value; no view keeps the value, and the
  field is cleared at once. The server sends back its result without a body, so a view gets ok or a refusal's words.
  GraceUI cannot mask a table's cells: the space publishes the status only.
- **Logs.** Neither the server nor the registry logs a command's arguments (D194), and settings payloads are never
  logged (§7.4).
- **The masked field** never puts its value in a log line, an error, the protocol's echo or browser storage, and clears
  itself on close (§7.4).
- **Paired devices** never set a secret: every secret setting and every row secret's command is refused from them
  (1.8, below).

A space never puts a secret's value in a topic, a result's `body` or `detail`, a notice or a report.

## This PC and paired devices

A browser on this PC reaches the browser view on loopback. Another device at home reaches it only while the player has
`dashboard.lan` on, and each only after pairing with a code shown in game (invariant 4, §7.2 item 16). GraceUI refuses
a device what a space keeps to this PC itself, with `not_now` and "Only on the PC the game runs on.", before the space
hears of it and before any confirmation is asked (D221, D264).

| What the space marks | What a paired device gets | Since |
|---|---|---|
| A command in `sensitive_commands` | Refused | 1.8 |
| A setting marked `sensitive`, and every `secret` setting | Its change and reset refused | 1.8 |
| Every row secret's command | Refused | 1.8 |
| A command in `file_commands` | Never: a file is sent by the upload block, in the browser on this PC alone | 1.9 |
| A report preview's Copy and Save | Not offered | 1.19 |
| A page marked `pc_only` | Left out of its page data, with every link to it; a category it leaves empty goes too. Its commands, where no page a device sees names them, are sensitive; its settings are left out of the device's settings topic | 1.20 |
| A topic marked `pc_only` | Its watch refused; the space never hears of it | 1.20 |

The lint and `register_pages` keep `pc_only` whole (D264): a `pc_only` topic is drawn on `pc_only` pages alone, and a
setting homed on a `pc_only` page is shown on no page a device sees and is sensitive or a secret. A topic a `pc_only`
page shares with other pages stays readable by a device.

A command's `view` says which channel sent it: `game`, `browser` or `device` (1.8, D224). GraceUI has already refused a
device what the space marks, so a space reads `view` only for rules of its own ([Commands](protocol.md#commands)).

## Private topics

A topic marked `private` (1.21, D274) holds a player's or a model's words. The lint keeps it to the blocks that draw it:
never a section's status, a header switch's state, a filter's or a setting's choices, the scope or a quick switch. A
view does not read the mark, and changes nothing for it.

## What GraceUI keeps and logs

GraceUI keeps nothing of a space's pages: no page content in any log line, in browser storage or in caches, no console
output in release builds, and no analytics (PV3).

- **Its log lines** hold GraceUI's own words, numbers and mods' ids, never a path, a user or machine name, or game text
  (PV3, D230). A copy of GraceUI is named by its name relative to the game's folder, never a path (D135). The lines
  about a mod are in [Troubleshooting](troubleshooting.md#graceuis-log).
- **Never logged**: a command's arguments, a setting's value, a secret, a view's bad message's payload (the message is
  logged once without it), the session cookie or a pairing code.
- **The browser's own transport** (`hello`, `welcome`, the ticket, `ping`, `pong` and `bye`) is GraceUI's alone: a space
  never sees the session cookie, a device's cookie or a ticket ([The protocol](protocol.md)).
- **An uploaded file** goes to the space with its command; GraceUI keeps no copy once it is written to the mod's event,
  and sends no file name (1.9; [File commands](protocol.md#file-commands-19)).
- **The `log` slot** answers `GRACEUI_NOT_YET` until the increment that builds it ([Log](abi.md#log)): a mod keeps its
  own log meanwhile.

## The clipboard

GraceUI puts text on the clipboard only on a click, after a preview (PV7). In a mod's pages a report preview's Copy
keeps it (1.19): the preview shows what the report holds, and its body's `share` holds either the text the space has
scanned, which Copy puts on the clipboard, or `blocked`, the space's words for what to change first
([Report previews](components.md#report-previews)).

## Nothing from outside

No page requests anything outside its own origin: no CDN, no remote font, no analytics (invariant 6, PV6).

- A mod supplies no HTML, script or CSS: its pages are data, drawn by GraceUI's components in both views (D81).
- Fonts, assets and docs ship with GraceUI and are served from this PC. The server serves only the files
  `graceui.toml` lists (§7.2 item 15), with `default-src 'self'` and `connect-src 'self'` in its CSP (§7.2 item 10).
- The space check fails a space's pages on a CSP violation or a request outside the page's origin
  ([Troubleshooting](troubleshooting.md#the-tools)).

## Spoilers

GraceUI's own form of spoiler rules is invariants 1 and 2 (§4.8): a view shows only what its space published, and
absence is silent. What a mod's pages may reveal of its game, and when, is the mod's own rule, kept by what it
publishes. EldenNet's spoiler rules, SP1 to SP9, are in EldenNet's PAGES §4.8, and its privacy rules PV1, PV2, PV4,
PV5 and PV8 in its PAGES §4.9 (G20).
