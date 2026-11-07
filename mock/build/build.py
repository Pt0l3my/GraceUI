#!/usr/bin/env python3
"""Write GraceUI's static mock pages (version 2: the framework categories) from one shared frame.

Run from the repository root with `python3 -I mock/build/build.py mock`. The ten HTML pages are its output: edit this
file, not the HTML. grace.css and mock.js are written by hand. Every text and drawing here is ours: invented names,
our own words, our own icons. Nothing is taken from SkyrimNet, SeverActions, Magelight UI or the game. MIT, see
../../LICENSE.
"""
import html
import sys
from pathlib import Path

OUT = Path(sys.argv[1])

# ---------------------------------------------------------------- icons (ours: 1.5 px stroke, 18 px grid)

ICONS = {
    "home": '<path d="M3 8.2 9 3.2l6 5V15h-4v-4H7v4H3z"/>',
    "ai": '<rect x="4.5" y="4.5" width="9" height="9" rx="1"/><path d="M7 2.5v2M11 2.5v2M7 13.5v2M11 13.5v2M2.5 7h2M2.5 11h2M13.5 7h2M13.5 11h2"/><path d="M9 7.2 10.8 9 9 10.8 7.2 9z"/>',
    "voice": '<path d="M3 7.5v3M6 5v8M9 3v12M12 5.5v7M15 7.5v3"/>',
    "characters": '<circle cx="7" cy="6" r="2.5"/><path d="M2.5 15c.6-2.6 2.3-4 4.5-4s3.9 1.4 4.5 4"/><path d="M11.5 3.8a2.5 2.5 0 0 1 0 4.6M13 11.2c1.2.6 2 1.8 2.5 3.8"/>',
    "history": '<path d="M3 3.5h4.5A1.5 1.5 0 0 1 9 5v10a1.5 1.5 0 0 0-1.5-1.5H3zM15 3.5h-4.5A1.5 1.5 0 0 0 9 5v10a1.5 1.5 0 0 1 1.5-1.5H15z"/>',
    "behaviour": '<path d="M10 2 4.5 10H9l-1 6 5.5-8H9z"/>',
    "system": '<rect x="3" y="3" width="12" height="5" rx="1"/><rect x="3" y="10" width="12" height="5" rx="1"/><path d="M5.5 5.5h.01M5.5 12.5h.01M9 5.5h3.5M9 12.5h3.5"/>',
    "tools": '<path d="M11.6 2.8a3.4 3.4 0 0 0-3.9 4.5l-4.9 4.9a1.4 1.4 0 0 0 2 2l4.9-4.9a3.4 3.4 0 0 0 4.5-3.9l-2 2-2-.6-.6-2z"/>',
    "settings": '<circle cx="9" cy="9" r="2"/><circle cx="9" cy="9" r="5"/><path d="M9 1.8V4M9 14v2.2M1.8 9H4M14 9h2.2M3.9 3.9l1.6 1.6M12.5 12.5l1.6 1.6M3.9 14.1l1.6-1.6M12.5 5.5l1.6-1.6"/>',
    "docs": '<path d="M4.5 2.5h6l3 3v10h-9z"/><path d="M10.5 2.5v3h3M6.5 9h5M6.5 11.5h5"/>',
    "packs": '<path d="M2.5 5.5 9 2.5l6.5 3v7L9 15.5l-6.5-3z"/><path d="M2.5 5.5 9 8.5l6.5-3M9 8.5v7"/>',
    "collapse": '<path d="M3 3v12M14.5 5.5 11 9l3.5 3.5M11 9H6"/>',
    "chev": '<path d="M7 4.5 11.5 9 7 13.5"/>',
    "clock": '<circle cx="9" cy="9" r="6.5"/><path d="M9 5.5V9l2.5 1.5"/>',
    "journey": '<path d="M5 15.5 7.5 2.5M13 15.5 10.5 2.5M9 4v1.5M9 8v2M9 12.5v2"/>',
    "palette": '<path d="M9 2.5a6.5 6.5 0 1 0 0 13c1 0 1.5-.6 1.5-1.4 0-1.1-1-1.4-1-2.3 0-.8.6-1.3 1.5-1.3H13a2.5 2.5 0 0 0 2.5-2.5C15.5 5 12.6 2.5 9 2.5z"/><path d="M5.5 8.5h.01M7.5 5.5h.01M11 5.5h.01"/>',
    "power": '<path d="M9 2.5v6"/><path d="M5.2 5a5.5 5.5 0 1 0 7.6 0"/>',
    "search": '<circle cx="8" cy="8" r="4.5"/><path d="m11.5 11.5 4 4"/>',
    "help": '<circle cx="9" cy="9" r="6.5"/><path d="M7.2 7.2a1.9 1.9 0 1 1 2.6 1.8c-.5.2-.8.6-.8 1.1v.4M9 12.6v.01"/>',
    "close": '<path d="M4.5 4.5l9 9M13.5 4.5l-9 9"/>',
    "lock": '<rect x="4" y="8" width="10" height="7.5" rx="1"/><path d="M6 8V6a3 3 0 0 1 6 0v2"/>',
    "warn": '<path d="M9 2.5 16 15H2z"/><path d="M9 7v3.5M9 12.7v.01"/>',
    "check": '<path d="M3.5 9.5 7 13l7.5-8"/>',
    "info": '<circle cx="9" cy="9" r="6.5"/><path d="M9 8.2v4.3M9 5.8v.01"/>',
    "reset": '<path d="M4 9a5 5 0 1 0 1.5-3.6"/><path d="M4 3v3h3"/>',
    "pause": '<path d="M6.5 4v10M11.5 4v10"/>',
    "eye": '<path d="M1.5 9s2.8-5 7.5-5 7.5 5 7.5 5-2.8 5-7.5 5-7.5-5-7.5-5z"/><circle cx="9" cy="9" r="2.2"/>',
    "plus": '<path d="M9 3.5v11M3.5 9h11"/>',
    "out": '<path d="M7 4.5h6.5V11M13.5 4.5 5 13"/>',
    "key": '<circle cx="6" cy="9" r="3"/><path d="M9 9h6.5M13 9v2.5M15.5 9v2"/>',
    "cloud": '<path d="M5 14h8a3 3 0 0 0 .4-6A4.5 4.5 0 0 0 4.7 8.6 2.7 2.7 0 0 0 5 14z"/>',
    "computer": '<rect x="2.5" y="3.5" width="13" height="9" rx="1"/><path d="M6.5 15.5h5M9 12.5v3"/>',
    "sort": '<path d="M9 3v12M5 11l4 4 4-4"/>',
    "sliders": '<path d="M4 3v12M9 3v12M14 3v12"/><path d="M2.5 11h3M7.5 6h3M12.5 10h3"/>',
    "flask": '<path d="M7 2.5h4M7.5 2.5v5L3.5 14a1 1 0 0 0 .9 1.5h9.2a1 1 0 0 0 .9-1.5l-4-6.5v-5"/><path d="M5.5 11h7"/>',
    "mic": '<rect x="6.5" y="2.5" width="5" height="8" rx="2.5"/><path d="M4 9a5 5 0 0 0 10 0M9 14v2"/>',
    "sword": '<path d="M14.5 3.5 6 12M12.5 3.5h2v2M4.5 10.5l3 3M3 15l2.2-2.2"/>',
    "speech": '<path d="M3 4h12v7.5H8.5L5 14.5v-3H3z"/>',
    "orb": '<circle cx="9" cy="9" r="5.5"/><path d="M6.2 7.2a3 3 0 0 1 2.6-1.6"/>',
    "world": '<path d="M2.5 13.5l4-6 3 4 2-2.5 4 4.5z"/><path d="M12.5 5.5v.01"/>',
    "meet": '<path d="M2.5 9h4M11.5 9h4"/><path d="M6.5 6.5 9 9l-2.5 2.5M11.5 6.5 9 9l2.5 2.5"/>',
    "lozenge": '<path d="M9 3 15 9 9 15 3 9z"/><path d="M9 6.5 11.5 9 9 11.5 6.5 9z"/>',
}


def sprite():
    out = ['<svg class="sprite" aria-hidden="true" focusable="false" xmlns="http://www.w3.org/2000/svg">']
    for name, body in ICONS.items():
        out.append(f'  <symbol id="i-{name}" viewBox="0 0 18 18"><g fill="none" stroke="currentColor" stroke-width="1.5" '
                   f'stroke-linecap="round" stroke-linejoin="round">{body}</g></symbol>')
    out.append("</svg>")
    return "\n".join(out)


def ic(name, cls="ic"):
    return f'<svg class="{cls}" aria-hidden="true" focusable="false"><use href="#i-{name}"/></svg>'


_uid = [0]


def uid(prefix="u"):
    _uid[0] += 1
    return f"{prefix}{_uid[0]}"


# ---------------------------------------------------------------- shared widgets

def switch(label_id, on=True):
    return (f'<span class="switch-wrap"><button class="switch" role="switch" aria-checked="{"true" if on else "false"}" '
            f'aria-labelledby="{label_id}"></button><span class="switch-word" aria-hidden="true">{"On" if on else "Off"}</span></span>')


def seg(label_id, options, chosen, cls="seg"):
    btns = "".join(f'<button role="radio" aria-checked="{"true" if o == chosen else "false"}">{o}</button>' for o in options)
    return f'<span class="{cls}" role="radiogroup" aria-labelledby="{label_id}">{btns}</span>'


def tri(label_id, value="Inherit"):
    return seg(label_id, ["Inherit", "On", "Off"], value)


def select(options, chosen=None, label=None, cls="select", lid=None):
    opts = "".join(f'<option{" selected" if o == chosen else ""}>{o}</option>' for o in options)
    aria = f' aria-label="{label}"' if label else (f' aria-labelledby="{lid}"' if lid else "")
    return f'<select class="{cls}"{aria}>{opts}</select>'


def mark(state, label=None):
    words = {"on": "on", "off": "off", "some": "some on"}
    if state == "on":
        shape = '<path class="on" d="M6 1 11 6 6 11 1 6z"/>'
    elif state == "off":
        shape = '<path class="off" d="M6 1 11 6 6 11 1 6z"/>'
    else:
        shape = '<path class="some-in" d="M6 1V11L1 6z"/><path class="some-out" d="M6 1 11 6 6 11 1 6z"/>'
    label = label or words[state]
    return (f'<span class="mark" title="{label[0].upper() + label[1:]}"><svg viewBox="0 0 12 12" aria-hidden="true">'
            f'{shape}</svg><span class="vh">{label}</span></span>')


def meter(value, total=100, cls="fill"):
    w = max(0, min(100, round(100 * value / total)))
    return (f'<svg class="meter" viewBox="0 0 100 3" preserveAspectRatio="none" aria-hidden="true">'
            f'<rect class="track" width="100" height="3"/><rect class="{cls}" width="{w}" height="3"/></svg>')


def avail(kind, text):
    icon = {"wait": "warn", "later": "lock", "v15": "info", "inert": "info", "off": "warn"}[kind]
    return f'<span class="avail {kind}">{ic(icon, "ic sm")}{text}</span>'


def src(kind, page="characters-types.html"):
    """An inherited value's source (EldenNet DESIGN §7.6's chain), linked to its own row on NPC types."""
    return f'<a href="{page}#kind-{kind}">kind: {kind}</a>'


def lives(where, href="#"):
    absent = ' data-mock="absent"' if href == "#" else ""
    return f'<a class="lives" href="{href}"{absent} title="This is a shortcut; the setting lives in {where}">lives in {where}{ic("out", "ic sm")}</a>'


def srow(name, desc=None, *, ctl="", default=None, changed=False, dim=None, lives_in=None, lives_href="#", badge=None,
         reset=False, lid=None):
    lid = lid or uid("l")
    cls = "srow" + (" changed" if changed else "") + (" dim" if dim else "")
    extra = ""
    if badge:
        extra += badge
    if lives_in:
        extra += lives(lives_in, lives_href)
    reset_html = (f'<button class="iconbtn reset" title="Reset to default" aria-label="Reset {name}">{ic("reset", "ic sm")}</button>'
                  if reset else "")
    d = ""
    if desc or default or dim:
        parts = []
        if desc:
            parts.append(desc)
        if default:
            parts.append(f'<span class="dflt">Default: {default}.</span>')
        d = f'<p class="desc">{" ".join(parts)}</p>'
        if dim:
            d += f'<p class="desc why">{ic("warn", "ic sm")} {dim}</p>'
    ctl_html = ctl if ctl else ""
    return (f'<div class="{cls}"><div class="name"><span id="{lid}">{name}</span>{extra}</div>{d}'
            f'<div class="ctl">{reset_html}{ctl_html}</div></div>')


