# The protocol

A space and GraceUI's views speak the bridge protocol, whose one schema is `src/grace-protocol/protocol.toml`
(DESIGN §6). Its frames are JSON objects in UTF-8, the same across the C ABI as on the browser's WebSocket, so a space
has one code path for both views (D169). A space publishes topics and answers commands; the browser's own transport
(`hello`, `welcome`, the ticket, `ping`, `pong` and `bye`) is GraceUI's alone, and a space never sees it. The minors
named on this page, such as (1.6), are the protocol's.

## Frames

Every frame is one object, `{"v": 1, "t": "<kind>", ...}`: `v` is the protocol's major, and every frame of a space's
traffic carries `space`, the space's id (D81). A space meets six kinds:

| Kind | From | Fields | How a space meets it |
|---|---|---|---|
| `sub` | a view | `id`, `topic`, and for a stream `since` | `GRACEUI_EVENT_WATCHED`'s frame, through `poll` |
| `unsub` | a view | `id`, `topic` | `GRACEUI_EVENT_UNWATCHED`'s frame |
| `state` | the space | `topic`, `rev`, `body` | `publish` |
| `rows` | the space | `topic`, `from_seq`, `rows`, `dropped` | `publish` |
| `cmd` | a view | `id`, `name`, `args`, `view` (1.8) | `GRACEUI_EVENT_COMMAND`'s frame, with a reply handle |
| `result` | the space | `id`, `ok` or `error`, `body`, `detail` (1.1) | `answer` |

A minor adds optional fields, topics, commands or capabilities and never removes or changes one; unknown fields are
ignored on both sides (§6.4). A space's page data names in `minor` the protocol minor its components need: the minor of
the GraceUI it names as its floor, not the newest. GraceUI refuses page data above its own minor, naming both minors in
its log and in one notice (D264).

## Topics

A space declares every topic its pages draw in its page data (`[[topic]]`, in [The space and its pages](pages.md)), at
most 64.

- **A state** is one body with a revision number, replaced whole on each change: a status, the settings, one record's
  page. There is no diffing or patching (§6.1).
- **A stream** is a bounded ring of rows with rising sequence numbers, only appended to: activity, events, a log. Its
  `ring` is the rows kept for a view that comes later.
- **A family** (1.5, D215) is a set of state topics, `<id>/<key>`, such as one for each row of a master list. Each
  member is watched and published on its own, never the family itself.
- **`pc_only`** (1.20, D264): GraceUI refuses a paired device's watch of the topic, or of a member of a family so
  marked, with `not_now` and "Only on the PC the game runs on."; the space never hears of the watch.
- **`private`** (1.21, D274): the topic holds a player's or a model's words. The lint keeps it to the blocks that draw
  it: never a section's status, a switch's state, a filter's or a setting's choices, the scope or a quick switch.
  Nothing of any topic reaches search, a page's title, a URL or browser storage, private or not.

`settings` is the framework's own topic: [The settings description](settings.md) gives its body and commands.

A view watches a topic while a page that draws it is open. The space hears `GRACEUI_EVENT_WATCHED`, with the view's
`sub` frame, once the first view (the in-game menu or a browser) watches a topic, and `GRACEUI_EVENT_UNWATCHED`, with
its `unsub` frame, once none does, the menu's close included (D169, D171). With nothing watched a space builds nothing
(§2.4).

Every word a player reads comes ready from the space ("2.5 s", "badly hurt", "3 min ago"), so both views show the same
words and the in-game view formats nothing (§6.1). Words are always text: a view never reads markup in them (D81).

## Publishing

`publish` takes one `state` or `rows` frame of the mod's own space, at most 256 KiB, and copies it before it returns.
GraceUI parses it there, on the publisher's thread, and both views draw the newest (§2.4).

<!-- snippet: frame -->

    {"v":1,"t":"state","space":…,"topic":…,"rev":…,"body":{…}}

- **A state** carries `rev`, its revision, and `body`, an object: the whole body, replacing the one before. The
  browser's server sends a state when its revision changes, at most 4 times a second (§2.4).
- **A stream's rows** come in batches: `rows`, up to 100 in order, each an object of at most 16 KiB carrying its
  `seq`; `from_seq`; and `dropped`, the rows lost to the ring.
