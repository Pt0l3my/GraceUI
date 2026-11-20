# SPDX-License-Identifier: MIT
"""Writes and checks GraceUI's version file, graceui.toml (DESIGN §2.7, D52, D83).

    python3 -I tools/release/manifest.py write FOLDER [--released YYYY-MM-DD]
    python3 -I tools/release/manifest.py check FOLDER

`write` lists every file of a GraceUI folder (FOLDER is the `GraceUI` folder as it will be installed: graceui.dll,
web/, fonts/ and the text files) with its size and SHA-256, under GraceUI's version (CMakeLists.txt), the ABI's major
and minor (graceui.h), the bridge protocol's MAJOR.MINOR and caps (protocol.toml) and the release date. It leaves out
graceui.toml itself and `user/`, which GraceUI writes at run time and no release carries (D84). `check` reads a
folder's graceui.toml and fails if a file is missing, extra, or of another size or hash, as EldenNet's release job and
launcher check it (§2.7).

The file keeps to the strict subset grace-serve reads (files.h): one `key = value` a line, basic strings with no
escape, plain integers, one-line arrays and inline tables. A name that would need an escape, or that is not segments
of A-Z a-z 0-9 and "-._~" joined by '/', fails the write: such a file has no place in a release.
"""
import argparse
import datetime
import hashlib
import os
import pathlib
import re
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
CMAKE = ROOT / "CMakeLists.txt"
HEADER = ROOT / "src" / "grace-abi" / "include" / "graceui.h"
PROTOCOL = ROOT / "src" / "grace-protocol" / "protocol.toml"
NAME = "graceui.toml"
SKIPPED = ("user",)  # D84: written at run time, in no release

SEGMENT = re.compile(r"[A-Za-z0-9\-_~][A-Za-z0-9\-._~]*")
VERSION = re.compile(r"\d+\.\d+\.\d+")
DATE = re.compile(r"\d{4}-\d{2}-\d{2}")
HEX64 = re.compile(r"[0-9a-f]{64}")
MAX_NAME = 255
MAX_FILES = 512


class ManifestError(Exception):
    pass


def graceui_version():
    m = re.search(r"project\(GraceUI VERSION (\d+\.\d+\.\d+)\b", CMAKE.read_text(encoding="utf-8"))
    if not m:
        raise ManifestError("CMakeLists.txt names no project(GraceUI VERSION x.y.z)")
    return m.group(1)


def abi_version():
    text = HEADER.read_text(encoding="utf-8")
    m = re.search(r"#define GRACEUI_ABI1_MINOR (\d+)u", text)
    if not m:
        raise ManifestError("graceui.h defines no GRACEUI_ABI1_MINOR")
    return [1, int(m.group(1))]


def protocol_version():
    with PROTOCOL.open("rb") as f:
        schema = tomllib.load(f)
    p = schema["protocol"]
    caps = schema.get("caps", [])
    for cap in caps:
        if not re.fullmatch(r"[a-z0-9._\-]{1,64}", cap):
            raise ManifestError(f"protocol.toml: cap {cap!r} is not an id")
    return [int(p["major"]), int(p["minor"])], sorted(caps)


def check_name(name):
    if len(name.encode("utf-8")) > MAX_NAME:
        raise ManifestError(f"{name}: longer than {MAX_NAME} bytes")
    for segment in name.split("/"):
        if not SEGMENT.fullmatch(segment):
            raise ManifestError(f"{name}: a name a release may not carry (segments of A-Z a-z 0-9 and -._~, "
                                "none starting with '.')")


def listing(folder):
    """Every file of the folder as (name, size, sha256), sorted by name; links and other kinds fail."""
    out = []
    for dirpath, dirnames, filenames in os.walk(folder, followlinks=False):
        rel_dir = pathlib.Path(dirpath).relative_to(folder)
        if rel_dir == pathlib.Path("."):
            dirnames[:] = [d for d in dirnames if d not in SKIPPED]
        for d in dirnames:
            if (pathlib.Path(dirpath) / d).is_symlink():
                raise ManifestError(f"{(rel_dir / d).as_posix()}: a link, which a release may not carry")
        for f in filenames:
            path = pathlib.Path(dirpath) / f
            name = (rel_dir / f).as_posix()
            if name == NAME:
                continue
            if path.is_symlink() or not path.is_file():
                raise ManifestError(f"{name}: not a plain file")
            check_name(name)
            data = path.read_bytes()
            out.append((name, len(data), hashlib.sha256(data).hexdigest()))
    out.sort()
    if len(out) > MAX_FILES:
        raise ManifestError(f"{len(out)} files, more than {MAX_FILES}")
    return out