def sw_row(name, desc=None, on=True, default=None, **kw):
    lid = uid("l")
    return srow(name, desc, ctl=switch(lid, on), default=default, lid=lid, **kw)


def slider(name, rid, value, mn, mx, step, unit, desc, default, *, zero=None, changed=False, lives_in=None, lives_href="#"):
    lid = uid("l")
    v = float(value)
    dec = 0 if "." not in str(step) else len(str(step).split(".")[1])
    shown = zero if (zero and v == 0) else f"{v:.{dec}f}" + (f" {unit}" if unit else "")
    zero_attr = f' data-zero="{zero}"' if zero else ""
    unit_attr = f' data-unit="{unit}"' if unit else ""
    extra = lives(lives_in, lives_href) if lives_in else ""
    cls = "srow slide" + (" changed" if changed else "")
    return (f'<div class="{cls}"><div class="name"><span id="{lid}">{name}</span>{extra}</div>'
            f'<div class="range-wrap"><input type="range" id="{rid}" min="{mn}" max="{mx}" step="{step}" value="{value}" '
            f'aria-labelledby="{lid}"{unit_attr}{zero_attr}></div>'
            f'<span class="value" data-for="{rid}">{shown}</span>'
            f'<p class="desc">{desc} <span class="dflt">Default: {default}.</span></p></div>')


# Reaction cooldowns: the slider of Behaviour › Reactions, 0–6 h of play in uneven steps (DESIGN §4.6.6)
DURATIONS = [0, 5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1200, 1800, 2700, 3600, 5400, 7200, 10800, 14400, 21600]


def dur_words(sec):
    if sec == 0:
        return "None"
    if sec < 60:
        return f"{sec} s"
    if sec < 3600:
        m, r = divmod(sec, 60)
        return f"{m} min" + (f" {r} s" if r else "")
    h, r = divmod(sec, 3600)
    return f"{h} h" + (f" {r // 60} min" if r else "")


def reaction_row(name, rid, desc, default_s, value_s=None, on=True, default_on=True):
    """A shortcut to one reaction's row on Behaviour › Reactions: its switch and its cooldown slider, the same controls."""
    lid = uid("l")
    v = default_s if value_s is None else value_s
    changed = v != default_s or on != default_on
    cls = "srow slide react" + (" changed" if changed else "")
    reset = (f'<button class="iconbtn reset" title="Reset to default" aria-label="Reset {name}">{ic("reset", "ic sm")}</button>'
             if changed else "")
    dflt = f'Default: {"on" if default_on else "off"}, {dur_words(default_s)}.'
    d = (desc + " " if desc else "") + f'<span class="dflt">{dflt}</span>'
    steps = ",".join(str(x) for x in DURATIONS)
    return (f'<div class="{cls}"><div class="name"><span id="{lid}">{name}</span>{lives("Behaviour › Reactions")}</div>'
            f'<div class="ctl">{reset}{switch(lid, on)}</div>'
            f'<div class="range-wrap"><input type="range" id="{rid}" min="0" max="{len(DURATIONS) - 1}" step="1" value="{DURATIONS.index(v)}" '
            f'aria-label="{name}: cooldown" aria-valuetext="{dur_words(v)}" data-durations="{steps}"></div>'
            f'<span class="value" data-for="{rid}">{dur_words(v)}</span>'
            f'<p class="desc">{d}</p></div>')


def card(label, body, *, desc=None, icon=None, collapsible=True, open_=True, tools="", cls="", cid=None, label_extra=""):
    cid = cid or uid("c")
    icon_html = ic(icon, "ic sm") if icon else ""
    desc_html = f'<p class="card-desc">{desc}</p>' if desc else ""
    toggle = ""
    if collapsible:
        toggle = (f'<button class="toggle" aria-expanded="{"true" if open_ else "false"}" aria-controls="{cid}-b" '
                  f'aria-label="Show or hide {html.escape(label)}">{ic("chev", "ic sm")}</button>')
    hidden = "" if open_ else " hidden"
    ccls = "card" + (" " + cls if cls else "") + ("" if open_ else " collapsed")
    return (f'<section class="{ccls}" aria-labelledby="{cid}-h"><div class="card-head"><div class="titles">'
            f'<h2 class="card-label" id="{cid}-h">{icon_html}{label}{label_extra}</h2>{desc_html}</div>'
            f'<div class="card-tools">{tools}{toggle}</div></div>'
            f'<div class="card-body" id="{cid}-b"{hidden}>{body}</div></section>')


def related(items):
    rows = []
    for title, desc, href in items:
        absent = ' data-mock="absent"' if href == "#" else ""
        t = " (not in this mock)" if href == "#" else ""
        rows.append(f'<a class="rel" href="{href}"{absent} title="{html.escape(title)}{t}"><span class="grow"><span class="t">{title}</span>'
                    f'<span class="d">{desc}</span></span>{ic("out")}</a>')
    return card("Related", "".join(rows), collapsible=False, cls="related")


def tiles(items, cls="tiles"):
    out = []
    for value, caption in items:
        out.append(f'<div class="tile"><span class="v">{value}</span><span class="c">{caption}</span></div>')
    return f'<div class="{cls}">{"".join(out)}</div>'


def tabs(name, items, panels):
    """items: (key, label, count); panels: dict key -> html."""
    btns, pans = [], []
    for i, (key, label, count) in enumerate(items):
        sel = i == 0
        cnt = f'<span class="count">{count}</span>' if count is not None else ""
        btns.append(f'<button class="tab" role="tab" id="t-{key}" aria-controls="p-{key}" aria-selected="{"true" if sel else "false"}"'
                    f'{"" if sel else " tabindex=" + chr(34) + "-1" + chr(34)} data-hash="{key}">{label}{cnt}</button>')
        pans.append(f'<div role="tabpanel" id="p-{key}" aria-labelledby="t-{key}"{"" if sel else " hidden"}>{panels[key]}</div>')
    return f'<div class="tabs" role="tablist" aria-label="{name}">{"".join(btns)}</div>{"".join(pans)}'


def monogram(letter, cls="monogram"):
    return (f'<svg class="{cls}" viewBox="0 0 64 64" aria-hidden="true"><path class="outer" d="M32 3 61 32 32 61 3 32z"/>'
            f'<path class="inner" d="M32 11 53 32 32 53 11 32z"/><text x="32" y="39" text-anchor="middle">{letter}</text></svg>')


# ---------------------------------------------------------------- the frame (IA v2 §1)

# Each category: (key, label, icon, pages); a page: (key, label, file or None, when it arrives ("v1.5" or "later", shown as a WIP tag) or None)
TREE = [
    ("home", "Home", "home", "index.html"),
    ("ai", "AI", "ai", [("ai.providers", "Providers", "ai-providers.html", None),
                        ("ai.models", "Models", "ai-models.html", None),
                        ("ai.selftest", "Self-test", None, None),
                        ("ai.requests", "Requests", None, None),
                        ("ai.director", "Director", None, "later")]),
    ("voice", "Voice", "voice", [("voice.text", "Text and subtitles", None, None),
                                 ("voice.tts", "Text-to-speech", "voice-tts.html", "v1.5"),
                                 ("voice.stt", "Speech-to-text", None, "v1.5")]),
    ("characters", "Characters", "characters", [("characters.playthrough", "Playthrough", "characters-playthrough.html", None),
                                                ("characters.named", "Named NPCs", "characters-named.html", None),
                                                ("characters.types", "NPC types", "characters-types.html", None)]),
    ("history", "History", "history", [("history.events", "Events", None, None),
                                       ("history.conversations", "Conversations", None, None),
                                       ("history.memories", "Memories", "history-memories.html", None),
                                       ("history.world", "World", None, None)]),
    ("behaviour", "Behaviour", "behaviour", [("behaviour.conversation", "Conversation", None, None),
                                             ("behaviour.speaking", "Speaking up", "behaviour-speaking.html", None),
                                             ("behaviour.combat", "Combat", None, None),
                                             ("behaviour.awareness", "Awareness", None, None),
                                             ("behaviour.reactions", "Reactions", None, None),
                                             ("behaviour.pacing", "Pacing", "behaviour-pacing.html", None),
                                             ("behaviour.acts", "Acts", None, "later")]),
    ("system", "System", "system", [("system.status", "Status", None, None),
                                    ("system.performance", "Performance", None, None),
                                    ("system.traces", "Traces", None, None),
                                    ("system.logs", "Logs", None, None)]),
    ("tools", "Tools", "tools", [("tools.monitor", "Event monitor", None, None),
                                 ("tools.report", "Bug report", None, None)]),
]
FOOT = [("settings", "Settings", "settings"), ("docs", "Docs", "docs"), ("packs", "Content packs", "packs")]

FILES = {}
for _k, _l, _i, _p in TREE:
    if isinstance(_p, str):
        FILES[_k] = _p
    else:
        for _pk, _pl, _pf, _pv in _p:
            if _pf:
                FILES[_pk] = _pf


def href(key):
    return FILES.get(key, "#")


def sidebar(active):
    cat_of_active = active.split(".")[0]
    out = ['<nav class="side grain" aria-label="EldenNet">',
           '<div class="brand"><span class="wordmark">EldenNet</span>'
           f'<button class="iconbtn collapse" aria-label="Collapse the sidebar" title="Collapse the sidebar">{ic("collapse")}</button></div>',
           '<ul class="nav">']
    for key, label, icon, pages in TREE:
        if isinstance(pages, str):
            cur = ' aria-current="page"' if active == key else ""
            out.append(f'<li><a class="cat" href="{pages}"{cur}>{ic(icon)}<span class="lbl">{label}</span></a></li>')
            continue
        is_cur = cat_of_active == key
        cls = "cat current" if is_cur else "cat"
        out.append(f'<li><button class="{cls}" aria-expanded="{"true" if is_cur else "false"}" aria-controls="sub-{key}" title="{label}">'
                   f'{ic(icon)}<span class="lbl">{label}</span>{ic("chev", "ic chev")}</button>')
        out.append(f'<ul class="subs" id="sub-{key}"{"" if is_cur else " hidden"}>')
        for pk, pl, pf, pv in pages:
            cur = ' aria-current="page"' if pk == active else ""
            later = " later" if pv else ""
            when = "arrives in v1.5" if pv == "v1.5" else "a later version"
            tag = f'<span class="ver" title="Work in progress: {when}">WIP<span class="vh">: work in progress, {when}</span></span>' if pv else ""
            target = pf or "#"
            absent = "" if pf else ' data-mock="absent" title="Not in this mock"'
            out.append(f'<li><a class="{later.strip()}" href="{target}"{cur}{absent}><span class="lbl">{pl}</span>{tag}</a></li>')
        out.append("</ul></li>")
    out.append("</ul>")
    out.append('<div class="side-foot"><ul>')
    for key, label, icon in FOOT:
        out.append(f'<li><a class="cat" href="#" data-mock="absent" title="{label} (not in this mock)">{ic(icon)}<span class="lbl">{label}</span></a></li>')
    out.append("</ul></div></nav>")
    return "\n".join(out)


QUICK = [
    ("EldenNet on", "Settings › General", True),
    ("Pause AI for this session", "Home", False),
    ("Conversation with named NPCs", "Behaviour › Conversation", True),
    ("Call out to other NPCs", "Behaviour › Conversation", True),
    ("Named NPCs speak unasked", "Behaviour › Speaking up", True),
    ("Other NPCs speak unasked", "Behaviour › Speaking up", True),
    ("NPCs greet and answer each other", "Behaviour › Speaking up", True),
    ("NPCs speak up on their own", "Behaviour › Speaking up", True),
    ("NPCs know what they said and how they are", "Behaviour › Awareness", True),
    ("NPCs notice statuses", "Behaviour › Awareness", True),
    ("Named NPCs remember", "History › Memories", True),
    ("Floating labels", "Voice › Text and subtitles", True),
]


