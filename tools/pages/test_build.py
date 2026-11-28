# SPDX-License-Identifier: MIT
"""Tests of tools/pages/build.py (D160): the built-in spaces pass and their outputs are current, and each rule of the
page lint fails on a space seeded to break it.

    python3 -I -m unittest discover -s tools/pages
"""
import contextlib
import copy
import io
import json
import pathlib
import re
import shutil
import sys
import tempfile
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

    def test_the_foot_comes_last(self):
        sp, desc, fx = demo()
        self.assertTrue(sp["categories"][-1].get("foot"))  # the demo's Records, at the rail's foot (D248)
        sp["categories"].reverse()
        self.expect("category lanterns follows records, at the rail's foot", sp, desc, fx)

    def test_filter_bars(self):
        # The demo's Keepers table and Lamps master list carry one each (G43, D250).
        sp, desc, fx = demo()
        keepers = page(sp, "demo.keepers")["sections"][0]["blocks"][0]
        self.assertIn("filter_bar", keepers)
        self.assertIn("filter_bar", page(sp, "demo.lamps")["master"])
        keepers["filter_bar"] = {}
        self.expect("its filter bar has a filter, a sort option or a search", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.keepers")["sections"][0]["blocks"][0]["filter_bar"]["filters"].append(
            {"column": "post", "label": "Again"})
        self.expect("filter on column post twice", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.keepers")["sections"][0]["blocks"][0]["filter_bar"]["search"] = {"columns": [], "label": "X"}
        self.expect("its search names one to four columns", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.lamps")["master"]["filter_bar"]["sort"].append({"key": "a.b", "label": "Bad"})
        self.expect("its master list: a sort option names its key", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"][0]["filter_bar"] = {"sort": [{"key": "a", "label": "A"}]}
        self.expect("a filter bar goes on a table, a stream or a page's master list", sp, desc, fx)
        # A stream's bar (D257): filters and a search, never a sort.
        sp, desc, fx = demo()
        feed = next(b for sec in page(sp, "demo.log")["sections"] for b in sec["blocks"]
                    if b.get("component") == "live_feed")
        feed["filter_bar"]["sort"] = [{"key": "time", "label": "Time"}]
        self.expect("a stream's filter bar has filters and a search, and no sort", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"].append(
            {"kind": "component", "component": "filter_bar", "topic": "lantern.keepers"})
        self.expect("a filter bar is a table's own field", sp, desc, fx)

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

    def test_component_props(self):
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"][0]["props"] = {"field": "Tiles!"}
        self.expect("props.field is a name", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.log")["sections"][0]["blocks"][0]["props"] = {"field": "lines"}
        self.expect("props.field is a name (a-z, 0-9, _; 1 to 32) on a state component", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"][0]["props"] = {"empty": "E" * 81}
        self.expect("props.empty is 1 to 80 characters", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"][0]["props"] = {"newest_first": True}
        self.expect("props.newest_first is a boolean on a log tail", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"][0]["props"] = {"field": "now", "empty": "All quiet."}
        self.assertEqual([f for f in failures(sp, desc, fx) if "props." in f], [])
        sp, desc, fx = demo()
        sp["topics"].append({"id": "space.state", "shape": "stream", "ring": 10})
        self.expect("topic space.state is a state topic", sp, desc, fx)

    def test_actions(self):
        sp, desc, fx = demo()
        page(sp, "demo.overview")["actions"][0]["command"] = "lantern.polish"
        self.expect("not a command of the space", sp, desc, fx)
        sp, desc, fx = demo()
        del page(sp, "demo.log")["actions"][0]["confirm"]
        self.expect("destructive action clear has no confirmation", sp, desc, fx)

    def test_rows_forms_and_choices(self):  # G34, D192 to D195
        def keepers(sp):
            return page(sp, "demo.keepers")["sections"]
        sp, desc, fx = demo()
        self.assertEqual(failures(sp, desc, fx), [])
        sp, desc, fx = demo()
        del keepers(sp)[0]["blocks"][0]["row_actions"][0]["confirm"]
        self.expect("destructive row action dismiss has no confirmation", sp, desc, fx)
        sp, desc, fx = demo()
        keepers(sp)[0]["blocks"][0]["row_actions"][0]["command"] = "keepers.sack"
        self.expect("row action dismiss has a label and sends a command of the space", sp, desc, fx)
        sp, desc, fx = demo()
        keepers(sp)[0]["blocks"][0]["row_secret"]["command"] = "keepers.whisper"
        self.expect("the row secret sends keepers.whisper", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"][0]["row_actions"] = \
            keepers(sp)[0]["blocks"][0]["row_actions"]
        self.expect("row actions and a row secret go on a table", sp, desc, fx)
        sp, desc, fx = demo()
        keepers(sp)[1]["blocks"][0]["fields"] = []
        self.expect("a form block has a declared command, one to four fields", sp, desc, fx)
        sp, desc, fx = demo()
        keepers(sp)[1]["blocks"][0]["fields"][1]["max"] = 201
        self.expect("form field post has a label and a max of 1 to 200", sp, desc, fx)
        sp, desc, fx = demo()
        keepers(sp)[2]["blocks"][0]["submit"] = "Go"
        self.expect("command, fields and submit belong to a form block", sp, desc, fx)
        sp, desc, fx = demo()
        setting(desc, "lantern.oil_kind")["choices_from"] = "lantern.log"
        self.expect("choices_from is on a text setting, naming a state topic", sp, desc, fx)
        sp, desc, fx = demo()
        setting(desc, "lantern.keeper")["choices_from"] = "lantern.oils"
        self.assertEqual([f for f in failures(sp, desc, fx) if "choices_from" in f], [])
        sp, desc, fx = demo()
        setting(desc, "lantern.wicks")["choices_from"] = "lantern.oils"
        self.expect("choices_from is on a text setting", sp, desc, fx)

    def test_section_cards(self):  # G36, D202
        def keepers(sp):
            return page(sp, "demo.keepers")["sections"]
        sp, desc, fx = demo()
        keepers(sp)[2]["chip"] = "a very long chip for a card"
        self.expect("chip: longer than 24 bytes", sp, desc, fx)  # the schema's bound, before the lint
        sp, desc, fx = demo()
        keepers(sp)[2]["chip"] = ""
        self.expect("its chip is 1 to 24 characters", sp, desc, fx)
        sp, desc, fx = demo()
        keepers(sp)[2]["status"] = {"topic": "lantern.log"}
        self.expect("its status names a state topic of the space's own", sp, desc, fx)
        sp, desc, fx = demo()
        keepers(sp)[2]["status"]["field"] = "Stock!"
        self.expect("its status field is a name", sp, desc, fx)
        sp, desc, fx = demo()
        del keepers(sp)[0]["actions"][0]["confirm"]
        self.expect("destructive action dismiss_all has no confirmation", sp, desc, fx)
        sp, desc, fx = demo()
        keepers(sp)[2]["actions"][0]["command"] = "oil.steal"
        self.expect("action order has a label and sends a command of the space", sp, desc, fx)

    def test_families_and_master_lists(self):  # G38, D215
        def topic(sp, tid):
            return next(t for t in sp["topics"] if t["id"] == tid)
        sp, desc, fx = demo()
        topic(sp, "lantern.lamp")["shape"] = "stream"
        topic(sp, "lantern.lamp")["ring"] = 10
        self.expect("a family is a state topic of the space's own", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.lamps")["master"]["detail"] = "lantern.lamps"
        self.expect("its master list's detail is a family the space declares", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.lamps")["master"]["topic"] = "lantern.lamp"
        self.expect("its master list's topic is a state topic of the space's own; a family only on a scoped page", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.lamps")["master"]["key"] = "lamp.id"
        self.expect("its master list has a key column (a name) and a label", sp, desc, fx)
        sp, desc, fx = demo()
        del page(sp, "demo.lamps")["master"]
        self.expect("draws family lantern.lamp on a page with no master list", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.keepers")["sections"][2]["status"] = {"topic": "lantern.lamp"}
        self.expect("its status names a state topic of the space's own", sp, desc, fx)
        sp, desc, fx = demo()
        for k in [k for k in fx if k.startswith("lantern.lamp/")]:
            del fx[k]
        self.expect("family lantern.lamp has no member's fixture", sp, desc, fx)
        sp, desc, fx = demo()
        fx["lantern.lamp/a/b"] = {}
        self.expect("is not an object for one member of lantern.lamp", sp, desc, fx)

    def test_slow_commands(self):  # D216
        sp, desc, fx = demo()
        sp["slow_commands"] = ["oil.steal"]
        self.expect("slow command oil.steal is a command of the space's own", sp, desc, fx)
        sp, desc, fx = demo()
        sp["slow_commands"] = ["settings.set"]
        self.expect("slow command settings.set is a command of the space's own, not a settings command", sp, desc, fx)
        sp, desc, fx = demo()
        sp["slow_commands"] = ["oil.order", "oil.order"]
        self.expect("slow command oil.order twice", sp, desc, fx)

    def test_quick_switches(self):  # G45, D252
        def hush(sp):
            return next(q for q in sp["quick"] if q.get("id") == "hush")
        sp, desc, fx = demo()
        sp["quick"].append({"key": "lantern.keeper"})  # a text setting
        self.expect("quick switch lantern.keeper: names a switch or a choice", sp, desc, fx)
        sp, desc, fx = demo()
        sp["quick"].append({"key": "lantern.nowhere"})
        self.expect("quick switch lantern.nowhere: names a switch or a choice", sp, desc, fx)
        sp, desc, fx = demo()
        sp["quick"][0]["label"] = "Lantern"
        self.expect("a setting's shortcut names its key alone", sp, desc, fx)
        sp, desc, fx = demo()
        del hush(sp)["home"]
        self.expect("quick switch hush: a session switch has an id, a label, a command, a state and a home", sp, desc, fx)
        for command in ("bell.ring", "settings.set", "keepers.import", "oil.order"):
            sp, desc, fx = demo()
            hush(sp)["command"] = command
            self.expect("quick switch hush: sends a command of the space's own", sp, desc, fx)
        for topic in ("lantern.log", "lantern.guide", "settings", "lantern.nowhere"):
            sp, desc, fx = demo()
            hush(sp)["state"]["topic"] = topic
            self.expect("quick switch hush: its state is on a state topic of the space's own", sp, desc, fx)
        sp, desc, fx = demo()
        hush(sp)["state"]["field"] = "Hushed"
        self.expect("quick switch hush: its state field is a name", sp, desc, fx)
        sp, desc, fx = demo()
        hush(sp)["home"] = "demo.nowhere"
        self.expect("quick switch hush: its home demo.nowhere is not a page", sp, desc, fx)
        sp, desc, fx = demo()
        sp["quick"].append({"key": "lantern.enabled"})
        self.expect("quick switch lantern.enabled twice", sp, desc, fx)

    def test_live_pages(self):  # G45, D252
        sp, desc, fx = demo()
        page(sp, "demo.behaviour")["live"] = True  # settings rows alone: nothing moves by itself
        self.expect("demo.behaviour: a live page draws a topic of the space's", sp, desc, fx)

    def test_a_gated_page(self):  # D258
        def bench(sp):
            return page(sp, "demo.workbench")
        for key in ("lantern.colour", "lantern.nowhere"):
            sp, desc, fx = demo()
            bench(sp)["shown_with"] = key
            self.expect("it is shown with a switch of the space's settings (D258)", sp, desc, fx)
        sp, desc, fx = demo()
        del bench(sp)["browser_only"]
        self.expect("a gate is a browser-only page's (D258)", sp, desc, fx)
        sp, desc, fx = demo()
        bench(sp)["gate"]["confirm"] = ""
        self.expect("its gate has its words (1 to 256 characters) and a confirm label (1 to 32)", sp, desc, fx)
        sp, desc, fx = demo()
        bench(sp)["ribbon"] = ""
        self.expect("its ribbon is 1 to 32 characters", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.behaviour")["browser_only"] = True
        self.expect("a browser-only page is no setting's home", sp, desc, fx)
        sp, desc, fx = demo()
        sp["quick"][2]["home"] = "demo.workbench"
        self.expect("its home is a page in game too, not a browser-only one (D258)", sp, desc, fx)

    def test_a_header_switch(self):  # D256
        def hush(sp):
            return next(a for a in page(sp, "demo.overview")["actions"] if a["id"] == "hush")
        for command in ("settings.set", "keepers.import", "oil.order"):
            sp, desc, fx = demo()
            hush(sp)["command"] = command
            self.expect("a switch sends a command of the space's own, neither a settings, a file nor a slow", sp, desc, fx)
        sp, desc, fx = demo()
        hush(sp)["tone"] = "destructive"
        hush(sp)["confirm"] = "Sure?"
        self.expect("a switch is neither destructive nor confirmed", sp, desc, fx)
        for topic in ("lantern.lamp", "settings", "lantern.log", "lantern.nowhere"):
            sp, desc, fx = demo()
            hush(sp)["state"]["topic"] = topic
            self.expect("its state is on a state topic of the space's own, not a family (D256)", sp, desc, fx)
        sp, desc, fx = demo()
        hush(sp)["state"]["field"] = "Hushed!"
        self.expect("its state field is a name", sp, desc, fx)
        sp, desc, fx = demo()
        section = next(sec for sec in page(sp, "demo.keepers")["sections"] if sec.get("actions"))
        section["actions"][0]["state"] = {"topic": "lantern.status", "field": "hushed"}
        self.expect("a switch is a header action's alone (D256)", sp, desc, fx)

    def test_scope_and_pages_read_a_page_at_a_time(self):  # G45, D254
        def lightings(sp):
            return page(sp, "demo.lightings")
        def history(sp):
            return next(b for sec in page(sp, "demo.log")["sections"] for b in sec["blocks"] if "page" in b)
        sp, desc, fx = demo()
        del sp["scope"]
        self.expect("demo.lightings: a scoped page follows the space's scope, which the space does not declare", sp, desc, fx)
        for topic in ("lantern.season", "settings", "lantern.log", "lantern.nowhere"):
            sp, desc, fx = demo()
            sp["scope"]["topic"] = topic
            self.expect("its scope is on a state topic of the space's own, not a family", sp, desc, fx)
        sp, desc, fx = demo()
        sp["scope"]["label"] = ""
        self.expect("its scope's label is 1 to 32 characters", sp, desc, fx)
        # A master list: a topic or a page source; a family only on a scoped page.
        sp, desc, fx = demo()
        lightings(sp)["master"]["topic"] = "lantern.status"
        self.expect("its master list reads a topic or a page source, one of the two", sp, desc, fx)
        sp, desc, fx = demo()
        del lightings(sp)["master"]["page"]
        lightings(sp)["master"]["topic"] = "lantern.season"
        del lightings(sp)["scoped"]
        self.expect("a family only on a scoped page", sp, desc, fx)
        for command in ("lightings.steal", "settings.set", "keepers.import", "oil.order"):
            sp, desc, fx = demo()
            lightings(sp)["master"]["page"]["command"] = command
            self.expect("its rows come from a command of the space's own, neither a settings, a file nor a slow", sp, desc, fx)
        sp, desc, fx = demo()
        history(sp)["page"]["changes"] = "lantern.log"
        self.expect("its changes topic is a state topic of the space's own, not a family", sp, desc, fx)
        # A table: a topic or a page source; a page source on a table alone.
        sp, desc, fx = demo()
        history(sp)["topic"] = "lantern.status"
        self.expect("a table reads a topic or a page source, one of the two", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"].append(
            {"kind": "component", "component": "key_value_list", "page": {"command": "lightings.page"}})
        self.expect("a page source belongs to a table block", sp, desc, fx)
        # A filter's own choices: on rows read a page at a time, and only there.
        sp, desc, fx = demo()
        del history(sp)["filter_bar"]["filters"][0]["choices_from"]
        self.expect("a filter on rows read a page at a time names its choices", sp, desc, fx)
        sp, desc, fx = demo()
        history(sp)["filter_bar"]["filters"][0]["choices"] = [{"value": "North light", "label": "North light"}]
        self.expect("a filter on rows read a page at a time names its choices", sp, desc, fx)
        sp, desc, fx = demo()
        history(sp)["filter_bar"]["filters"][0]["choices_from"] = {"topic": "lantern.season"}
        self.expect("a filter's choices_from is a state topic of the space's own, not a family", sp, desc, fx)
        sp, desc, fx = demo()
        keepers = next(b for sec in page(sp, "demo.keepers")["sections"] for b in sec["blocks"] if "filter_bar" in b)
        keepers["filter_bar"]["filters"][0]["choices"] = [{"value": "x", "label": "X"}]
        self.assertEqual(failures(sp, desc, fx), [])  # its own choices word its rows' values (D262)
        sp, desc, fx = demo()
        keepers = next(b for sec in page(sp, "demo.keepers")["sections"] for b in sec["blocks"] if "filter_bar" in b)
        keepers["filter_bar"]["filters"][0]["choices_from"] = {"topic": "lantern.season"}
        self.expect("a filter takes its choices from a state topic only on rows read a page at a time", sp, desc, fx)
        # A family on a scoped page with no master list is the scope's member.
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"].append(
            {"kind": "component", "component": "key_value_list", "topic": "lantern.season"})
        self.expect("draws family lantern.season on a page with no master list", sp, desc, fx)
        page(sp, "demo.overview")["scoped"] = True
        self.assertFalse(any("lantern.season" in f for f in failures(sp, desc, fx)))

    def test_this_pc_alone(self):  # D264
        sp, desc, fx = demo()
        self.assertEqual(failures(sp, desc, fx), [])  # the Workbench and its readings, both on this PC alone
        del page(sp, "demo.workbench")["pc_only"]
        self.expect("page demo.workbench draws lantern.readings, a topic on this PC alone", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"].append(
            {"kind": "component", "component": "key_value_list", "topic": "lantern.readings"})
        self.expect("page demo.overview draws lantern.readings, a topic on this PC alone", sp, desc, fx)
        sp, desc, fx = demo()
        setting(desc, "lantern.brightness")["home"] = "demo.workbench"
        self.expect("setting lantern.brightness lives on pc_only page demo.workbench: it is sensitive too", sp, desc, fx)
        setting(desc, "lantern.brightness")["sensitive"] = True
        self.assertFalse(any("it is sensitive too" in f for f in failures(sp, desc, fx)))
        # And shown on no page, nor by a quick switch, a paired device sees.
        self.expect("shows setting lantern.brightness, which lives on a pc_only page", sp, desc, fx)
        sp, desc, fx = demo()
        setting(desc, "lantern.enabled")["home"] = "demo.workbench"
        setting(desc, "lantern.enabled")["sensitive"] = True
        self.expect("quick switch lantern.enabled is a pc_only page's", sp, desc, fx)

    def test_a_confirmation_that_waits(self):  # D274
        sp, desc, fx = demo()
        self.assertEqual(failures(sp, desc, fx), [])  # Dismiss all waits a second before its second press counts
        self.assertEqual(build.CONFIRM_AFTER_MAX_MS, SCHEMA["limits"]["confirm_after_max_ms"])
        roster = page(sp, "demo.keepers")["sections"][0]
        roster["actions"][0]["confirm_after_ms"] = 0
        self.expect("action dismiss_all: confirm_after_ms is 1 to 3000", sp, desc, fx)
        roster["actions"][0]["confirm_after_ms"] = 3001
        self.expect("action dismiss_all: confirm_after_ms is 1 to 3000", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["actions"][0]["confirm_after_ms"] = 1000  # Trim the wick: primary, no confirm words
        self.expect("action trim: confirm_after_ms waits on a confirmation", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["actions"][1]["confirm_after_ms"] = 1000  # Hush the bell: a header switch
        self.expect("a switch is neither destructive nor confirmed", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.keepers")["sections"][0]["blocks"][0]["row_actions"][0]["confirm_after_ms"] = 1000
        self.assertEqual(failures(sp, desc, fx), [])  # a destructive row action may wait too
        sp, desc = copy.deepcopy(SPACES["graceui"])
        fx = copy.deepcopy(build.fixtures_of("graceui"))
        key = next(a for sec in page(sp, "graceui.hotkeys")["sections"] for b in sec["blocks"]
                   for a in b.get("row_actions", []) if a.get("capture") == "key")
        key.update(confirm="Use this key?", confirm_after_ms=1000)
        self.expect("action key: confirm_after_ms on a capture", sp, desc, fx)

    def test_a_categorys_icon(self):  # D277
        sp, desc, fx = demo()
        self.assertEqual(failures(sp, desc, fx), [])  # the lantern and the book
        self.assertEqual(SCHEMA["enums"]["icon"]["values"][:3], ["home", "spark", "voice"])
        sp["categories"][0]["icon"] = "dragon"
        self.expect("categories[0].icon: not one of icon's names", sp, desc, fx)
        sp, desc, fx = demo()
        del sp["categories"][0]["icon"]
        self.assertEqual(failures(sp, desc, fx), [])  # optional: the rail draws its initial

    def test_a_flow(self):  # D278
        sp, desc, fx = demo()
        self.assertEqual(failures(sp, desc, fx), [])  # the demo's setup flow, outside the rail
        flow = page(sp, "demo.setup")
        self.assertEqual([t["id"] for t in flow["tabs"]], ["welcome", "flame", "trim", "ready"])
        sp["categories"][0]["pages"].append("demo.setup")
        self.expect("page demo.setup is a flow, outside the rail: no category lists it", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.setup")["category"] = "nowhere"
        self.expect("flow demo.setup names category nowhere, which is not one of the space's", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.setup")["tabs"] = page(sp, "demo.setup")["tabs"][:1]
        self.expect("a flow's steps are its tabs, two to twelve", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.setup")["live"] = True
        self.expect("a flow has no master list and is neither live nor scoped", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.setup")["flow"]["topic"] = "lantern.journal"  # a stream, and private
        self.expect("its state is a state topic of the space's own, neither a family nor private", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.setup")["flow"]["command"] = "keepers.import"
        self.expect("it sends a command of the space's own, neither a settings nor a file command", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.setup")["flow"]["optional"] = ["trim", "ready"]
        self.expect("its last step is never optional: its button is Done", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.setup")["flow"]["optional"] = ["dusk"]
        self.expect("optional step dusk is not one of its steps", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.setup")["flow"]["then"] = "demo.workbench"
        self.expect("then names a page of the space's rail, in game too, not a flow", sp, desc, fx)
        sp, desc, fx = demo()
        setting(desc, "lantern.colour")["home"] = "demo.setup"
        self.expect("a flow is no setting's home", sp, desc, fx)
        sp, desc, fx = demo()
        fx["lantern.setup"]["step"] = "dusk"
        self.expect("a flow's state names its own steps", sp, desc, fx)
        sp, desc, fx = demo()
        fx["lantern.setup"]["start"] = "yes"
        self.expect("start a boolean", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"].append({"kind": "component", "component": "wizard_steps",
                                                                    "topic": "lantern.setup"})
        self.expect("wizard_steps is no block of its own", sp, desc, fx)

    def test_an_action_that_opens_a_page(self):  # D278
        sp, desc, fx = demo()
        actions = page(sp, "demo.overview")["actions"]
        self.assertEqual(actions[2]["opens"], "demo.setup")
        actions[2]["opens"] = "demo.overview"
        self.expect("opens demo.overview, which is not another page of the space's", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["actions"][1]["opens"] = "demo.setup"  # the bell's header switch
        self.expect("a switch opens no page", sp, desc, fx)
        sp, desc, fx = demo()
        section = page(sp, "demo.setup")["tabs"][2]["sections"][0]
        section["actions"][0]["opens"] = "demo.lamps"
        self.assertEqual(failures(sp, desc, fx), [])  # a section's action may open one too

    def test_a_private_topic(self):  # D274
        sp, desc, fx = demo()
        self.assertEqual(failures(sp, desc, fx), [])  # the journal is private, drawn in its log tail alone
        section = page(sp, "demo.keepers")["sections"][2]  # Oil: its chip's status
        section["status"] = {"topic": "lantern.journal", "field": "line"}
        self.expect("names lantern.journal, a private topic, as a status", sp, desc, fx)
        sp, desc, fx = demo()
        setting(desc, "lantern.keeper")["choices_from"] = "lantern.journal"
        self.expect("setting lantern.keeper takes its choices from lantern.journal, a private topic", sp, desc, fx)
        sp, desc, fx = demo()
        sp["scope"]["topic"] = "lantern.journal"
        self.expect("the scope names lantern.journal, a private topic", sp, desc, fx)
        sp, desc, fx = demo()
        sp["quick"][2]["state"]["topic"] = "lantern.journal"
        self.expect("a quick switch's state is on lantern.journal, a private topic", sp, desc, fx)

    def test_a_report_previews_share(self):  # D263
        def report(sp):
            return next(b for sec in page(sp, "demo.log")["sections"] for b in sec["blocks"]
                        if b.get("component") == "report_preview")
        sp, desc, fx = demo()
        self.assertEqual(failures(sp, desc, fx), [])  # the demo's Copy and Save
        report(sp)["share"] = {"copy": True}
        self.assertEqual(failures(sp, desc, fx), [])  # Copy alone
        report(sp)["share"] = {"copy": False}
        self.expect("a report preview's share offers Copy, Save or both", sp, desc, fx)
        sp, desc, fx = demo()
        sp["sensitive_commands"].remove("report.save")
        self.expect("a report preview's Save sends a command of the space's, sensitive", sp, desc, fx)
        sp, desc, fx = demo()
        report(sp)["share"]["save"] = "report.nowhere"
        self.expect("a report preview's Save sends a command of the space's, sensitive", sp, desc, fx)
        sp, desc, fx = demo()
        report(sp)["share"]["save"] = "keepers.import"  # sensitive, but a file command
        self.expect("a report preview's Save sends a command of the space's, sensitive", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.overview")["sections"][0]["blocks"][0]["share"] = {"copy": True}
        self.expect("share belongs to a report preview", sp, desc, fx)
        sp, desc, fx = demo()
        report(sp)["topic"] = "lantern.lamp"  # a family: its Save would name no member
        self.expect("a report preview's share is on a topic of its own, not a family", sp, desc, fx)

    def test_file_commands_and_upload_blocks(self):  # D226
        def roster_block(sp):
            section = next(s for s in page(sp, "demo.keepers")["sections"] if s["id"] == "roster_file")
            return section["blocks"][0]
        sp, desc, fx = demo()
        sp["sensitive_commands"] = []
        self.expect("file command keepers.import is marked sensitive too", sp, desc, fx)
        sp, desc, fx = demo()
        sp["file_commands"] = ["keepers.import", "settings.set"]
        self.expect("file command settings.set is a command of the space's own, not a settings command", sp, desc, fx)
        sp, desc, fx = demo()
        sp["slow_commands"].append("keepers.import")
        self.expect("file command keepers.import is not a slow command", sp, desc, fx)
        sp, desc, fx = demo()
        roster_block(sp)["command"] = "keepers.hire"
        self.expect("an upload block has a file command of the space's", sp, desc, fx)
        sp, desc, fx = demo()
        del roster_block(sp)["submit"]
        self.expect("an upload block has a file command of the space's", sp, desc, fx)
        sp, desc, fx = demo()
        roster_block(sp)["accept"] = "zip"
        self.expect("an upload block's accept lists extensions", sp, desc, fx)
        sp, desc, fx = demo()
        page(sp, "demo.keepers")["sections"][1]["blocks"][0]["accept"] = ".zip"
        self.expect("accept belongs to an upload block", sp, desc, fx)
        # Sent only by an upload block (D229): never by a form, a row action or a page's action.
        sp, desc, fx = demo()
        form = next(b for s in page(sp, "demo.keepers")["sections"] for b in s["blocks"] if b.get("kind") == "form")
        form["command"] = "keepers.import"
        self.expect("keepers.import is a file command, sent only by an upload block", sp, desc, fx)
        sp, desc, fx = demo()
        table = next(b for s in page(sp, "demo.keepers")["sections"] for b in s["blocks"] if b.get("row_actions"))
        table["row_actions"][0]["command"] = "keepers.import"
        self.expect("keepers.import is a file command, sent only by an upload block", sp, desc, fx)

    def test_setting_descriptions(self):
        cases = [
            ("lantern.oil_token", "default", "hunter2", "a secret's default is empty"),
            ("lantern.colour", "default", "green", "not one of its choices"),
            ("lantern.festival", "locked", False, "a later setting is locked"),
            ("lantern.wicks", "default", 12, "outside 1 to 8"),
            ("lantern.brightness", "step", 0.00025, "its step 0.00025 has more than four decimals"),
            ("lantern.brightness", "min", 0.00001, "its min 1e-05 has more than four decimals"),
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


EXAMPLE = build.ROOT / "examples" / "hello-space" / "space"


def run_quietly(argv):
    """build.run with its output kept: (status, what it printed to stdout and stderr)."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        status = build.run(argv)
    return status, out.getvalue() + err.getvalue()


def header_arrays(text):
    """The char arrays of a header --build wrote, decoded: {NAME: bytes}, each checked against its _LENGTH."""
    arrays = {}
    for name, body in re.findall(r"static const char (\w+)\[\d+\] = \{([^}]*)\};", text):
        data = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", body))
        length = re.search(rf"#define {name}_LENGTH (\d+)u", text)
        assert length and int(length.group(1)) == len(data), name
        arrays[name] = data
    return arrays


class BuildOne(unittest.TestCase):  # D283: --build, a mod's space compiled
    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)

    def copy_example(self):
        source = self.dir / "src"
        shutil.copytree(EXAMPLE, source)
        return source

    def test_the_example_builds_and_its_outputs_hold(self):
        out, header = self.dir / "space", self.dir / "hello_space_data.h"
        args = ["--build", str(EXAMPLE / "space.toml"), "--out", str(out), "--header", str(header)]
        status, printed = run_quietly(args)
        self.assertEqual(status, 0, printed)
        pages = json.loads((out / "pages.json").read_text(encoding="utf-8"))
        settings = json.loads((out / "settings.json").read_text(encoding="utf-8"))
        self.assertEqual(pages["space"], "hello-space")
        self.assertEqual(pages["minor"], SCHEMA["protocol"]["minor"])
        arrays = header_arrays(header.read_text(encoding="ascii"))
        self.assertEqual(set(arrays), {"HELLO_SPACE_PAGES", "HELLO_SPACE_SETTINGS"})
        self.assertEqual(json.loads(arrays["HELLO_SPACE_PAGES"]), pages)
        self.assertEqual(json.loads(arrays["HELLO_SPACE_SETTINGS"]), settings)
        self.assertTrue(all(b < 0x80 for b in arrays["HELLO_SPACE_PAGES"]))  # ASCII: no narrowing in a C++ char
        self.assertEqual(run_quietly(args + ["--check"])[0], 0)
        (out / "settings.json").write_text("{}\n", encoding="utf-8")
        status, printed = run_quietly(args + ["--check"])
        self.assertEqual(status, 1)
        self.assertIn("settings.json is not what", printed)

    def test_the_minor_named(self):
        out = self.dir / "space"
        base = ["--build", str(EXAMPLE / "space.toml"), "--out", str(out)]
        self.assertEqual(run_quietly(base + ["--minor", "21"])[0], 0)
        self.assertEqual(json.loads((out / "pages.json").read_text(encoding="utf-8"))["minor"], 21)
        newest = SCHEMA["protocol"]["minor"]
        for minor in (newest + 1, -1):
            status, printed = run_quietly(base + ["--minor", str(minor)])
            self.assertEqual(status, 1)
            self.assertIn(f"0 to {newest}", printed)

    def test_a_builtin_space_is_not_a_mods(self):
        status, printed = run_quietly(["--build", str(build.SPACES / "demo" / "space.toml"), "--out", str(self.dir)])
        self.assertEqual(status, 1)
        self.assertIn("one of GraceUI's own", printed)

    def test_its_fixtures_are_linted_and_a_failure_writes_nothing(self):
        source = self.copy_example()
        fixtures = json.loads((source / "topics.json").read_text(encoding="utf-8"))
        del fixtures["hello.log"]
        (source / "topics.json").write_text(json.dumps(fixtures), encoding="utf-8")
        out = self.dir / "space"
        status, printed = run_quietly(["--build", str(source / "space.toml"), "--out", str(out)])
        self.assertEqual(status, 1)
        self.assertIn("hello.log", printed)
        self.assertFalse(out.exists())

    def test_the_lints_rules_hold(self):
        source = self.copy_example()
        text = (source / "space.toml").read_text(encoding="utf-8")
        (source / "space.toml").write_text(text.replace('command = "hello.add"', 'command = "hello.double"'), "utf-8")
        status, printed = run_quietly(["--build", str(source / "space.toml"), "--out", str(self.dir / "space")])
        self.assertEqual(status, 1)
        self.assertIn("hello.double", printed)

    def test_its_options_go_together(self):
        for argv in (["--build", str(EXAMPLE / "space.toml")], ["--out", str(self.dir)], ["--minor", "3"],
                     ["--build", str(EXAMPLE / "space.toml"), "--out", str(self.dir), "--lint", "x.toml"],
                     ["--build", ""], ["--build", "", "--out", str(self.dir)], ["--out", ""], ["--header", ""]):
            with self.assertRaises(SystemExit, msg=argv):  # never the built-in spaces' outputs rewritten instead
                run_quietly(argv)

    def test_a_header_name_never_starts_with_a_digit(self):
        self.assertEqual(build.c_name("hello-space"), "HELLO_SPACE")
        self.assertEqual(build.c_name("3d-map"), "SPACE_3D_MAP")
        source = self.copy_example()
        text = (source / "space.toml").read_text(encoding="utf-8")
        (source / "space.toml").write_text(text.replace('id = "hello-space"', 'id = "3d-map"'), "utf-8")
        header = self.dir / "map.h"
        status, printed = run_quietly(["--build", str(source / "space.toml"), "--out", str(self.dir / "space"),
                                       "--header", str(header)])
        self.assertEqual(status, 0, printed)
        self.assertEqual(set(header_arrays(header.read_text(encoding="ascii"))), {"SPACE_3D_MAP_PAGES",
                                                                                  "SPACE_3D_MAP_SETTINGS"})
        self.assertIn("#ifndef SPACE_3D_MAP_SPACE_DATA_H", header.read_text(encoding="ascii"))

    def test_a_source_not_in_utf8_is_an_error_not_a_traceback(self):
        source = self.copy_example()
        (source / "topics.json").write_bytes(b'{"hello.status": "\xff"}')
        status, printed = run_quietly(["--build", str(source / "space.toml"), "--out", str(self.dir / "space")])
        self.assertEqual(status, 1)
        self.assertIn("pages:", printed)


if __name__ == "__main__":
    unittest.main()
