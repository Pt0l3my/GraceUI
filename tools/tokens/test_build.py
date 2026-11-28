# SPDX-License-Identifier: MIT
"""Tests of tools/tokens/build.py (D157): §3.3's contrast table reproduced from the tokens, the outputs current, and each
check failing on tokens seeded to break it.

    python3 -I -m unittest discover -s tools/tokens
"""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import build  # noqa: E402

# GraceUI DESIGN §3.3, as the design writes it: each ratio rounded to 0.01, then to 0.1, so a cell may sit 0.05 above
# the ratio rounded once (11.3479 is written 11.4).
TABLE = {
    "ink.hi": [15.3, 14.9, 14.2, 13.4, 12.2, 11.0],
    "ink.body": [11.7, 11.4, 10.8, 10.2, 9.3, 8.4],
    "ink.mute": [7.4, 7.2, 6.8, 6.5, 5.9, 5.3],
    "ink.faint": [6.4, 6.2, 6.0, 5.6, 5.1, 4.6],
    "gold": [8.6, 8.3, 8.0, 7.5, 6.9, 6.2],
    "gold.hi": [11.8, 11.5, 11.0, 10.4, 9.4, 8.5],
    "gold.name": [9.5, 9.3, 8.9, 8.4, 7.6, 6.9],
    "rot": [6.9, 6.7, 6.4, 6.0, 5.5, 4.9],
    "ember": [7.9, 7.7, 7.4, 7.0, 6.4, 5.7],
    "moss": [8.8, 8.5, 8.2, 7.7, 7.0, 6.3],
    "glint": [9.3, 9.1, 8.7, 8.2, 7.5, 6.7],
}
GROUNDS = ["ground.well", "ground.0", "ground.1", "ground.2", "ground.3", "ground.4"]


def source():
    return build.SOURCE.read_text(encoding="utf-8")


def failures_of(text):
    data, variants = build.load(text)
    return build.check_contrast(data, variants) + build.check_type(data) + build.check_motion(data)


class Table(unittest.TestCase):
    def test_contrast_table_is_the_designs(self):
        _, variants = build.load()
        colours = variants["grace"]["colour"]
        for token, row in TABLE.items():
            for ground, want in zip(GROUNDS, row):
                got = build.contrast(colours[token][:3], colours[ground][:3])
                self.assertLessEqual(abs(got - want), 0.06, f"{token} on {ground}: {got:.3f}, the design {want}")

    def test_the_named_pairs(self):
        _, variants = build.load()
        colours = variants["grace"]["colour"]
        self.assertAlmostEqual(build.contrast(colours["on.gold"][:3], colours["gold"][:3]), 8.1, delta=0.06)
        self.assertAlmostEqual(build.contrast(colours["ink.hi"][:3], colours["rot.fill"][:3]), 5.8, delta=0.06)
        for ground in GROUNDS:
            ratio = build.contrast(colours["edge"][:3], colours[ground][:3])
            self.assertTrue(3.05 <= ratio <= 4.35, f"edge on {ground}: {ratio:.2f}, §3.3 says 3.1 to 4.3")

    def test_clear_is_high_contrast(self):
        _, variants = build.load()
        clear = variants["clear"]["colour"]
        self.assertAlmostEqual(build.contrast(clear["ink.hi"][:3], clear["ground.well"][:3]), 18.3, delta=0.06)
        self.assertAlmostEqual(build.contrast(clear["ink.mute"][:3], clear["ground.well"][:3]), 10.9, delta=0.06)
        self.assertEqual(clear["ink.faint"], clear["ink.mute"])  # no faint level
        self.assertEqual(variants["clear"]["size"]["edge.width"], 2)
        self.assertEqual(variants["clear"]["size"]["focus.width"], 3)

    def test_rule_over_ground_2_is_the_notices(self):
        _, variants = build.load()
        colours = variants["grace"]["colour"]
        self.assertEqual(build.over(colours["rule"], colours["ground.2"]), (0x35, 0x2E, 0x1F))


class Outputs(unittest.TestCase):
    def test_outputs_are_current(self):
        data, variants = build.load()
        self.assertEqual(build.CSS_OUT.read_text(encoding="utf-8"), build.build_css(data, variants))
        self.assertEqual(build.HEADER_OUT.read_text(encoding="utf-8"), build.build_header(data, variants))

    def test_the_tokens_pass(self):
        self.assertEqual(failures_of(source()), [])
        self.assertEqual(build.check_stylesheets(), [])

    def test_reduced_motion(self):
        data, _ = build.load()
        css = build.build_css(*build.load())
        self.assertIn("@media (prefers-reduced-motion: reduce)", css)
        self.assertIn("--dur-close: 60ms;", css)        # a fade: at most 60 ms
        self.assertIn("--dur-state: 0ms;", css)          # a transition: 0
        self.assertIn("--dur-row-tint: 0ms;", css)       # a hold: 0
        self.assertEqual(data["dur"]["close"]["ms"], 200)


