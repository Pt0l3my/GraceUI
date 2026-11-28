# Features by version

A mod meets two version numbers.

- **The bridge protocol** (`src/grace-protocol/protocol.toml`): its page data names the protocol minor it needs, its
  `minor`. GraceUI refuses page data above its own minor, saying which it needs (D264); a view of an older minor draws
  nothing it does not know.
- **The C ABI** (`src/grace-abi/include/graceui.h`): a mod connects naming the ABI version it was built against. The
  table of slots only grows within a major; every struct starts with its size, so an older mod's smaller struct is read
  as it was. A mod asks whether a feature is there by its cap string (`has_cap`), never by GraceUI's version.

Both majors are 1. The decision named in each row is in `docs/DESIGN.md` §13.

## The protocol

| Minor | GraceUI | Decision | What it adds |
|---|---|---|---|
| 1.0 | 0.2.0 | D159, D160 | The envelope and message kinds, the settings description, a space's page data: categories, pages, tabs, sections, settings and shortcut blocks, components bound to topics, header actions |
| 1.1 | 0.2.0 | D172 | A result's `detail`, the refusal in the space's own words; a stream cleared by an empty batch past a gap |
| 1.2 | 0.2.8 | D192 to D195 | A table's row actions and row secret; the form block; a text setting's `choices_from` |
| 1.3 | 0.2.12 | D202 | A section's chip, live status and own actions |
| 1.4 | 0.2.16 | D209 | A row action that captures a key or a pad chord first |
| 1.5 | 0.2.19 | D215 | Topic families; a page's master list, its tabs and sections the chosen row's detail |
| 1.6 | 0.2.20 | D216 | Slow commands, answered within 90 s |
| 1.7 | 0.2.21 | D217 | The editor block, and the `conflict` error |
| 1.8 | 0.2.25 | D221, D224 | Sensitive commands and settings, refused from paired devices; the view that sent a command |
| 1.9 | 0.2.26 | D226 | File commands, the upload block and its ticket; welcome's view |
| 1.10 | 0.2.42 | D248 | A category at the rail's foot |
| 1.11 | 0.2.43 | D250 | A table's or a master list's filter bar |
| 1.12 | 0.2.44 | D251 | The document block; a form's prose field; the report preview's command |
| 1.13 | 0.2.45 | D252 | A space's quick switches; a page's live updates |
| 1.14 | 0.2.47 | D254 | A space's scope and its scoped pages; tables and master lists read a page at a time; a filter's own choices |
| 1.15 | 0.2.49 | D256 | A header action that is a switch on a state topic's boolean |
| 1.16 | 0.2.50 | D257 | A filter bar on a stream, a live feed or a log tail |
| 1.17 | 0.2.51 | D258 | A page in the browser alone, shown with a setting, gated, with a ribbon |
| 1.18 | 0.2.55 | D262 | A filter's own choices on any bar, words for its rows' values |
| 1.19 | 0.2.56 | D263 | A report preview's Copy and Save, and the text it shares |
| 1.20 | 0.2.57 | D264 | A page and a topic on this PC alone, never on a paired device |
| 1.21 | 0.2.67 | D274 | A confirmation that waits before its second press counts; a private topic |
| 1.22 | 0.2.70 | D277 | A category's line icon, one of GraceUI's own |
| 1.23 | 0.2.71 | D278 | A page that is a flow, outside the rail, its tabs its steps; an action that opens a page once answered |

## The C ABI

| Minor | GraceUI | Decision | Cap | What it adds |
|---|---|---|---|---|
| 1.0 | 0.1 | D69 to D78 | `notices`, `panels`, `textures`, `faces`, `input`, `world-state`, `hotkeys` | `GraceUI_RequestApi` and `connect`; notices; panels and their checked draw lists; textures and faces in the registration window; the input holder; world-state providers; hotkeys |
| 1.0 | 0.2.0 | D169 | `pages` | `register_pages`, `publish`, `answer`, and poll's protocol events: a mod's space in both views |
| 1.1 | 0.2.1 | D176 | `holder-events` | The holder's keys, characters, pointer and pad through poll (`GRACEUI_INPUT_EVENTS`); `set_caret` for the IME |
| 1.2 | 0.2.2 | D180 | `display` | The display at connect and `GRACEUI_EVENT_DISPLAY`; faces baked at the player's scale (`GRACEUI_FACE_SCALED`) |
| 1.3 | 0.2.14 | D204 | `pad-chords` | A hotkey's pad chord |
| 1.4 | 0.2.16 | D210 | `hotkey-bindings` | `describe_hotkey`, and `GRACEUI_EVENT_HOTKEY_CHANGED` when the player rebinds one |
| 1.5 | 0.2.17 | D212 | `wake` | `wake_event`, a handle set when something is kept for the mod |
| 1.6 | 0.2.26 | D226 | — | A file command's file, uploaded in the browser on this PC, in poll's event |
| 1.7 | 0.2.38 | D243 | `hotkey-by` | Whether a hotkey was pressed by its key or its pad chord |
| 1.8 | 0.2.52 | D259 | `on-present` | `set_on_present`: a panel drawn in each frame by its mod's callback, timed by GraceUI |
| 1.9 | 0.2.61 | D268 | `plain-text` | Whether Plain text is on, at connect and in `GRACEUI_EVENT_DISPLAY` |
| 1.10 | 0.2.69 | D276 | `pad-keyboard` | `pad_keyboard`: Steam's floating keyboard for the holder's field |
| 1.11 | 0.2.72 | D280 | `hotkey-pause` | `set_hotkey_enabled`: one of the mod's own hotkeys paused and resumed, a press under way kept whole |

A cap names what a mod may rely on; GraceUI's version does not. A mod that finds a cap missing goes without the
feature, and says so in its own words if the player would notice.
