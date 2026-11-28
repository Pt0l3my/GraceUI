# Threads and budgets

GraceUI runs where its hooks are, on the game's threads, and on a few threads of its own. A mod's code runs on the
mod's own threads: GraceUI never calls a mod on a thread the mod did not choose (§2.9), never calls the mod's code to
wake it, and no call posts work to the game's thread (D71). The one exception is opt-in: an `on_present` callback, on
the present thread, within its budget (ABI 1.8, D259).

## GraceUI's threads

| Thread | GraceUI's work |
|---|---|
| Init, short-lived | Started by the first `GraceUI_RequestApi`, in the copy that holds the mutex: waits for the game's window, installs the hooks, then exits (D71, D72) |
| Present | The menu, every visible panel's newest draw list and the notices, on their layers; the opted-in `on_present` callbacks |
| The game's window procedure | Characters with surrogates paired, the IME, focus loss, hotkeys, the player's exit (D77, D155) |
| Input detours (DirectInput, XInput) | Capture and masking while a holder has input, and until a closing press is released (invariants 7 and 8) |
| Server | The browser view's I/O thread and its worker, while a space asks for the browser view and the player has it on, never beside an anti-cheat (§2.3, D169) |

## Which call, from which thread

| Call | From |
|---|---|
| `GraceUI_Version` | Any thread, at any time after the DLL is loaded; it starts nothing |
| `graceui_sys_find`, `GraceUI_RequestApi` | A thread of the mod's own, never `DllMain`: the first request takes a mutex and starts a thread, which must not happen under the loader's lock |
| `register_pages`, `publish`, `poll`, `answer` | Any thread of the mod's, during GraceUI's warm-up too, never `DllMain`; none waits for a view |
| `set_hotkey_enabled` | Any of the mod's threads: it never waits on the game |
| `pad_keyboard` | The mod's own thread: it may take a moment, as Steam's call does |
| `set_on_present` that replaces or clears a callback, and `disconnect` | Never `DllMain`, nor a thread the present thread may be waiting for: each waits for the frame being drawn to end |
| Every slot but `has_cap` | Never from inside an `on_present` callback: each answers `GRACEUI_REFUSED` |

Every other slot is called from the mod's own threads (§2.3): `connect`, `notice`, `submit`, `request_input`,
`set_world_state` and the rest. A mod takes its events, hotkey presses among them, through `poll` on its own thread.

## The present thread

The present thread never waits and allocates nothing per frame after warm-up, in GraceUI and in every opted-in
`on_present` callback (invariant 10).

- `publish` and `submit` copy on the caller's thread: `publish` parses a frame into a bounded ring, `submit` puts a draw
  list into a triple buffer (§2.3). The cost of a mod's publishing falls on its own thread.
- The present thread reads only the newest snapshot, and never frees one: the publisher's side frees a superseded one
  on a later call (§2.9).
- A panel is drawn from the newest list its mod submitted, one frame or more after the mod built it.
- Nothing is uploaded after load: an upload needs a copy and a fence the present thread must not wait on (invariant 11,
  D76). So textures and faces are registered first, in the mod's registration window (below).

## on_present

A mod that must change a panel in every frame, such as a fade or a label that follows the camera, asks
`set_on_present` for a callback on the game's present thread instead of waiting for its next submit (ABI 1.8, cap
`on-present`, D259). It is set for one of the mod's panels, with the room its lists need:

| `GraceUiPresentHook` | Meaning |
|---|---|
| `vertex_capacity` | The most vertices a list it draws holds: 1 to 13,107 |
| `index_capacity` | The most indices: 1 to 131,072 |
| `command_capacity` | The most commands: 1 to 64 |
| `callback` | `uint32_t (*)(void* user, const GraceUiPresentFrame* frame, GraceUiDrawList* list)`; null clears the panel's callback |
| `user` | Given back to the callback as is |

In each frame the panel is shown, GraceUI calls the callback with the frame and a `list` zeroed but for its `size` and
`command_size`. `GraceUiPresentFrame` holds:

