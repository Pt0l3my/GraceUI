#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""tools/guide/snippets.py: the modders' guide's code is the example mods' own, or the ABI mock's (D85, D284).

    python3 -I tools/guide/snippets.py           check every code block in docs/modders/
    python3 -I tools/guide/snippets.py --find    print where each block's lines are found in the sources

Each indented code block in docs/modders/*.md has a marker on the line before the blank line that precedes it:

    <!-- snippet: examples/<mod>/<file> -->   the block's lines, in order and with their relative indentation, are a
                                              run of that file's lines: the example compiles in both Windows builds and
                                              runs in the harness, so the block does too
    <!-- snippet: src/graceui-mock/<file> --> likewise, a file of the ABI mock's (D285), whose tests CTest runs
    <!-- snippet: shell -->                   a command, for a reader to type
    <!-- snippet: frame -->                   a protocol frame, its values elided

A block without a marker, a marker with no block after it, a file that is not under examples/ or src/graceui-mock/ or
does not exist, or a block not found in its file fails the check. Standard library only; it reads nothing but
docs/modders/, examples/ and src/graceui-mock/.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
GUIDE = ROOT / "docs" / "modders"
EXAMPLES = ROOT / "examples"
MOCK = ROOT / "src" / "graceui-mock"
MARKER = re.compile(r"^<!-- snippet: (\S+) -->$")
KINDS = {"shell", "frame"}
SOURCES = (".c", ".cpp", ".h", ".hpp", ".txt", ".toml", ".json")


def blocks(path):
    """Each indented code block of a Markdown file: (line number, its marker or None, its lines without the indent)."""
    lines = path.read_text(encoding="utf-8").split("\n")
    out = []
    at = 0
    while at < len(lines):
        line = lines[at]
        starts = line.startswith("    ") and line.strip() and at > 0 and lines[at - 1] == ""
        if not starts:
            at += 1
            continue
        marker = None
        if at >= 2:
            found = MARKER.match(lines[at - 2])
            marker = found.group(1) if found else None
        if not marker and at >= 2 and lines[at - 2].strip() and not lines[at - 2].startswith("    ") and \
                re.match(r"^\s*(- |\d+\. |\|)", lines[at - 2]):
            at += 1  # a list item's or a table's next paragraph, indented: not code
            continue
        first = at
        body = []
        while at < len(lines) and (lines[at].startswith("    ") or
                                   (lines[at] == "" and at + 1 < len(lines) and lines[at + 1].startswith("    "))):
            body.append(lines[at][4:])
            at += 1
        out.append((first + 1, marker, body))
    return out


def markers(path):
    """Every marker's line number in a file."""
    return [n + 1 for n, line in enumerate(path.read_text(encoding="utf-8").split("\n")) if MARKER.match(line)]


def indent(line):
    return len(line) - len(line.lstrip(" "))


def found_in(body, source_lines):
    """The 1-based line at which `body` is a run of `source_lines`, word for word and indent for indent, or 0."""
    want = [line.strip() for line in body]
    shape = [indent(line) - indent(body[0]) if line.strip() else None for line in body]
    for start in range(len(source_lines) - len(body) + 1):
        run = source_lines[start:start + len(body)]
        if [line.strip() for line in run] != want:
            continue
        base = indent(run[0])
        if all(s is None or indent(line) - base == s for line, s in zip(run, shape)):
            return start + 1
    return 0


def roots():
    return {"examples/": EXAMPLES, "src/graceui-mock/": MOCK}


def sources():
    return sorted(p for root in roots().values() for p in root.rglob("*") if p.is_file() and p.suffix in SOURCES)


def check():
    failures = []
    checked = 0
    for doc in sorted(GUIDE.glob("*.md")):
        name = doc.relative_to(ROOT).as_posix()
        used = set()
        for line, marker, body in blocks(doc):
            if marker is None:
                failures.append(f"{name}:{line}: a code block with no snippet marker before it")
                continue
            used.add(line - 2)
            if marker in KINDS:
                continue
            source = (ROOT / marker).resolve()
            if not any(marker.startswith(prefix) and root.resolve() in source.parents
                       for prefix, root in roots().items()) or not source.is_file():
                failures.append(f"{name}:{line}: its marker names {marker}, which is no file of examples/ or "
                                "src/graceui-mock/")
                continue
            if not found_in(body, source.read_text(encoding="utf-8").split("\n")):
                failures.append(f"{name}:{line}: its {len(body)} line(s) are not a run of {marker}")
                continue
            checked += 1
        for line in markers(doc):
            if line not in used:
                failures.append(f"{name}:{line}: a snippet marker with no code block after it")
    return failures, checked


def find():
    texts = {p: p.read_text(encoding="utf-8").split("\n") for p in sources()}
    for doc in sorted(GUIDE.glob("*.md")):
        for line, marker, body in blocks(doc):
            where = [f"{p.relative_to(ROOT).as_posix()}:{at}" for p, lines in texts.items()
                     if (at := found_in(body, lines))]
            print(f"{doc.relative_to(ROOT).as_posix()}:{line}: marker {marker or 'NONE'}; found in "
                  f"{', '.join(where) or 'nothing'}: {body[0].strip()[:60]}")


def run(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--find", action="store_true", help="print where each block is found in examples/")
    args = parser.parse_args(argv)
    if args.find:
        find()
        return 0
    failures, checked = check()
    for failure in failures:
        print(f"snippets: {failure}", file=sys.stderr)
    if not failures:
        print(f"snippets: ok ({checked} blocks found in their files)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