class Seeded(unittest.TestCase):
    """Each check fails on tokens seeded to break it."""

    def seeded(self, old, new):
        text = source()
        self.assertIn(old, text)
        return failures_of(text.replace(old, new, 1))

    def test_faint_text_too_dark(self):
        found = self.seeded('"ink.faint" = "#9A927F"', '"ink.faint" = "#6A6452"')
        self.assertTrue(any("ink.faint on ground" in f and "below 4.5:1" in f for f in found), found)

    def test_clear_variant_checked_too(self):
        found = self.seeded('[variant.clear.colour]\n"ground.well" = "#000000"',
                            '[variant.clear.colour]\n"ground.well" = "#000000"\n"ground.4" = "#8A8270"')
        self.assertTrue(any(f.startswith("clear:") for f in found), found)

    def test_edge_too_faint(self):
        found = self.seeded('"edge" = "#7E7460"', '"edge" = "#4A4436"')
        self.assertTrue(any("edge on ground" in f and "below 3.0:1" in f for f in found), found)

    def test_never_text_listed_as_text(self):
        found = self.seeded('text = ["ink.hi",', 'text = ["gold.deep", "ink.hi",')
        self.assertTrue(any("both as text and as never text" in f for f in found), found)

    def test_text_that_is_a_mix(self):
        found = self.seeded('"glint" = "#94B6D2"', '"glint" = { mix = "gold", alpha = 0.5 }')
        self.assertTrue(any("glint is not opaque" in f for f in found), found)

    def test_unknown_mix(self):
        with self.assertRaises(build.TokenError):
            failures_of(source().replace('{ mix = "gold", alpha = 0.14 }', '{ mix = "golden", alpha = 0.14 }', 1))

    def test_type_below_its_minimum(self):
        found = self.seeded('"ui.sm" = { font = "sans", size = 13.5,', '"ui.sm" = { font = "sans", size = 12,')
        self.assertTrue(any("type ui.sm" in f for f in found), found)
        found = self.seeded('"display.md" = { font = "display", size = 22,', '"display.md" = { font = "display", size = 18,')
        self.assertTrue(any("type display.md" in f and "display faces" in f for f in found), found)

    def test_motion(self):
        found = self.seeded('"close.danger" = { ms = 0, kind = "instant" }',
                            '"close.danger" = { ms = 50, kind = "instant" }')
        self.assertTrue(any("instant, but 50 ms" in f for f in found), found)
        found = self.seeded('"open" = { ms = 120, kind = "fade" }', '"open" = { ms = 120, kind = "fade", repeat = 3 }')
        self.assertTrue(any("nothing repeats" in f for f in found), found)
        found = self.seeded('"page" = { ms = 120, kind = "fade" }', '"page" = { ms = 120, kind = "pulse" }')
        self.assertTrue(any("kind pulse" in f for f in found), found)

    def test_looping_stylesheet(self):
        with tempfile.TemporaryDirectory() as folder:
            web = pathlib.Path(folder)
            (web / "a.css").write_text(".lamp { animation: glow 2s ease-in-out infinite; }\n", encoding="utf-8")
            (web / "b.ts").write_text("el.animate(frames, { duration: 300, iterations: Infinity });\n",
                                      encoding="utf-8")
            (web / "c.css").write_text(".row { transition: background var(--dur-state); }\n", encoding="utf-8")
            old_src, old_root = build.WEB_SRC, build.ROOT
            build.WEB_SRC, build.ROOT = web, web
            try:
                found = build.check_stylesheets()
            finally:
                build.WEB_SRC, build.ROOT = old_src, old_root
            found = [f for f in found if "looping" in f]  # a.css's 2s is also a literal duration (the next test)
            self.assertEqual(len(found), 2, found)
            self.assertTrue(found[0].startswith("a.css:1") and found[1].startswith("b.ts:1"), found)

    def test_the_web_apps_stylesheets_take_tokens_only(self):
        # D166: the web app's own CSS names colours, type sizes and durations only through tokens.css, fetches nothing,
        # and its components set no inline style (the page's CSP refuses one).
        with tempfile.TemporaryDirectory() as folder:
            web = pathlib.Path(folder)
            (web / "ok.css").write_text(
                "/* #fff in a comment is fine */\n.a { color: var(--ink-hi); font-size: var(--type-ui-size);\n"
                "  transition: color var(--dur-state); font: inherit; box-shadow: 0 8px 24px var(--shadow); }\n",
                encoding="utf-8")
            (web / "bad.css").write_text(
                ".b { color: #E8E2D0; }\n.c { background: rgb(0 0 0 / 50%); }\n.d { font-size: 15px; }\n"
                ".e { transition: opacity 120ms; }\n.f { background: url(https://example.invalid/x.png); }\n"
                ".g { border-color: white; }\n@font-face { font-family: X; }\n:root { --type-ui-size: 99px; }\n",
                encoding="utf-8")
            (web / "c.tsx").write_text("const a = <div style={{ color: 'red' }} />;\n", encoding="utf-8")
            old_src, old_root, old_css = build.WEB_SRC, build.ROOT, build.CSS_OUT
            build.WEB_SRC, build.ROOT, build.CSS_OUT = web, web, web / "tokens.css"
            try:
                found = build.check_stylesheets()
            finally:
                build.WEB_SRC, build.ROOT, build.CSS_OUT = old_src, old_root, old_css
            self.assertFalse(any(f.startswith("ok.css") for f in found), found)
            for expected in ("bad.css:1: a colour", "bad.css:2: a colour", "bad.css:3: font-size",
                             "bad.css:4: a duration", "bad.css:5: a url()", "bad.css:6: a colour",
                             "bad.css: @import or @font-face", "bad.css:8: --type-ui-size redefines",
                             "c.tsx:1: an inline style"):
                self.assertTrue(any(f.startswith(expected) for f in found), (expected, found))


