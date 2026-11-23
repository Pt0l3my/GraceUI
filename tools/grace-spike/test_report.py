# SPDX-License-Identifier: MIT
"""Tests for report.py, the spike reports' reader (D175), on invented reports."""
import contextlib
import io
import json
import pathlib
import tempfile
import unittest

import report


def line(kind, **keys):
    return {"report": "graceui_spike", "schema": 1, "t": 1000, "k": kind, **keys}


def header(spike, **keys):
    base = {"spike": spike, "graceui": [0, 2, 0], "abi_minor": 0, "found": "by_name", "build": "test",
            "eldennet_at_find": "absent", "steam_overlay": "absent", "standin": "absent", "connect": "ok",
            "typing_ms": 60000, "state_ms": 5000}
    base.update(keys)
    return line("header", **base)


def state(**keys):
    base = {"host": "hooked", "install_runs": 1, "init_threads": 1, "hooks": 7, "frames_drawn": 100, "render_off": 0,
            "allocations": 12, "lists_refused": 0, "consumers": 1, "eldennet": "absent", "standin": "absent",
            "standin_presents": 0, "atlas": [1024, 2048], "ui_scale_x100": 100, "handles": 900, "private_kb": 4000,
            "prologue": "plain"}
    base.update(keys)
    return line("state", **base)


def typing(**keys):
    base = {"started": 2000, "held_ms": 60050, "target_ms": 60000, "chars": 240, "end": "released",
            "passed": [0, 3, 0, 0, 0], "masked_held": [300, 200, 0]}
    base.update(keys)
    return line("typing", **base)


