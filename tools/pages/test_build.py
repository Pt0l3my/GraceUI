# SPDX-License-Identifier: MIT
"""Tests of tools/pages/build.py (D160): the built-in spaces pass and their outputs are current, and each rule of the
page lint fails on a space seeded to break it.

    python3 -I -m unittest discover -s tools/pages
"""
import copy
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import build  # noqa: E402

SCHEMA = build.protocol.load()
SPACES = {sp["space"]: (sp, desc) for sp, desc in build.load_all(SCHEMA)}
KNOWN = {name: {p["id"] for p in sp["pages"]} for name, (sp, _) in SPACES.items()}


def demo():
    space_pages, description = copy.deepcopy(SPACES["demo"])
    return space_pages, description, copy.deepcopy(build.fixtures_of("demo"))


def page(space_pages, pid):
    return next(p for p in space_pages["pages"] if p["id"] == pid)


def setting(description, key):
    return next(s for s in description["settings"] if s["key"] == key)


def failures(space_pages, description, fixtures=None):
    return build.check(SCHEMA, space_pages, description, fixtures, KNOWN)


class Builtin(unittest.TestCase):
    def test_every_builtin_space_passes(self):
        for name, (space_pages, description) in SPACES.items():
            self.assertEqual(failures(space_pages, description, build.fixtures_of(name)), [], name)

    def test_outputs_are_current(self):
        self.assertEqual(build.BUILTIN.read_text(encoding="utf-8"), build.build_builtin(build.load_all(SCHEMA)))
        for name, (space_pages, description) in SPACES.items():
            self.assertEqual((build.FIXTURES / name / "pages.json").read_text(encoding="utf-8"),
                             build.pretty(space_pages))
            self.assertEqual((build.FIXTURES / name / "settings.json").read_text(encoding="utf-8"),
                             build.pretty(description))

    def test_a_cross_space_link(self):
        sp, desc, fx = demo()
        page(sp, "demo.overview")["related"].append("graceui/graceui.menu")
        self.assertEqual(failures(sp, desc, fx), [])
        page(sp, "demo.overview")["related"].append("graceui/graceui.nowhere")
        self.assertTrue(any("names no page of a built-in space" in f for f in failures(sp, desc, fx)))

    def test_the_hash_follows_the_description(self):
        _, desc = SPACES["demo"]
        changed = copy.deepcopy(desc["settings"])
        changed[0]["label"] = "Lit"
        self.assertNotEqual(build.hashlib.sha256(build.json.dumps(changed, ensure_ascii=False, separators=(",", ":"),
                                                                    sort_keys=True).encode()).hexdigest()[:16],
                            desc["schema"]["hash"])