def topbar(live=False, journey=False):
    parts = ['<header class="topbar">',
             f'<label class="tb-search" title="Search settings and pages (/)">{ic("search", "ic sm")}<span class="vh">Search settings and pages</span>'
             '<input type="search" placeholder="Search settings and pages" autocomplete="off" spellcheck="false"><kbd>/</kbd></label>',
             '<span class="spacer"></span>',
             '<a class="tb-btn lamp" href="#" data-mock="absent" title="Ready: every feature works on this build. Opens System › Status">'
             '<span class="dot moss"></span><span class="word">Ready</span></a>']
    if live:
        parts.append('<div class="tb-wrap"><button class="tb-btn" aria-haspopup="true" aria-expanded="false" aria-controls="m-live" '
                     f'title="Live updates" data-hash="live">{ic("clock", "ic sm")}<span class="val">Live</span>{ic("chev", "ic chev")}</button>'
                     '<div class="menu" id="m-live" role="menu" hidden><p class="mhead t-eyebrow">Live updates</p>'
                     '<button class="opt" role="menuitemradio" aria-checked="true">Live</button>'
                     '<button class="opt" role="menuitemradio" aria-checked="false">Every 5 s</button>'
                     '<button class="opt" role="menuitemradio" aria-checked="false">Paused</button>'
                     '<p class="note">Slows or holds this page so a moving list can be read. For this session only.</p></div></div>')
    if journey:
        parts.append('<div class="tb-wrap"><button class="tb-btn" aria-haspopup="true" aria-expanded="false" aria-controls="m-journey" '
                     f'title="The journey in view" data-hash="journey">{ic("journey", "ic sm")}<span class="pc">Brannoc</span>'
                     f'<span class="dim">· Journey 1</span>{ic("chev", "ic chev")}</button>'
                     '<div class="menu" id="m-journey" role="menu" hidden><p class="mhead t-eyebrow">Journeys of this playthrough</p>'
                     '<button class="opt" role="menuitemradio" aria-checked="true">Journey 1 · now</button>'
                     '<p class="note">A new journey begins with each new cycle. Earlier journeys are shown here, read-only.</p></div></div>')
    parts.append('<div class="tb-wrap"><button class="tb-btn" aria-haspopup="true" aria-expanded="false" aria-controls="m-theme" '
                 f'title="Theme" data-hash="theme">{ic("palette", "ic sm")}<span class="val">Grace</span>{ic("chev", "ic chev")}</button>'
                 '<div class="menu" id="m-theme" role="menu" hidden><p class="mhead t-eyebrow">Theme</p>'
                 '<button class="opt" role="menuitemradio" aria-checked="true">Grace</button>'
                 '<button class="opt" role="menuitemradio" aria-checked="false">Grace Clear · higher contrast</button>'
                 '<p class="note">A shortcut: the theme lives in Settings › Menu.</p></div></div>')
    rows = []
    for name, where, on in QUICK:
        lid = uid("q")
        rows.append(f'<div class="qrow"><span class="name" id="{lid}">{name}</span><span class="qlives">lives in {where}</span>{switch(lid, on)}</div>')
    parts.append('<div class="tb-wrap"><button class="tb-btn" aria-haspopup="true" aria-expanded="false" aria-controls="m-quick" '
                 f'title="Quick switches (Start on a pad)" data-hash="quick">{ic("power", "ic sm")}<span class="val">Quick switches</span>{ic("chev", "ic chev")}</button>'
                 f'<div class="menu quick" id="m-quick" role="menu" hidden><p class="mhead t-eyebrow">Quick switches</p>{"".join(rows)}'
                 '<p class="note">Shortcuts only: each switch lives on its own page, where its help and default are.</p></div></div>')
    parts.append(f'<button class="iconbtn" aria-label="Close the menu" title="Close the menu (Esc). In game only">{ic("close")}</button>')
    parts.append("</header>")
    return "\n".join(parts)


def help_dialog(title, purpose, steps, rel):
    steps_html = "".join(f"<li>{s}</li>" for s in steps)
    links = "".join(f'<a href="{h}"{" data-mock=" + chr(34) + "absent" + chr(34) if h == "#" else ""}>{t}{ic("out", "ic sm")}</a>' for t, h in rel)
    return (f'<div class="scrim" id="help" hidden role="dialog" aria-modal="true" aria-labelledby="help-title">'
            f'<div class="dialog"><div class="dh"><h2 class="t-display-md" id="help-title">{title}</h2>'
            f'<button class="iconbtn" data-close aria-label="Close help">{ic("close")}</button></div>'
            f'<p class="prose">{purpose}</p><h3 class="t-eyebrow">How to use it</h3><ol>{steps_html}</ol>'
            f'<h3 class="t-eyebrow">Related</h3><p class="rel-links">{links}</p></div></div>')


def header(title, desc, actions="", ver=None):
    when = "arrives in v1.5" if ver == "v1.5" else "a later version"
    vt = f'<span class="ver-title" title="Work in progress: {when}">WIP<span class="vh">: work in progress, {when}</span></span>' if ver else ""
    act = f'<div class="ph-actions">{actions}</div>' if actions else ""
    return (f'<div class="ph"><div class="ph-text"><h1 class="t-display-lg">{title}{vt}'
            f'<button class="help-mark" aria-label="About this page" title="About this page" data-hash="help">{ic("help")}</button></h1>'
            f'<p class="desc">{desc}</p></div>{act}</div>')


CSP = ("default-src 'none'; style-src 'self'; script-src 'self'; img-src 'self' data:; font-src 'self'; "
       "connect-src 'none'; base-uri 'none'; form-action 'none'")


def page(fname, title, active, body, helpd, live=False, journey=False):
    doc = f"""<!DOCTYPE html>
<html lang="en-GB">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="{CSP}">
<meta name="referrer" content="no-referrer">
<meta name="color-scheme" content="dark">
<title>{title} · EldenNet</title>
<link rel="stylesheet" href="grace.css">
</head>
<body>
{sprite()}
<div class="app">
{sidebar(active)}
<div class="main">
{topbar(live, journey)}
<main class="page" id="page"><div class="page-inner">
{body}
</div></main>
</div>
</div>
{helpd}
<script src="mock.js"></script>
</body>
</html>
"""
    (OUT / fname).write_text(doc, encoding="utf-8")


# ---------------------------------------------------------------- Home (IA v2 §2)

def home():
    actions = (f'<button class="btn secondary" aria-pressed="false" title="Pause AI for this session only">{ic("pause", "ic sm")}Pause AI</button>'
               f'<button class="btn secondary">{ic("flask", "ic sm")}Run self-test</button>'
               f'<button class="btn quiet">{ic("sliders", "ic sm")}Customise</button>')
    model = (f'<div class="subcard"><h3 class="card-label">{ic("ai", "ic sm")}Model</h3>'
             f'<span class="fl">Provider</span><p class="t-ui ink-hi">OpenRouter <span class="keychip set">{ic("check", "ic sm")}Key set</span></p>'
             f'<span class="fl">Preset</span>{select(["Cloud budget", "Local 12 GB", "Local 16 GB+", "Custom"], "Cloud budget", "Model preset")}'
             f'<div class="row"><button class="btn primary">{ic("flask", "ic sm")}Test</button>'
             f'<a class="iconbtn boxed" href="ai-models.html" title="Open AI › Models">{ic("out")}</a></div>'
             f'<p class="fl c-moss">{ic("check", "ic sm")} Quick: the first words in 1.2 s</p></div>')
    voice = (f'<div class="subcard"><h3 class="card-label">{ic("voice", "ic sm")}Voice</h3>'
             '<p class="t-ui grow">NPCs speak in text in this version. Spoken voices arrive in v1.5.</p>'
             f'<div class="row"><a class="btn secondary" href="voice-tts.html">About voices{ic("out", "ic sm")}</a></div></div>')
    mic = (f'<div class="subcard"><h3 class="card-label">{ic("mic", "ic sm")}Microphone</h3>'
           '<p class="t-ui grow">You type to named NPCs. Talking to them aloud arrives in v1.5.</p>'
           f'<div class="row"><a class="btn secondary" href="#" data-mock="absent">About talking aloud{ic("out", "ic sm")}</a></div></div>')
    setup = card("Setup", f'<div class="subcards">{model}{voice}{mic}</div>'
                 '<div class="card-foot"><span>The self-test passed at the title screen today, so this card starts closed; you opened it.</span>'
                 '<a class="btn quiet sm" href="#" data-mock="absent">Run setup again</a></div>',
                 icon="lozenge", cls="gc-setup")

    status_body = f"""<dl class="kv">
<dt>State</dt><dd class="c-moss"><span class="dot moss"></span>Ready</dd>
<dt>Talking</dt><dd class="c-moss">You can talk now · <kbd>T</kbd></dd>
<dt>AI</dt><dd>Running</dd>
</dl><div class="kvhr" aria-hidden="true"></div><dl class="kv">
<dt>Conversation</dt><dd><span class="dot moss"></span>healthy</dd>
<dt>Quick lines</dt><dd><span class="dot moss"></span>healthy</dd>
<dt>Prepared lines</dt><dd><span class="dot moss"></span>healthy</dd>
<dt>Memory</dt><dd class="c-ember">{ic("warn", "ic sm")}cooling 40 s</dd>
</dl><div class="kvhr" aria-hidden="true"></div><dl class="kv">
<dt>EldenNet</dt><dd class="mono">0.6.0</dd>
<dt>Game build</dt><dd class="c-moss">{ic("check", "ic sm")}supported</dd>
<dt>Session</dt><dd class="mono">1 h 12 min</dd>
<dt>Dashboard</dt><dd class="mono">127.0.0.1:8642</dd>
</dl>"""
    open_status = f'<a class="open iconbtn" href="#" data-mock="absent" title="Open System › Status">{ic("out", "ic sm")}</a>'
    status = card("Status", status_body, collapsible=False, cls="gc gc-status", tools=open_status)

    people = [
        ('<span class="who npc">Idrel</span>', "Named NPC", '<span>calm · <span class="tag gold">Speaking</span></span>', "thinks well of you"),
        ('<span class="who">Wandering merchant</span>', "Merchant", "<span>calm</span>", ""),
        ('<span class="who">Road bandit</span>', "Humanoid", f'<span class="foe">{ic("sword", "ic sm")}fighting others</span>', "bloodied"),
        ('<span class="who">Wolves</span>', "Creature", f'<span class="foe">{ic("sword", "ic sm")}fighting others</span>', "badly hurt"),
        ('<span class="who">Steed</span>', "Mount", "<span>calm</span>", ""),
    ]
    rows = []
    for who, kind, st, s2 in people:
        s2h = f'<span class="s2">{s2}</span>' if s2 else ""
        rows.append(f'<li><div>{who}<div class="kind">{kind}</div></div><div class="st">{st}{s2h}</div></li>')
    rows.append('<li><div><span class="veiled" title="Someone you can see whose name the game has not shown">Someone you have not met</span></div><div class="st"></div></li>')
    nearby = card("Nearby", f'<ul class="people">{"".join(rows)}</ul>', desc="Only those you can perceive · twice a second",
                  collapsible=False, cls="gc gc-nearby", label_extra=' <span class="mono ink-faint">5</span>',
                  tools=f'<a class="open iconbtn" href="characters-playthrough.html" title="Open Characters › Playthrough">{ic("out", "ic sm")}</a>')

    happening = card("Happening now", f"""<ul class="ttl">
<li><div class="t1"><span>A fight nearby: a road bandit and wolves</span><span class="left">22 s</span></div>{meter(22, 30)}</li>
<li><div class="t1"><span>Idrel is speaking</span><span class="left">4 s</span></div>{meter(4, 8)}</li>
</ul>""", desc="Short-lived events you can perceive", collapsible=False, cls="gc gc-happening")

    problems = card("Problems", f"""<div class="problem">{ic("warn")}<div><p class="p1">The local model server timed out three times</p>
<p class="p2">Memory work uses its fallback model for 40 s more, then tries the server again. If it keeps happening, see <a href="ai-providers.html#health">AI › Providers › Health</a>.</p></div></div>
<p class="note">{ic("check", "ic sm")}Nothing else needs you.</p>""", collapsible=False, cls="gc gc-problems")

    feed = [
        ("4:12", "dialogue", "speech", '<span class="speaker npc">Idrel</span><span class="tag">speaking up</span>',
         '<p class="line">“Mind the third plank. The river took a bite out of it last spring.”</p>', True),
        ("4:11", "dialogue", "speech", '<span class="event">Conversation with <span class="c-name">Idrel</span> closed · you walked away</span>', "", False),
        ("4:08", "dialogue", "speech", '<span class="event">Conversation with <span class="c-name">Idrel</span> opened · by you</span>', "", False),
        ("4:06", "combat", "sword", '<span class="speaker">Road bandit</span><span class="tag">enters a fight</span>',
         '<p class="line">“Wolves! Backs together, now!”</p>', False),
        ("4:03", "system", "orb", '<span class="event">Prepared lines are ready</span>', "", False),
        ("3:58", "dialogue", "speech", '<span class="speaker">Unmarked soldier</span><span class="tag">idle remark</span>',
         '<p class="line">“Quiet road tonight. I don’t trust a quiet road.”</p>', False),
    ]
    items = []
    for t, fam, icon, head, line, fresh in feed:
        cls = ' class="fresh"' if fresh else ""
        items.append(f'<li{cls}><span class="time">{t}</span><span class="fam {fam}" title="{fam}">{ic(icon, "ic sm")}</span>'
                     f'<div><div class="who-line">{head}</div>{line}</div></li>')
    lines = card("Lines", f'<ul class="feed">{"".join(items)}</ul>', desc="Lines you were shown, by time of play",
                 collapsible=False, cls="gc gc-lines",
                 tools=f'<a class="open iconbtn" href="#" data-mock="absent" title="Open History › Conversations">{ic("out", "ic sm")}</a>')

    calls = [
        ("4:12:31", "Conversation", "Own-initiative line", '<span class="c-name">Idrel</span>', "chat-medium-it", "0.41 s", "1.32 s", f'<span class="pill moss">{ic("check", "ic sm")}Shown</span>'),
        ("4:11:20", "Memory", "Memory work", '<span class="c-name">Idrel</span>', "chat-large-it", "—", "3.90 s", f'<span class="pill ember">{ic("warn", "ic sm")}Fallback model</span>'),
        ("4:10:02", "Conversation", "Reply", '<span class="c-name">Idrel</span>', "chat-medium-it", "0.38 s", "2.05 s", f'<span class="pill moss">{ic("check", "ic sm")}Shown</span>'),
        ("4:09:30", "Conversation", "Reply", '<span class="c-name">Idrel</span>', "chat-medium-it", "0.40 s", "1.88 s", '<span class="pill faint">Not shown</span>'),
        ("4:06:40", "Quick lines", "Own-initiative line", "Road bandit", "chat-medium-it", "0.52 s", "0.97 s", '<span class="pill faint">Not shown</span>'),
        ("4:03:15", "Prepared lines", "Prepared-line batch", '<span class="ink-faint">—</span>', "chat-fast", "0.88 s", "6.80 s", f'<span class="pill moss">{ic("check", "ic sm")}Pool filled</span>'),
    ]
    rows = []
    for t, slot, purpose, speaker, model_, first, total, outcome in calls:
        rows.append(f'<tr><td class="num">{t}</td><td>{slot}</td><td class="sub">{purpose}</td><td>{speaker}</td>'
                    f'<td class="sub mono">{model_}</td><td class="num r">{first}</td><td class="num r">{total}</td><td>{outcome}</td></tr>')
    calls_body = f"""<div class="scroll-x"><table class="tbl">
<thead><tr><th scope="col" class="sorted">Time ↓</th><th scope="col">Slot</th><th scope="col">Purpose</th><th scope="col">Speaker</th>
<th scope="col">Model</th><th scope="col" class="r">First word</th><th scope="col" class="r">Total</th><th scope="col">Outcome</th></tr></thead>
<tbody>{"".join(rows)}</tbody></table></div>
<div class="card-foot"><span>Own-initiative calls this hour: <span class="mono ink-hi">14</span> of <span class="mono">240</span> · this session <span class="mono">41.6k</span> tokens in, <span class="mono">3.9k</span> out</span>
<span class="note">{ic("info", "ic sm")}Text shows only for lines you were shown.</span></div>"""
    model_calls = card("Model calls", calls_body, desc="The last 20 calls, newest first", collapsible=False, cls="gc gc-calls",
                       tools=f'<a class="open iconbtn" href="#" data-mock="absent" title="Open AI › Requests">{ic("out", "ic sm")}</a>')

    evs = [
        ("4:12", "Idrel speaks up", "name", "speech"),
        ("4:11", "You walk away from Idrel", "name", "speech"),
        ("4:08", "You speak to Idrel", "name", "speech"),
        ("4:06", "A road bandit and wolves start fighting", "rot", "fights"),
        ("4:06", "The road bandit takes a heavy blow", "rot", "fights"),
        ("3:58", "You come near an unmarked soldier", "gold", "meetings"),
        ("3:40", "You enter Mistfold Downs", "moss", "world"),
    ]
    ev_rows = "".join(f'<li><span class="time">{t}</span><span class="what">{w}</span><span class="pill {c}">{f}</span></li>' for t, w, c, f in evs)
    recent = card("Recent events", f'<ul class="evs">{ev_rows}</ul>', desc="What you saw happen, in our words",
                  collapsible=False, cls="gc gc-events",
                  tools=f'<a class="open iconbtn" href="#" data-mock="absent" title="Open History › Events">{ic("out", "ic sm")}</a>')

    body = (header("Home", "Is EldenNet working, who is near you, and what was just said and asked of the model.", actions)
            + setup + f'<div class="grid">{status}{nearby}{happening}{problems}{lines}{recent}{model_calls}</div>')
    helpd = help_dialog("Home", "The dashboard: whether EldenNet is working, who is near you, what was just said, and what was asked of your model.",
                        ["Read Status first: one word for the whole of EldenNet, then one dot for each model job.",
                         "Setup holds your model, voice and microphone. It closes itself once the self-test has passed.",
                         "Select a card's arrow to open its page.",
                         "Customise shows, hides and orders the cards. The order is kept between sessions."],
                        [("System › Status", "#"), ("AI › Providers", "ai-providers.html"), ("History › Events", "#")])
    page("index.html", "Home", "home", body, helpd, live=True)


