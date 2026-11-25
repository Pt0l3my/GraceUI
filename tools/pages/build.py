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
  2. The rail: every category's pages exist, and every page is in exactly the category it names; categories at the
     rail's foot come after every other (D248).
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
               "sections", "master", "live", "scoped", "browser_only", "shown_with", "gate", "ribbon", "pc_only")
SETTING_FIELDS = ("key", "type", "default", "min", "max", "step", "unit", "choices", "choices_from", "third", "label", "help", "synonyms",
                  "home", "effect", "availability", "locked", "sensitive")


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
    if "sensitive_commands" in space:
        space_pages["sensitive_commands"] = space["sensitive_commands"]  # D221
    if "file_commands" in space:
        space_pages["file_commands"] = space["file_commands"]  # D226
    if "quick" in space:
        space_pages["quick"] = space["quick"]  # D252
    if "scope" in space:
        space_pages["scope"] = space["scope"]  # D254
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

    def filter_bar(bar, where, paged=False):
        # A table's or a master list's filter bar (G43, D250): at least one part; each filter on a column by its name;
        # each sort option on a column or a member of each row by its name; the search on one to four columns. Its
        # columns are the topic's, published later, so a view leaves out any it does not find.
        filters, sorts, search = bar.get("filters") or [], bar.get("sort") or [], bar.get("search")
        if not filters and not sorts and search is None:
            failures.append(f"{where}: its filter bar has a filter, a sort option or a search (D250)")
        for f in filters:
            if not FIELD_NAME.match(f.get("column", "")) or not f.get("label"):
                failures.append(f"{where}: a filter names its column (a name) and has a label (D250)")
            # Rows read a page at a time (D254): the view holds one page, so a filter names its choices, itself or by a
            # state topic's list of them; anywhere else they come from the rows.
            named = [k for k in ("choices", "choices_from") if k in f]
            if paged and len(named) != 1:
                failures.append(f"{where}: a filter on rows read a page at a time names its choices, or a state "
                                "topic's list of them, one of the two (D254)")
            if not paged and "choices_from" in f:
                failures.append(f"{where}: a filter takes its choices from a state topic only on rows read a page at "
                                "a time; others' come from the rows, or its own choices word them (D254, D262)")
            source = f.get("choices_from")
            if source is not None and (source.get("topic") not in topics or
                                       topics[source["topic"]]["shape"] != "state" or
                                       source["topic"] in ("settings", SPACE_STATE) or
                                       topics[source["topic"]].get("family") or
                                       ("field" in source and not FIELD_NAME.match(source["field"]))):
                failures.append(f"{where}: a filter's choices_from is a state topic of the space's own, not a "
                                "family, and a member's name (D254)")
        unique("filter on column", [f.get("column") for f in filters], where)
        for s in sorts:
            if not FIELD_NAME.match(s.get("key", "")) or not s.get("label"):
                failures.append(f"{where}: a sort option names its key (a name) and has a label (D250)")
        unique("sort on key", [s.get("key") for s in sorts], where)
        if search is not None and (not 1 <= len(search.get("columns", [])) <= 4 or not search.get("label") or
                                   not all(FIELD_NAME.match(c) for c in search.get("columns", []))):
            failures.append(f"{where}: its search names one to four columns (names) and has a label (D250)")

    unique("page", [p["id"] for p in space_pages["pages"]], space)
    unique("category", [c["id"] for c in space_pages["categories"]], space)
    unique("topic", list(t["id"] for t in space_pages["topics"]), space)
    unique("setting", [s["key"] for s in description["settings"]], space)
    for name in space_pages.get("slow_commands", []):
        if name not in commands or name in ("settings.set", "settings.reset"):
            failures.append(f"{space}: slow command {name} is a command of the space's own, not a settings command "
                            "(D216)")
    unique("slow command", space_pages.get("slow_commands", []), space)
    for name in space_pages.get("sensitive_commands", []):
        if name not in commands or name in ("settings.set", "settings.reset"):
            failures.append(f"{space}: sensitive command {name} is a command of the space's own, not a settings "
                            "command: a setting is marked sensitive itself (D221)")
    unique("sensitive command", space_pages.get("sensitive_commands", []), space)
    for name in space_pages.get("file_commands", []):
        if name not in commands or name in ("settings.set", "settings.reset"):
            failures.append(f"{space}: file command {name} is a command of the space's own, not a settings command (D226)")
        if name not in space_pages.get("sensitive_commands", []):
            failures.append(f"{space}: file command {name} is marked sensitive too: never from a paired device (D226)")
        if name in space_pages.get("slow_commands", []):
            failures.append(f"{space}: file command {name} is not a slow command: its bytes are not held 90 s (D226)")
    unique("file command", space_pages.get("file_commands", []), space)
    files = set(space_pages.get("file_commands", []))

    def file_uses(node, where):
        # A file command is sent only by an upload block (D229): never by an action, a row's, a form or an editor.
        if isinstance(node, dict):
            named = [node.get("command")] if node.get("kind") != "upload" else []
            named += [node.get(part) for part in ("save", "check", "reset", "restore", "draft")]
            for name in named:
                if isinstance(name, str) and name in files:
                    failures.append(f"{where}: {name} is a file command, sent only by an upload block (D229)")
            where = f"{space}: {node['id']}" if isinstance(node.get("id"), str) and "category" in node else where
            for value in node.values():
                file_uses(value, where)
        elif isinstance(node, list):
            for value in node:
                file_uses(value, where)

    file_uses(space_pages.get("pages", []), space)
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
    foot = None
    for c in space_pages["categories"]:
        if c.get("foot"):
            foot = c["id"]
        elif foot is not None:
            failures.append(f"{space}: category {c['id']} follows {foot}, at the rail's foot; the foot's categories "
                            f"come last (D248)")

    # The top bar's Quick switches (G45, D252): each a shortcut to a switch or a choice of the space's, by its key
    # alone; or a session switch of the space's own, its command sent {on}, its state a boolean member of a state topic
    # of the space's, and the page it lives in.
    quick = space_pages.get("quick", [])
    for q in quick:
        qwhere = f"{space}: quick switch {q.get('key') or q.get('id')}"
        if "key" in q:
            if set(q) != {"key"}:
                failures.append(f"{qwhere}: a setting's shortcut names its key alone; its words and home are the "
                                "setting's (D252)")
            setting = settings.get(q["key"])
            if setting is None or setting["type"] not in ("bool", "choice"):
                failures.append(f"{qwhere}: names a switch or a choice of the space's settings (D252)")
            continue
        if set(q) != {"id", "label", "command", "state", "home"}:
            failures.append(f"{qwhere}: a session switch has an id, a label, a command, a state and a home, and no "
                            "key (D252)")
            continue
        name = q["command"]
        if (name not in commands or name in ("settings.set", "settings.reset") or name in files or
                name in space_pages.get("slow_commands", [])):
            failures.append(f"{qwhere}: sends a command of the space's own, neither a settings, a file nor a slow "
                            "command (D252)")
        topic = q["state"].get("topic")
        if (topic not in topics or topics[topic]["shape"] != "state" or topic in ("settings", SPACE_STATE) or
                topic in families):
            failures.append(f"{qwhere}: its state is on a state topic of the space's own, not a family (D252)")
        if not FIELD_NAME.match(q["state"].get("field", "")):
            failures.append(f"{qwhere}: its state field is a name (a-z, 0-9, _; 1 to 32)")
        if q["home"] not in pages:
            failures.append(f"{qwhere}: its home {q['home']} is not a page of the space's (D252)")
        elif pages[q["home"]].get("browser_only"):
            failures.append(f"{qwhere}: its home is a page in game too, not a browser-only one (D258)")
        if not q["label"]:
            failures.append(f"{qwhere}: has a label")
    unique("quick switch", [q.get("key") or q.get("id") for q in quick], space)

    # A space's scope (G45, D254): its chooser's label and the state topic (or member) holding its choices; a scoped
    # page follows it.
    scope = space_pages.get("scope")
    if scope is not None:
        st = scope.get("topic")
        if st not in topics or topics[st]["shape"] != "state" or st in families or st in ("settings", SPACE_STATE):
            failures.append(f"{space}: its scope is on a state topic of the space's own, not a family (D254)")
        if "field" in scope and not FIELD_NAME.match(scope["field"]):
            failures.append(f"{space}: its scope's field is a name (a-z, 0-9, _; 1 to 32)")
        if not 1 <= len(scope.get("label", "")) <= 32:
            failures.append(f"{space}: its scope's label is 1 to 32 characters")

    def page_source(src, where):
        # Rows read a page at a time (D254): a command of the space's own that answers within 10 s, and a state topic
        # of the space's own whose change means new rows.
        name = src.get("command")
        if (name not in commands or name in ("settings.set", "settings.reset") or name in files or
                name in space_pages.get("slow_commands", [])):
            failures.append(f"{where}: its rows come from a command of the space's own, neither a settings, a file "
                            "nor a slow command (D254)")
        changes = src.get("changes")
        if changes is not None and (changes not in topics or topics[changes]["shape"] != "state" or
                                    changes in families or changes in ("settings", SPACE_STATE)):
            failures.append(f"{where}: its changes topic is a state topic of the space's own, not a family (D254)")

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
        if p.get("live"):
            # A live page (D252): something on it moves by itself, a topic a block draws or its master list's.
            drawn = [b for sec in sections for b in sec["blocks"] + sec.get("advanced", [])
                     if b.get("topic") not in (None, "settings") or "page" in b]
            if not drawn and "master" not in p:
                failures.append(f"{where}: a live page draws a topic of the space's, which Live updates holds (D252)")
        if p.get("scoped") and scope is None:
            failures.append(f"{where}: a scoped page follows the space's scope, which the space does not declare (D254)")
        master = p.get("master")  # a master list, its detail the page's own tabs and sections (D215)
        if master is not None:
            mt = master.get("topic")
            if ("topic" in master) == ("page" in master):
                failures.append(f"{where}: its master list reads a topic or a page source, one of the two (D254)")
            elif "page" in master:
                page_source(master["page"], f"{where}: its master list")
            elif (mt not in topics or topics[mt]["shape"] != "state" or mt in ("settings", SPACE_STATE) or
                  (mt in families and not p.get("scoped"))):
                failures.append(f"{where}: its master list's topic is a state topic of the space's own; a family only "
                                "on a scoped page, whose scope names its member (D215, D254)")
            if master.get("detail") not in families:
                failures.append(f"{where}: its master list's detail is a family the space declares (D215)")
            if not FIELD_NAME.match(master.get("key", "")) or not master.get("label"):
                failures.append(f"{where}: its master list has a key column (a name) and a label")
            if "filter_bar" in master:
                filter_bar(master["filter_bar"], f"{where}: its master list", paged="page" in master)
            for words in ("empty", "none"):
                if words in master and not 1 <= len(master[words]) <= 160:
                    failures.append(f"{where}: its master list's {words} is 1 to 160 characters")
        # (A block may draw any family on a page with a master list: the chosen row's key names its member, D215.)
        unique("tab", [t["id"] for t in p.get("tabs", [])], where)
        unique("section", [s["id"] for s in sections], where)
        unique("action", [a["id"] for a in p.get("actions", [])], where)
        # A page in the browser alone, shown with a setting, gated, with a ribbon (D258; D5, D10's Workshop).
        if "shown_with" in p:
            setting = settings.get(p["shown_with"])
            if setting is None or setting["type"] != "bool":
                failures.append(f"{where}: it is shown with a switch of the space's settings (D258)")
        if "gate" in p:
            if not p.get("browser_only"):
                failures.append(f"{where}: a gate is a browser-only page's (D258)")
            if not 1 <= len(p["gate"].get("words", "")) <= 256 or not 1 <= len(p["gate"].get("confirm", "")) <= 32:
                failures.append(f"{where}: its gate has its words (1 to 256 characters) and a confirm label (1 to 32)")
        if "ribbon" in p and not 1 <= len(p["ribbon"]) <= 32:
            failures.append(f"{where}: its ribbon is 1 to 32 characters")
        if p.get("browser_only"):
            homed_here = [k for k, st in settings.items() if st.get("home") == p["id"]]
            if homed_here:
                failures.append(f"{where}: a browser-only page is no setting's home, for every setting is reachable "
                                f"in game ({homed_here[0]}) (D258)")
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
                if "state" in action:
                    failures.append(f"{swhere}: action {action['id']} has a state; a switch is a header action's "
                                    "alone (D256)")
                if action["command"] not in commands or not action["label"]:
                    failures.append(f"{swhere}: action {action['id']} has a label and sends a command of the space")
                if action["tone"] == "destructive" and not action.get("confirm"):
                    failures.append(f"{swhere}: destructive action {action['id']} has no confirmation")
        for action in p.get("actions", []):
            if action["command"] not in commands:
                failures.append(f"{where}: action {action['id']} sends {action['command']}, not a command of the space")
            if action["tone"] == "destructive" and not action.get("confirm"):
                failures.append(f"{where}: destructive action {action['id']} has no confirmation")
            state = action.get("state")
            if state is not None:
                # A header switch (D256): a session switch's form, as a quick switch's (D252), drawn in the header.
                awhere = f"{where}: action {action['id']}"
                name = action["command"]
                if (name in ("settings.set", "settings.reset") or name in files or
                        name in space_pages.get("slow_commands", [])):
                    failures.append(f"{awhere}: a switch sends a command of the space's own, neither a settings, a "
                                    "file nor a slow command (D256)")
                if action["tone"] == "destructive" or "confirm" in action:
                    failures.append(f"{awhere}: a switch is neither destructive nor confirmed: it sends {{on}} at "
                                    "once (D256)")
                topic = state.get("topic")
                if (topic not in topics or topics[topic]["shape"] != "state" or topic in ("settings", SPACE_STATE) or
                        topic in families):
                    failures.append(f"{awhere}: its state is on a state topic of the space's own, not a family (D256)")
                if not FIELD_NAME.match(state.get("field", "")):
                    failures.append(f"{awhere}: its state field is a name (a-z, 0-9, _; 1 to 32)")
        for section in sections:
            for block in section["blocks"] + section.get("advanced", []):
                kind = block["kind"]
                bwhere = f"{where}, section {section['id']}"
                preview = kind == "component" and block.get("component") == "report_preview"
                if kind not in ("form", "upload") and any(k in block for k in ("fields", "submit")):
                    failures.append(f"{bwhere}: command, fields and submit belong to a form block (D195)")
                if kind not in ("form", "upload") and "command" in block and not preview:
                    failures.append(f"{bwhere}: command, fields and submit belong to a form block (D195), and a "
                                    "command to a report preview (D251)")
                if preview and "command" in block and (block["command"] not in commands or
                                                       block["command"] in space_pages.get("file_commands", [])):
                    failures.append(f"{bwhere}: a report preview's switches send a command of the space's, not a file "
                                    "command (D251)")
                if "share" in block:
                    # A report preview's Copy and Save (D263): one or both; Save the space's own command, sensitive
                    # (never from a paired device, D221) and not a file command.
                    share = block["share"]
                    save = share.get("save")
                    if not preview:
                        failures.append(f"{bwhere}: share belongs to a report preview (D263)")
                    elif share.get("copy") is not True and save is None:
                        failures.append(f"{bwhere}: a report preview's share offers Copy, Save or both (D263)")
                    elif block.get("topic") in families:
                        failures.append(f"{bwhere}: a report preview's share is on a topic of its own, not a family "
                                        "(D263: its Save names no member)")
                    if save is not None and (save not in commands or save.startswith("settings.") or
                                             save in space_pages.get("file_commands", []) or
                                             save not in space_pages.get("sensitive_commands", [])):
                        failures.append(f"{bwhere}: a report preview's Save sends a command of the space's, sensitive "
                                        "and not a file command (D221, D263)")
                if kind == "document":
                    # A document (G44, D251): a family's member on a master page, drawn whole, and nothing else.
                    topic = block.get("topic")
                    if master is None or topic not in families:
                        failures.append(f"{bwhere}: a document block draws a family on a page with a master list (D251)")
                    if any(k in block for k in ("keys", "component", "props", "row_actions", "row_secret", "editor",
                                                "command", "fields", "submit", "accept", "filter_bar")):
                        failures.append(f"{bwhere}: a document block has its topic, and nothing else (D251)")
                    continue
                if kind != "upload" and "accept" in block:
                    failures.append(f"{bwhere}: accept belongs to an upload block (D226)")
                if kind == "upload":
                    accept = block.get("accept", "")
                    if (block.get("command") not in space_pages.get("file_commands", []) or not block.get("submit") or
                            any(k in block for k in ("keys", "component", "topic", "props", "fields", "row_actions",
                                                     "row_secret", "editor"))):
                        failures.append(f"{bwhere}: an upload block has a file command of the space's, a submit label "
                                        "and an optional accept, and nothing else (D226)")
                    if accept and not all(re.fullmatch(r"\.[a-z0-9]{1,8}", part.strip()) for part in accept.split(",")):
                        failures.append(f"{bwhere}: an upload block's accept lists extensions such as \".zip, .toml\"")
                    continue
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
                    elif topic in families and master is None and not p.get("scoped"):
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
                stream = kind == "component" and block.get("component") in ("log_tail", "live_feed")
                if "filter_bar" in block and not stream and (kind != "component" or block.get("component") != "table"):
                    failures.append(f"{bwhere}: a filter bar goes on a table, a stream or a page's master list (D250, "
                                    "D257)")
                elif "filter_bar" in block:
                    if stream and block["filter_bar"].get("sort"):
                        failures.append(f"{bwhere}: a stream's filter bar has filters and a search, and no sort: its "
                                        "rows come in time's order (D257)")
                    filter_bar(block["filter_bar"], bwhere, paged="page" in block)
                if "page" in block and (kind != "component" or block.get("component") != "table"):
                    failures.append(f"{bwhere}: a page source belongs to a table block (D254)")
                if kind == "component" and block.get("component") == "filter_bar":
                    failures.append(f"{bwhere}: a filter bar is a table's own field, filter_bar, not a block of its own "
                                    "(D250)")
                if kind == "form":
                    fields = block.get("fields") or []
                    if (block.get("command") not in commands or not 1 <= len(fields) <= 4 or
                            not block.get("submit") or any(k in block for k in ("keys", "component", "topic"))):
                        failures.append(f"{bwhere}: a form block has a declared command, one to four fields and a "
                                        "submit label, and nothing else (D195)")
                    unique("form field", [f["id"] for f in fields], bwhere)
                    for f in fields:
                        most = 2000 if f.get("prose") else 200  # a prose field takes several lines (D251)
                        if not f.get("label") or not 1 <= f.get("max", 0) <= most:
                            failures.append(f"{bwhere}: form field {f['id']} has a label and a max of 1 to {most}")
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
                    if "page" in block:
                        # A table read a page at a time (D254): its source instead of a topic.
                        if topic is not None:
                            failures.append(f"{bwhere}: a table reads a topic or a page source, one of the two (D254)")
                        elif component == "table":
                            page_source(block["page"], bwhere)
                    elif component in NO_TOPIC_COMPONENTS:
                        if topic is not None:
                            failures.append(f"{bwhere}: {component} takes no topic")
                    elif topic not in topics:
                        failures.append(f"{bwhere}: {component}'s topic {topic} is not one the space declares")
                    elif (topics[topic]["shape"] == "stream") != (component in STREAM_COMPONENTS):
                        failures.append(f"{bwhere}: {component} cannot draw {topics[topic]['shape']} topic {topic}")
                    elif topic == SPACE_STATE:
                        failures.append(f"{bwhere}: {SPACE_STATE} is the framework's topic, drawn by no component")
                    elif topic in families and master is None and not p.get("scoped"):
                        failures.append(f"{bwhere}: {component} draws family {topic} on a page with no master list, "
                                        "whose chosen row names its member (D215)")
                    props = block.get("props") or {}
                    if "field" in props and (component in STREAM_COMPONENTS or topic is None or
                                             not isinstance(props["field"], str) or
                                             not FIELD_NAME.match(props["field"])):
                        failures.append(f"{bwhere}: props.field is a name (a-z, 0-9, _; 1 to 32) on a state component")
                    if "empty" in props and ((topic is None and "page" not in block) or not isinstance(props["empty"], str) or
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
    lint_pc_only(space, space_pages, description, failures)
    return failures


def topics_named(node):
    """Every topic a part of page data names: a "topic" or "changes" id, a "choices_from" id or its topic."""
    out = set()
    if isinstance(node, dict):
        for key, value in node.items():
            if key in ("topic", "changes") and isinstance(value, str):
                out.add(value)
            elif key == "choices_from" and isinstance(value, str):
                out.add(value)
            out |= topics_named(value)
    elif isinstance(node, list):
        for item in node:
            out |= topics_named(item)
    return out


def keys_named(node):
    """Every setting a part of page data shows: the keys of its settings and shortcut blocks."""
    out = set()
    if isinstance(node, dict):
        for key, value in node.items():
            if key == "keys" and isinstance(value, list):
                out |= {k for k in value if isinstance(k, str)}
            out |= keys_named(value)
    elif isinstance(node, list):
        for item in node:
            out |= keys_named(item)
    return out


def lint_pc_only(space, space_pages, description, failures):
    """This PC alone (D264): a pc_only topic is drawn on pc_only pages alone, never on a page, the scope or a quick
    switch a paired device sees; a setting homed on a pc_only page is sensitive, so that no device changes it."""
    pc_topics = {t["id"] for t in space_pages["topics"] if t.get("pc_only")}
    pc_pages = {p["id"] for p in space_pages["pages"] if p.get("pc_only")}
    for p in space_pages["pages"]:
        if p["id"] in pc_pages:
            continue
        for tid in sorted(topics_named(p) & pc_topics):
            failures.append(f"{space}: page {p['id']} draws {tid}, a topic on this PC alone: on pc_only pages alone "
                            "(D264)")
    for tid in sorted(topics_named({"scope": space_pages.get("scope"), "quick": space_pages.get("quick")}) & pc_topics):
        failures.append(f"{space}: the top bar names {tid}, a topic on this PC alone, which a paired device sees (D264)")
    pc_keys = {s["key"] for s in description["settings"] if s.get("home") in pc_pages}
    for p in space_pages["pages"]:
        if p["id"] not in pc_pages:
            for key in sorted(keys_named(p) & pc_keys):
                failures.append(f"{space}: page {p['id']} shows setting {key}, which lives on a pc_only page a paired "
                                "device never sees (D264)")
    for q in space_pages.get("quick", []):
        if q.get("key") in pc_keys or q.get("home") in pc_pages:
            failures.append(f"{space}: quick switch {q.get('key') or q.get('id')} is a pc_only page's, which a paired "
                            "device's top bar would show (D264)")
    for s in description["settings"]:
        if s.get("choices_from") in pc_topics and s.get("home") not in pc_pages:
            failures.append(f"{space}: setting {s['key']} takes its choices from {s['choices_from']}, a topic on this PC "
                            "alone, but lives on a page a paired device sees (D264)")
        if s.get("home") in pc_pages and not s.get("sensitive") and s.get("type") != "secret":
            failures.append(f"{space}: setting {s['key']} lives on pc_only page {s['home']}: it is sensitive too, so that "
                            "no paired device changes it (D264)")


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
