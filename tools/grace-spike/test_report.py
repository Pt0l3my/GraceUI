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