# ---------------------------------------------------------------- AI › Providers (IA v2 §3.1)

def providers():
    actions = (f'<button class="btn secondary">{ic("search", "ic sm")}Find local servers</button>'
               f'<button class="btn primary">{ic("plus", "ic sm")}Add provider</button>')
    advice = (f'<div class="card"><div class="prov-head"><span class="monob">{ic("lozenge")}</span><div class="grow">'
              '<p class="t">Cloud budget <span class="pill fill-gold">Advised</span></p>'
              '<p class="s">Suits most players: quick replies on any graphics card, paid per use with your own key. '
              'Your graphics card has 8 GB of memory, which is too little for conversation on a model of your own.</p></div>'
              '<a class="btn primary" href="ai-models.html">Use this</a></div></div>')

    def provider(name, kind, used, icon, health, address, keyhtml, conc, conc_default, removable=False):
        lid1, lid2, lid3, lid4 = uid("f"), uid("f"), uid("f"), uid("f")
        adv = uid("a")
        rm = f'<button class="btn danger sm">{ic("close", "ic sm")}Remove</button>' if removable else ""
        return f"""<section class="card" aria-labelledby="{lid1}-h">
<div class="prov-head"><span class="monob">{ic(icon)}</span><div class="grow"><h2 class="t" id="{lid1}-h">{name}</h2><p class="s">{kind} · used by {used}</p></div>{health}{rm}</div>
<div class="card-body"><div class="fgrid">
<div><label class="flabel" for="{lid1}">Address</label><div class="field"><input id="{lid1}" type="text" value="{address}" spellcheck="false"></div>
<p class="fhelp">Shown on this page only; never in a log, a report or a title.</p></div>
<div><label class="flabel" for="{lid2}">Key</label>{keyhtml.replace("KEYID", lid2)}</div>
<div><span class="flabel" id="{lid3}">Requests at once</span>{select(["1", "2", "4", "8"], conc, lid=lid3)}<p class="fhelp">How many calls may wait on this provider at once. <span class="dflt">Default: {conc_default}.</span></p></div>
<div><span class="flabel" id="{lid4}">JSON answers</span>{seg(lid4, ["Off", "Object", "Schema"], "Schema")}<p class="fhelp">Asks the model for structured answers where the provider supports them. <span class="dflt">Default: Schema.</span></p></div>
</div>
<button class="adv-toggle" aria-expanded="false" aria-controls="{adv}">{ic("chev", "ic sm")}Advanced</button>
<div class="adv" id="{adv}" hidden><div class="fgrid"><div><span class="flabel">Extra header names</span><div class="field"><input type="text" value="" placeholder="None" aria-label="Extra header names"></div>
<p class="fhelp">Their values are write-only, like keys.</p></div></div></div>
</div></section>"""

    # The key field is a text input masked by the stylesheet, outside any form, never type=password (DESIGN §4.6.2, §7.4)
    key_attrs = 'type="text" class="masked" autocomplete="off" spellcheck="false" autocapitalize="off" autocorrect="off"'
    key_set = (f'<div class="field"><input id="KEYID" {key_attrs} placeholder="Set; a key is never shown again">'
               f'<span class="keychip set">{ic("check", "ic sm")}Set</span></div>'
               f'<div class="toolbar"><button class="btn secondary sm">{ic("key", "ic sm")}Set key</button><button class="btn quiet sm">Clear key</button></div>')
    key_none = (f'<div class="field"><input id="KEYID" {key_attrs} placeholder="Not needed for this server">'
                f'<span class="keychip none">Not needed</span></div>'
                f'<div class="toolbar"><button class="btn secondary sm">{ic("key", "ic sm")}Set key</button></div>')
    p1 = provider("OpenRouter", "Cloud · OpenAI-compatible", "Conversation, Quick lines, Prepared lines and Self-test", "cloud",
                  '<span class="health c-moss"><span class="dot moss"></span>Healthy</span>', "https://openrouter.ai/api/v1", key_set, "2", "2")
    p2 = provider("Model server on this computer", "Local · Ollama · found on port 11434", "Memory", "computer",
                  f'<span class="health c-ember">{ic("warn", "ic sm")}Cooling down · 40 s</span>', "http://localhost:11434/v1", key_none, "1", "1",
                  removable=True)

    found_rows = [
        ("LM Studio", "1234", '<span class="ink-faint">Not found</span>', "", ""),
        ("Ollama", "11434", f'<span class="c-moss">{ic("check", "ic sm")} Found</span>', "3", '<span class="tag gold">In use</span>'),
        ("llama.cpp server", "9931, then 8080", '<span class="ink-faint">Not found</span>', "", ""),
        ("KoboldCpp", "5001", '<span class="ink-faint">Not found</span>', "", ""),
    ]
    fr = "".join(f'<tr><td>{n}</td><td class="num">{p}</td><td>{f}</td><td class="num r">{m}</td><td class="r">{a}</td></tr>' for n, p, f, m, a in found_rows)
    found = card("Local servers found", f'<table class="tbl"><thead><tr><th scope="col">Server</th><th scope="col">Port</th><th scope="col">Found</th>'
                 f'<th scope="col" class="r">Models</th><th scope="col" class="r"><span class="vh">Use</span></th></tr></thead><tbody>{fr}</tbody></table>',
                 desc="EldenNet looks for four known servers on this computer, only when you ask. Searched at the title screen today.", icon="computer")

    health_rows = [
        ("OpenRouter", '<span class="c-moss"><span class="dot moss"></span> Healthy</span>', "4:12", "0", "Conversation, Quick lines, Prepared lines, Self-test"),
        ("Model server on this computer", f'<span class="c-ember">{ic("warn", "ic sm")} Cooling down · 40 s</span><br><span class="sub">Timed out</span>', "4:01", "3 timed out", "Memory (on its fallback meanwhile)"),
    ]
    hr = "".join(f'<tr><td class="name">{n}</td><td>{s}</td><td class="num">{l}</td><td class="sub">{e}</td><td class="sub">{u}</td></tr>' for n, s, l, e, u in health_rows)
    health = card("Health", f'<table class="tbl"><thead><tr><th scope="col">Provider</th><th scope="col">State</th><th scope="col">Last success</th>'
                  f'<th scope="col">Errors this session</th><th scope="col">Slots</th></tr></thead><tbody>{hr}</tbody></table>'
                  f'<div class="notes"><p class="note">{ic("info", "ic sm")}Three failures in a row cool a provider down for 60 s; its slots use their fallback model meanwhile.</p>'
                  f'<p class="note">{ic("info", "ic sm")}A refused key turns its provider off until you set a new one.</p></div>', collapsible=False)

    panel1 = f'<div class="stack">{advice}{p1}{p2}{found}</div>'
    body = (header("Providers", "Where EldenNet’s model calls go: a cloud service with your own key, or a model server on this computer.", actions)
            + tabs("Providers", [("providers", "Providers", 2), ("health", "Health", None)], {"providers": panel1, "health": health})
            + related([("Models", "Which model does each job, and the preset that sets them all.", "ai-models.html"),
                       ("Self-test", "Is the model fast and well formed enough for conversation?", "#"),
                       ("Docs › What is sent to your provider", "What leaves your computer, and when.", "#")]))
    helpd = help_dialog("Providers", "Where EldenNet sends its model calls. A cloud provider uses a key of your own; a model server on this computer needs none.",
                        ["Choose the advised preset, or add a provider of your own.",
                         "Set a key: it goes in once and is never shown again, here or anywhere.",
                         "Find local servers looks for known servers on this computer, only when you ask.",
                         "Health shows any provider that is cooling down, and why, in words."],
                        [("AI › Models", "ai-models.html"), ("AI › Self-test", "#"), ("Docs", "#")])
    page("ai-providers.html", "Providers", "ai.providers", body, helpd)


