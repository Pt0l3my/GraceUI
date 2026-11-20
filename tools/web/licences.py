#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""tools/web/licences.py: the web app's supply chain, checked, and the bundle's licence file written (GraceUI DESIGN §7.5;
D166). Python's standard library only; run it with python3 -I from anywhere, after `npm ci --ignore-scripts` in web/:

    python3 -I tools/web/licences.py           check, then write web/dist/licences.txt if the bundle is built
    python3 -I tools/web/licences.py --check   check, and fail if web/dist/licences.txt is not what it would write

The checks:
  1. web/.npmrc keeps installs exact and scriptless: save-exact, ignore-scripts, fund off, audit on.
  2. web/package.json names exact versions only (no range, tag, URL or path).
  3. web/package-lock.json: every package resolved from registry.npmjs.org with a sha512 integrity.
  4. Every package installed under web/node_modules, nested ones too, has a licence on the reviewed allowlist (MIT, ISC,
     BSD-2-Clause, BSD-3-Clause, Apache-2.0, 0BSD, BlueOak-1.0.0, CC0-1.0); an SPDX expression passes when every AND
     part and at least one OR alternative do. Anything else fails, named with its version and its licence.
  5. No licence or notice file in the tree has a clause on the use of AI (EldenNet's rule for dependencies), and no
     package is a project EldenNet's O47 or O48 excludes.
  6. The bundle's licence file: the packages Rollup put into dist/ (Vite's build records them in
     node_modules/.cache/graceui/bundled.json), each with its licence text from its own package; a bundled package with
     no licence file fails. GraceUI's own licence is LICENSE.txt in GraceUI's folder, which the file points to.
