# SPDX-License-Identifier: MIT
"""Tests of tools/web/licences.py (D166): each check failing on a web folder seeded to break it, and the bundle's
licence file written from what the build recorded.

    python3 -I -m unittest discover -s tools/web
"""
import contextlib
import io
import json
import os
import pathlib
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import licences  # noqa: E402

MIT = "The MIT License\n\nCopyright (c) 2026 An Invented Author\n\nPermission is hereby granted...\n"


class Folder:
    """A web folder in a temporary directory: .npmrc, package.json, a lockfile and node_modules."""

    def __init__(self, root):
        self.root = pathlib.Path(root)
        (self.root / ".npmrc").write_text("save-exact=true\nignore-scripts=true\nfund=false\naudit=true\n")
        self.meta = {"name": "w", "version": "0.0.0", "dependencies": {"tiny": "1.0.0"}}
        self.lock = {"lockfileVersion": 3, "packages": {"": {}, "node_modules/tiny": {
            "version": "1.0.0", "resolved": licences.REGISTRY + "tiny/-/tiny-1.0.0.tgz", "integrity": "sha512-x"}}}
        self.package("tiny", "1.0.0", "MIT", MIT)
        self.save()

    def save(self):
        (self.root / "package.json").write_text(json.dumps(self.meta))
        (self.root / "package-lock.json").write_text(json.dumps(self.lock))

    def package(self, name, version, licence, text=None, under="node_modules"):
        folder = self.root / under / name
        folder.mkdir(parents=True, exist_ok=True)
        meta = {"name": name, "version": version}
        if licence is not None:
            meta["license"] = licence
        (folder / "package.json").write_text(json.dumps(meta))
        if text is not None:
            (folder / "LICENSE").write_text(text)
        return folder

    def build(self, packages):
        dist = self.root / "dist"
        dist.mkdir(exist_ok=True)
        (dist / "index.html").write_text("<!doctype html>")
        record = self.root / "node_modules" / ".cache" / "graceui"
        record.mkdir(parents=True, exist_ok=True)
        later = time.time() + 5
        (record / "bundled.json").write_text(json.dumps({"bundle": "0.2.0", "packages": packages}))
        os.utime(record / "bundled.json", (later, later))

    def run(self, *argv):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            status = licences.run(list(argv), web=self.root)
        return status, err.getvalue()


class LicencesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.web = Folder(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_real_tree_passes(self):
        if not (licences.WEB / "node_modules").is_dir():
            self.skipTest("web/node_modules is not installed")
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(licences.run(["--check"]), 0, err.getvalue())

    def test_a_clean_folder_passes(self):
        self.assertEqual(self.web.run(), (0, ""))

    def test_a_licence_off_the_list_fails_and_is_named(self):
        self.web.package("copyleft", "2.1.0", "GPL-3.0-only", "GNU GENERAL PUBLIC LICENSE\n")
        status, err = self.web.run()
        self.assertEqual(status, 1)
        self.assertIn("copyleft 2.1.0: GPL-3.0-only is not on the reviewed allowlist", err)

    def test_a_nested_package_is_walked_too(self):
        self.web.package("deep", "1.0.0", "MPL-2.0", "Mozilla Public License\n", under="node_modules/tiny/node_modules")
        status, err = self.web.run()
        self.assertEqual(status, 1)
        self.assertIn("deep 1.0.0: MPL-2.0", err)

    def test_expressions(self):
        self.assertTrue(licences.allowed("MIT"))
        self.assertTrue(licences.allowed("(MIT OR GPL-3.0-only)"))
        self.assertTrue(licences.allowed("Apache-2.0 AND MIT"))
        self.assertFalse(licences.allowed("MIT AND GPL-3.0-only"))
        self.assertFalse(licences.allowed("UNLICENSED"))
        self.assertFalse(licences.allowed("SEE LICENSE IN LICENSE.txt"))

    def test_no_licence_declared_fails(self):
        self.web.package("bare", "1.0.0", None)
        self.assertIn("bare 1.0.0: no licence declared", self.web.run()[1])

    def test_a_clause_on_ai_fails(self):
        self.web.package("fenced", "1.0.0", "MIT", MIT + "\nThis software may not be used to train AI models.\n")
        status, err = self.web.run()
        self.assertEqual(status, 1)
        self.assertIn("fenced 1.0.0: LICENSE has a clause on AI", err)

    def test_an_excluded_project_fails(self):
        self.web.package("me3-helper", "1.0.0", "MIT", MIT)
        self.assertIn("O47 or O48", self.web.run()[1])

    def test_scripts_and_ranges_fail(self):
        (self.web.root / ".npmrc").write_text("save-exact=true\nfund=false\naudit=true\n")
        self.web.meta["dependencies"]["tiny"] = "^1.0.0"
        self.web.save()
        err = self.web.run()[1]
        self.assertIn("ignore-scripts=true is required", err)
        self.assertIn("tiny '^1.0.0' is not an exact version", err)

    def test_the_lockfile_names_the_registry_and_a_hash(self):
        self.web.lock["packages"]["node_modules/tiny"]["resolved"] = "https://example.invalid/tiny.tgz"
        del self.web.lock["packages"]["node_modules/tiny"]["integrity"]
        self.web.save()
        err = self.web.run()[1]
        self.assertIn("is not resolved from", err)
        self.assertIn("has no sha512 integrity", err)

    def test_the_bundles_licence_file(self):
        self.web.build([{"name": "tiny", "version": "1.0.0"}])
        self.assertEqual(self.web.run(), (0, ""))
        text = (self.web.root / "dist" / "licences.txt").read_text()
        self.assertIn("tiny 1.0.0", text)
        self.assertIn("Copyright (c) 2026 An Invented Author", text)
        self.assertEqual(self.web.run("--check"), (0, ""))
        (self.web.root / "dist" / "licences.txt").write_text("stale")
        self.assertEqual(self.web.run("--check")[0], 1)

    def test_a_bundled_package_needs_its_licence_text(self):
        self.web.package("quiet", "1.0.0", "MIT")
        self.web.build([{"name": "quiet", "version": "1.0.0"}])
        self.assertIn("quiet 1.0.0 is in the bundle but carries no licence file", self.web.run()[1])

    def test_a_stale_record_asks_for_a_build(self):
        self.web.build([{"name": "tiny", "version": "1.0.0"}])
        record = self.web.root / "node_modules" / ".cache" / "graceui" / "bundled.json"
        os.utime(record, (0, 0))
        self.assertIn("run the build again", self.web.run()[1])


if __name__ == "__main__":
    unittest.main()
