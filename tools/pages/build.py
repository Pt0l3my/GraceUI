#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""tools/pages/build.py: the page descriptions of GraceUI's built-in spaces, checked and compiled (GraceUI DESIGN §2.5,
§4, §6.9; D160; invariant 12).

    python3 -I tools/pages/build.py                 check every space, then write its outputs
    python3 -I tools/pages/build.py --check         check, and fail if an output is not what the spaces build
    python3 -I tools/pages/build.py --lint FILE...  check other spaces' sources (EldenNet's, against this GraceUI)

Each space is one TOML file, src/grace-pages/spaces/<id>/space.toml: its rail, pages, topics, commands and settings.
Its outputs: fixtures/<id>/pages.json (the protocol's space_pages), fixtures/<id>/settings.json (its settings
description) and src/grace-pages/builtin.cpp, which compiles every built-in space into grace-pages. Both JSON documents
are checked against the protocol's schema as grace-protocol's decoders check them, then by the page lint:

  1. Ids: pages, categories, topics and settings unique in the space; tabs, sections and actions unique in their page.
  2. The rail: every category's pages exist, and every page is in exactly the category it names.
  3. A page: tabs or sections, not both; an increment or a tag; help of three to five steps; Related links to pages
     that exist (this space's, or another built-in space's as "<space>/<page>"), never itself.
  4. Settings (invariant 12): every key on a settings block is the space's, homed on that page; every setting appears
     on exactly one settings block, on its home page; a shortcut is to a key homed elsewhere.
  5. A setting's description: its default of its type and within its bounds; min, max and step only for numbers;
     choices only for a choice, and its default one of them; a secret's default empty (PV3); an availability in its
     form (live, later, inert, check:<id>, version:<x.y>, requires:<key>); a later setting locked; a tri-state's
     default on, off or its third value (auto, or inherit, D163).
  6. Components: a component's topic declared by the space and of the shape it draws (a feed or a log tail a stream,
     every other a state topic); a block's fields those of its kind; its props (D185 to D187): `field` a name on a
     state component, `empty` 1 to 80 characters on a component with a topic, `newest_first` a boolean on a log tail.
     The framework topic `space.state` (D188), if declared, a state topic that no component draws.
  7. Actions: each command one the space declares; a destructive action has a confirmation.
  8. Fixtures, where fixtures/<id>/topics.json exists: every declared topic but `settings` has one, a state topic's an
     object and a stream topic's a list of rows with rising sequence numbers.