class Reader(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.folder.name)

    def tearDown(self):
        self.folder.cleanup()

    def write(self, run, lines, tail=""):
        path = self.root / run / report.FILE
        path.parent.mkdir(parents=True)
        path.write_text("".join(json.dumps(item) + "\n" for item in lines) + tail, encoding="utf-8")
        return path

    def results(self, verdict):
        return {r["rule"]: r["result"] for r in verdict["rules"]}

    def test_s4_passes_with_the_owners_word_on_movement(self):
        path = self.write("s4-1", [header("s4"), state(frames_drawn=0), state(), typing(), state(frames_drawn=4000)])
        verdict = report.judge(path, {"movement": True})
        self.assertEqual(verdict["verdict"], "passed", verdict)
        self.assertEqual(self.results(verdict)["pad_masked"], "not_measured")  # no pad: optional

    def test_s4_without_movement_is_incomplete_never_failed(self):
        path = self.write("s4-1", [header("s4"), state(), typing(), state(frames_drawn=200)])
        verdict = report.judge(path, {})
        self.assertEqual(verdict["verdict"], "incomplete")
        self.assertEqual(self.results(verdict)["movement"], "not_measured")

    def test_a_key_that_reached_the_game_fails_s4(self):
        path = self.write("s4-1", [header("s4"), state(), typing(passed=[1, 0, 0, 0, 0]), state()])
        verdict = report.judge(path, {"movement": True})
        self.assertEqual(verdict["verdict"], "failed")
        self.assertEqual(self.results(verdict)["nothing_passed"], "failed")

    def test_a_typing_test_stopped_early_is_not_measured(self):
        path = self.write("s4-1", [header("s4"), state(), typing(end="player", held_ms=20000), state()])
        self.assertEqual(self.results(report.judge(path, {}))["typing_60s"], "not_measured")

    def test_what_the_panel_received_is_judged_when_the_typing_line_has_it(self):
        def result(**keys):
            path = self.write(f"s4-{len(list(self.root.iterdir())) + 1}", [header("s4"), state(), typing(**keys),
                                                                         state()])
            return self.results(report.judge(path, {}))["panel_received"]
        self.assertEqual(result(), "not_measured")  # a typing line from before 0.2.1: optional
        heard = {"received": [239, 300, 40, 0], "dropped": 0, "end_event": "released", "imgui_chars": 239}
        self.assertEqual(result(**heard), "passed")  # one character typed as the test ended: within the slack
        self.assertEqual(result(**{**heard, "received": [200, 300, 40, 0]}), "failed")
        self.assertEqual(result(**{**heard, "imgui_chars": 100}), "failed")  # feed lost them
        self.assertEqual(result(**{**heard, "end_event": "none"}), "failed")
        self.assertEqual(result(**{**heard, "received": [0, 0, 0, 0], "chars": 0}), "not_measured")

    def test_s0e_with_eldennet_and_the_stand_in_passes(self):
        lines = [header("s0e", standin="patched"),
                 state(standin="patched", standin_presents=10, frames_drawn=50),
                 state(eldennet="present", consumers=2, standin="patched", standin_presents=900, frames_drawn=880)]
        verdict = report.judge(self.write("s0e-1", lines), {})
        self.assertEqual(verdict["verdict"], "passed", verdict)
        self.assertEqual(self.results(verdict)["steam_overlay"], "not_measured")

    def test_s0e_alone_with_eldennet_in_the_process_fails(self):
        lines = [header("s0e_alone", eldennet_at_find="present"), state(eldennet="present"), state()]
        self.assertEqual(self.results(report.judge(self.write("s0e_alone-1", lines), {}))["alone"], "failed")

    def test_an_allocation_after_warm_up_fails_no_leak(self):
        lines = [header("s0e_alone"), state(allocations=12), state(allocations=13)]
        verdict = report.judge(self.write("s0e_alone-1", lines), {})
        self.assertEqual(self.results(verdict)["no_leak"], "failed")
        self.assertEqual(verdict["verdict"], "failed")

    def test_a_stand_in_that_stopped_counting_fails(self):
        lines = [header("s0e_alone", standin="patched"), state(standin="patched", standin_presents=40),
                 state(standin="patched", standin_presents=40, frames_drawn=300)]
        self.assertEqual(self.results(report.judge(self.write("s0e_alone-1", lines), {}))["beside_standin"], "failed")

    def test_an_init_that_never_hooked_says_where_it_stopped(self):
        lines = [header("s0e_alone"), state(host="started", install_runs=0, hooks=0, frames_drawn=0, init_step=4)]
        verdict = report.judge(self.write("s0e_alone-1", lines), {})
        hooks = [r for r in verdict["rules"] if r["rule"] == "hooks_once"][0]
        self.assertEqual(hooks["result"], "failed")
        self.assertIn("step 4, reading the hook targets", hooks["detail"])
        # An older test build has no step: nothing is said of it.
        older = report.judge(self.write("s0e_alone-2", [header("s0e_alone"), state(host="started", install_runs=0)]), {})
        self.assertNotIn("step", [r for r in older["rules"] if r["rule"] == "hooks_once"][0]["detail"])

    def test_a_run_that_fell_in_graceuis_first_frames_says_where(self):  # D230
        lines = [header("s0e"), state(frames_drawn=0, frame_step=7)]
        verdict = report.judge(self.write("s0e-7", lines), {})
        drawing = [r for r in verdict["rules"] if r["rule"] == "drawing"][0]
        self.assertEqual(drawing["result"], "failed")
        self.assertIn("step 7, Dear ImGui's backends begun", drawing["detail"])
        # Past its first frames, or from an older test build, nothing is said of it.
        done = report.judge(self.write("s0e-8", [header("s0e"), state(frame_step=11)]), {})
        self.assertNotIn("step", [r for r in done["rules"] if r["rule"] == "drawing"][0]["detail"])
        older = report.judge(self.write("s0e-9", [header("s0e"), state()]), {})
        self.assertNotIn("step", [r for r in older["rules"] if r["rule"] == "drawing"][0]["detail"])

    def test_faults_are_listed_never_judged(self):  # D231
        fault = {"report": "graceui_spike", "schema": 1, "t": 9, "k": "fault", "code": 3221225477, "access": "execute",
                 "null_target": 1, "frame_step": 2, "thread": 7, "at": ["null+0x0", "other+0x1a2b", "graceui+0x40"]}
        lines = [header("s0e"), state(), fault]
        verdict = report.judge(self.write("s0e-f", lines), {})
        clean = report.judge(self.write("s0e-g", [header("s0e"), state()]), {})
        self.assertEqual(verdict["verdict"], clean["verdict"])
        self.assertEqual(verdict["faults"], [{"code": 3221225477, "access": "execute", "null_target": 1, "frame_step": 2,
                                              "at": ["null+0x0", "other+0x1a2b", "graceui+0x40"]}])
        self.assertEqual(clean["faults"], [])

    def test_a_heap_guard_breach_is_listed_with_its_maker(self):  # D235
        breach = {"report": "graceui_spike", "schema": 1, "t": 9, "k": "fault", "code": 0xE0475548, "access": "none",
                  "null_target": 0, "frame_step": 11, "thread": 7, "at": ["kernel+0x13f07", "graceui+0x4f5e"],
                  "heap_guard": {"breach": "past_end", "size": 24, "block": 4163,
                                 "made_at": ["graceui+0x5161", "eldennet+0x68dc"]}}
        lines = [header("s0e"), state(heap=[1200, 9900000, 4096, 0, 40, 0], heap_read=1, heap_breach="none"),
                 state(heap=[0, 0, 0, 0, 0, 0], heap_read=0, heap_breach="none"), breach]
        verdict = report.judge(self.write("s0e-h", lines), {})
        self.assertEqual(verdict["faults"][0]["heap_guard"],
                         {"breach": "past_end", "size": 24, "block": 4163, "made_at": ["graceui+0x5161", "eldennet+0x68dc"]})
        # The last state line that read the counts; one that could not (the guard held at that moment) is passed over.
        self.assertEqual(verdict["heap_guard"], {"live_blocks": 1200, "live_bytes": 9900000, "kept": 4096, "damaged": 0,
                                                 "checks": 40, "breaches": 0, "breach": "none"})
        older = report.judge(self.write("s0e-i", [header("s0e"), state()]), {})
        self.assertIsNone(older["heap_guard"])

    def test_the_atlas_at_150_is_judged_against_its_limit(self):
        lines = [header("s4"), state(ui_scale_x100=150, atlas=[4096, 8192]), typing(), state()]
        self.assertEqual(self.results(report.judge(self.write("s4-1", lines), {}))["atlas_150"], "failed")

    def test_a_last_line_cut_short_by_a_crash_is_left_out(self):
        path = self.write("s4-1", [header("s4"), state()], tail='{"report":"graceui_spike","sch')
        self.assertEqual(len(report.read_lines(path)), 2)

    def test_a_line_of_another_kind_is_a_bad_report(self):
        path = self.write("s4-1", [header("s4"), {"report": "recording", "schema": 1}, state()])
        with self.assertRaises(report.ReportError):
            report.read_lines(path)

    def test_the_command_line_finds_reports_in_folders_and_exits_1_on_a_failure(self):
        self.write("s4-1", [header("s4"), state(), typing(passed=[0, 0, 2, 0, 0]), state()])
        self.write("s0e-1", [header("s0e", eldennet_at_find="present"), state(consumers=2), state()])
        with contextlib.redirect_stdout(io.StringIO()) as out, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(report.run([str(self.root)]), 1)
            self.assertEqual(report.run([str(self.root / "s0e-1"), "--json"]), 0)
            self.assertEqual(report.run([str(self.root), "--by-hand", "speed=yes"]), 2)
        self.assertIn("s4-1 (s4): failed", out.getvalue())


if __name__ == "__main__":
    unittest.main()