if __name__ == "__main__":
    unittest.main()


class Icons(unittest.TestCase):
    """The line icons (D277): drawn from tokens/icons.toml, checked against protocol 1.22's names."""

    PROTOCOL = '[enums.icon]\nvalues = ["home", "ring"]\n'

    def load(self, body):
        return build.load_icons("stroke = 1.5\nsize = 18\n" + body, self.PROTOCOL)

    def test_the_icons_pass_and_follow_the_protocol(self):
        icons, failures = build.load_icons()
        self.assertEqual(failures, [])
        names = [name for name, _, _ in icons["icons"]]
        self.assertEqual(names[:3], ["home", "spark", "voice"])
        self.assertIn("lamp", names)
        self.assertNotIn("lantern", names)  # the release bundle carries no word of the demo's (its bundle scan)

    def test_lines_rings_and_arcs(self):
        icons, failures = self.load("[icon.home]\nlines = [[2, 9, 9, 2.5, 16, 9]]\n"
                                    "[icon.ring]\nrings = [[9, 9, 5]]\narcs = [[9, 9, 5, 0, 90]]\n")
        self.assertEqual(failures, [])
        home, ring = icons["icons"]
        self.assertEqual(home[1], [(2.0, 9.0, 9.0, 2.5), (9.0, 2.5, 16.0, 9.0)])  # a polyline, segment by segment
        self.assertEqual(ring[2], [(9.0, 9.0, 5.0)])
        self.assertEqual(len(ring[1]), 6)  # 90 degrees in steps of 15
        self.assertEqual(ring[1][0][:2], (14.0, 9.0))
        self.assertEqual(ring[1][-1][2:], (9.0, 14.0))  # clockwise on the display: from the right, down

    def test_what_fails(self):
        self.assertIn("outside 1.5 to 16.5", self.load("[icon.home]\nlines = [[1, 9, 9, 9]]\n[icon.ring]\n"
                                                        "rings = [[9, 9, 2]]\n")[1][0])
        self.assertIn("not within the margin", self.load("[icon.home]\nlines = [[2, 9, 9, 9]]\n[icon.ring]\n"
                                                         "rings = [[9, 9, 9]]\n")[1][0])
        self.assertIn("two points or more", self.load("[icon.home]\nlines = [[2, 9]]\n[icon.ring]\n"
                                                      "rings = [[9, 9, 2]]\n")[1][0])
        self.assertIn("not protocol 1.22's", self.load("[icon.ring]\nrings = [[9, 9, 2]]\n[icon.home]\n"
                                                       "lines = [[2, 9, 9, 9]]\n")[1][0])

    def test_numbers_for_cpp_have_a_point(self):
        self.assertEqual(build.icon_float(18), "18.0f")
        self.assertEqual(build.icon_float(2.5), "2.5f")
        self.assertEqual(build.icon_float(-0.0001), "0.0f")