# ---------------------------------------------------------------- AI › Models (IA v2 §3.2)

def models():
    lid = uid("l")
    actions = (f'<span class="vh" id="{lid}">Preset</span>{select(["Cloud budget", "Local 12 GB", "Local 16 GB+", "Custom"], "Cloud budget", lid=lid)}'
               f'<button class="btn primary">Apply</button><button class="btn secondary">{ic("reset", "ic sm")}Reset to preset</button>')
    advice = card("Advice for this machine", f"""<p class="prose">Cloud budget: your graphics card has 8 GB of memory, which suits memory work at rest but is too slow for conversation.</p>
<div class="notes"><p class="note">{ic("info", "ic sm")}The advice changed in this version: Prepared lines now use a faster model.</p>
<p class="note c-ember">{ic("warn", "ic sm")}Free models are not advised for prepared lines or for NPCs speaking up on their own; the free presets turn that off.</p></div>""",
                  icon="lozenge")
    differs = f'<div class="banner glint">{ic("info")}<span class="grow">One slot differs from Cloud budget: Memory runs on the model server on this computer.</span></div>'

    def slot(title, sid, desc, prov, model_, fb, health, budget, extra="", differs_=False):
        l1, l2, l3 = uid("s"), uid("s"), uid("s")
        tag = '<span class="tag gold" title="Not the preset’s choice">Differs from preset</span>' if differs_ else ""
        provs = ["OpenRouter", "Model server on this computer"]
        mlist = ["chat-medium-it", "chat-large-it", "chat-fast", "chat-small-q4", "chat-mid-q4"]
        return f"""<section class="card" aria-labelledby="{l1}-h">
<div class="card-head"><div class="titles"><h2 class="t-title ink-hi" id="{l1}-h">{title} <span class="slotid">{sid}</span> {tag}</h2><p class="card-desc">{desc}</p></div>{health}</div>
<div class="card-body"><div class="fgrid three">
<div><span class="flabel" id="{l1}">Provider</span>{select(provs, prov, lid=l1)}</div>
<div><span class="flabel" id="{l2}">Model</span>{select(mlist, model_, lid=l2)}</div>
<div><span class="flabel" id="{l3}">Fallback model</span>{select(["None"] + mlist, fb, lid=l3)}</div>
</div>{extra}
<p class="budget">{budget} · <span>the same in every preset</span></p></div></section>"""

    ok = '<span class="health c-moss"><span class="dot moss"></span>Healthy</span>'
    cool = f'<span class="health c-ember">{ic("warn", "ic sm")}Cooling · 40 s</span>'
    pool_extra = (f'<div class="srows adv">{slider("Prepared-line batches per session", "pool-jobs", 60, 0, 200, 5, "", "How many batches EldenNet may prepare in one session. 0 turns prepared lines off.", "60", zero="Off")}'
                  f'{srow("Prepared lines now", "Ready for the 3 types you have met · 3 of 60 batches used this session · pauses while you fight.", ctl="<button class=" + chr(34) + "btn secondary sm" + chr(34) + ">Prepare lines now</button>")}'
                  f'<p class="note">{ic("info", "ic sm")}Preparing runs only at rest or at the title screen.</p></div>')
    slots = [
        slot("Conversation", "dialogue", "Replies to you, and named NPCs speaking up.", "OpenRouter", "chat-medium-it", "chat-large-it", ok,
             "Up to <b>140</b> tokens · temperature <b>0.8</b> · first word within <b>6 s</b> · whole reply within <b>20 s</b> · <b>no</b> retries"),
        slot("Quick lines", "bark_live", "Call-out replies, and other NPCs speaking up on their own.", "OpenRouter", "chat-medium-it", "None", ok,
             "Up to <b>32</b> tokens · temperature <b>0.9</b> · first word within <b>1.5 s</b> · whole line within <b>3 s</b> · <b>no</b> retries"),
        slot("Prepared lines", "bark_pool", "Batches of short lines prepared ahead for each type of NPC.", "OpenRouter", "chat-fast", "chat-medium-it", ok,
             "Up to <b>600</b> tokens · temperature <b>1.0</b> · first word within <b>15 s</b> · whole batch within <b>60 s</b> · <b>1</b> retry", pool_extra),
        slot("Memory", "memory", "Writes each named NPC’s journal after a conversation.", "Model server on this computer", "chat-small-q4", "chat-large-it", cool,
             "Up to <b>500</b> tokens · temperature <b>0.3</b> · first word within <b>20 s</b> · whole journal within <b>60 s</b> · <b>2</b> retries", differs_=True),
        slot("Self-test", "selftest", "The short test that checks speed and well-formed answers.", "OpenRouter", "chat-medium-it", "None", ok,
             "Up to <b>32</b> tokens · temperature <b>0</b> · first word within <b>10 s</b> · whole answer within <b>20 s</b> · <b>no</b> retries"),
    ]
    decisions = (f'<section class="card dimmed" aria-labelledby="dec-h"><div class="card-head"><div class="titles">'
                 f'<h2 class="t-title ink-mute" id="dec-h">Decisions <span class="slotid">meta</span> <span class="ver" title="Work in progress: a later version">WIP<span class="vh">: work in progress, a later version</span></span></h2>'
                 '<p class="card-desc">Short typed decisions, such as who speaks next, come with a later version. Nothing to set now.</p></div></div></section>')
    body = (header("Models", "Which model does each job, and the preset that sets them all.", actions)
            + f'<div class="stack">{advice}{differs}<div class="slots">{"".join(slots)}{decisions}</div></div>'
            + related([("Providers", "Where the calls go, and the keys they use.", "ai-providers.html"),
                       ("Self-test", "Check speed and well-formed answers.", "#"),
                       ("Requests", "Every model call this session and what it cost.", "#"),
                       ("Behaviour › Combat", "Where prepared lines are used.", "#")]))
    helpd = help_dialog("Models", "Each job EldenNet gives a model has its own slot: a provider, a model and a fallback. A preset sets them all at once.",
                        ["Pick a preset and Apply it, or set a slot by hand.",
                         "Each slot’s fallback is used while its provider cools down.",
                         "Budgets (tokens, time limits, retries) are fixed: the same in every preset.",
                         "Prepared lines can be made ahead at rest, so fights need no live calls."],
                        [("AI › Providers", "ai-providers.html"), ("AI › Requests", "#")])
    page("ai-models.html", "Models", "ai.models", body, helpd)


# ---------------------------------------------------------------- Characters › NPC types (IA v2 §5.2)

def R(name, sub, tags, near, sp, un, ba, kind, changed=0, inert=False, selected=False, kindrow=False, npc=False, link=None):
    return dict(name=name, sub=sub, tags=tags, near=near, sp=sp, un=un, ba=ba, kind=kind, changed=changed,
                inert=inert, selected=selected, kindrow=kindrow, npc=npc, link=link)


TYPE_ROWS = [
    ("Named", "named", [
        R("Named, every NPC", "The kind’s own row: every named NPC inherits from it", [], 1, "on", "on", "on", "named", kindrow=True),
        R("Idrel", "Ferry-warden · met 3 times", ["Named", "Human", "Own voice"], 1, "on", "on", "on", "named", changed=1, npc=True, link="characters-named.html"),
        R("Wenna", "Met once", ["Named", "Human", "Own voice"], 0, "on", "on", "on", "named", npc=True),
        R("The hooded pilgrim", "Not met", ["Named"], 0, "on", "on", "on", "named", npc=True),
        R("The masked duellist", "Not met", ["Named"], 0, "on", "on", "on", "named", npc=True),
        R("The old cartographer", "Not met", ["Named"], 0, "on", "on", "on", "named", npc=True),
        R("The quiet smith", "Not met", ["Named"], 0, "on", "on", "on", "named", npc=True),
        R("Old Tamsin", "Seen once", ["Named", "Human"], 0, "on", "on", "on", "named", inert=True, npc=True),
    ]),
    ("Merchant", "merchant", [
        R("Merchant, every type", "The kind’s own row: the types below inherit from it", [], 1, "on", "on", "on", "merchant", inert=True, kindrow=True),
        R("Wandering merchant", "", ["Merchant", "Human"], 1, "on", "on", "on", "merchant", inert=True),
    ]),
    ("Humanoid", "humanoid", [
        R("Humanoid, every type", "The kind’s own row: the types below inherit from it", [], 1, "on", "on", "on", "humanoid", kindrow=True),
        R("Keep soldier", "Written and prepared lines", ["Humanoid", "Human", "~Keep garrison"], 0, "on", "some", "on", "humanoid", changed=2, selected=True),
        R("Road bandit", "Written and prepared lines", ["Humanoid", "Human", "Outlaw"], 1, "on", "on", "on", "humanoid"),
        R("Unmarked soldier", "Soldiers EldenNet cannot place", ["Humanoid"], 0, "on", "on", "on", "humanoid"),
    ]),
    ("Creature", "creature", [
        R("Creature, every type", "They never speak in the game", [], 1, "off", "off", "on", "creature", kindrow=True),
        R("Wolves", "Battle awareness only", ["Creature", "Beast", "Pack"], 1, "off", "off", "on", "creature"),
        R("Carrion birds", "Battle awareness only", ["Creature", "~Bird"], 0, "off", "off", "on", "creature"),
        R("Lantern moths", "Battle awareness only", ["Creature", "Insect"], 0, "off", "off", "on", "creature"),
    ]),
    ("Boss", "boss", [R("Great foe, while its bar shows", "", ["Boss", "Live"], 0, "on", "off", "on", "boss", inert=True)]),
    ("Invader", "invader", [R("Invader", "", ["Invader", "Live"], 0, "on", "off", "on", "invader", inert=True)]),
    ("Summon", "summon", [
        R("Summon, every type", "The kind’s own row: the types below inherit from it, each from its own default", [], 0, "some", "off", "on", "summon", inert=True, kindrow=True),
        R("Spirit summon", "They never speak in the game", ["Summon", "Spirit"], 0, "off", "off", "on", "summon"),
        R("Summoned ally", "", ["Summon", "Ally"], 0, "on", "off", "off", "summon", changed=1, inert=True),
    ]),
    ("Mount", "mount", [R("Steed", "Never speaks", ["Mount"], 1, "off", "off", "on", "mount")]),
]


def arow(name, inh, value="Inherit", changed=False, badge=None, locked=False, reset=False):
    lid = uid("a")
    cls = "arow" + (" changed" if changed else "") + (" locked" if locked else "")
    if locked:
        ctl = avail("later", "Later version")
    else:
        rs = f'<button class="iconbtn reset" title="Reset to inherited" aria-label="Reset {name}">{ic("reset", "ic sm")}</button>' if reset else ""
        ctl = rs + tri(lid, value)
    b = badge or ""
    return f'<div class="{cls}"><span class="name" id="{lid}">{name}</span><span class="inh">{inh}</span><div class="ctl">{ctl}</div>{b}</div>'