- **A cleared stream** (1.1, D172): a batch whose `from_seq` is past a view's next row starts the stream over there, so
  an empty batch past a gap clears the stream in every view at once.

| `publish` answers | When |
|---|---|
| `GRACEUI_OK` | Taken; dropped if no view watches the topic |
| `GRACEUI_REFUSED` | Another space's frame, a topic the space does not declare or not of its shape, too long, or not a frame |
| `GRACEUI_NOT_YET` | Before `register_pages` |

## Commands

A view changes nothing itself. It sends a command, and GraceUI routes it to the space that owns the page, whose one
entry point checks it and answers (invariant 3). A view is untrusted input: the space checks types, bounds, keys and
guards itself, never only in a view (§7.3).

Every command a space's pages send is named in its page data's `commands`, at most 64, with `settings.set` and
`settings.reset` among them if it has settings. GraceUI checks the framework's commands' arguments before the space
sees them.

A command comes through `poll` as `GRACEUI_EVENT_COMMAND`: its `cmd` frame in the event's buffer, and `reply`, the
handle `answer` takes.

| Member | Meaning |
|---|---|
| `id` | The view's number for the command, which GraceUI sets in the result |
| `name` | One of the space's commands |
| `args` | An object of at most 16 KiB, checked by the space |
| `view` | Who sent it, from the channel it came on: `game` (the in-game menu), `browser` (a browser on this PC) or `device` (a device paired at home); whatever a view put there is never kept (1.8, D224) |

GraceUI already refuses a device what the space marks sensitive, so a space reads `view` only for rules of its own, such
as its wording or a log line (D224).

## Answers

`answer` takes the result frame of the command `reply` names:

<!-- snippet: frame -->

    {"v":1,"t":"result","space":…,"id":…,"ok":true}

or with `error` and an optional `body`. GraceUI sets its space and id and routes it to the one view that asked, once.

| Member | Meaning |
|---|---|
| `ok` | Done |
| `error` | One of the codes below |
| `detail` | The reason in the space's own words, which a player reads: at most 256 bytes, never a secret, a payload or a path (1.1, D172) |
| `body` | The command's own answer, an object of at most 256 KiB: a page of rows, a draft |

When a refusal has a `detail`, both views show it in place of the code's words, and focus returns to the control that
sent it with the reason under it (§5.7). A refused command's control goes back to its previous value (§2.8).

| Code | Meaning | What the view shows |
|---|---|---|
| `bad_message` | Not JSON, not an object, missing fields, wrong types | Nothing (logged once, without the payload); after 3 the connection closes |
| `unknown_topic`, `unknown_command`, `unknown_key` | Not in this space's version | "Not available in this version" |
| `invalid_value` | Wrong type or out of bounds | The field's error state, with the bounds in words |
| `locked` | A later-version, always-on or developer-only item | The row's badge |
| `not_now` | Not allowed at this moment | The reason in words |
| `busy` | The same action is already running | "Already running" |
| `confirm_required` | A destructive command without `confirm` | The confirmation |
| `conflict` | A save against a revision no longer the newest (1.7, D217) | "Changed elsewhere since you opened it.", with Reload |
| `unauthorised` | No or stale session cookie or device cookie, or a stale ticket | The page reloads once; a device is asked to pair |
| `rate_limited` | Over the command rate | Retried once after 1 s |
| `version` | Protocol major mismatch | "This page needs updating" |
| `internal` | A fault in the space, or in GraceUI, while handling it | "Something went wrong; the log has the details", and the control reverts |

| `answer` answers | When |
|---|---|
| `GRACEUI_OK` | Routed |
| `GRACEUI_REFUSED` | Not a result with `ok` or an `error`, too long, or a reply that is unknown, answered already or past its deadline |

A command unanswered for 10 s (`GRACEUI_ANSWER_DEADLINE_MS`) is answered `internal` by GraceUI, with one log line naming
the space (D169). A space whose work takes longer answers at once and publishes its progress on a topic, as EldenNet's
slow commands do (D169), or names the command slow.

### Slow commands (1.6)

A space may name up to 8 of its own commands in `slow_commands`, such as a model's draft (D216). Settings commands are
never slow. A slow command is answered within 90 s (`GRACEUI_SLOW_ANSWER_DEADLINE_MS`), whichever view sent it; past
that GraceUI answers it `internal`. It still counts once in a view's 20 a second and in the space's queue of 32, and
other commands are not held behind it. In game the control that sent it reads "Working…" and takes no press until its
answer comes.

