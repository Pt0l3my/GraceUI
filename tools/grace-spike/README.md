# The in-game spikes S0-E and S4: GraceUI's part (D175)

What GraceUI brings to the spike runs on the owner's PC: S0-E (GraceUI's hooks beside
EldenNet and beside another overlay, in both load orders) and S4 (input: nothing reaches the game while GraceUI's
panel holds the controls). EldenNet's spike package carries these files and its own README gives each run's launcher
command; this page says what each run is for and what to do in the game.

## What the package carries of GraceUI's

- `GraceUI\graceui.dll`: GraceUI's **test build** (the same code as a release, with read-only counters for the
  observer), with its `graceui.toml`.
- `grace-spike.dll`: the observer. In a spike run it shows the notice "GraceUI spike: recording. F9 opens the typing
  test." and writes `graceui-spike.jsonl` into the run's folder (the launcher names it in `ELDENNET_SPIKE_DIR`,
  `%APPDATA%\EldenNet\spikes\<run>`). Outside a spike run it does nothing. Its report holds numbers and fixed words
  only: no path, no name, nothing you type (only how many characters).
- `grace-standin.dll`: a stand-in for another overlay, of our own, that counts the game's frames on their way to the
  screen. It is never part of a release.

Neither reads the game's memory or calls the game's code. EldenNet makes no call into the game in these runs (its
banner says "spike: no call").

## The runs, in order

Each run stands alone: stop after any of them and send what you have.

### A1 to A4: S0-E with EldenNet (about 4 minutes each)

| Run | GraceUI loaded | Injected |
|---|---|---|
| A1 | by the launcher, before EldenNet | early (`suspended`) |
| A2 | by the launcher, before EldenNet | late (`window`) |
| A3 | by EldenNet itself, after the window | early (`suspended`) |
| A4 | by EldenNet itself, after the window | late (`window`) |

In the game:

1. At the title screen, check that GraceUI's notice "GraceUI spike: recording" and EldenNet's banner both show.
2. Load a save.
3. Press F5: GraceUI's menu opens. Look round it for a few seconds, then press F5 again to close it.
4. Press F11: EldenNet's view opens. Press F11 again to close it.
5. If the Steam overlay shows in this game (Shift+Tab), open it and close it again, and note whether GraceUI's notice
   is still drawn afterwards.
6. Play for two minutes, then quit from the game's own menu.

### A0: S0-E with GraceUI alone (about 4 minutes)

GraceUI, the observer and the stand-in, without EldenNet, injected early. The same steps, without F11. This tells a
fault of GraceUI's own from one that comes only with EldenNet beside it.

### B: S4, input (about 10 minutes)

With EldenNet, injected early.

1. Load a save and stand somewhere safe, away from enemies.
2. Press F9: the typing test opens in the middle of the screen and takes the keyboard, the mouse and the pad.
3. For 60 seconds, type anything (W, A, S and D among it, and some held down) and move the mouse about. What you type
   shows in the test's own field (GraceUI 0.2.1 and later), on screen only: the report counts it and never keeps it.
   The test closes by itself after 60 seconds; holding Esc for a second stops it early.
4. Note whether the Tarnished moved or the camera turned at any time while the test was open.
5. Hold W so the Tarnished walks, press F5 while still holding it, let W go, then press F5 again: the Tarnished must
   stand still once the menu is open, and must not start walking again when it closes.
6. If you have a pad: open GraceUI's menu with F5, then press B twice (the first goes back to the list on the left, the
   second closes the menu): the Tarnished must not dodge or roll.
7. Once: in GraceUI's menu, on the Menu page, set Menu size to 1.50, close the menu and open it again. Then set
   it back to 1.25, its default (or press R on it).
8. Quit from the game's own menu.

If you have them, the Steam floating keyboard and Proton are rows of their own: tell us what happened.

## After the runs

Send the files in `%APPDATA%\EldenNet\spikes\` as EldenNet's README says, with your answers to:

- Did the Tarnished stay still and the camera stay put during the typing test (run B)? (yes or no)
- If the Steam overlay showed: was GraceUI still drawing after it closed? (yes or no)

We read the reports with `python3 -I tools/grace-spike/report.py <the spikes folder> --by-hand movement=yes` (and
`steam_overlay=`, `steam_keyboard=`, `proton=` as you answer). A run passes when every rule it needs passes, is
incomplete when one is not measured, and fails otherwise; a row "if you have one" never makes a run incomplete. Until
EldenNet's recording can measure movement (once EldenNet's start is ready, S10.1.m; R1 is retired, O106), the
movement row is your answer. Since GraceUI 0.2.24 (D220) each state line also says how far GraceUI's start got
(`init_step`), and a run whose hooks never went in names that step in its hooks row. Since GraceUI 0.2.27 (D230) each
state line also says how far GraceUI's first frames got (`frame_step`, 1 to 11: a Present through GraceUI's detour,
the swap chain bound, the queue found, the atlas asked, baked, the renderer's objects made, Dear ImGui's backends
begun, the atlas's upload submitted, the atlas on the GPU, GraceUI's first frame submitted, the game's Present returned
after it), the observer writes a state line at once whenever that step changes, and a run that drew nothing names the
step in its drawing row. The observer also copies GraceUI's own log lines into `graceui-log.txt` beside the report,
never into it (the report keeps to numbers and its own words): it is GraceUI's text, with no path and no name in it,
so read it before you send it if you like. To place a crash more closely, set `GRACE_SPIKE_STATE_MS=200` in the run's
environment (200 to 60000 ms between state lines; 5000 by default). Since GraceUI 0.2.28 (D231) the observer also
records faults, never handling one: a `fault` line in the report for each access violation, illegal instruction, the
heap's corruption or the like, with the faulting thread's frames as fixed words and offsets, and `graceui-faults.txt` beside it with the
modules' file names; a run that fell has its last fault there. The game's own routine faults show up too, once each:
`report.py` lists them and judges none.
