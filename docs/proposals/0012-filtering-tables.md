# Proposal 0012: Filtering and sorting a table

**Status: proposal, 2026-11-24, put to the owner and EldenNet's session (G43). EldenNet's session answered §6 the same day (§7). The owner chose (a) the same day, as recommended. Built as D250 in GraceUI 0.2.43 (protocol 1.11); where this
text and the design differ, the design holds.** EldenNet's PAGES gives four pages a
filter bar above their table: Requests (0.2), Named NPCs and Memories (0.3), and Events (0.4). §2.5 lists a filter
bar and a table "with sort" among the components, and §3.8 and §4.2 describe how they look. `filter_bar` has been a
name in the protocol's component list since 1.0. But neither view draws one: a `filter_bar` block shows "Not shown in
this version", and a table neither filters nor sorts. This adds a framework feature every mod may use (G16, G18), so
it is put here rather than settled alone. A bare § is a section of [DESIGN.md](../DESIGN.md).

## 1. What EldenNet's pages ask for

| Page (version) | Filters | Sort | Search |
|---|---|---|---|
| Requests (0.2) | slot, purpose, outcome | yes | none, by its rule |
| Named NPCs (0.3), a master list | status, region | last met, first met, name | the names as shown |
| Memories (0.3) | NPC, basis | time, weight | none |
| Events (0.4), a paged history | family, who, region | yes, with a direction | none, by its rule |

The first three hold their rows in a state topic, as every table does today. Events reads its rows a page at a time
with `history.page` (§6.6), because a journey's events can number thousands.

## 2. Proposed: a table's own filter bar (the next protocol minor)

A table block, or a page's master list, may carry `filter_bar`:

```json
"filter_bar": {
  "filters": [{"column": "status", "label": "Status"}, {"column": "region", "label": "Region"}],
  "sort": [{"key": "last_met_s", "label": "Last met", "direction": "descending"},
           {"key": "first_met_s", "label": "First met", "direction": "descending"},
           {"key": "name", "label": "Name"}],
  "search": {"columns": ["name"], "label": "Find a name"}
}
```

- **Filters**, at most four. Each names one of the table's columns and is drawn as a compact select. Its choices are
  "All", then the values found in that column of the rows the space has published, in the order the rows give them.
  A value no row has is never offered (invariant 2).
- **Sort**, at most four options. The first is the default. A key is a column, or a member of each row that is not
  shown: `last_met_s` above is a number of seconds, while the column beside it reads "2 h ago". Two numbers are
  compared as numbers; otherwise both are compared as text, letter case ignored. Each option has its own default
  direction (`ascending` unless it says otherwise). The direction button reverses it. Rows that compare equal keep
  the space's order.
- **Search**, optional. A text field matching the named columns, letter case ignored. GraceUI cannot tell what a
  column holds, so the guide states the rule: a space names no column that holds chat or story text. For EldenNet's
  pages, D21 (names as shown only) and EldenNet's PV4 already say so.
- **The view does it all.** Filtering and sorting happen in the view, on the rows already published. No command goes
  to the space, nothing is sent or stored, and nothing is put in a URL. The choices are kept for the session, per page
  and per view, and reset when the space publishes new columns.
- **What it shows.** A line, "12 of 40 shown", appears while a filter or search holds. If nothing matches, the table
  says "Nothing matches these filters." with a Clear filters button. Its own empty words (`props.empty`, D186) stay
  for a table with no rows at all.
- **In game.** The bar is one row of the page's focus order, above the table: ← and → move between its controls; on a
  select, Enter opens it and ↑ ↓ choose, as a choice setting does; Enter on the direction button reverses it; the
  search field is G38's text field. A pad walks it the same way. In the browser view it is §3.8's 40 px strip.
- **The lint** checks that each filter's column is one of the table's own (a column the space publishes later is
  checked when it arrives: an unknown one is dropped, with one log line), that the keys are names, and the counts.
  A `filter_bar` block on its own becomes a lint error that points to the table's field. No mod uses one today, and
  a filter bar means nothing without its table.

*Recommended.*

## 3. Paged history (Events, 0.4)

A table whose rows come a page at a time cannot be filtered in the view: the view holds one page. The space must
filter, sort and page, and the bar's choices go to it as the `history.page` command's `filters` (§6.6 names them
already). **Recommended:** leave this for 0.4, as a proposal of its own once EldenNet's Events and Conversations
pages are designed in detail. §2's field is built so that a paged table can reuse it, with the same words and look,
and with `paged: true` sending the choices to the space instead.

## 4. The options (G43)

(a) **§2 now, §3 for 0.4** (recommended). (b) **§2 and §3 together now**: one change, but paged tables would be
designed before any page uses one. (c) **A filter bar that sends every change to the space**, which publishes the
filtered rows: one design for both kinds of table, but every space must then filter and sort for itself, and each
change costs a round trip in game. (d) **Nothing yet**: EldenNet's pages draw their tables unfiltered until 0.3.

## 5. Cost

`protocol.toml` gains `filter_bar` (three small types) on `page_block` and `page_master`, at the next protocol minor (1.11, D248 having taken 1.10). Then come
the page lint, both views (a new row in game, a strip in the browser), the demo space's Keepers and Lamps pages as
the example, and the view's, the web's and grace-dev's tests and goldens. There is no change to the ABI. About the
size of G38's master–detail (D215).

## 6. For EldenNet's session

1. Do the Requests, Named NPCs and Memories pages hold all their rows in a state topic, so that §2 covers them? Does
   Requests keep a session's calls whole, or as a stream ring (a live feed)? A view can filter a ring too, but its
   rows then come and go.
2. Which hidden sort keys would your rows carry (seconds since met, a weight as a number)?
3. Would Named NPCs' "names as shown" be §2's search over the name column, or a filter of names?

## 7. EldenNet's answers (2026-11-24)

1. Requests reads `calls`, a state topic holding the whole kept set (20 calls, or 200 in developer mode), newest
   first and published whole at each call; its rows come and go only as the oldest drops out. Named NPCs' master list,
   `characters.named`, holds the whole roster. Memories (0.3) will hold one NPC's memories whole, bounded. So §2
   covers all three.
2. Hidden sort keys: Named NPCs, `last_met_s` and `first_met_s` (play-time seconds, larger is later) and the name;
   Requests, `at_ms`, `first_token_ms`, `total_ms` and `tokens`; Memories, the time and the weight, as numbers. A row
   with no value for the key (someone never met has no `last_met_s`) sorts last in either direction.
3. "Names as shown" is §2's search over the name column, which only ever holds the name as the game shows it or the
   veiled label (EldenNet's SP5), so a hidden name can never be matched. It is not a filter of names.