### Confirmations

A destructive action has a confirmation and sends `"confirm": true`; a destructive command needs `confirm: true` and an
exact target (§7.3). A command without the `confirm` it needs is answered `confirm_required`, and the view shows its
confirmation. GraceUI answers that itself for a command a destructive action sends, from every view, before the space
hears of it; a space still checks `confirm` on its own destructive commands. A row action's `confirm_required` answer with words in its `detail` is asked as that question (the browser's confirm dialog,
the game's modal with Enter) and sent again with `confirm` (1.4, D209). An action's `confirm_after_ms` makes its second
press count only after a wait of up to 3000 ms (1.21, D274). `settings.reset` of a page or all needs `confirm: true`.

### Sensitive commands (1.8)

A space names in `sensitive_commands`, at most 16, the commands a paired device may not send. GraceUI refuses each from
a device itself, with `not_now` and "Only on the PC the game runs on.", before any confirmation is asked (D221); so too
a sensitive setting's change, every secret setting and every row secret's command. The commands that only a `pc_only`
page names are sensitive as well (D264).

### File commands (1.9)

A space names in `file_commands`, at most 4, the commands that take one uploaded file, each sensitive, not slow and not
a settings command (D226). Only the upload block sends one, in the browser on this PC. GraceUI takes the file by its own
route, checks its size (at most 32 MiB) and its SHA-256, and gives the bytes to the space with the command:
`GRACEUI_EVENT_COMMAND` writes them into the event's `file` buffer beside the frame (ABI 1.6). A buffer too small
answers `GRACEUI_FULL` with both lengths set, and the event waits for the next poll; GraceUI keeps no copy once it is
written, and no file name is sent. A mod whose event is smaller than ABI 1.6's never gets a file command: GraceUI
answers it `not_now` itself. The space answers it as any command. A `cmd` that names a file command is refused (`invalid_value`,
"This takes a file: add it with its button."). The block's `accept` is a hint; the space checks the file.

### Rows a page at a time (1.14)

A table block, or a page's master list, may read its rows from a command of the space's instead of a topic:
`page = { command, args, changes }` (D254). The command is neither a settings, a file nor a slow command; `args` is an
object of at most 4096 bytes; `changes` is a state topic of the space's, not a family, whose change means new rows.

- The view sends the command with its `args` and, as the player sets them, `filters` (each filter's chosen value by its
  column), `sort` (its `key` and `direction`), `search` (trimmed, if it holds words), `scope` on a scoped page, and
  `cursor` for Show more.
- The space answers in the result's `body`: `{"columns", "rows", "next", "total"}`. The view reads at most 200 object
  rows; `next` is the cursor for Show more, and with `total` the view shows "1,234 in all".
- Any change of filter, sort, search or scope asks for the first page again. A change of the `changes` topic asks for
  it again while only the first page shows; past it, "New rows · Show" waits for the player.
- A refusal's words stand in for the rows. Nothing a page answered is stored or searched.

## Limits

| Limit | Value |
|---|---|
| A frame from a view: a `sub`, an `unsub` or a `cmd` | 16 KiB (`GRACEUI_FRAME_FROM_PAGE_MAX`, which always suffices for poll's buffer) |
| A frame to a view: a `state`, `rows` or a `result` | 256 KiB (`GRACEUI_FRAME_TO_PAGE_MAX`) |
| A stream batch | 100 rows, each at most 16 KiB |
| A view's frames | 20 a second per connection, every frame counted; 32 subscriptions per connection |
| Each space's command queue | 32 commands; a full queue drops the newest with a notice and never blocks the present thread |
| An answer | 10 s; a slow command's 90 s |
| An uploaded file | 32 MiB (`GRACEUI_UPLOAD_MAX`) |
| An action's `confirm_after_ms` | 3000 ms; a longer one is drawn as 3000 by both views and refused by the lint |
| A space's page data | 64 topics, 64 commands, 8 slow, 16 sensitive and 4 file commands, 16 categories, 128 pages, 12 quick switches |
| Page data and settings description | 1 MiB each at `register_pages` (EST, D169) |

Limits apply per connection and per space (§6.8).