def types_page():
    rows = []
    for group, gk, items in TYPE_ROWS:
        rows.append(f'<tr class="group" data-kind="{gk}"><td colspan="6">{group}</td></tr>')
        for r in items:
            tag_html = ""
            for t in r["tags"]:
                if t.startswith("~"):
                    tag_html += f'<span class="tag hollow" title="Not yet confirmed on your build">{t[1:]}</span>'
                else:
                    tag_html += f'<span class="tag">{t}</span>'
            if r["inert"]:
                tag_html += '<span class="tag glint" title="No voice source for this type in v1; its settings are kept for later versions">No lines in this version</span>'
            sub_html = f'<div class="sub">{r["sub"]}</div>' if r["sub"] else ""
            name_cls = "name npc" if r["npc"] else "name"
            name_html = (f'<a class="{name_cls}" href="{r["link"]}">{r["name"]}</a>' if r["link"] else f'<span class="{name_cls}">{r["name"]}</span>')
            c = r["changed"]
            ch = (f'<span class="tag gold" title="{c} allowance{"s" if c != 1 else ""} changed from the inherited value">◆ {c}<span class="vh"> changed</span></span>') if c else ""
            attrs = ' aria-selected="true"' if r["selected"] else ""
            cls = f' class="kindrow" id="kind-{r["kind"]}"' if r["kindrow"] else ""
            tb = f'<div class="tags">{tag_html}</div>' if tag_html else ""
            rows.append(f'<tr{cls}{attrs} data-select data-kind="{r["kind"]}"><td><div>{name_html}</div>{sub_html}{tb}</td>'
                        f'<td class="num r">{r["near"]}</td><td class="c">{mark(r["sp"])}</td><td class="c">{mark(r["un"])}</td>'
                        f'<td class="c">{mark(r["ba"])}</td><td class="r">{ch}</td></tr>')
    rows.append(f'<tr class="locked" data-kind="unknown"><td colspan="6"><span class="name ink-mute">{ic("lock", "ic sm")} Unknown</span>'
                '<div class="sub">Characters EldenNet hasn’t identified stay silent. This row cannot be changed.</div></td></tr>')

    chips = [("All", "all", True), ("Named", "named", False), ("Merchant", "merchant", False), ("Humanoid", "humanoid", False),
             ("Creature", "creature", False), ("Boss", "boss", False), ("Invader", "invader", False), ("Summon", "summon", False), ("Mount", "mount", False)]
    chip_html = "".join(f'<button class="chip" aria-pressed="{"true" if on else "false"}" data-kind-filter="{k}">{n}</button>' for n, k, on in chips)
    toolbar = (f'<div class="filters"><label class="field">{ic("search", "ic sm")}<span class="vh">Search types and tags</span>'
               '<input type="search" placeholder="Search types and tags" autocomplete="off" spellcheck="false"></label>'
               f'<button class="btn secondary sm">{ic("reset", "ic sm")}Reset page</button>'
               f'<div class="chips" data-target="#types" role="group" aria-label="Kind">{chip_html}</div></div>')
    master = f"""<div class="card flush"><table class="tbl" id="types" aria-label="NPC types">
<thead><tr><th scope="col">Type</th><th scope="col" class="r">Near</th><th scope="col" class="c">Speech</th><th scope="col" class="c">Unasked</th><th scope="col" class="c">Battle</th><th scope="col" class="r"><span class="vh">Changed</span></th></tr></thead>
<tbody>{"".join(rows)}</tbody></table></div>
<div class="legend notes" aria-label="Legend"><span>{mark("on")} on</span><span>{mark("some")} some on</span><span>{mark("off")} off</span><span><span class="tag hollow">Tag</span> not yet confirmed on your build</span></div>"""

    detail = f"""<aside class="card detail" aria-label="Allowances of the chosen type">
<div class="detail-head">{monogram("K")}<div class="grow"><h2 class="t-title ink-hi">Keep soldier</h2><p class="sub">Humanoid · a type · none near now</p>
<div class="tags"><span class="tag">Humanoid</span><span class="tag">Human</span><span class="tag hollow" title="Not yet confirmed on your build">Keep garrison</span></div>
<p class="prose notes">Soldiers of a keep’s garrison, in its colours, who hold its walls and gate.</p></div></div>
<div class="dgroup"><span class="t-eyebrow">Speech</span>
{arow("Dialogue", f"On, from {src('humanoid', '')}. Off turns off every reason to speak.")}
{arow("Voice", f"On, from {src('humanoid', '')}", badge=avail("v15", "Takes effect in v1.5"))}
</div>
<div class="dgroup"><span class="t-eyebrow">Unasked</span>
{arow("Greets you", f"On, from {src('humanoid', '')} · never while hostile")}
{arow("Talks with other NPCs", f"On, from {src('humanoid', '')}")}
{arow("Speaks up on its own", f"Inherited: on, from {src('humanoid', '')}", value="Off", changed=True, reset=True)}
</div>
<div class="dgroup"><span class="t-eyebrow">Reactions</span>
{arow("Idle remarks", f"Inherited: on, from {src('humanoid', '')}", value="Off", changed=True, reset=True)}
{arow("Status reactions", f"On, from {src('humanoid', '')}", badge=avail("wait", "Waits on a game check"))}
</div>
<div class="dgroup"><span class="t-eyebrow">Battle</span>
{arow("Combat lines", f"On, from {src('humanoid', '')}")}
{arow("Boss fights", f"On, from {src('humanoid', '')}", badge=avail("wait", "Waits on a game check"))}
</div>
<div class="dgroup"><span class="t-eyebrow">Awareness</span>
{arow("Knows its own recent lines", f"On, from {src('humanoid', '')}")}
{arow("Notices statuses", f"On, from {src('humanoid', '')}", badge=avail("wait", "Waits on a game check"))}
</div>
<div class="dgroup"><span class="t-eyebrow">Acts</span>
{arow("Acts", "", locked=True)}
{arow("Gestures", "", locked=True)}
</div>
<div class="notes toolbar"><button class="btn secondary sm">{ic("reset", "ic sm")}Reset this type</button><a class="btn quiet sm" href="#" data-mock="absent">Show in Combat</a></div>
</aside>"""
    banner = f'<div class="banner">{ic("info")}<span class="grow">Allowances only narrow what a type may do. Safety and spoiler rules always apply. Changes apply as you make them.</span></div>'
    body = (header("NPC types", "Every type of NPC, labelled and tagged, and what each may do. The home of every allowance.")
            + f'<div class="stack">{banner}{toolbar}<div class="md"><div>{master}</div>{detail}</div></div>'
            + related([("Named NPCs", "The named characters EldenNet voices.", "characters-named.html"),
                       ("Reactions", "Every kind of event NPCs react to, with its cooldown.", "#"),
                       ("Combat", "Combat lines for every combatant.", "#")]))
    helpd = help_dialog("NPC types", "Every NPC EldenNet may voice belongs to a kind and a type. Each type inherits what it may do from its kind, and you can narrow it here.",
                        ["Pick a row to see its allowances at the right.",
                         "Each allowance is Inherit, On or Off; the inherited value and where it comes from are shown under it.",
                         "A gold lozenge marks a change; its reset button returns it to inherited.",
                         "Every type is listed from the start, described as you would see one on a first meeting."],
                        [("Named NPCs", "characters-named.html"), ("Reactions", "#"), ("Combat", "#")])
    page("characters-types.html", "NPC types", "characters.types", body, helpd)


# ---------------------------------------------------------------- Characters › Playthrough (G5, G8; DESIGN §4.6.4)

def playthrough_page():
    stat = tiles([("2", "People met"), ("2", "Stories seen"), ("1", "Spoken with this session")])
    world = card("World now", '<p class="prose">The ferry road is open again after the spring floods.</p>'
                 f'<p class="note notes">{ic("info", "ic sm")}<span>The latest change that holds in your world. Every change is on History › World.</span></p>',
                 icon="world", collapsible=False,
                 tools=f'<a class="open iconbtn" href="#" data-mock="absent" title="Open History › World">{ic("out", "ic sm")}</a>')
    l1, l2, l3 = uid("l"), uid("l"), uid("l")
    filters = (f'<div class="filters"><label class="field">{ic("search", "ic sm")}<span class="vh">Search names</span>'
               '<input type="search" placeholder="Search names" autocomplete="off" spellcheck="false"></label>'
               f'<span class="lbl" id="{l1}">Status</span>{select(["Everyone met", "Here now", "Alive when last met", "Away", "Fell"], "Everyone met", lid=l1)}'
               f'<span class="lbl" id="{l2}">Region</span>{select(["All regions", "Mistfold Downs"], "All regions", lid=l2)}'
               f'<span class="lbl" id="{l3}">Sort</span>{select(["Last met", "First met", "Name"], "Last met", lid=l3)}</div>')
    people = [
        ("Idrel", '<span class="c-moss">Here now</span>', "The ferry crossing · now",
         "You helped keep the ferry running after the spring floods.", "Nothing new", True),
        ("Wenna", "Alive when last met", "The mill on the downs · 1 h 20 min ago",
         "She asked you to listen for word of her brother on the north road.", "The ferry road opened again", False),
    ]
    tr = []
    for name, status, last, story, note, sel in people:
        attrs = ' aria-selected="true"' if sel else ""
        # Last met sits under the status word, and "since you last spoke" shows only on wide screens (the chosen
        # person's notes are in the detail pane), so the table fits beside the detail pane at 1280 × 720.
        tr.append(f'<tr{attrs} data-select><td class="name npc nowrap">{name}</td><td><span class="nowrap">{status}</span><div class="sub">{last}</div></td>'
                  f'<td class="content">{story}</td><td class="sub hide-narrow">{note}</td></tr>')
    master = ('<div class="card flush"><div class="scroll-x"><table class="tbl" aria-label="People met on this journey"><thead><tr>'
              '<th scope="col">Name</th><th scope="col">Status · last met</th>'
              '<th scope="col">Their story, as you last saw it</th><th scope="col" class="hide-narrow">Since you last spoke</th></tr></thead>'
              f'<tbody>{"".join(tr)}</tbody></table></div></div>'
              f'<p class="note notes">{ic("info", "ic sm")}<span>Only people you have met on this journey, and only what you have seen. What lies ahead is never shown.</span></p>')
    detail = f"""<aside class="card detail" aria-label="Idrel on this journey">
<div class="detail-head">{monogram("I")}<div class="grow"><h2 class="t-title c-name">Idrel</h2><p class="sub">Ferry-warden · met 3 times · first met at the ferry crossing</p></div></div>
<div class="dgroup"><span class="t-eyebrow">Status</span>
<p class="statewords"><span class="c-moss">Here now</span><span class="sep">·</span><span>calm</span><span class="sep">·</span><span>thinks well of you</span></p>
<dl class="kv notes"><dt>Last met</dt><dd>the ferry crossing · now</dd><dt>Last spoke</dt><dd class="mono">4:12</dd></dl></div>
<div class="dgroup"><span class="t-eyebrow">Their story</span>
<p class="prose">You helped Idrel keep the ferry running after the spring floods. Wenna had sent you to him.</p>
<p class="sub">As you saw it at 4:12</p></div>
<div class="dgroup"><span class="t-eyebrow">Related</span>
<ul class="heard"><li><span class="wl">Wenna asked you to listen for word of her brother on the north road.</span><a class="pill gold" href="#" data-mock="absent" title="Wenna’s row">Wenna</a></li></ul></div>
<div class="dgroup"><span class="t-eyebrow">Has heard of</span>
<ul class="heard"><li><span class="wl">The ferry road is open again after the spring floods.</span><span class="pill moss">certain</span></li>
<li><span class="wl">Wolves have come down from the high moor.</span><span class="pill gold">likely</span></li></ul></div>
<div class="dgroup"><span class="t-eyebrow">Since you last spoke</span>
<p class="note">{ic("info", "ic sm")}<span>Nothing new since you last spoke.</span></p></div>
<div class="notes toolbar"><a class="btn secondary sm" href="characters-named.html">General profile{ic("out", "ic sm")}</a>
<a class="btn secondary sm" href="history-memories.html">Memories{ic("out", "ic sm")}</a></div>
</aside>"""
    body = (header("Playthrough", "The people you have met on this journey, how they are now, and where their stories stand as you have seen them.")
            + f'<div class="stack">{stat}{world}{filters}<div class="md"><div>{master}</div>{detail}</div></div>'
            + related([("Named NPCs", "Who each character appears to be, and what each may do.", "characters-named.html"),
                       ("Memories", "What each named NPC remembers of you.", "history-memories.html"),
                       ("World", "Every change that holds in your world.", "#")]))
    helpd = help_dialog("Playthrough", "The people you have met on this journey and how they are now. Only what you have seen is shown: a story line is the stage you last saw them at, never what comes next.",
                        ["Pick a person to see their status, their story as you last saw it, related stories and what has changed since you last spoke.",
                         "Status comes from what you saw: here now, alive when last met, away when you last passed, or fell.",
                         "Choose an earlier journey in the top bar to look back at it, read-only."],
                        [("Named NPCs", "characters-named.html"), ("Memories", "history-memories.html"), ("World", "#")])
    page("characters-playthrough.html", "Playthrough", "characters.playthrough", body, helpd, journey=True)


# ---------------------------------------------------------------- Characters › Named NPCs (IA v2 §5.1)