def render(version, abi, protocol, caps, released, files):
    def array(items):
        return "[" + ", ".join(f'"{i}"' if isinstance(i, str) else str(i) for i in items) + "]"

    lines = [
        "# GraceUI's version file (DESIGN §2.7, D52): written by tools/release/manifest.py; do not edit.",
        f'graceui = "{version}"',
        f"abi = {array(abi)}",
        f"protocol = {array(protocol)}",
        f"caps = {array(caps)}",
        f'released = "{released}"',
        "",
        "[files]",
    ]
    lines += [f'"{name}" = {{ size = {size}, sha256 = "{digest}" }}' for name, size, digest in files]
    return "\n".join(lines) + "\n"


def write(folder, released):
    folder = pathlib.Path(folder)
    if not folder.is_dir():
        raise ManifestError(f"{folder}: not a folder")
    try:
        if not DATE.fullmatch(released):
            raise ValueError
        datetime.date.fromisoformat(released)
    except ValueError:
        raise ManifestError(f"{released}: not a date as YYYY-MM-DD") from None
    files = listing(folder)
    if not any(name == "graceui.dll" for name, _, _ in files):
        raise ManifestError("the folder holds no graceui.dll")
    protocol, caps = protocol_version()
    text = render(graceui_version(), abi_version(), protocol, caps, released, files)
    (folder / NAME).write_text(text, encoding="utf-8", newline="\n")
    return text


def read(folder):
    with (pathlib.Path(folder) / NAME).open("rb") as f:
        manifest = tomllib.load(f)
    for key, kind in (("graceui", str), ("abi", list), ("protocol", list), ("caps", list), ("released", str),
                      ("files", dict)):
        if not isinstance(manifest.get(key), kind):
            raise ManifestError(f"{NAME}: {key} missing or not a {kind.__name__}")
    if not VERSION.fullmatch(manifest["graceui"]):
        raise ManifestError(f"{NAME}: graceui is not x.y.z")
    for name, entry in manifest["files"].items():
        check_name(name)
        if set(entry) != {"size", "sha256"} or not isinstance(entry["size"], int) \
                or not isinstance(entry["sha256"], str) or not HEX64.fullmatch(entry["sha256"]):
            raise ManifestError(f"{NAME}: {name}: wants exactly size and a lowercase SHA-256")
    return manifest


def check(folder):
    """The differences between the folder and its graceui.toml, as lines; empty when they agree."""
    manifest = read(folder)
    listed = {name: (e["size"], e["sha256"]) for name, e in manifest["files"].items()}
    present = {name: (size, digest) for name, size, digest in listing(pathlib.Path(folder))}
    out = []
    for name in sorted(listed.keys() - present.keys()):
        out.append(f"{name}: listed, missing")
    for name in sorted(present.keys() - listed.keys()):
        out.append(f"{name}: present, not listed")
    for name in sorted(listed.keys() & present.keys()):
        if listed[name][0] != present[name][0]:
            out.append(f"{name}: {present[name][0]} bytes, listed {listed[name][0]}")
        elif listed[name][1] != present[name][1]:
            out.append(f"{name}: its SHA-256 differs from the listed one")
    return out


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    w = sub.add_parser("write")
    w.add_argument("folder")
    w.add_argument("--released", default=None, help="the release date, YYYY-MM-DD (default: SOURCE_DATE_EPOCH's, "
                   "else today's, in UTC)")
    c = sub.add_parser("check")
    c.add_argument("folder")
    args = parser.parse_args(argv)
    try:
        if args.command == "write":
            released = args.released
            if released is None:
                epoch = os.environ.get("SOURCE_DATE_EPOCH")
                when = datetime.datetime.fromtimestamp(int(epoch), datetime.UTC) if epoch else \
                    datetime.datetime.now(datetime.UTC)
                released = when.date().isoformat()
            text = write(args.folder, released)
            print(f"manifest: wrote {NAME}, {text.count(' sha256 = ')} files")
            return 0
        problems = check(args.folder)
        for p in problems:
            print(f"manifest: {p}", file=sys.stderr)
        print(f"manifest: {'ok' if not problems else f'{len(problems)} difference(s)'}")
        return 0 if not problems else 1
    except (ManifestError, OSError, tomllib.TOMLDecodeError) as e:
        print(f"manifest: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