Exit status 0 when every check passes.
"""
import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
WEB = ROOT / "web"

ALLOWED = ("MIT", "ISC", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "0BSD", "BlueOak-1.0.0", "CC0-1.0")
NPMRC = {"save-exact": "true", "ignore-scripts": "true", "fund": "false", "audit": "true"}
EXACT = re.compile(r"^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$")
REGISTRY = "https://registry.npmjs.org/"
LICENCE_FILE = re.compile(r"^(licen[cs]e|copying|notice)([.-].*)?$", re.IGNORECASE)
AI_CLAUSE = re.compile(r"\b(artificial intelligence|machine learning|large language models?|LLMs?|NON-AI|generative AI|"
                       r"AI[- ](?:training|models?|systems?|generated|assisted)|neural networks?)\b", re.IGNORECASE)
EXCLUDED = re.compile(r"\bme3\b|fromsoftware-rs|fromsoftware_rs", re.IGNORECASE)
BUNDLE_LICENCES = "licences.txt"


class Problems(list):
    def add(self, message):
        self.append(message)


def allowed(expression):
    """An SPDX expression on the allowlist: every AND part, and one OR alternative, allowed."""
    text = expression.strip()
    while text.startswith("(") and text.endswith(")"):
        text = text[1:-1].strip()
    if re.search(r"[()]", text):
        return all(allowed(part) for part in re.split(r"\s+AND\s+", text)) if " OR " not in text else \
            any(allowed(part) for part in re.split(r"\s+OR\s+", text))
    alternatives = re.split(r"\s+OR\s+", text)
    return any(all(part.strip() in ALLOWED for part in re.split(r"\s+AND\s+", alt)) for alt in alternatives)


def licence_of(meta):
    value = meta.get("license")
    if isinstance(value, dict):
        value = value.get("type")
    if value is None and isinstance(meta.get("licenses"), list):
        kinds = [entry.get("type") for entry in meta["licenses"] if isinstance(entry, dict)]
        value = " OR ".join(k for k in kinds if isinstance(k, str)) or None
    return value if isinstance(value, str) else None


def packages(node_modules):
    """Every installed package: (name, folder, package.json), nested node_modules included."""
    found = []
    if not node_modules.is_dir():
        return found

    def visit(folder):
        for entry in sorted(folder.iterdir()):
            if entry.name.startswith(".") or not entry.is_dir():
                continue
            if entry.name.startswith("@"):
                visit(entry)
                continue
            meta_path = entry / "package.json"
            if meta_path.is_file():
                meta = json.loads(meta_path.read_text(encoding="utf-8"))
                found.append((meta.get("name", entry.name), entry, meta))
            nested = entry / "node_modules"
            if nested.is_dir():
                visit(nested)

    visit(node_modules)
    return found


def licence_files(folder):
    return sorted(p for p in folder.iterdir() if p.is_file() and LICENCE_FILE.match(p.name))


def check_config(web, problems):
    npmrc = web / ".npmrc"
    settings = {}
    if npmrc.is_file():
        for line in npmrc.read_text(encoding="utf-8").splitlines():
            if "=" in line and not line.lstrip().startswith(("#", ";")):
                key, value = line.split("=", 1)
                settings[key.strip()] = value.strip()
    for key, want in NPMRC.items():
        if settings.get(key) != want:
            problems.add(f"web/.npmrc: {key}={want} is required")
    meta = json.loads((web / "package.json").read_text(encoding="utf-8"))
    for section in ("dependencies", "devDependencies", "optionalDependencies", "peerDependencies"):
        for name, version in (meta.get(section) or {}).items():
            if not isinstance(version, str) or not EXACT.match(version):
                problems.add(f"web/package.json: {name} {version!r} is not an exact version")
    lock_path = web / "package-lock.json"
    if not lock_path.is_file():
        problems.add("web/package-lock.json is missing: run npm install once, then commit it")
        return
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    for path, entry in (lock.get("packages") or {}).items():
        if path == "" or entry.get("link"):
            continue
        if not str(entry.get("resolved", "")).startswith(REGISTRY):
            problems.add(f"web/package-lock.json: {path} is not resolved from {REGISTRY}")
        if not str(entry.get("integrity", "")).startswith("sha512-"):
            problems.add(f"web/package-lock.json: {path} has no sha512 integrity")
        if EXCLUDED.search(path):
            problems.add(f"web/package-lock.json: {path} is a project EldenNet's O47 or O48 excludes")


def check_tree(web, problems):
    installed = packages(web / "node_modules")
    if not installed:
        problems.add("web/node_modules is empty: run npm ci --ignore-scripts in web/")
    for name, folder, meta in installed:
        version = meta.get("version", "?")
        licence = licence_of(meta)
        if licence is None:
            problems.add(f"{name} {version}: no licence declared")
        elif not allowed(licence):
            problems.add(f"{name} {version}: {licence} is not on the reviewed allowlist ({', '.join(ALLOWED)})")
        if EXCLUDED.search(name):
            problems.add(f"{name} {version}: a project EldenNet's O47 or O48 excludes")
        for path in licence_files(folder):
            text = path.read_text(encoding="utf-8", errors="replace")
            match = AI_CLAUSE.search(text)
            if match:
                problems.add(f"{name} {version}: {path.name} has a clause on AI ({match.group(0)!r}); read it before "
                             "adopting the package")
    return installed


def bundle_licences(web, installed, problems):
    """The text of dist/licences.txt, or None when the bundle is not built."""
    dist = web / "dist"
    if not (dist / "index.html").is_file():
        return None
    record = web / "node_modules" / ".cache" / "graceui" / "bundled.json"
    if not record.is_file() or record.stat().st_mtime + 1 < (dist / "index.html").stat().st_mtime:
        problems.add("the bundle's package record is missing or older than dist/: run the build again (npm run build)")
        return None
    bundled = json.loads(record.read_text(encoding="utf-8"))
    by_name = {name: (folder, meta) for name, folder, meta in installed}
    lines = [
        f"GraceUI's browser view {bundled.get('bundle', '')}: the licences of what this bundle contains",
        "=" * 72,
        "",
        "GraceUI's own code is under the MIT License: see LICENSE.txt in GraceUI's folder. The scripts beside this file",
        "also contain the packages below, each under its own licence, given in full.",
        "",
    ]
    for entry in bundled.get("packages", []):
        name, version = entry.get("name"), entry.get("version")
        if name not in by_name:
            problems.add(f"the bundle holds {name} {version}, which is not installed")
            continue
        folder, meta = by_name[name]
        if meta.get("version") != version:
            problems.add(f"the bundle holds {name} {version}, but {meta.get('version')} is installed: build again")
        files = [p for p in licence_files(folder) if not p.name.lower().startswith("notice")] + \
                [p for p in licence_files(folder) if p.name.lower().startswith("notice")]
        if not files:
            problems.add(f"{name} {version} is in the bundle but carries no licence file")
            continue
        title = f"{name} {version}"
        lines += [title, "-" * len(title), "", f"Licence:  {licence_of(meta)}",
                  f"Source:   {REGISTRY}{name}/-/{name.split('/')[-1]}-{version}.tgz", ""]
        for path in files:
            lines += ["    " + line if line else "" for line in path.read_text(encoding="utf-8").rstrip().splitlines()]
            lines.append("")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def run(argv, web=WEB):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if dist/licences.txt is not what it would write")
    args = parser.parse_args(argv)
    problems = Problems()
    check_config(web, problems)
    installed = check_tree(web, problems)
    text = bundle_licences(web, installed, problems)
    if problems:
        for problem in problems:
            print(f"licences: {problem}", file=sys.stderr)
        return 1
    target = web / "dist" / BUNDLE_LICENCES
    if text is not None:
        if args.check:
            if not target.is_file() or target.read_text(encoding="utf-8") != text:
                print(f"licences: web/dist/{BUNDLE_LICENCES} is not what the bundle holds: run "
                      "python3 -I tools/web/licences.py", file=sys.stderr)
                return 1
        else:
            target.write_text(text, encoding="utf-8")
    print(f"licences: ok, {len(installed)} packages installed" +
          ("" if text is None else f"; web/dist/{BUNDLE_LICENCES} " + ("checked" if args.check else "written")))
    return 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