def named_page():
    def entry(name, sub, tag="", cur=False, unmet=False):
        c = ' aria-current="true"' if cur else ""
        n = "n unmet" if unmet else "n"
        return f'<li><a href="#"{"" if cur else " data-mock=" + chr(34) + "absent" + chr(34)}{c}><span class="{n}">{name}</span><span class="s">{sub}</span>{tag}</a></li>'
    roster = (f'<div class="card roster" aria-label="Named NPCs"><label class="field">{ic("search", "ic sm")}<span class="vh">Search names</span>'
              '<input type="search" placeholder="Search names" autocomplete="off" spellcheck="false"></label>'
              '<div class="chips" data-single role="group" aria-label="Show"><button class="chip" aria-pressed="false">Met</button>'
              '<button class="chip" aria-pressed="false">Nearby</button><button class="chip" aria-pressed="true">All</button></div>'
              '<p class="group">Met</p><ul>'
              + entry("Idrel", "Ferry-warden · here now", '<span class="tag moss">Here</span>', cur=True)
              + entry("Wenna", "Met once")
              + '</ul><p class="group">Not met</p><ul>'
              + entry("The hooded pilgrim", "A traveller in a deep hood", unmet=True)
              + entry("The masked duellist", "A fighter behind a mask", unmet=True)
              + entry("The old cartographer", "Draws maps for coin", unmet=True)
              + entry("The quiet smith", "Works a forge in silence", unmet=True)
              + '</ul><p class="group">Seen</p><ul>'
              + entry("Old Tamsin", "Seen once", '<span class="tag glint" title="No voice source for this NPC in v1">No lines</span>')
              + "</ul></div>")

    who = card("General profile", '<p class="prose">Keeps the rope ferry across the slow river below the downs, and has kept it longer than most of the bridges have stood. '
               'Wary of strangers who come after dark; generous with anyone who lends a hand with the rope.</p>',
               desc="What a first meeting shows. The same in every playthrough.", collapsible=False)
    news = card("Their way with news", '<p class="prose">Passes on what they hear to people they trust, and asks after the road in return.</p>'
                '<p class="tags"><span class="pill gold">Shares with friends</span></p>', collapsible=False)
    onj = card("On this journey", '<p class="statewords"><span class="c-moss">Here now</span><span class="sep">·</span>'
               '<span>thinks well of you</span><span class="sep">·</span><span>met 3 times</span></p>'
               f'<div class="toolbar notes"><a class="btn secondary sm" href="characters-playthrough.html">Open in Playthrough{ic("out", "ic sm")}</a></div>',
               collapsible=False)
    profile = f'<div class="two-col">{who}{news}{onj}</div>'

    allow = f"""<div class="card"><div class="dgroup"><span class="t-eyebrow">Speech</span>
{arow("Dialogue", f"On, from {src('named')}")}{arow("Voice", f"On, from {src('named')}", badge=avail("v15", "Takes effect in v1.5"))}</div>
<div class="dgroup"><span class="t-eyebrow">Unasked</span>
{arow("Greets you", f"On, from {src('named')}")}{arow("Raises their own business", f"On, from {src('named')}")}{arow("Passes on news", f"On, from {src('named')}")}
{arow("Speaks up on their own", f"Inherited: on, from {src('named')}", value="Off", changed=True, reset=True)}</div>
<p class="note notes">{ic("info", "ic sm")}<span>These rows are shortcuts: {lives("Characters › NPC types", "characters-types.html")}</span></p></div>"""
    # The slot's per-reaction allowances, allow."persona:…".reactions.<id> (EldenNet DESIGN §7.6); cooldowns are global
    reac = f"""<div class="card"><div class="dgroup"><span class="t-eyebrow">Reactions</span>
{arow("Idle remarks", f"On, from {src('named')}")}{arow("Saw a kill", f"On, from {src('named')}")}</div>
<p class="note notes">{ic("info", "ic sm")}<span>These rows are shortcuts: {lives("Characters › NPC types", "characters-types.html")}. Cooldowns are the same for everyone and live in Behaviour › Reactions.</span></p></div>"""
    head = (f'<div class="npc-head">{monogram("I", "monogram lg")}<div class="grow"><h2 class="t-display-lg">Idrel</h2>'
            '<p class="sub t-ui">Ferry-warden</p></div>'
            f'<a class="btn secondary sm" href="characters-playthrough.html">Playthrough{ic("out", "ic sm")}</a>'
            f'<a class="btn secondary sm" href="history-memories.html">Memories{ic("out", "ic sm")}</a>'
            f'<a class="btn secondary sm" href="#" data-mock="absent">Conversations{ic("out", "ic sm")}</a></div>')
    detail = (f'<div class="stack">{head}'
              + tabs("Idrel", [("profile", "Profile", None), ("allowances", "Allowances", None), ("reactions", "Reactions", 2)],
                     {"profile": profile, "allowances": allow, "reactions": reac}) + "</div>")
    body = (header("Named NPCs", "Every named character EldenNet voices: who they appear to be, and what each may do. How they are on this journey is on Playthrough.")
            + f'<div class="md roster-left">{roster}<div>{detail}</div></div>'
            + related([("Playthrough", "How Idrel is on this journey.", "characters-playthrough.html"),
                       ("Memories", "What Idrel remembers of you.", "history-memories.html"),
                       ("Conversations", "Every conversation with Idrel, as it was shown.", "#"),
                       ("NPC types", "Where every allowance lives.", "characters-types.html")]))
    helpd = help_dialog("Named NPCs", "Every named character is listed from the start with what a first meeting would tell you, under the name the game uses before it tells you theirs. How they are on this journey is on Playthrough.",
                        ["Pick a name at the left.",
                         "Profile shows who they appear to be and their way with news; for someone you have met, a line links to Playthrough.",
                         "Allowances and Reactions are shortcuts to this NPC’s rows in NPC types; cooldowns live in Behaviour › Reactions."],
                        [("Playthrough", "characters-playthrough.html"), ("History › Memories", "history-memories.html"), ("NPC types", "characters-types.html")])
    page("characters-named.html", "Named NPCs", "characters.named", body, helpd, journey=True)


# ---------------------------------------------------------------- History › Memories (IA v2 §6.3)

def memories():
    lid = uid("l")
    actions = (f'<div class="srow"><div class="name"><span id="{lid}">Named NPCs remember</span></div>'
               f'<p class="desc">Journals update after each conversation. <span class="dflt">Default: on.</span></p><div class="ctl">{switch(lid, True)}</div></div>')
    stat = tiles([("2", "NPCs met"), ("7", "Keepsakes"), ("1", "Memory work waiting")])
    l1, l2, l3 = uid("l"), uid("l"), uid("l")
    filters = (f'<div class="filters"><span class="lbl" id="{l1}">NPC</span>{select(["All NPCs", "Idrel", "Wenna"], "Idrel", lid=l1)}'
               f'<span class="lbl" id="{l2}">Basis</span>{select(["All", "Seen", "You told them"], "All", lid=l2)}'
               f'<span class="lbl" id="{l3}">Sort</span>{select(["Newest first", "Weight"], "Newest first", lid=l3)}'
               f'<button class="iconbtn boxed" aria-label="Newest first; change the direction" title="Direction">{ic("sort", "ic sm")}</button></div>')
    # Facts are counters EldenNet keeps (MEM §2.2), shown in words on the NPC card; they have no weight and are never rows
    npc = (f'<div class="card"><div class="detail-head">{monogram("I")}<div class="grow"><h2 class="t-title c-name">Idrel</h2>'
           '<p class="prose">A traveller who helps with the rope and listens to the whole answer. Walked off once while I was still talking, but came back.</p>'
           '<dl class="kv notes"><dt>Feels</dt><dd>thinks well of you</dd><dt>Knows you as</dt><dd>Brannoc</dd>'
           '<dt>Met</dt><dd>3 times · first met at the ferry crossing</dd><dt>Journal last written</dt><dd class="mono">4:11</dd></dl></div></div></div>')
    toolbar = (f'<div class="toolbar"><span class="count">Showing 5 of Idrel’s keepsakes, this journey</span><span class="spacer"></span>'
               f'<button class="btn danger">{ic("close", "ic sm")}Forget Idrel</button>'
               f'<button class="btn danger-fill">{ic("warn", "ic sm")}Forget this journey</button></div>')
    # Keepsakes only (at most 8 per NPC, MEM §2.3); basis Seen or You told them (a claim, MEM §2.5); one protected keepsake
    rows = [
        ("4:11", "Walked off while I was still talking about the bridge. Came back later, so perhaps it was not meant unkindly.", "0.40", "slight", "Seen", "You", False),
        ("4:10", "Said they had crossed this river before the bridge fell.", "0.55", "moderate", "You told them", "You", False),
        ("3:31", "Helped me haul the ferry rope back onto its posts before the current took it.", "0.80", "strong", "Seen", "You", True),
        ("3:31", "Came alone, on foot, down the north road.", "0.30", "slight", "Seen", "You", False),
        ("3:29", "Felt easier with someone else on the rope for once.", "0.20", "faint", "Seen", "Themselves", False),
    ]
    tr = []
    for t, content, w, ww, basis, about, protected in rows:
        k = mark("on", "Protected") if protected else '<span class="ink-faint" aria-hidden="true">—</span><span class="vh">Not protected</span>'
        tr.append(f'<tr><td class="num">{t}</td><td class="name npc nowrap">Idrel</td>'
                  f'<td class="content quote">{content}</td><td><span class="weight"><span class="w">{w}</span><span class="ww">{ww}</span></span></td>'
                  f'<td class="sub nowrap">{basis}</td><td class="sub">{about}</td><td class="c">{k}</td></tr>')
    table = (f'<div class="card flush"><div class="scroll-x"><table class="tbl"><thead><tr><th scope="col" class="sorted">When ↓</th><th scope="col">NPC</th>'
             f'<th scope="col">Keepsake</th><th scope="col">Weight</th><th scope="col">Basis</th><th scope="col">About</th><th scope="col" class="c">Protected</th></tr></thead>'
             f'<tbody>{"".join(tr)}</tbody></table></div></div>')
    work = card("Memory work", '<dl class="kv"><dt>Waiting</dt><dd>1 · Wenna’s journal</dd><dt>Running</dt><dd>none</dd>'
                f'<dt>Last result</dt><dd class="c-moss">{ic("check", "ic sm")}Idrel’s journal saved at 4:11</dd></dl>', icon="sliders")
    backups = card("Backups", '<dl class="kv"><dt>Backups</dt><dd>3</dd><dt>The last</dt><dd class="mono">4:11</dd></dl>'
                   f'<p class="note notes">{ic("info", "ic sm")}Forget takes a backup first. Backups stay beside your settings.</p>', icon="history")
    body = (header("Memories", "What each named NPC remembers of you, and the means to make them forget.", actions)
            + f'<div class="stack">{stat}{filters}{npc}{toolbar}{table}<div class="cols2">{work}{backups}</div>'
            + f'<p class="note">{ic("lock", "ic sm")}Diaries and scene memories: later version.</p></div>'
            + related([("Conversations", "Every conversation with a named NPC, as it was shown.", "#"),
                       ("Named NPCs", "Who they are and how they are now.", "characters-named.html"),
                       ("Settings › Developer", "Recordings and the log level.", "#")]))
    helpd = help_dialog("Memories", "Each named NPC you meet keeps a short journal of you: up to eight keepsakes, things they saw or things you told them, and facts EldenNet counts.",
                        ["Choose an NPC to read their journal, what they think of you and the facts EldenNet keeps.",
                         "Weight is how much a keepsake matters to them; the protected one is never dropped when the journal is rewritten.",
                         "Forget removes one NPC’s memories, or the whole journey’s. Each takes a backup and asks you twice.",
                         "Nothing here can be edited: memories come only from what happened."],
                        [("Conversations", "#"), ("Named NPCs", "characters-named.html")])
    page("history-memories.html", "Memories", "history.memories", body, helpd, journey=True)


# ---------------------------------------------------------------- Behaviour › Speaking up (IA v2 §7.2)

# One help text per Pacing setting, for its home row and every shortcut to it (DESIGN §6.9, principle 1)
HELP_TALK_MAX = "Greetings, quest business, exchanges between NPCs and autonomy."
HELP_IDLE_MAX = "Remarks about the moment from NPCs near you. Sparse is 2, normal is 4."
HELP_HEAR_M = "How near lines between others must be for you to see them."