class Seeded(unittest.TestCase):
    """Each rule of the lint fails on a demo space seeded to break it."""

    def expect(self, words, sp, desc, fx=None):
        found = failures(sp, desc, fx)
        self.assertTrue(any(words in f for f in found), f"{words!r} not in {found}")

    def test_schema_shape_first(self):
        sp, desc, fx = demo()
        page(sp, "demo.log")["name"] = "L" * 65
        self.expect("pages: pages[2].name: longer than 64 bytes", sp, desc, fx)

    def test_setting_homed_elsewhere(self):
        sp, desc, fx = demo()
        setting(desc, "lantern.wicks")["home"] = "demo.overview"
        self.expect("lantern.wicks is homed on demo.overview, not here", sp, desc, fx)

    def test_setting_on_no_block_and_twice(self):
        sp, desc, fx = demo()
        flame = page(sp, "demo.behaviour")["tabs"][0]["sections"][0]["blocks"][0]
        flame["keys"].remove("lantern.mode")
        self.expect("lantern.mode appears on 0 settings blocks", sp, desc, fx)
        flame["keys"] += ["lantern.mode", "lantern.mode"]
        self.expect("lantern.mode appears on 2 settings blocks", sp, desc, fx)

    def test_shortcut_to_its_own_home(self):
        sp, desc, fx = demo()
        page(sp, "demo.behaviour")["tabs"][0]["sections"][0]["blocks"].append({"kind": "shortcuts",
                                                                               "keys": ["lantern.enabled"]})
        self.expect("a shortcut to lantern.enabled, which is homed here", sp, desc, fx)

    def test_the_rail(self):
        sp, desc, fx = demo()
        sp["categories"][0]["pages"].append("demo.nowhere")
        self.expect("lists demo.nowhere, which is not a page", sp, desc, fx)
        sp, desc, fx = demo()
        sp["categories"][1]["pages"].append("demo.overview")
        self.expect("page demo.overview in two categories", sp, desc, fx)

    def test_tabs_and_sections(self):
        sp, desc, fx = demo()
        page(sp, "demo.behaviour")["sections"] = [{"id": "x", "title": "X", "blocks": []}]
        self.expect("tabs or sections, not both or neither", sp, desc, fx)

    def test_help_steps_and_increment(self):
        sp, desc, fx = demo()
        page(sp, "demo.log")["help"]["steps"] = ["One.", "Two."]
        self.expect("its help has 2 steps", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.festival")["increment"] = "0.2"
        self.expect("an increment or a tag, one of them", sp, desc, fx)

    def test_related_links(self):
        sp, desc, fx = demo()
        page(sp, "demo.log")["related"] = ["demo.archive"]
        self.expect("Related link demo.archive is not a page", sp, desc, fx)
        page(sp, "demo.log")["related"] = ["demo.log"]
        self.expect("links to itself", sp, desc, fx)

    def test_components_and_topics(self):
        sp, desc, fx = demo()
        page(sp, "demo.log")["sections"][0]["blocks"][0]["topic"] = "lantern.status"
        self.expect("live_feed cannot draw state topic lantern.status", sp, desc, fx)
        page(sp, "demo.log")["sections"][0]["blocks"][0]["topic"] = "lantern.smoke"
        self.expect("topic lantern.smoke is not one the space declares", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.festival")["sections"][0]["blocks"][0]["topic"] = "lantern.status"
        self.expect("statement takes no topic", sp, desc, fx)

    def test_actions(self):
        sp, desc, fx = demo()
        page(sp, "demo.overview")["actions"][0]["command"] = "lantern.polish"
        self.expect("not a command of the space", sp, desc, fx)
        sp, desc, fx = demo()
        del page(sp, "demo.log")["actions"][0]["confirm"]
        self.expect("destructive action clear has no confirmation", sp, desc, fx)

    def test_setting_descriptions(self):
        cases = [
            ("lantern.oil_token", "default", "hunter2", "a secret's default is empty"),
            ("lantern.colour", "default", "green", "not one of its choices"),
            ("lantern.festival", "locked", False, "a later setting is locked"),
            ("lantern.wicks", "default", 12, "outside 1 to 8"),
            ("lantern.mode", "default", "maybe", "a tri-state's default"),
            ("lantern.shield", "third", "auto", "a tri-state's default is on, off or auto"),
            ("lantern.enabled", "availability", "soon", "is not live, later"),
            ("lantern.trim_key", "default", "Hyper+F9", "not a key's name"),
        ]
        for key, field, value, words in cases:
            with self.subTest(key=key):
                sp, desc, fx = demo()
                setting(desc, key)[field] = value
                self.expect(words, sp, desc, fx)
        sp, desc, fx = demo()
        setting(desc, "lantern.enabled")["min"] = 0
        self.expect("min on a bool", sp, desc, fx)
        sp, desc, fx = demo()
        setting(desc, "lantern.enabled")["third"] = "inherit"
        self.expect("third on a bool", sp, desc, fx)

    def test_fixtures(self):
        sp, desc, fx = demo()
        del fx["lantern.journal"]
        self.expect("topic lantern.journal has no fixture", sp, desc, fx)
        sp, desc, fx = demo()
        fx["lantern.log"][1]["seq"] = 1
        self.expect("not a list of rows with rising seq numbers", sp, desc, fx)
        sp, desc, fx = demo()
        fx["lantern.status"] = []
        self.expect("fixture lantern.status is not an object", sp, desc, fx)

    def test_unknown_fields_in_the_source(self):
        text = (build.SPACES / "demo" / "space.toml").read_text(encoding="utf-8")
        bad = build.SPACES.parent / "_test_bad_space.toml"
        try:
            bad.write_text(text.replace('label = "Lantern lit"', 'label = "Lantern lit"\ncolour = "gold"'), "utf-8")
            with self.assertRaises(build.PagesError):
                build.documents(bad, SCHEMA)
        finally:
            bad.unlink()


if __name__ == "__main__":
    unittest.main()
