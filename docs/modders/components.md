# Components

A `component` block draws one of the space's topics with one of GraceUI's components. The space publishes the topic's
body; GraceUI draws it, in game and in the browser, and draws nothing the space has not published. A block's topic is
watched while its page is open, so the space publishes it only while someone is looking.

## Props

A block's `props` shape how its body is read:

| Prop | Meaning |
|---|---|
| `field` | A member of a state topic's body to draw, so that one topic can feed several blocks (D185) |
| `empty` | The space's own words, 1 to 80 characters, once its topic has spoken and has nothing to show; "Nothing to show." without them (D186, D249) |
| `newest_first` | A log tail's newest row first (D187) |
| `text` | A statement's words: a statement takes no topic |

## Drawn in both views

| Component | Topic | Its body |
|---|---|---|
| `stat_tiles` | state | `{tiles: [{label, value, unit}]}`: a value in the display face, its unit small after it |
| `computed_sentence` | state | `{sentence}`: one sentence, wrapped |
| `key_value_list` | state | `{items: [{label, value}]}` |
| `check_list` | state | `{checks: [{label, state, detail}]}`, `state` one of `pass`, `warn`, `fail`, `wait` (Passed, Warning, Failed, Waiting) |
| `table` | state, or a page source | `{columns: [...], rows: [...]}`, below |
| `report_preview` | state | `{parts: [...], share}`, below (1.12, 1.19) |
| `live_feed` | stream | Rows `{seq, time, line}`, newest first, moving by themselves |
| `log_tail` | stream | Rows `{seq, time, line}`, oldest first unless `newest_first` |
| `statement` | none | `props.text`: words of the page's own, such as "This arrives in a later version." |

A stream's rows carry `seq`, rising; other members of a row may be filtered on by a filter bar (1.16). Words are always
text: a view never reads markup in them (D81).

### Tables

A table's body has its `columns` and its `rows`:

- Each column has an `id` (the row's member it shows) and a `label`.
  - `numeric` aligns it as a number.
  - `drawer` shows it only in the row's drawer, opened from the row (1.15).
  - `switch: {command, key, on, off}` makes each cell a switch. It shows the state the space published, and sends
    `command` with the row's `key` and `on` (D181).
  - `choice: {command, key, choices}` makes each cell a drop-down of one to sixteen `{value, label}` (1.24, D279). It
    shows the label of the value the row holds; a value that is none of the choices shows as its own until changed. A
    change sends `command` with the row's `key` and `value`. Answer `confirm_required` with your question, and the view
    asks it before sending the same again with `"confirm": true`; any other refusal's words show under the row. A row
    without the `key` member shows the label alone, and the first column, which names the row, is never a drop-down.
    In game it is a field in its column's place, stepped with ← and →; in the browser, a `<select>`. One switch column
    and one choice column a table.
- Each row is an object of its members.

On the block:
- `row_actions`: up to three buttons on each row. Each sends its `command` with the row's `key` member.
  - A destructive one asks first.
  - One with `capture` takes a key or a pad chord from the player first (1.2, 1.4).
- `row_secret`: a write-only secret per row (1.2). Its `column` holds the status the space publishes: "set", empty
  (Missing), or "none" for a row that needs none. Its `command` sets it, sent with the row's `key` and the value,
  which no view keeps; an empty value clears it.
- `filter_bar`: filters, a sort and a search over the rows the view holds, never sent to the space (1.11).
- `page`: rows read a page at a time from a command of the space's, with Show more (1.14). See the protocol part,
  *to come*.

### Report previews

A report preview shows what a report would hold before the player sends or saves it (1.12):

- Each part has an `id`, a `label` and its `fields`: `{label, value, code}`, `code` drawn in the mono face with its
  lines kept.
- A part may be `optional`. Its switch sends the block's `command` with `{id, on}`, and the space publishes whether it
  is `included`.
- The block's `share` offers Copy, Save or both (1.19).
  - The body's `share` is `{text, file}`, the text the space has scanned, or `{blocked}`, its words for what to change
    first.
  - Copy puts the text on the clipboard. Save sends the space's command in game, where the space writes the file, and
    downloads the text in the browser.
  - Neither is offered on a paired device.

## Other blocks that draw a topic

| Block | Its body |
|---|---|
| `editor` | A document of fields on a state topic, or a family's member on a master page: edited in the view, saved through the space's commands, a draft beside it (1.7, D217) |
| `document` | `{title, blocks}` on a family's member: headings, paragraphs, lists, notes, code and plain tables, with links between documents (1.12, D251) |

## Not drawn yet

`card_grid`, `segmented_switch`, `journal`, `transcript` and `action_button` are named in the protocol and drawn by
neither view yet. In game a block of them says "Shown in the browser view."; in the browser, "Not shown in this
version of the browser view." `master_detail` and `filter_bar` are a page's `master` and a table's `filter_bar`
fields, not blocks. `wizard_steps` is drawn by a flow from its tabs, and the lint refuses it as a block (1.23).