| Field | Meaning |
|---|---|
| `panel` | The panel |
| `frame` | GraceUI's frames this session in which it drew shown panels, counting this one |
| `time_us` | This frame's time: microseconds of a steady clock (on Windows, `QueryPerformanceCounter`'s) |
| `delta_seconds` | Since the frame before it in that count: 1/60 for the first, and at most 0.25 after a pause |
| `x`, `y`, `width`, `height` | The panel's place on the display now, its anchor applied; the list stays in the panel's own pixels |
| `display_width`, `display_height` | The back buffer's size |
| `flags` | `GRACEUI_PRESENT_MENU_OPEN` while GraceUI's menu is open in this frame |
| `spent_us` | The mod's callbacks together in the frame before, on GraceUI's clock: what its budget weighs |

It answers:
- `GRACEUI_PRESENT_KEEP`: draw the panel's newest submitted list, as without a callback;
- `GRACEUI_PRESENT_DRAW`: draw the list it wrote into `list`, from arrays of its own that hold until it returns. GraceUI
  copies it into the room it reserved when the callback was set, checks the copy as `submit` checks a list, and draws
  it in this frame alone. A list that does not fit or fails the check is not drawn (the submitted one is), and is
  logged once.

Any other answer is KEEP. Setting a callback closes the mod's registration window, as its first submit does.

**The rules.** A callback binds invariant 10 as GraceUI's own drawing does: it never waits, takes no lock, allocates
nothing per frame after its first frames, and never lets an exception leave it. It calls no slot of the table: from
inside it each answers `GRACEUI_REFUSED`, `has_cap` alone excepted.

**The budget.** A mod's callbacks together have 500 µs in each frame (`GRACEUI_PRESENT_BUDGET_US`, EST), measured on
GraceUI's clock around each call. Over it in 120 frames running (`GRACEUI_PRESENT_OVER_FRAMES`; a frame the mod's
callbacks did not run in ends the run), or an exception out of one, and every callback of the mod is suspended for the
session:
- GraceUI shows one notice naming the mod ("‹Mod› slowed the game's frames, so what it draws in each frame is off for
  this session") and logs one line (D259);
- its panels then draw their submitted lists;
- `set_on_present` answers `GRACEUI_REFUSED` for a callback; clearing one is taken;
- the suspension is kept by the mod's id, so connecting again does not lift it.

**Taking one out.** `set_on_present` that replaces or clears a callback, and `disconnect`, return only once the old
callback is not running, and it is never called again: the mod may then free what `user` points to, or unload. Each
waits for the frame being drawn to end, for as long as that takes; GraceUI's present thread never waits for them.

## The registration window

Textures and faces are registered once, before the drawing starts, within their budgets (invariant 11).

- **Textures.** A mod's registration window runs from its `connect` until its first `submit` or `set_on_present`.
  `register_texture` copies the pixels before it returns, and GraceUI uploads each once, at the overlay's warm-up or,
  for a mod that comes later, at its next frame; a command that names it is drawn once it is on the GPU. After the
  window, `register_texture` answers `GRACEUI_TOO_LATE`. A callback set with `set_on_present` fixes the textures its
  lists may name (D259).
- **Faces.** A face joins the one shared atlas, which GraceUI bakes once at warm-up and never grows: `register_face` is
  taken only before warm-up, and answers `GRACEUI_TOO_LATE` after it. `glyph_table` answers `GRACEUI_NOT_READY` until
  warm-up.
- **Scaled faces** (ABI 1.2, D180). With `GRACEUI_FACE_SCALED` a face is baked at its `pixel_height` times the content
  scale, (H / 1080) × `ui.scale` / its default, rounded to a whole pixel (6 to 128 px), so a mod designs at 1080 lines
  and the default Menu size. `connect`'s answer gives `ui_scale` and its default; the display's size is 0 until the
  game's swap chain is bound, as it often is at connect. After a later change of the display or of `ui.scale`, the atlas
  is not baked again: the mod draws its glyphs scaled by `content_scale / faces_scale`, a little soft, until the next
  session.

## poll and the wake event

`poll` answers at once and never waits (D155). A mod calls it until it gives `GRACEUI_EVENT_NONE`. `hello-debug-view`
calls it every 10 ms, on its own thread.

`wake_event` (ABI 1.5, cap `wake`, D212) lets the mod's thread wait for GraceUI instead of polling on a timer.

- It gives a Win32 `HANDLE` of the mod's own, with SYNCHRONIZE access: wait on it, and close it when done; ask again for
  another. The event behind it is one for the connection's life, auto-reset.
- GraceUI sets it whenever something is kept for the mod's poll (a hotkey's press or change, a holder's event, a view's
  watch, unwatch or command, the display event), and once when it is asked for.