Exit status 0 when every space passes (and, with --check, nothing has drifted).
"""
import argparse
import hashlib
import json
import pathlib
import re
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "protocol"))
import generate as protocol  # noqa: E402

SPACES = ROOT / "src" / "grace-pages" / "spaces"
FIXTURES = ROOT / "fixtures"
BUILTIN = ROOT / "src" / "grace-pages" / "builtin.cpp"

STREAM_COMPONENTS = {"live_feed", "log_tail"}
FIELD_NAME = re.compile(r"^[a-z0-9_]{1,32}$")
SPACE_STATE = "space.state"  # the framework topic a space's greyed pages come on (D188)
NO_TOPIC_COMPONENTS = {"statement", "filter_bar", "segmented_switch", "action_button"}
NUMERIC = {"integer", "number", "duration_s"}
TRI_THIRDS = ("auto", "inherit")
AVAILABILITY = re.compile(r"live|later|inert|check:[a-z0-9._-]{1,48}|version:\d+\.\d+|requires:[a-z0-9._/-]{1,64}")
PAGE_FIELDS = ("id", "category", "name", "description", "help", "related", "increment", "tag", "actions", "tabs",
               "sections", "master")
SETTING_FIELDS = ("key", "type", "default", "min", "max", "step", "unit", "choices", "choices_from", "third", "label", "help", "synonyms",
                  "home", "effect", "availability", "locked")


class PagesError(Exception):
    pass


# ---- From a space's TOML to the protocol's documents.

def documents(path, schema):
    source = tomllib.loads(path.read_text(encoding="utf-8"))
    space = source.get("space", {})
    if not isinstance(space.get("id"), str):
        raise PagesError(f"{path}: no [space] with an id")
    pages = []
    for page in source.get("page", []):
        unknown = set(page) - set(PAGE_FIELDS)
        if unknown:
            raise PagesError(f"{space['id']}: page {page.get('id')} has fields the format does not know: "
                             f"{', '.join(sorted(unknown))}")
        pages.append({k: page[k] for k in PAGE_FIELDS if k in page})
    space_pages = {
        "space": space["id"],
        "name": space.get("name"),
        "minor": schema["protocol"]["minor"],
        "categories": source.get("category", []),
        "pages": pages,
        "topics": source.get("topic", []),
        "commands": space.get("commands", []),
    }
    if "slow_commands" in space:
        space_pages["slow_commands"] = space["slow_commands"]  # D216
    settings = []
    for entry in source.get("setting", []):
        unknown = set(entry) - set(SETTING_FIELDS)
        if unknown:
            raise PagesError(f"{space['id']}: setting {entry.get('key')} has fields §6.9 does not name: "
                             f"{', '.join(sorted(unknown))}")
        one = {k: entry[k] for k in SETTING_FIELDS if k in entry}
        one.setdefault("synonyms", [])
        one.setdefault("locked", False)
        settings.append({k: one[k] for k in SETTING_FIELDS if k in one})
    canonical = json.dumps(settings, ensure_ascii=False, separators=(",", ":"), sort_keys=True)
    description = {"schema": {"number": space.get("settings_schema", 1),
                              "hash": hashlib.sha256(canonical.encode("utf-8")).hexdigest()[:16]},
                   "settings": settings}
    return space_pages, description


# ---- The lint.

def lint_setting(space, s, failures):
    key, kind, default = s["key"], s["type"], s["default"]
    where = f"{space}: setting {key}"
    if kind in NUMERIC:
        if isinstance(default, bool) or not isinstance(default, (int, float)):
            failures.append(f"{where}: its default is not a number")
        else:
            if kind == "integer" and not isinstance(default, int):
                failures.append(f"{where}: an integer's default is not whole")
            lo, hi = s.get("min"), s.get("max")
            if lo is None or hi is None or s.get("step") is None:
                failures.append(f"{where}: a number needs min, max and step")
            elif not lo <= default <= hi:
                failures.append(f"{where}: its default {default} is outside {lo} to {hi}")
    else:
        for bound in ("min", "max", "step"):
            if bound in s:
                failures.append(f"{where}: {bound} on a {kind}")
    if kind == "choice":
        values = [c["value"] for c in s.get("choices", [])]
        if len(values) < 2 or len(set(values)) != len(values):
            failures.append(f"{where}: a choice needs two or more distinct choices")
        if default not in values:
            failures.append(f"{where}: its default is not one of its choices")
    elif "choices" in s:
        failures.append(f"{where}: choices on a {kind}")
    if kind == "bool" and not isinstance(default, bool):
        failures.append(f"{where}: its default is not true or false")
    if kind == "tri_state":
        third = s.get("third", "auto")
        if default not in ("on", "off", third):
            failures.append(f"{where}: a tri-state's default is on, off or {third}")
    elif "third" in s:
        failures.append(f"{where}: third on a {kind}")
    if kind in ("text", "key_name", "secret") and not isinstance(default, str):
        failures.append(f"{where}: its default is not text")
    if kind == "key_name" and not re.fullmatch(r"(Ctrl\+|Shift\+|Alt\+)*[A-Za-z0-9]{1,12}", default):
        failures.append(f"{where}: its default is not a key's name")
    if kind == "secret" and default != "":
        failures.append(f"{where}: a secret's default is empty, never a value (PV3)")
    if not AVAILABILITY.fullmatch(s["availability"]):
        failures.append(f"{where}: availability {s['availability']} is not live, later, inert, check:<id>, "
                        "version:<x.y> or requires:<key>")
    if s["availability"] == "later" and not s.get("locked"):
        failures.append(f"{where}: a later setting is locked")


def lint(space_pages, description, fixtures, known_spaces):
    failures = []
    space = space_pages["space"]
    pages = {p["id"]: p for p in space_pages["pages"]}
    settings = {s["key"]: s for s in description["settings"]}
    topics = {t["id"]: t for t in space_pages["topics"]}
    commands = set(space_pages["commands"])

    def unique(kind, ids, where):
        seen = set()
        for i in ids:
            if i in seen:
                failures.append(f"{where}: {kind} {i} twice")
            seen.add(i)

    unique("page", [p["id"] for p in space_pages["pages"]], space)
    unique("category", [c["id"] for c in space_pages["categories"]], space)
    unique("topic", list(t["id"] for t in space_pages["topics"]), space)
    unique("setting", [s["key"] for s in description["settings"]], space)
    for name in space_pages.get("slow_commands", []):
        if name not in commands or name in ("settings.set", "settings.reset"):
            failures.append(f"{space}: slow command {name} is a command of the space's own, not a settings command "
                            "(D216)")
    unique("slow command", space_pages.get("slow_commands", []), space)
    for t in space_pages["topics"]:
        if (t["shape"] == "stream") != ("ring" in t):
            failures.append(f"{space}: topic {t['id']}: a stream names its ring, a state does not")
        if t["id"] == SPACE_STATE and t["shape"] != "state":
            failures.append(f"{space}: topic {SPACE_STATE} is a state topic (D188)")
        if t.get("family") and (t["shape"] != "state" or t["id"] in ("settings", SPACE_STATE)):
            failures.append(f"{space}: topic {t['id']}: a family is a state topic of the space's own (D215)")
    families = {tid for tid, t in topics.items() if t.get("family")}

    # 2. The rail.
    placed = {}
    for c in space_pages["categories"]:
        for p in c["pages"]:
            if p not in pages:
                failures.append(f"{space}: category {c['id']} lists {p}, which is not a page")
            elif p in placed:
                failures.append(f"{space}: page {p} in two categories")
            placed[p] = c["id"]
    for pid, p in pages.items():
        if placed.get(pid) != p["category"]:
            failures.append(f"{space}: page {pid} names category {p['category']}, but is not listed in it")

    # 3 and 4: each page, its blocks and its settings.
    homed = {}
    for pid, p in pages.items():
        where = f"{space}: page {pid}"
        if ("tabs" in p) == ("sections" in p):
            failures.append(f"{where}: tabs or sections, not both or neither")
        if ("increment" in p) == ("tag" in p):
            failures.append(f"{where}: an increment or a tag, one of them")
        if not 3 <= len(p["help"]["steps"]) <= 5:
            failures.append(f"{where}: its help has {len(p['help']['steps'])} steps, not three to five")
        for link in p["related"]:
            other, _, target = link.rpartition("/")
            if link == pid:
                failures.append(f"{where}: links to itself")
            elif other:
                if target not in known_spaces.get(other, set()):
                    failures.append(f"{where}: Related link {link} names no page of a built-in space")
            elif link not in pages:
                failures.append(f"{where}: Related link {link} is not a page")
        sections = list(p.get("sections", []))
        for tab in p.get("tabs", []):
            sections += tab["sections"]
        master = p.get("master")  # a master list, its detail the page's own tabs and sections (D215)
        if master is not None:
            mt = master.get("topic")
            if mt not in topics or topics[mt]["shape"] != "state" or mt in families or mt in ("settings", SPACE_STATE):
                failures.append(f"{where}: its master list's topic is a state topic of the space's own, not a family")
            if master.get("detail") not in families:
                failures.append(f"{where}: its master list's detail is a family the space declares (D215)")
            if not FIELD_NAME.match(master.get("key", "")) or not master.get("label"):
                failures.append(f"{where}: its master list has a key column (a name) and a label")
            for words in ("empty", "none"):
                if words in master and not 1 <= len(master[words]) <= 160:
                    failures.append(f"{where}: its master list's {words} is 1 to 160 characters")
        # (A block may draw any family on a page with a master list: the chosen row's key names its member, D215.)
        unique("tab", [t["id"] for t in p.get("tabs", [])], where)
        unique("section", [s["id"] for s in sections], where)
        unique("action", [a["id"] for a in p.get("actions", [])], where)
        for section in sections:  # a section's chip, live status and own actions (D202)
            swhere = f"{where}, section {section['id']}"
            if "chip" in section and not 1 <= len(section["chip"]) <= 24:
                failures.append(f"{swhere}: its chip is 1 to 24 characters")
            status = section.get("status")
            if status is not None:
                topic = status.get("topic")
                if (topic not in topics or topics[topic]["shape"] != "state" or topic in ("settings", SPACE_STATE) or
                        topic in families):
                    failures.append(f"{swhere}: its status names a state topic of the space's own")
                if "field" in status and not FIELD_NAME.match(status["field"]):
                    failures.append(f"{swhere}: its status field is a name (a-z, 0-9, _; 1 to 32)")
            unique("section action", [a["id"] for a in section.get("actions", [])], swhere)
            for action in section.get("actions", []):
                if action["command"] not in commands or not action["label"]:
                    failures.append(f"{swhere}: action {action['id']} has a label and sends a command of the space")
                if action["tone"] == "destructive" and not action.get("confirm"):
                    failures.append(f"{swhere}: destructive action {action['id']} has no confirmation")
        for action in p.get("actions", []):
            if action["command"] not in commands:
                failures.append(f"{where}: action {action['id']} sends {action['command']}, not a command of the space")
            if action["tone"] == "destructive" and not action.get("confirm"):
                failures.append(f"{where}: destructive action {action['id']} has no confirmation")
        for section in sections:
            for block in section["blocks"] + section.get("advanced", []):
                kind = block["kind"]
                bwhere = f"{where}, section {section['id']}"
                if kind != "form" and any(k in block for k in ("command", "fields", "submit")):
                    failures.append(f"{bwhere}: command, fields and submit belong to a form block (D195)")
                if kind != "editor" and "editor" in block:
                    failures.append(f"{bwhere}: editor belongs to an editor block (D217)")
                if kind == "editor":
                    ed = block.get("editor") or {}
                    topic = block.get("topic")
                    if (not ed or any(k in block for k in ("keys", "component", "props", "row_actions", "row_secret"))):
                        failures.append(f"{bwhere}: an editor block has its topic and its editor, and nothing else (D217)")
                    if (topic not in topics or topics[topic]["shape"] != "state" or
                            topic in ("settings", SPACE_STATE)):
                        failures.append(f"{bwhere}: an editor's topic is a state topic or a family of the space's own")
                    elif topic in families and master is None:
                        failures.append(f"{bwhere}: an editor draws family {topic} on a page with no master list (D215)")
                    for part in ("save", "check", "reset", "restore", "draft"):
                        if part in ed and ed[part] not in commands:
                            failures.append(f"{bwhere}: the editor's {part} sends {ed[part]}, not a command of the space")
                    if "save" not in ed:
                        failures.append(f"{bwhere}: an editor has a save command")
                    if "draft" in ed and ed["draft"] not in space_pages.get("slow_commands", []):
                        failures.append(f"{bwhere}: the editor's draft is a slow command (D216)")
                    if "notes_max" in ed and not 1 <= ed["notes_max"] <= 400:
                        failures.append(f"{bwhere}: the editor's notes_max is 1 to 400")
                    continue
                if (("row_actions" in block or "row_secret" in block) and
                        (kind != "component" or block.get("component") != "table")):
                    failures.append(f"{bwhere}: row actions and a row secret go on a table (D192, D194)")
                if kind == "form":
                    fields = block.get("fields") or []
                    if (block.get("command") not in commands or not 1 <= len(fields) <= 4 or
                            not block.get("submit") or any(k in block for k in ("keys", "component", "topic"))):
                        failures.append(f"{bwhere}: a form block has a declared command, one to four fields and a "
                                        "submit label, and nothing else (D195)")
                    unique("form field", [f["id"] for f in fields], bwhere)
                    for f in fields:
                        if not f.get("label") or not 1 <= f.get("max", 0) <= 200:
                            failures.append(f"{bwhere}: form field {f['id']} has a label and a max of 1 to 200")
                    continue
                if kind in ("settings", "shortcuts"):
                    if not block.get("keys") or "component" in block or "topic" in block:
                        failures.append(f"{bwhere}: a {kind} block has keys alone")
                    for key in block.get("keys", []):
                        if key not in settings:
                            failures.append(f"{bwhere}: {key} is not a setting of the space")
                        elif kind == "settings":
                            if settings[key]["home"] != pid:
                                failures.append(f"{bwhere}: {key} is homed on {settings[key]['home']}, not here")
                            homed.setdefault(key, []).append(pid)
                        elif settings[key]["home"] == pid:
                            failures.append(f"{bwhere}: a shortcut to {key}, which is homed here")
                else:
                    component = block.get("component")
                    if component is None or "keys" in block:
                        failures.append(f"{bwhere}: a component block names its component, and no keys")
                        continue
                    topic = block.get("topic")
                    if component in NO_TOPIC_COMPONENTS:
                        if topic is not None:
                            failures.append(f"{bwhere}: {component} takes no topic")
                    elif topic not in topics:
                        failures.append(f"{bwhere}: {component}'s topic {topic} is not one the space declares")
                    elif (topics[topic]["shape"] == "stream") != (component in STREAM_COMPONENTS):
                        failures.append(f"{bwhere}: {component} cannot draw {topics[topic]['shape']} topic {topic}")
                    elif topic == SPACE_STATE:
                        failures.append(f"{bwhere}: {SPACE_STATE} is the framework's topic, drawn by no component")
                    elif topic in families and master is None:
                        failures.append(f"{bwhere}: {component} draws family {topic} on a page with no master list, "
                                        "whose chosen row names its member (D215)")
                    props = block.get("props") or {}
                    if "field" in props and (component in STREAM_COMPONENTS or topic is None or
                                             not isinstance(props["field"], str) or
                                             not FIELD_NAME.match(props["field"])):
                        failures.append(f"{bwhere}: props.field is a name (a-z, 0-9, _; 1 to 32) on a state component")
                    if "empty" in props and (topic is None or not isinstance(props["empty"], str) or
                                             not 1 <= len(props["empty"]) <= 80):
                        failures.append(f"{bwhere}: props.empty is 1 to 80 characters on a component with a topic")
                    if "newest_first" in props and (component != "log_tail" or
                                                    not isinstance(props["newest_first"], bool)):
                        failures.append(f"{bwhere}: props.newest_first is a boolean on a log tail")
                    unique("row action", [a["id"] for a in block.get("row_actions", [])], bwhere)
                    for action in block.get("row_actions", []):
                        if action["command"] not in commands or not action["label"]:
                            failures.append(f"{bwhere}: row action {action['id']} has a label and sends a command "
                                            "of the space")
                        if action["tone"] == "destructive" and not action.get("confirm"):
                            failures.append(f"{bwhere}: destructive row action {action['id']} has no confirmation")
                    if "row_secret" in block and block["row_secret"]["command"] not in commands:
                        failures.append(f"{bwhere}: the row secret sends {block['row_secret']['command']}, "
                                        "not a command of the space")
    for key, s in settings.items():
        lint_setting(space, s, failures)
        source = s.get("choices_from")
        if source is not None and (s["type"] != "text" or "choices" in s or source not in topics or
                                   topics[source]["shape"] != "state" or source in ("settings", SPACE_STATE) or
                                   source in families):
            failures.append(f"{space}: setting {key}: choices_from is on a text setting, naming a state topic of "
                            "the space's own (D193)")
        if source is not None and len(str(s["default"]).encode("utf-8")) > 200:
            failures.append(f"{space}: setting {key}: a picked choice is at most 200 bytes")
        if s["home"] not in pages:
            failures.append(f"{space}: setting {key} is homed on {s['home']}, which is not a page")
        found = homed.get(key, [])
        if len(found) != 1:
            failures.append(f"{space}: setting {key} appears on {len(found)} settings blocks, not exactly one "
                            "(invariant 12)")

    # 8. Fixtures.
    if fixtures is not None:
        for tid, t in topics.items():
            if tid == "settings":
                continue
            if tid in families:
                members = [k for k in fixtures if k.startswith(tid + "/")]
                if not members:
                    failures.append(f"{space}: family {tid} has no member's fixture")
                for k in members:
                    if "/" in k[len(tid) + 1:] or not isinstance(fixtures[k], dict):
                        failures.append(f"{space}: fixture {k} is not an object for one member of {tid}")
                continue
            body = fixtures.get(tid)
            if body is None:
                failures.append(f"{space}: topic {tid} has no fixture")
            elif t["shape"] == "state" and not isinstance(body, dict):
                failures.append(f"{space}: fixture {tid} is not an object (a state)")
            elif t["shape"] == "stream":
                seqs = [row.get("seq") for row in body] if isinstance(body, list) else None
                if not seqs or any(not isinstance(r, dict) for r in body) or \
                        any(not isinstance(q, int) for q in seqs) or seqs != sorted(set(seqs)):
                    failures.append(f"{space}: fixture {tid} is not a list of rows with rising seq numbers")
        for tid in fixtures:
            if tid not in topics and tid.rpartition("/")[0] not in families:
                failures.append(f"{space}: fixture {tid} is for no declared topic or family")
    return failures


def check(schema, space_pages, description, fixtures, known_spaces):
    failures = [f"{space_pages['space']}: pages: {e}" for e in protocol.validate(schema, space_pages, "type.space_pages")]
    failures += [f"{space_pages['space']}: settings: {e}"
                 for e in protocol.validate(schema, description, "type.settings_description")]
    if failures:
        return failures  # the lint reads the documents' shape: fix it first
    return lint(space_pages, description, fixtures, known_spaces)


# ---- Outputs.

def pretty(value):
    return json.dumps(value, ensure_ascii=False, indent=2) + "\n"


def compact(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def build_builtin(spaces):
    out = ["// SPDX-License-Identifier: MIT",
           "// Generated by tools/pages/build.py from src/grace-pages/spaces/*/space.toml: do not edit (GraceUI D160). The",
           "// built-in spaces' page data and settings descriptions, as the protocol's JSON, checked by the page lint.",
           '#include "grace/pages/builtin.h"', "", "#include <array>", "", "namespace grace::pages {", "namespace {", ""]
    for space_pages, description in spaces:
        name = space_pages["space"].replace("-", "_")
        for what, value in (("pages", space_pages), ("settings", description)):
            text = compact(value)
            if ")grace\"" in text:
                raise PagesError("a space's text holds the raw string's delimiter")
            out.append(f'constexpr std::string_view k{name.capitalize()}{what.capitalize()} = R"grace({text})grace";')
    out += ["", f"constexpr std::array<BuiltinSpace, {len(spaces)}> kSpaces = {{{{"]
    for space_pages, _ in spaces:
        name = space_pages["space"].replace("-", "_")
        out.append(f'    {{"{space_pages["space"]}", k{name.capitalize()}Pages, k{name.capitalize()}Settings}},')
    out += ["}};", "", "}  // namespace", "",
            "std::span<const BuiltinSpace> builtin_spaces() noexcept { return kSpaces; }", "",
            "}  // namespace grace::pages", ""]
    return "\n".join(out)


def load_all(schema):
    spaces = []
    for path in sorted(SPACES.glob("*/space.toml")):
        spaces.append(documents(path, schema))
    order = {"graceui": 0}
    spaces.sort(key=lambda d: (order.get(d[0]["space"], 1), d[0]["space"]))
    return spaces


def fixtures_of(space):
    path = FIXTURES / space / "topics.json"
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else None


def run(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if an output is not what the spaces build")
    parser.add_argument("--lint", nargs="+", metavar="FILE", help="check other spaces' space.toml files only")
    args = parser.parse_args(argv)
    try:
        schema = protocol.load()
        builtin = load_all(schema)
        known = {sp["space"]: {p["id"] for p in sp["pages"]} for sp, _ in builtin}
        targets = [documents(pathlib.Path(f), schema) for f in args.lint] if args.lint else builtin
        for sp, _ in targets:
            known.setdefault(sp["space"], {p["id"] for p in sp["pages"]})
        failures = []
        for space_pages, description in targets:
            failures += check(schema, space_pages, description,
                              None if args.lint else fixtures_of(space_pages["space"]), known)
    except (PagesError, protocol.SchemaError, tomllib.TOMLDecodeError, KeyError, TypeError) as error:
        print(f"pages: {error}", file=sys.stderr)
        return 1
    if failures:
        for failure in failures:
            print(f"pages: {failure}", file=sys.stderr)
        return 1
    if args.lint:
        print(f"pages: {len(targets)} space(s) pass the page lint")
        return 0
    outputs = {BUILTIN: build_builtin(builtin)}
    for space_pages, description in builtin:
        outputs[FIXTURES / space_pages["space"] / "pages.json"] = pretty(space_pages)
        outputs[FIXTURES / space_pages["space"] / "settings.json"] = pretty(description)
    if args.check:
        stale = [p for p, text in outputs.items() if not p.is_file() or p.read_text(encoding="utf-8") != text]
        for path in stale:
            print(f"pages: {path.relative_to(ROOT).as_posix()} is not what the spaces build: run "
                  "python3 -I tools/pages/build.py", file=sys.stderr)
        if not stale:
            print(f"pages: ok ({len(builtin)} spaces)")
        return 1 if stale else 0
    for path, text in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    print("pages: wrote " + ", ".join(p.relative_to(ROOT).as_posix() for p in outputs))
    return 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
