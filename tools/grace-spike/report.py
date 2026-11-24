#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reads grace-spike.dll's reports from the in-game spikes S0-E and S4 (D175) and applies their pass rules.

Each report is one run's graceui-spike.jsonl: JSON Lines, kind "graceui_spike", schema 1, in a closed vocabulary
(numbers, fixed words and our own labels). Give the files, or the folders that hold them (each run's folder, or the
spikes folder above them). For each run it prints every rule's result and the run's verdict, as EldenNet's
spike-report does: a run passes when every rule it needs passes, is incomplete when one is not measured, and fails
otherwise. A rule marked optional (a pad, the Steam overlay, the Steam floating keyboard, Proton: "if you have one")
never makes a run incomplete. What only the owner can see is folded in with --by-hand RULE=yes|no.

  python3 -I tools/grace-spike/report.py SPIKES_FOLDER [--by-hand movement=yes] [--json]

Exit status 0 when no run failed (passed or incomplete), 1 when one did, 2 for a file it cannot read.
"""
import argparse
import json
import pathlib
import sys

REPORT = "graceui_spike"
SCHEMA = 1
FILE = "graceui-spike.jsonl"
TYPING_MS = 60000
# GraceUI's init thread's steps (D220), as GraceUI_Test_InitStep names them.
INIT_STEPS = {1: "its fonts", 2: "waiting for the game's window", 3: "binding Direct3D",
              4: "reading the hook targets from a swap chain of its own", 5: "finding the input targets",
              6: "the install", 7: "done"}
# How far GraceUI's first frames got (D230), as GraceUI_Test_FrameStep names them.
FRAME_STEPS = {1: "a Present through GraceUI's detour", 2: "the game's swap chain bound", 3: "the present queue found",
               4: "the atlas asked of GraceUI's thread", 5: "the atlas baked",
               6: "the renderer's objects made on the game's device", 7: "Dear ImGui's backends begun",
               8: "the atlas's upload submitted", 9: "the atlas on the GPU", 10: "GraceUI's first frame submitted",
               11: "the game's Present returned after it"}
ATLAS_LIMIT = 4096  # DESIGN §3.16: within 4096 x 4096 at 4K or 150%
RECEIVED_SLACK = 2  # characters typed as a test ends, discarded with its holder before its panel took them (D176)
HAND_RULES = ("movement", "steam_overlay", "steam_keyboard", "proton")

PASSED, FAILED, NOT_MEASURED = "passed", "failed", "not_measured"


class ReportError(Exception):
    pass


def read_lines(path):
    lines = []
    for number, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not text.strip():
            continue
        try:
            line = json.loads(text)
        except json.JSONDecodeError as error:
            # A crash may cut the last line short; any other bad line is a bad report.
            if number == len(path.read_text(encoding="utf-8").splitlines()):
                break
            raise ReportError(f"{path.name} line {number}: not JSON ({error.msg})") from None
        if not isinstance(line, dict) or line.get("report") != REPORT or line.get("schema") != SCHEMA:
            raise ReportError(f"{path.name} line {number}: not a {REPORT} line of schema {SCHEMA}")
        lines.append(line)
    return lines


def of(lines, kind):
    return [line for line in lines if line.get("k") == kind]


def rule(name, result, detail, optional=False):
    return {"rule": name, "result": result, "detail": detail, "optional": optional}


def order_of(header, states):
    if header.get("eldennet_at_find") == "present":
        return "eldennet_first"
    if any(state.get("eldennet") == "present" for state in states):
        return "graceui_first"
    return "graceui_alone"


def common_rules(header, states):
    rules = []
    connected = header.get("connect") == "ok" and header.get("build") == "test"
    rules.append(rule("connected", PASSED if connected else FAILED,
                      f"GraceUI {header.get('found', 'absent')}, connect {header.get('connect', 'absent')}, "
                      f"build {header.get('build', '?')}"))
    if not states:
        rules.append(rule("hooks_once", NOT_MEASURED, "no state line"))
        return rules
    last = states[-1]
    once = last.get("host") == "hooked" and last.get("install_runs") == 1 and last.get("init_threads", 0) <= 1
    # Where an init that never hooked stopped (D220): the step GraceUI's init thread reached, from a test build that has it.
    stopped = ""
    if not once and last.get("init_step", 0) in INIT_STEPS and last.get("init_step") != 7:
        stopped = f"; its init stopped at step {last['init_step']}, {INIT_STEPS[last['init_step']]}"
    rules.append(rule("hooks_once", PASSED if once else FAILED,
                      f"host {last.get('host')}, {last.get('hooks')} hooks from {last.get('install_runs')} install "
                      f"run(s), {last.get('init_threads')} init thread(s){stopped}"))
    drawn = max(state.get("frames_drawn", 0) for state in states)
    off = any(state.get("render_off", 0) for state in states)
    # Where GraceUI's first frames stopped (D230), from a test build that has the step, when the run ended before them.
    step = last.get("frame_step", 0)
    first = f"; its first frames stopped at step {step}, {FRAME_STEPS[step]}" if step in FRAME_STEPS and step != 11 else ""
    rules.append(rule("drawing", PASSED if drawn > 0 and not off else FAILED,
                      f"{drawn} frames drawn, render {'OFF' if off else 'on'}, Present's prologue "
                      f"{last.get('prologue')}{first}"))
    warm = [state for state in states if state.get("frames_drawn", 0) > 0]
    if warm:
        grew = warm[-1].get("allocations", 0) - warm[0].get("allocations", 0)
        refused = last.get("lists_refused", 0)
        rules.append(rule("no_leak", PASSED if grew == 0 and refused == 0 else FAILED,
                          f"{grew} allocations on the present thread after warm-up, {refused} lists refused; "
                          f"handles {warm[0].get('handles')} to {last.get('handles')}, private "
                          f"{warm[0].get('private_kb')} to {last.get('private_kb')} KB (shown, not judged)"))
    else:
        rules.append(rule("no_leak", NOT_MEASURED, "nothing drawn"))
    return rules


def s0e_rules(spike, header, states, hand):
    rules = []
    order = order_of(header, states)
    if spike == "s0e":
        connected = any(state.get("consumers", 0) >= 2 for state in states) and order != "graceui_alone"
        rules.append(rule("eldennet_connected", PASSED if connected else FAILED,
                          f"order {order}, at most {max((s.get('consumers', 0) for s in states), default=0)} mods "
                          f"connected"))
    else:
        rules.append(rule("alone", PASSED if order == "graceui_alone" else FAILED, f"order {order}"))
    standin = [state for state in states if state.get("standin") not in (None, "absent")]
    if standin:
        rising = standin[-1].get("standin_presents", 0) > standin[0].get("standin_presents", 0)
        drawn = standin[-1].get("frames_drawn", 0) > standin[0].get("frames_drawn", 0)
        ok = standin[-1].get("standin") == "patched" and rising and drawn
        rules.append(rule("beside_standin", PASSED if ok else FAILED,
                          f"the stand-in {standin[-1].get('standin')}, its Present "
                          f"{'called' if rising else 'NOT CALLED'}, GraceUI {'drawing' if drawn else 'NOT DRAWING'}"))
    else:
        rules.append(rule("beside_standin", NOT_MEASURED, "the stand-in was not loaded", optional=True))
    rules.append(hand_rule("steam_overlay", hand, f"the Steam overlay {header.get('steam_overlay', 'absent')}",
                           optional=True))
    return rules


def s4_rules(header, states, typings, hand):
    rules = []
    full = [t for t in typings if t.get("end") == "released" and t.get("held_ms", 0) >= TYPING_MS]
    if full:
        rules.append(rule("typing_60s", PASSED, f"held {full[0]['held_ms']} ms, {full[0].get('chars', 0)} characters"))
    elif typings:
        early = typings[-1]
        rules.append(rule("typing_60s", NOT_MEASURED,
                          f"ended '{early.get('end')}' after {early.get('held_ms')} ms (target "
                          f"{early.get('target_ms')} ms)"))
    else:
        rules.append(rule("typing_60s", NOT_MEASURED, "no typing test"))
    if typings:
        passed = [sum(t.get("passed", [0] * 5)[i] for t in typings) for i in range(5)]
        leaked = passed[0] + passed[2] + passed[3] + passed[4]
        rules.append(rule("nothing_passed", PASSED if leaked == 0 else FAILED,
                          f"while held: {passed[0]} presses, {passed[2]} characters, {passed[3]} mouse, {passed[4]} "
                          f"raw input passed to the game; {passed[1]} releases of presses the game saw begin"))
        masked = [sum(t.get("masked_held", [0] * 3)[i] for t in typings) for i in range(3)]
        rules.append(rule("pad_masked", PASSED if masked[2] > 0 else NOT_MEASURED,
                          f"XInput reads masked while held: {masked[2]} (DirectInput: {masked[0]} keyboard, "
                          f"{masked[1]} mouse)", optional=True))
    else:
        rules.append(rule("nothing_passed", NOT_MEASURED, "no typing test"))
    rules.append(received_rule(typings))
    rules.append(hand_rule("movement", hand, "EldenNet's recording measures it once EldenNet's start is ready (S10.1.m)"))
    scaled = [state for state in states if state.get("ui_scale_x100", 0) >= 150]
    if scaled:
        largest = max(max(state.get("atlas", [0, 0])) for state in scaled)
        rules.append(rule("atlas_150", PASSED if largest <= ATLAS_LIMIT else FAILED,
                          f"atlas side at ui.scale 1.5 or more: {largest} (limit {ATLAS_LIMIT})", optional=True))
    else:
        rules.append(rule("atlas_150", NOT_MEASURED, "ui.scale never reached 1.5", optional=True))
    rules.append(hand_rule("steam_keyboard", hand, "the Steam floating keyboard", optional=True))
    rules.append(hand_rule("proton", hand, "Proton", optional=True))
    return rules


def received_rule(typings):
    """What the observer's own panel received through its holder's events (GraceUI 0.2.1, D176), beside what GraceUI's
    own Dear ImGui counted, and whether it heard its holder end as GraceUI recorded it."""
    with_events = [t for t in typings if "received" in t]
    if not with_events:
        return rule("panel_received", NOT_MEASURED, "no typing test with its holder's events (before GraceUI 0.2.1)",
                    optional=True)
    got = [sum(t.get("received", [0] * 4)[i] for t in with_events) for i in range(4)]
    counted = sum(t.get("chars", 0) for t in with_events)
    dropped = sum(t.get("dropped", 0) for t in with_events)
    fed = sum(t.get("imgui_chars", 0) for t in with_events)
    ends = [f"{t.get('end_event')} (GraceUI: {t.get('end')})" for t in with_events]
    detail = (f"its panel received {got[0]} characters, {got[1]} keys, {got[2]} pointer and {got[3]} pad events "
              f"({dropped} dropped: its own polling), and its Dear ImGui took {fed} characters from feed; GraceUI's "
              f"own Dear ImGui counted {counted} characters; its end heard as {', '.join(ends)}")
    slack = RECEIVED_SLACK * len(with_events)
    lost = got[0] + slack < counted or fed + slack < got[0]
    misheard = any(t.get("end_event") != t.get("end") for t in with_events)
    if lost or misheard:
        return rule("panel_received", FAILED, detail)
    if counted == 0 and got[0] == 0:
        return rule("panel_received", NOT_MEASURED, detail)
    return rule("panel_received", PASSED, detail)


def hand_rule(name, hand, what, optional=False):
    if name not in hand:
        return rule(name, NOT_MEASURED, f"by hand: {what}", optional=optional)
    return rule(name, PASSED if hand[name] else FAILED, f"by hand: {what}: {'yes' if hand[name] else 'no'}",
                optional=optional)


def judge(path, hand):
    lines = read_lines(path)
    headers = of(lines, "header")
    if not headers:
        raise ReportError(f"{path.name}: no header line")
    header = headers[0]
    states = of(lines, "state")
    spike = header.get("spike", "unknown")
    rules = common_rules(header, states)
    if spike in ("s0e", "s0e_alone"):
        rules += s0e_rules(spike, header, states, hand)
    elif spike == "s4":
        rules += s4_rules(header, states, of(lines, "typing"), hand)
    results = [r["result"] for r in rules]
    needed = [r["result"] for r in rules if not r["optional"]]
    verdict = FAILED if FAILED in results else "incomplete" if NOT_MEASURED in needed else PASSED
    # The faults the observer recorded (D231): listed, never judged, since a game's own first-chance faults are no
    # failure of a run; a run that fell has its last one among them. The modules' names are in graceui-faults.txt.
    faults = []
    for f in of(lines, "fault"):
        fault = {"code": f.get("code"), "access": f.get("access"), "null_target": f.get("null_target"),
                 "frame_step": f.get("frame_step"), "at": f.get("at", [])}
        if isinstance(f.get("heap_guard"), dict):  # D235: the guard's breach, its block and the code that made it
            guard = f["heap_guard"]
            fault["heap_guard"] = {"breach": guard.get("breach"), "size": guard.get("size"),
                                   "block": guard.get("block"), "made_at": guard.get("made_at", [])}
        faults.append(fault)
    return {"run": path.parent.name, "spike": spike, "verdict": verdict, "rules": rules, "faults": faults,
            "heap_guard": heap_guard_of(states), "window": window_of(states), "stalls": stalls_of(states)}


def stalls_of(states):
    """The input detours' longest calls and the game's longest gaps between DirectInput reads (D239), at the run's
    last state line, with the times of the state lines at which a call over 8 ms or a gap over 100 ms was first
    counted; None from a build without them. Listed, never judged: a loading screen makes long gaps too."""
    seen = [s for s in states if isinstance(s.get("stall_max_us"), list) and isinstance(s.get("poll_gaps"), list)]
    if not seen:
        return None
    last = seen[-1]
    over = [s.get("stalls_over", [0] * 6) for s in seen]
    gaps = [s["poll_gaps"] for s in seen]
    slow_at = [seen[i].get("t") for i in range(1, len(seen)) if sum(over[i][3:6]) > sum(over[i - 1][3:6])]
    gap_at = [seen[i].get("t") for i in range(1, len(seen)) if gaps[i][2] + gaps[i][3] > gaps[i - 1][2] + gaps[i - 1][3]]
    return {"max_us": dict(zip(("window", "dinput", "xinput"), last["stall_max_us"])),
            "over_2ms": dict(zip(("window", "dinput", "xinput"), last.get("stalls_over", [0] * 6)[0:3])),
            "over_8ms": dict(zip(("window", "dinput", "xinput"), last.get("stalls_over", [0] * 6)[3:6])),
            "gap_max_ms": {"keyboard": last["poll_gaps"][0], "mouse": last["poll_gaps"][1]},
            "gaps_over_100ms": {"keyboard": last["poll_gaps"][2], "mouse": last["poll_gaps"][3]},
            "guard_in_detour_us": last.get("guard_in_detour_us", 0),
            "heap_check_max_us": (last.get("heap_check_us") or [0, 0])[1],
            "slow_calls_at": slow_at, "long_gaps_at": gap_at}


def window_of(states):
    """The game's window across the run (D238), from a build that reports it, else None: the state lines on which it
    was not the foreground window or had no keyboard focus (with their times), its focus messages and the presses that
    reached the game while no one held input, at the last state line. Listed, never judged."""
    seen = [s for s in states if isinstance(s.get("window_focus"), list) and len(s["window_focus"]) == 7]
    if not seen:
        return None
    last = seen[-1]
    downs = last.get("free_downs", [0, 0, 0, 0])
    return {"lines": len(seen),
            "not_foreground": [s.get("t") for s in seen if s["window_focus"][0] == 0],
            "no_focus": [s.get("t") for s in seen if s["window_focus"][2] == 0],
            "focus_gained": last["window_focus"][3], "focus_lost": last["window_focus"][4],
            "app_active": last["window_focus"][5], "app_inactive": last["window_focus"][6],
            "free_downs": {"dinput_keys": downs[0], "dinput_buttons": downs[1], "window_keys": downs[2],
                           "window_buttons": downs[3]}}


def heap_guard_of(states):
    """The heap guard's counts at the run's last state line that read them (D235), or None from a build without it.
    Listed, never judged: a breach ends the run, and its fault line says where."""
    for s in reversed(states):
        heap = s.get("heap")
        if isinstance(heap, list) and len(heap) == 6 and s.get("heap_read", 1) == 1:
            return {"live_blocks": heap[0], "live_bytes": heap[1], "kept": heap[2], "damaged": heap[3],
                    "checks": heap[4], "breaches": heap[5], "breach": s.get("heap_breach", "none")}
    return None


def reports_in(paths):
    found = []
    for given in paths:
        path = pathlib.Path(given)
        if path.is_dir():
            found += sorted(path.rglob(FILE))
        else:
            found.append(path)
    return found


def parse_hand(values):
    hand = {}
    for value in values or []:
        name, _, answer = value.partition("=")
        if name not in HAND_RULES or answer not in ("yes", "no"):
            raise ReportError(f"--by-hand takes {', '.join(HAND_RULES)} = yes or no, not {value!r}")
        hand[name] = answer == "yes"
    return hand


def run(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="+", help="reports, or folders that hold them")
    parser.add_argument("--by-hand", action="append", metavar="RULE=yes|no", help="what the owner saw")
    parser.add_argument("--json", action="store_true", help="print the verdicts as JSON")
    args = parser.parse_args(argv)
    try:
        hand = parse_hand(args.by_hand)
        runs = [judge(path, hand) for path in reports_in(args.paths)]
    except (OSError, ReportError) as error:
        print(f"report: {error}", file=sys.stderr)
        return 2
    if not runs:
        print("report: no report found", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps({"report": "graceui_spike_verdicts", "schema": 1, "runs": runs}, indent=1))
    else:
        for verdict in runs:
            print(f"{verdict['run']} ({verdict['spike']}): {verdict['verdict']}")
            for r in verdict["rules"]:
                tag = " (if you have one)" if r["optional"] else ""
                print(f"  {r['rule']}: {r['result']}{tag}: {r['detail']}")
            guard = verdict["heap_guard"]
            if guard is not None:
                print(f"  heap guard (recorded, not judged): {guard['checks']} whole checks; at the last state line "
                      f"{guard['live_blocks']} live blocks ({guard['live_bytes']} bytes), {guard['kept']} kept after "
                      f"their free, {guard['damaged']} damaged; breach: {guard['breach']}")
            w = verdict["window"]
            if w is not None:
                def spans(times):
                    return f"{len(times)} (from t={times[0]} ms to t={times[-1]} ms)" if times else "0"
                print(f"  the game's window (recorded, not judged): of {w['lines']} state lines, not the foreground "
                      f"window on {spans(w['not_foreground'])}, without the keyboard's focus on {spans(w['no_focus'])}; "
                      f"focus lost {w['focus_lost']} and gained {w['focus_gained']} times, the game deactivated "
                      f"{w['app_inactive']} and activated {w['app_active']} times")
                d = w["free_downs"]
                print(f"  presses that reached the game with no holder: DirectInput {d['dinput_keys']} keys and "
                      f"{d['dinput_buttons']} buttons; window messages {d['window_keys']} keys and "
                      f"{d['window_buttons']} buttons")
            st = verdict["stalls"]
            if st is not None:
                m, o2, o8 = st["max_us"], st["over_2ms"], st["over_8ms"]
                print(f"  input detours (recorded, not judged): longest call {m['window']} us in the window filter, "
                      f"{m['dinput']} us in DirectInput's, {m['xinput']} us in XInput's; over 2 ms {o2['window']}, "
                      f"{o2['dinput']}, {o2['xinput']}; over 8 ms {o8['window']}, {o8['dinput']}, {o8['xinput']}"
                      f"{' (first counted at t=' + ', '.join(str(t) for t in st['slow_calls_at'][:5]) + ' ms)' if st['slow_calls_at'] else ''}")
                g, n = st["gap_max_ms"], st["gaps_over_100ms"]
                print(f"  the game's DirectInput reads: longest gap {g['keyboard']} ms (keyboard), {g['mouse']} ms "
                      f"(mouse); gaps over 100 ms: {n['keyboard']} and {n['mouse']}"
                      f"{' (counted at t=' + ', '.join(str(t) for t in st['long_gaps_at'][:8]) + ' ms)' if st['long_gaps_at'] else ''}; "
                      f"the heap guard's longest time in a detour {st['guard_in_detour_us']} us, its longest check "
                      f"{st['heap_check_max_us']} us")
            for f in verdict["faults"]:
                code = f["code"] if isinstance(f["code"], int) else 0
                print(f"  fault (recorded, not judged): {code:#x}, {f['access']}"
                      f"{' of the null page' if f['null_target'] else ''}, frame_step {f['frame_step']}, "
                      f"at {', '.join(str(a) for a in f['at'][:6])}")
                if "heap_guard" in f:
                    g = f["heap_guard"]
                    print(f"    heap guard: {g['breach']}, a block of {g['size']} bytes (block {g['block']}), "
                          f"made at {', '.join(str(a) for a in g['made_at'])}")
    return 1 if any(v["verdict"] == FAILED for v in runs) else 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
