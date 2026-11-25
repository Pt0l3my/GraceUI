# Proposal 0015: A report's copy and save

**Status: proposal, 2026-11-25, sent to EldenNet's session for its formats (§6). To be settled under the owner's
instruction of 2026-11-24 to carry on without asking, once EldenNet answers, and built for GraceUI 0.6's "Bug
report's copy and save" (DESIGN §10), sooner if EldenNet wants it.**

## 1. What there is now

- A `report_preview` block (D251, protocol 1.12) shows its topic's `parts` in both views, with a switch on each
  optional part that sends the block's `command` with `{id, on}`.
- EldenNet's Tools › Bug report (its PAGES §4.6.8) builds that preview at 0.2. It plans `report.copy` and
  `report.save` for 0.6, marked sensitive, "copy or save only after EldenNet's deterministic scan passes".
- GraceUI's own rules: PV7, "clipboard only on a click, after a preview"; PV5, relative paths only; D221, a sensitive
  command is refused from a paired device. DESIGN §6.6's command table says of `report.copy`: "In game only: EldenNet puts
  the scanned text on the clipboard on this click. The browser copies from the previewed text itself."
- Neither view offers Copy or Save, and the protocol has no way for a space to hand over the text to copy.

## 2. Proposed: the shareable text, and its scan, from the space

The preview's body gains one optional member:

```json
{"parts": [...],
 "share": {"text": "<the whole report, plain UTF-8, as the space's scan passed it>",
           "file": "eldennet-report-2026-11-25.txt"}}
```

or, while the space's scan does not pass,

```json
{"parts": [...], "share": {"blocked": "<the space's words: what to change before it can be shared>"}}
```

- `text` is the space's to compose and to scan: GraceUI never builds it from the parts, never changes it, and never
  stores it. At most 256 KiB (a frame's bound).
- `file` is a name the browser's download takes: 1 to 64 of `a-z`, `0-9`, `-`, `_` and `.`, ending `.txt`, with no
  path. GraceUI checks it and drops a `share` whose name fails.
- With no `share`, or a `blocked` one, Copy and Save are shown greyed, with the space's `blocked` words under them, if
  any (invariant 2: nothing is promised that the space has not published).

## 3. Proposed: Copy and Save on the block

The block says which it offers:

```toml
{ kind = "component", component = "report_preview", topic = "report", command = "report.part",
  share = { copy = true, save = "report.save" } }
```

- **Copy** (`copy = true`): GraceUI itself puts `share.text` on the clipboard, in both views, on the click alone
  (PV7). In the browser it uses the Clipboard API, on the click's own event. In game, GraceUI's own thread uses the
  Windows clipboard, as plain text. The button says "Copied" for 2 s. GraceUI keeps no copy and logs nothing of it.
  The space need not touch the clipboard, which is why §6 (a) asks EldenNet to drop `report.copy`'s clipboard work.
- **Save** (`save = "<a command>"`):
  - In game: the space's command, sent with `{}`. The space writes the file where it keeps its own (GraceUI never
    writes a mod's files) and answers with words that name it relative to the game's folder (PV5), shown under the
    button.
  - In the browser on this PC: a download of `share.text` as `share.file`, from a Blob made on the click, with no
    request to the server. The space's command is not sent; §6 (c) asks whether EldenNet wants it told.
- **Not on a paired device**: neither button is drawn in the view of a device paired at home (D224), since the text is
  this PC's. `save` must be one of the space's `sensitive_commands` (D221), which the lint and registration check.
- **Busy and refused**: a Save in game is a command as any, answered within its 10 s (D169). Its refusal's words show under the
  button, and focus returns to it (§5.7).

## 4. What changes

- `protocol.toml`: the block's `share` (`copy`, a bool; `save`, an id), and the preview body's `share` as the views
  read it. Protocol 1.18.
- The page lint and registration: `share` only on a `report_preview`; `save` declared, and sensitive.
- Both views: the two buttons under the preview, greyed with the space's words while `share` is absent or blocked.
  In the browser, the Clipboard API and the download. In game, a clipboard request to the host (a new host-side
  function on GraceUI's own thread, `user32`'s clipboard calls, already in `graceui.dll`'s allowed imports).
- The demo space's lamp report gains a `share` with a copy and a save, `report.save` answered by grace-dev and the
  harness.
- No change of the ABI.

## 5. Options

(a) **As above** (recommended): GraceUI copies in both views; the space saves in game; the browser downloads.
(b) **The space does both, in both views**, by commands (`report.copy`, `report.save`). GraceUI only shows the
buttons. Every mod that wants a copy button then needs clipboard code of its own, and the browser's copy would have to
come back from the game.
(c) **Not now**: EldenNet keeps its preview without copy or save until 0.6.

## 6. Questions for EldenNet

(a) Will EldenNet hand over its scanned text as `share.text`, and let GraceUI do the clipboard in game, in place of
    `report.copy`'s own clipboard work? If EldenNet wants to know a copy happened (to count it, or to log "copied"),
    GraceUI can send `report.copy` after the copy, as a notice of it, with no text.
(b) Is the text's form EldenNet's alone? (Recommended: yes; GraceUI never composes it.)
(c) Should the browser's Save send `report.save` too, so that EldenNet also keeps a copy in its folder, or download
    alone (recommended: download alone; the player chose the browser)?
(d) The file name: `eldennet-report-<date>.txt`, or another of EldenNet's?
(e) The blocked words: one sentence of EldenNet's, such as "Remove the path in What happened before sharing."?
