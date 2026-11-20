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
ATLAS_LIMIT = 4096  # DESIGN §3.16: within 4096 x 4096 at 4K or 150%
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
    rules.append(rule("hooks_once", PASSED if once else FAILED,
                      f"host {last.get('host')}, {last.get('hooks')} hooks from {last.get('install_runs')} install "
                      f"run(s), {last.get('init_threads')} init thread(s)"))
    drawn = max(state.get("frames_drawn", 0) for state in states)
    off = any(state.get("render_off", 0) for state in states)
    rules.append(rule("drawing", PASSED if drawn > 0 and not off else FAILED,
                      f"{drawn} frames drawn, render {'OFF' if off else 'on'}, Present's prologue "
                      f"{last.get('prologue')}"))
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
    rules.append(hand_rule("movement", hand, "EldenNet's recording measures it once R1 has its task"))
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
    return {"run": path.parent.name, "spike": spike, "verdict": verdict, "rules": rules}


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
    return 1 if any(v["verdict"] == FAILED for v in runs) else 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