def speaking():
    # Reaction rows here are shortcuts drawn as on Reactions, at ARCH §11.5's defaults
    who = card("Who speaks unasked",
               '<div class="srows">'
               + sw_row("Named NPCs speak unasked", "Greetings, their own business, news and remarks from the named characters.", True, "on")
               + sw_row("Other NPCs speak unasked", "Soldiers, merchants and others, from written and prepared lines.", True, "on")
               + slider("Talk per window", "talk-max-s", 4, 0, 12, 1, "", HELP_TALK_MAX, "4",
                        zero="Off", lives_in="Behaviour › Pacing", lives_href="behaviour-pacing.html")
               + "</div>")
    greet = card("Greetings",
                 '<div class="srows">' + reaction_row("Greet you", "r-greet", "When a named NPC first comes near you.", 600)
                 + f'</div><p class="note notes">{ic("info", "ic sm")}In this version only named NPCs greet you: the soldiers and creatures you meet are hostile.</p>')
    business = card("Quest business",
                     '<div class="srows">' + reaction_row("Raise their own business with you", "r-stage", "A named NPC may bring up what they need from you.", 300)
                     + '</div><ul class="ready notes">'
                     + f'<li><span class="n">Idrel</span><span class="c-moss">{ic("check", "ic sm")} Ready</span></li>'
                     + f'<li><span class="n">Wenna</span><span class="c-ember">{ic("warn", "ic sm")} Waits for a game check on your build</span></li></ul>'
                     + f'<p class="note notes">{ic("info", "ic sm")}Only NPCs you have met are listed. A readiness word never names or hints at what they will say.</p>')
    news = card("News",
                '<div class="srows">' + reaction_row("Pass on news", "r-news", "A named NPC who has heard of a change in the world mentions it when you next meet, if it is new to you both.", 10800)
                + "</div>")
    between = card("Between NPCs",
                   '<div class="srows">'
                   + sw_row("NPCs greet and answer each other", "Short exchanges between NPCs near you, on the same side.", True, "on")
                   + reaction_row("Greet each other", "r-greet-npc", "Two NPCs near you on the same side greet each other.", 300)
                   + reaction_row("Answer each other", "r-answer", "An NPC answers a line it heard.", 20)
                   + slider("Overheard-lines range", "hear-m-s", 20, 8, 30, 1, "m", HELP_HEAR_M, "20 m",
                            lives_in="Behaviour › Pacing", lives_href="behaviour-pacing.html")
                   + f'</div><p class="note notes">{ic("info", "ic sm")}Exchanges end by themselves after a few lines.</p>')
    autonomy = card("Autonomy",
                    '<div class="srows">'
                    + sw_row("NPCs speak up on their own", "Now and then an NPC near you may consider saying something unprompted.", True, "on")
                    + slider("How often each NPC considers it", "auto-period", 120, 30, 1800, 30, "s", "Each NPC near you, in turn.", "120 s")
                    + f'</div><div class="notes"><p class="note">{ic("info", "ic sm")}At most 240 own-initiative calls an hour, whatever this is set to.</p>'
                    + f'<p class="note c-ember">{ic("warn", "ic sm")}The free presets turn autonomy off: free models are not advised for it.</p></div>')
    lid = uid("l")
    calm = card("Calm remarks",
                '<div class="srows">'
                + reaction_row("Idle remarks", "r-idle", "An NPC near you says something about the moment.", 90)
                + reaction_row("Saw a kill", "r-kill", "An NPC near you remarks on a kill it saw.", 60)
                + reaction_row("Saw a victory", "r-victory", "An NPC near you remarks on a fight that was won.", 600)
                + srow("Idle remarks per window", ctl=seg(lid, ["Off", "Sparse", "Normal", "Custom"], "Sparse"), lid=lid,
                       desc=HELP_IDLE_MAX, default="sparse", lives_in="Behaviour › Pacing", lives_href="behaviour-pacing.html")
                + "</div>")
    banner = (f'<div class="banner">{ic("info")}<span class="grow">Speaking up is held by <a class="c-gold" href="behaviour-pacing.html">pacing</a>, '
              'by each type’s allowances and by safe moments, never by counts. Safety and spoiler rules always apply.</span></div>')
    body = (header("Speaking up", "Everything about NPCs starting to speak on their own: greetings, quest business, news, talk between NPCs and autonomy.",
                   f'<button class="btn secondary">{ic("reset", "ic sm")}Reset page</button>')
            + '<div class="lozenge-rule" aria-hidden="true"></div>'
            + f'<div class="stack">{banner}<div class="cols2"><div class="stack">{who}{greet}{business}{news}</div><div class="stack">{between}{autonomy}{calm}</div></div></div>'
            + related([("Reactions", "Every kind of event NPCs react to, with its cooldown.", "#"),
                       ("Pacing", "How often anyone speaks unasked, and how far lines reach.", "behaviour-pacing.html"),
                       ("NPC types", "Which types may speak unasked at all.", "characters-types.html"),
                       ("Director", "Choosing who speaks next: a later version.", "#")]))
    helpd = help_dialog("Speaking up", "NPCs may start lines themselves: greetings, their own business, news, exchanges with each other and remarks of their own.",
                        ["Turn whole groups on or off under Who speaks unasked.",
                         "Rows marked “lives in Reactions” are shortcuts: the same switch and cooldown as on Reactions.",
                         "How often lines may come at all is set on Pacing."],
                        [("Pacing", "behaviour-pacing.html"), ("Reactions", "#"), ("NPC types", "characters-types.html")])
    page("behaviour-speaking.html", "Speaking up", "behaviour.speaking", body, helpd)


# ---------------------------------------------------------------- Behaviour › Pacing (IA v2 §7.6)

def pacing():
    lid = uid("l")
    actions = (f'<span class="vh" id="{lid}">Preset</span>{seg(lid, ["Quiet", "Default", "Lively"], "Default", "seg lg")}'
               f'<button class="btn secondary">{ic("reset", "ic sm")}Reset page</button>')
    means = card("What this means", '<p class="means">No two unasked lines come closer than <b>2.5 s</b>. In any <b>minute</b>, at most <b>6</b> lines in a fight, '
                 '<b>2</b> idle remarks and <b>4</b> other unasked lines. NPCs start lines to you within <b>8 m</b>; you overhear lines within <b>20 m</b> '
                 'and battle lines within <b>40 m</b>.</p>'
                 f'<p class="note notes">{ic("check", "ic sm")}Replies to you are never limited.</p>', collapsible=False, icon="lozenge")
    gaps = card("Gaps", '<div class="srows">'
                + slider("Time between lines", "gap", 2.5, 0.5, 10, 0.5, "s", "The least time between any two unasked lines, whoever speaks them.", "2.5 s")
                + slider("Time between lines from one fighting group", "group-gap", 8, 0, 30, 1, "s", "The least time between two battle lines from one group.", "8 s")
                + "</div>")
    l2 = uid("l")
    window = card("Window", '<div class="srows">'
                  + slider("Window length", "window", 60, 30, 300, 10, "s", "The rolling stretch of play the counts below apply to.", "60 s")
                  + slider("Lines per window in a fight", "fight-max", 6, 0, 20, 1, "", "Battle lines of every combatant, either side.", "6", zero="Off")
                  + srow("Idle remarks per window", HELP_IDLE_MAX,
                         ctl=seg(l2, ["Off", "Sparse", "Normal", "Custom"], "Sparse"), default="sparse", lid=l2)
                  + slider("Talk per window", "talk-max", 4, 0, 12, 1, "", HELP_TALK_MAX, "4", zero="Off")
                  + "</div>")
    reach = card("Reach", '<div class="srows">'
                 + slider("Speaking-to-you range", "talk-m", 8, 3, 16, 1, "m", "How near an NPC must be to start a line to you.", "8 m")
                 + slider("Overheard-lines range", "hear-m", 20, 8, 30, 1, "m", HELP_HEAR_M, "20 m")
                 + slider("Combat-lines range", "fight-m", 40, 10, 40, 1, "m", "How near a fight must be for its lines to show.", "40 m")
                 + "</div>")
    body = (header("Pacing", "How often anyone speaks unasked, and how far lines reach.", actions)
            + '<div class="lozenge-rule" aria-hidden="true"></div>'
            + f'<div class="stack">{means}<div class="cols2"><div class="stack">{gaps}{window}</div><div class="stack">{reach}</div></div></div>'
            + related([("Speaking up", "Who may speak unasked, and about what.", "behaviour-speaking.html"),
                       ("Reactions", "The cooldown of each kind of event.", "#"),
                       ("Combat", "Battle lines for every combatant.", "#")]))
    helpd = help_dialog("Pacing", "Pacing is the only limit on how often NPCs speak unasked: a gap between lines, a count within a window, and a cooldown for each kind of event.",
                        ["Pick Quiet, Default or Lively to set every row at once.",
                         "Or move a slider: “What this means” says the result in words.",
                         "Replies to you are never limited by any of this."],
                        [("Speaking up", "behaviour-speaking.html"), ("Reactions", "#")])
    page("behaviour-pacing.html", "Pacing", "behaviour.pacing", body, helpd)


# ---------------------------------------------------------------- Voice › Text-to-speech, a v1.5 statement (IA v2 §4.2)

def tts():
    adds = card("What v1.5 adds", '<ul class="bullets">'
                '<li>A voice for each named NPC and each type of NPC, from a speech engine that runs outside the game.</li>'
                '<li>Lines paced by their audio, so the words on screen and the voice end together.</li>'
                '<li>On this page then: the engine, its address, a test voice and the volume.</li></ul>', collapsible=False, icon="voice")
    never = card("What never comes", '<ul class="bullets never">'
                 '<li>Copies of the voices of the game’s own cast.</li>'
                 '<li>Sound from this menu: a voice preview will play through EldenNet’s own audio, only when you ask.</li></ul>',
                 collapsible=False, icon="lock")
    kinds = [("Named NPCs", "on", "On, from kind"), ("Merchant", "on", "On, from kind"), ("Humanoid", "some", "Some types off"),
             ("Creature", "off", "They never speak in the game"), ("Boss", "on", "On, from kind"), ("Invader", "on", "On, from kind"),
             ("Summon", "some", "Some types off"), ("Mount", "off", "Never speaks")]
    tr = "".join(f'<tr><td>{k}</td><td class="c">{mark(m)}</td><td class="sub">{n}</td><td>{avail("v15", "Takes effect in v1.5")}</td>'
                 f'<td class="r"><a class="iconbtn" href="characters-types.html" title="Open {k} in NPC types">{ic("out", "ic sm")}</a></td></tr>' for k, m, n in kinds)
    who = card("Who will speak aloud", f'<table class="tbl"><thead><tr><th scope="col">Kind</th><th scope="col" class="c">Voice</th><th scope="col">Note</th>'
               f'<th scope="col">When</th><th scope="col" class="r"><span class="vh">Open</span></th></tr></thead><tbody>{tr}</tbody></table>',
               desc="Each kind’s voice allowance, read-only here. It lives in Characters › NPC types.", collapsible=False, icon="characters")
    lead = (f'<div class="card statement"><p class="lead">In this version every NPC line appears as text: a floating label over the speaker, '
            'or the feed at the side of the screen.</p><p class="prose">Spoken voices arrive in v1.5. Nothing on this page can be set yet, '
            'so nothing here pretends to work.</p>'
            f'<div class="toolbar notes"><a class="btn secondary" href="#" data-mock="absent">Text and subtitles{ic("out", "ic sm")}</a></div></div>')
    body = (header("Text-to-speech", "Spoken voices for NPCs. In this version NPCs speak in text.", ver="v1.5")
            + '<div class="lozenge-rule" aria-hidden="true"></div>'
            + f'<div class="stack">{lead}<div class="cols2">{adds}{never}</div>{who}</div>'
            + related([("Text and subtitles", "How NPC lines appear on screen now.", "#"),
                       ("Speech-to-text", "Talking aloud to named NPCs: also v1.5.", "#"),
                       ("NPC types", "Where each type’s voice allowance lives.", "characters-types.html")]))
    helpd = help_dialog("Text-to-speech", "Spoken voices are planned for v1.5. Until then this page says what is coming and shows who will speak aloud.",
                        ["Read what v1.5 adds and what never comes.",
                         "Each kind’s voice allowance can already be set in NPC types; it takes effect in v1.5."],
                        [("Text and subtitles", "#"), ("NPC types", "characters-types.html")])
    page("voice-tts.html", "Text-to-speech", "voice.tts", body, helpd)


home()
providers()
models()
types_page()
playthrough_page()
named_page()
memories()
speaking()
pacing()
tts()
print("wrote", sorted(p.name for p in OUT.glob("*.html")))
