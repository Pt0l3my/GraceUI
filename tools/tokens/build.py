#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""tools/tokens/build.py: the Grace theme's tokens, from tokens/grace.toml to both views (GraceUI DESIGN §3.15, D108,
D157; invariants 13 and 14).

    python3 -I tools/tokens/build.py            write web/src/tokens.css and src/grace-tokens/include/grace/tokens.h
    python3 -I tools/tokens/build.py --check    fail if either is not what the tokens build, or a check fails
    python3 -I tools/tokens/build.py --table    print §3.3's contrast table, as the tokens give it

Before anything is written, the tokens pass four checks, each failure printed with what failed:
  1. Contrast (§3.3, U7): every text token on every ground at `text_min` (4.5:1) or better, every edge token at
     `edge_min` (3:1), and each named pair at `text_min`, in every variant; text tokens are opaque; a token listed as
     never text is not listed as text.
  2. Type (§3.4, §3.5, D23): display faces at 20 px or more, serif text at the prose minimum, capitals at the eyebrow
     minimum, every other size at the UI minimum; every line at least its size.
  3. Motion (§3.10, invariant 14): each duration a fade, a transition, a hold or instant, 0 to 2,000 ms, instant
     exactly 0, nothing that repeats.
  4. The stylesheets (invariant 14): no CSS or script under web/src asks for a looping animation (`infinite` in an
     animation, `iterations: Infinity`).