- So a mod waits on it, polls until `GRACEUI_EVENT_NONE`, then waits again. A wake with nothing to poll can happen, and
  costs one poll.
- Setting it never waits and calls no mod code (D212).

## Capacities

Numbers marked EST are estimates.

| What | Limit | Constant |
|---|---|---|
| Mods connected at once | 32 | `GRACEUI_MODS_MAX` |
| Spaces | 16 (EST); past them `register_pages` answers `GRACEUI_FULL` | |
| A mod's notices showing at once | 4, each 1 to 512 bytes | `GRACEUI_NOTICES_PER_MOD`, `GRACEUI_NOTICE_TEXT_MAX` |
| Panels | 16 a mod, 64 in the session (EST) | `GRACEUI_PANELS_PER_MOD`, `GRACEUI_PANELS_MAX` |
| Textures | 8 a mod, 64 in the session, 8 MiB of a mod's pixels at 4 bytes each (EST); each side at most 4096 | `GRACEUI_TEXTURES_PER_MOD`, `GRACEUI_TEXTURES_MAX`, `GRACEUI_TEXTURE_BYTES_PER_MOD`, `GRACEUI_TEXTURE_SIDE_MAX` |
| Faces | 2 a mod (EldenNet's 8), 24 for every mod together, a font file at most 4 MiB (EST); 64 code-point ranges a face | `GRACEUI_FACES_PER_MOD`, `GRACEUI_FACES_ELDENNET`, `GRACEUI_FACES_ALL_MODS`, `GRACEUI_FACE_BYTES_MAX`, `GRACEUI_FACE_RANGES_MAX` |
| A submission | 256 KiB of vertices (13,107), 256 KiB of indices, 64 commands, a command stride of at most 256 bytes (EST); at most 65,536 vertices a command | `GRACEUI_DRAW_VERTEX_BYTES_MAX`, `GRACEUI_DRAW_INDEX_BYTES_MAX`, `GRACEUI_DRAW_COMMANDS_MAX`, `GRACEUI_DRAW_COMMAND_SIZE_MAX` |
| Hotkeys | 8 a mod, 64 in the session (EST) | `GRACEUI_HOTKEYS_PER_MOD`, `GRACEUI_HOTKEYS_MAX` |
| `on_present` callbacks | 4 a mod, 8 in the session (EST) | `GRACEUI_PRESENT_HOOKS_PER_MOD`, `GRACEUI_PRESENT_HOOKS_MAX` |
| A holder's events waiting | 256; the newest are dropped, and `dropped` counts them | |
| A space's command queue | 32; a full queue drops the newest with a notice | |
| A frame from a view, to a view | 16 KiB, 256 KiB | `GRACEUI_FRAME_FROM_PAGE_MAX`, `GRACEUI_FRAME_TO_PAGE_MAX` |
| An uploaded file | 32 MiB | `GRACEUI_UPLOAD_MAX` |
| An answer's deadline | 10 s (EST); a slow command's 90 s | `GRACEUI_ANSWER_DEADLINE_MS`, `GRACEUI_SLOW_ANSWER_DEADLINE_MS` |

## Budgets

From DESIGN §8, as they bind a mod:

| Item | Budget |
|---|---|
| All panels together, menu closed | At most 1.0 ms p99 CPU on the present thread, the host's check and copy included (EST) |
| A mod's `on_present` callbacks together | 0.5 ms a frame (EST), suspended after 120 frames over it |
| A submission | Within the bounds above; a larger one is refused and logged (D76) |
| Allocation on the present thread | None per frame after warm-up, in `graceui.dll` and in every `on_present` callback |
| Textures | Registered once in each mod's registration window, within its budget |
| Font atlas | At most two faces for each mod other than EldenNet (D80) |