Exit status 0 when all pass (and, with --check, nothing has drifted).
"""
import argparse
import math
import pathlib
import re
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SOURCE = ROOT / "tokens" / "grace.toml"
CSS_OUT = ROOT / "web" / "src" / "tokens.css"
HEADER_OUT = ROOT / "src" / "grace-tokens" / "include" / "grace" / "tokens.h"
WEB_SRC = ROOT / "web" / "src"

HEX = re.compile(r"^#[0-9A-Fa-f]{6}$")
KINDS = ("fade", "transition", "hold", "instant")
DUR_MAX_MS = 2000
DISPLAY_MIN_PX = 20
LOOPING = (
    re.compile(r"animation(?:-iteration-count)?\s*:[^;{}]*\binfinite\b", re.IGNORECASE),
    re.compile(r"\biterations\s*:\s*Infinity\b"),
)


class TokenError(Exception):
    pass


# ---- Colours.

def parse_hex(text):
    return (int(text[1:3], 16), int(text[3:5], 16), int(text[5:7], 16))


def resolve_colours(raw, name):
    """Each colour token of a variant as (r, g, b, alpha): a hex is opaque; a mix is another token's colour, or a hex,
    at an alpha. Composited over a ground only where it is drawn, as both views do."""
    out = {}

    def one(token, seen):
        if token in out:
            return out[token]
        if token in seen:
            raise TokenError(f"{name}: colour {token} mixes itself")
        value = raw.get(token)
        if value is None:
            raise TokenError(f"{name}: no colour named {token}")
        if isinstance(value, str):
            if not HEX.match(value):
                raise TokenError(f"{name}: colour {token} is not #RRGGBB: {value}")
            result = (*parse_hex(value), 1.0)
        elif isinstance(value, dict) and set(value) == {"mix", "alpha"}:
            alpha = value["alpha"]
            if not isinstance(alpha, (int, float)) or not 0 < alpha < 1:
                raise TokenError(f"{name}: colour {token}'s alpha is not between 0 and 1")
            base = value["mix"]
            if isinstance(base, str) and HEX.match(base):
                rgb = parse_hex(base)
            else:
                r, g, b, a = one(base, seen | {token})
                if a != 1.0:
                    raise TokenError(f"{name}: colour {token} mixes {base}, which is not opaque")
                rgb = (r, g, b)
            result = (*rgb, float(alpha))
        else:
            raise TokenError(f"{name}: colour {token} is neither #RRGGBB nor a mix of a token at an alpha")
        out[token] = result
        return result

    for token in raw:
        one(token, frozenset())
    return out


def alpha_byte(alpha):
    return int(math.floor(alpha * 255 + 0.5))


def over(top, ground):
    """`top` (r, g, b, alpha) composited over an opaque ground, as both views draw it, with the alpha as a byte."""
    a = alpha_byte(top[3]) / 255
    return tuple(int(math.floor(t * a + g * (1 - a) + 0.5)) for t, g in zip(top[:3], ground[:3]))


def luminance(rgb):
    def channel(c):
        c /= 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (channel(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


# ---- Loading.

def load(text=None):
    data = tomllib.loads(text if text is not None else SOURCE.read_text(encoding="utf-8"))
    if data.get("schema") != 1:
        raise TokenError("tokens: schema is not 1")
    raw_variants = data.get("variant", {})
    if "grace" not in raw_variants:
        raise TokenError("tokens: no variant named grace, the default")
    variants = {}
    for key, variant in raw_variants.items():
        colours = dict(variant.get("colour", {}))
        sizes = dict(variant.get("size", {}))
        base = variant.get("base")
        if base is not None:
            if base not in raw_variants or raw_variants[base].get("base") is not None:
                raise TokenError(f"variant {key}: its base {base} is not a variant without a base of its own")
            extra = set(colours) - set(raw_variants[base].get("colour", {}))
            extra |= set(sizes) - set(raw_variants[base].get("size", {}))
            if extra:
                raise TokenError(f"variant {key}: {', '.join(sorted(extra))} not in its base {base}")
            colours = {**raw_variants[base].get("colour", {}), **colours}
            sizes = {**raw_variants[base].get("size", {}), **sizes}
        variants[key] = {"name": variant.get("name", key), "colour": resolve_colours(colours, f"variant {key}"),
                         "size": sizes}
    return data, variants


# ---- The checks.

def check_contrast(data, variants):
    rules = data["contrast"]
    failures = []
    overlap = set(rules["text"]) & set(rules["never_text"])
    if overlap:
        failures.append(f"contrast: {', '.join(sorted(overlap))} listed both as text and as never text")
    for key, variant in variants.items():
        colours = variant["colour"]
        for token in [*rules["text"], *rules["grounds"], *rules["edges"]]:
            if token not in colours:
                failures.append(f"{key}: no colour named {token}")
            elif colours[token][3] != 1.0:
                failures.append(f"{key}: {token} is not opaque, so its contrast depends on what is under it")
        if failures:
            continue
        for token in rules["text"]:
            for ground in rules["grounds"]:
                ratio = contrast(colours[token][:3], colours[ground][:3])
                if ratio < rules["text_min"]:
                    failures.append(f"{key}: {token} on {ground} is {ratio:.2f}:1, below {rules['text_min']}:1")
        for token in rules["edges"]:
            for ground in rules["grounds"]:
                ratio = contrast(colours[token][:3], colours[ground][:3])
                if ratio < rules["edge_min"]:
                    failures.append(f"{key}: {token} on {ground} is {ratio:.2f}:1, below {rules['edge_min']}:1")
        for text, ground in rules["pairs"]:
            ratio = contrast(colours[text][:3], colours[ground][:3])
            if ratio < rules["text_min"]:
                failures.append(f"{key}: {text} on {ground} is {ratio:.2f}:1, below {rules['text_min']}:1")
    return failures


def check_type(data):
    failures = []
    fonts = data["font"]
    minimum = data["type_min"]
    for token, entry in data["type"].items():
        font = entry.get("font")
        if font not in fonts:
            failures.append(f"type {token}: no font role {font}")
            continue
        size, line = entry["size"], entry["line"]
        upper = entry.get("case", "none") == "upper"
        if entry.get("case", "none") not in ("none", "upper"):
            failures.append(f"type {token}: case is neither none nor upper")
        if font == "display":
            floor, why = DISPLAY_MIN_PX, "display faces at 20 px or more"
        elif font == "serif":
            floor, why = minimum["prose"], "serif text at the prose minimum"
        elif upper:
            floor, why = minimum["eyebrow"], "capitals at the eyebrow minimum"
        else:
            floor, why = minimum["ui"], "UI text at its minimum"
        if size < floor:
            failures.append(f"type {token}: {size} px is below {floor} px ({why})")
        if line < size:
            failures.append(f"type {token}: its line, {line} px, is shorter than its size, {size} px")
    return failures


def check_motion(data):
    failures = []
    durations = data["dur"]
    for token, entry in durations.items():
        if token == "reduced_fade_max":
            continue
        if not isinstance(entry, dict) or set(entry) - {"ms", "kind"}:
            failures.append(f"dur {token}: only ms and kind are taken (nothing repeats)")
            continue
        ms, kind = entry.get("ms"), entry.get("kind")
        if kind not in KINDS:
            failures.append(f"dur {token}: kind {kind} is not one of {', '.join(KINDS)}")
        if not isinstance(ms, int) or not 0 <= ms <= DUR_MAX_MS:
            failures.append(f"dur {token}: {ms} is not a whole number of ms from 0 to {DUR_MAX_MS}")
        elif kind == "instant" and ms != 0:
            failures.append(f"dur {token}: instant, but {ms} ms")
    return failures


def check_stylesheets():
    failures = []
    if not WEB_SRC.is_dir():
        return failures
    for path in sorted(WEB_SRC.rglob("*")):
        if path.suffix not in (".css", ".ts", ".tsx", ".js") or not path.is_file():
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if any(pattern.search(line) for pattern in LOOPING):
                failures.append(f"{path.relative_to(ROOT).as_posix()}:{number}: a looping animation (invariant 14)")
    return failures


def reduced(entry, fade_max):
    return min(entry["ms"], fade_max) if entry["kind"] == "fade" else 0


# ---- The outputs.

def css_name(token):
    return token.replace(".", "-")


def css_colour(value):
    r, g, b, a = value
    if a == 1.0:
        return f"#{r:02X}{g:02X}{b:02X}"
    return f"rgb({r} {g} {b} / {round(a * 100, 2):g}%)"


def number(value):
    return f"{value:g}"


def build_css(data, variants):
    lines = ["/* Generated by tools/tokens/build.py from tokens/grace.toml: do not edit (GraceUI D108, D157). */", ""]
    fonts = data["font"]
    common = []
    for role, family in fonts.items():
        common.append(f'  --font-{role}: "{family}";')
    for token, entry in data["type"].items():
        name = css_name(token)
        common.append(f"  --type-{name}-family: var(--font-{entry['font']});")
        common.append(f"  --type-{name}-size: {number(entry['size'])}px;")
        common.append(f"  --type-{name}-line: {number(entry['line'])}px;")
        common.append(f"  --type-{name}-weight: {entry['weight']};")
        common.append(f"  --type-{name}-tracking: {number(entry.get('tracking', 0))}em;")
        common.append(f"  --type-{name}-case: {'uppercase' if entry.get('case') == 'upper' else 'none'};")
    for step in data["space"]["steps"]:
        common.append(f"  --space-{step}: {step}px;")
    for token, value in data["size"].items():
        common.append(f"  --size-{css_name(token)}: {number(value)}px;")
    for token, value in data["radius"].items():
        common.append(f"  --radius-{css_name(token)}: {number(value)}px;")
    durations = {k: v for k, v in data["dur"].items() if k != "reduced_fade_max"}
    for token, entry in durations.items():
        common.append(f"  --dur-{css_name(token)}: {entry['ms']}ms;")
    fade_max = data["dur"]["reduced_fade_max"]
    for key, variant in variants.items():
        selector = f':root, [data-theme="{key}"]' if key == "grace" else f'[data-theme="{key}"]'
        lines.append(f"/* {variant['name']} */")
        lines.append(selector + " {")
        if key == "grace":
            lines.extend(common)
        for token, value in variant["colour"].items():
            lines.append(f"  --{css_name(token)}: {css_colour(value)};")
        for token, value in variant["size"].items():
            lines.append(f"  --size-{css_name(token)}: {number(value)}px;")
        lines.append("}")
        lines.append("")
    reduced_lines = [f"  --dur-{css_name(t)}: {reduced(e, fade_max)}ms;" for t, e in durations.items()]
    lines.append("/* Reduced motion (§3.10): every duration 0, but a fade, which is at most "
                 f"{fade_max} ms. */")
    lines.append("@media (prefers-reduced-motion: reduce) {")
    lines.append("  :root, [data-theme] {")
    lines.extend("  " + line for line in reduced_lines)
    lines.append("  }")
    lines.append("}")
    lines.append("[data-reduce-motion] {")
    lines.extend(reduced_lines)
    lines.append("}")
    return "\n".join(lines) + "\n"


def cpp_name(token):
    return token.replace(".", "_")


def cpp_float(value):
    text = f"{float(value):g}"
    return text + ("f" if "." in text or "e" in text else ".0f")


def build_header(data, variants):
    colours = list(variants["grace"]["colour"])
    sizes = list(variants["grace"]["size"])
    out = [
        "// SPDX-License-Identifier: MIT",
        "// Generated by tools/tokens/build.py from tokens/grace.toml: do not edit (GraceUI D108, D157). The Grace theme's",
        "// tokens for the in-game view, the same values the browser's tokens.css holds (invariant 13).",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace grace::tokens {",
        "",
        "// A colour, its alpha as a byte: 255 for an opaque token, less for a mix drawn over whatever ground is under it.",
        "struct Rgba {",
        "  std::uint8_t r = 0;",
        "  std::uint8_t g = 0;",
        "  std::uint8_t b = 0;",
        "  std::uint8_t a = 255;",
        "  constexpr std::uint32_t rgb() const noexcept {",
        "    return (std::uint32_t{r} << 16) | (std::uint32_t{g} << 8) | std::uint32_t{b};",
        "  }",
        "  constexpr bool operator==(const Rgba&) const noexcept = default;",
        "};",
        "",
        "// `top` composited over an opaque `ground`, rounded as tools/tokens/build.py rounds it.",
        "constexpr Rgba over(Rgba top, Rgba ground) noexcept {",
        "  const auto mix = [&](std::uint8_t t, std::uint8_t g) {",
        "    return static_cast<std::uint8_t>((t * top.a + g * (255 - top.a) + 127) / 255);",
        "  };",
        "  return Rgba{mix(top.r, ground.r), mix(top.g, ground.g), mix(top.b, ground.b), 255};",
        "}",
        "",
        "struct Palette {",
    ]
    out += [f"  Rgba {cpp_name(token)};" for token in colours]
    out += ["};", "", "struct Sizes {"]
    out += [f"  float {cpp_name(token)};" for token in sizes]
    out += ["};", "", "struct Variant {", "  const char* id;", "  const char* name;", "  Palette colour;",
            "  Sizes size;", "};", ""]
    for key, variant in variants.items():
        out.append(f"inline constexpr Variant k{key.capitalize()}{{")
        out.append(f'    "{key}",')
        out.append(f'    "{variant["name"]}",')
        out.append("    {")
        for token in colours:
            r, g, b, a = variant["colour"][token]
            out.append(f"        Rgba{{{r}, {g}, {b}, {alpha_byte(a)}}},  // {token}")
        out.append("    },")
        out.append("    {" + ", ".join(cpp_float(variant["size"][t]) for t in sizes) + "},")
        out.append("};")
    out += ["", "enum class Font : std::uint8_t {"]
    out += [f"  {role}," for role in data["font"]]
    out += ["};", "", "inline constexpr const char* kFontFamily[] = {"]
    out += [f'    "{family}",' for family in data["font"].values()]
    out += ["};", "", "struct Type {", "  Font font;", "  float size;", "  float line;", "  int weight;",
            "  float tracking;  // in em", "  bool upper;", "};", "", "namespace type {"]
    for token, entry in data["type"].items():
        out.append(f"inline constexpr Type {cpp_name(token)}{{Font::{entry['font']}, {cpp_float(entry['size'])}, "
                   f"{cpp_float(entry['line'])}, {entry['weight']}, {cpp_float(entry.get('tracking', 0))}, "
                   f"{'true' if entry.get('case') == 'upper' else 'false'}}};")
    out += ["}  // namespace type", "", "inline constexpr float kSpace[] = {"
            + ", ".join(cpp_float(s) for s in data["space"]["steps"]) + "};", "", "namespace size {"]
    out += [f"inline constexpr float {cpp_name(t)} = {cpp_float(v)};" for t, v in data["size"].items()]
    out += ["}  // namespace size", "", "namespace radius {"]
    out += [f"inline constexpr float {cpp_name(t)} = {cpp_float(v)};" for t, v in data["radius"].items()]
    out += ["}  // namespace radius", "",
            "// A duration in ms, and what it becomes with reduced motion (§3.10). Nothing loops (invariant 14).",
            "struct Duration {", "  int ms;", "  int reduced_ms;", "};", "", "namespace dur {"]
    fade_max = data["dur"]["reduced_fade_max"]
    for token, entry in data["dur"].items():
        if token != "reduced_fade_max":
            out.append(f"inline constexpr Duration {cpp_name(token)}{{{entry['ms']}, {reduced(entry, fade_max)}}};")
    out += ["}  // namespace dur", "", "}  // namespace grace::tokens", ""]
    return "\n".join(out)


def table(data, variants):
    rules = data["contrast"]
    colours = variants["grace"]["colour"]
    short = {"ground.well": "well", "ground.0": "g0", "ground.1": "g1", "ground.2": "g2", "ground.3": "g3",
             "ground.4": "g4"}
    lines = ["| Text | " + " | ".join(short.get(g, g) for g in rules["grounds"]) + " |",
             "|---|" + "---|" * len(rules["grounds"])]
    for token in rules["text"]:
        ratios = [contrast(colours[token][:3], colours[g][:3]) for g in rules["grounds"]]
        lines.append(f"| `{token}` | " + " | ".join(f"{r:.1f}" for r in ratios) + " |")
    return "\n".join(lines)


def run(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if an output is not what the tokens build")
    parser.add_argument("--table", action="store_true", help="print §3.3's contrast table")
    args = parser.parse_args(argv)
    try:
        data, variants = load()
    except (TokenError, KeyError, tomllib.TOMLDecodeError) as error:
        print(f"tokens: {error}", file=sys.stderr)
        return 1
    failures = check_contrast(data, variants) + check_type(data) + check_motion(data) + check_stylesheets()
    if failures:
        for failure in failures:
            print(f"tokens: {failure}", file=sys.stderr)
        return 1
    if args.table:
        print(table(data, variants))
        return 0
    outputs = {CSS_OUT: build_css(data, variants), HEADER_OUT: build_header(data, variants)}
    if args.check:
        stale = [path for path, text in outputs.items()
                 if not path.is_file() or path.read_text(encoding="utf-8") != text]
        for path in stale:
            print(f"tokens: {path.relative_to(ROOT).as_posix()} is not what tokens/grace.toml builds: run "
                  "python3 -I tools/tokens/build.py", file=sys.stderr)
        if not stale:
            print("tokens: ok")
        return 1 if stale else 0
    for path, text in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    print("tokens: wrote " + ", ".join(path.relative_to(ROOT).as_posix() for path in outputs))
    return 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
