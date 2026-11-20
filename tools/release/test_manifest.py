# SPDX-License-Identifier: MIT
"""Tests of tools/release/manifest.py (D52): a written graceui.toml lists every file but itself and user/, keeps to
grace-serve's subset, reads back, and `check` names each kind of difference.

    python3 -I -m unittest discover -s tools/release
"""
import hashlib
import pathlib
import re
import sys
import tempfile
import tomllib
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import manifest  # noqa: E402

# grace-serve's subset (files.h): a comment, a blank line, a table header, or one key = value.
LINE = re.compile(r'(#.*|\[[a-z]+\]|[a-z]+ = .+|"[A-Za-z0-9\-._~/]+" = \{ size = \d+, sha256 = "[0-9a-f]{64}" \})')


class Manifest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.folder = pathlib.Path(self.tmp.name) / "GraceUI"
        for name, data in [("graceui.dll", b"MZ\0\0dll"), ("LICENSE.txt", b"MIT\n"), ("web/index.html", b"<!doctype html>"),
                           ("web/assets/app-1a2b3c.js", b"export {}"), ("fonts/OFL.txt", b"OFL\n"),
                           ("user/settings.toml", b"secret = 1\n")]:
            path = self.folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    def tearDown(self):
        self.tmp.cleanup()

    def test_written_lists_every_file_but_itself_and_user(self):
        text = manifest.write(self.folder, "2026-11-20")
        for line in text.splitlines():
            self.assertTrue(line == "" or LINE.fullmatch(line), line)
        data = tomllib.loads(text)
        self.assertEqual(data["graceui"], manifest.graceui_version())
        self.assertEqual(data["abi"][0], 1)
        self.assertEqual(data["released"], "2026-11-20")
        self.assertEqual(sorted(data["files"]), ["LICENSE.txt", "fonts/OFL.txt", "graceui.dll",
                                                 "web/assets/app-1a2b3c.js", "web/index.html"])
        self.assertEqual(data["files"]["web/index.html"],
                         {"size": 15, "sha256": hashlib.sha256(b"<!doctype html>").hexdigest()})
        self.assertEqual(manifest.check(self.folder), [])
        self.assertEqual(manifest.write(self.folder, "2026-11-20"), text)  # the same folder, the same file

    def test_check_names_each_difference(self):
        manifest.write(self.folder, "2026-11-20")
        (self.folder / "web/index.html").write_bytes(b"<!doctype html>!")
        (self.folder / "LICENSE.txt").write_bytes(b"MIT\r")
        (self.folder / "fonts/OFL.txt").unlink()
        (self.folder / "web/stale.js").write_bytes(b"")
        (self.folder / "user/log.txt").write_bytes(b"not listed and not counted")
        self.assertEqual(manifest.check(self.folder), [
            "fonts/OFL.txt: listed, missing",
            "web/stale.js: present, not listed",
            "LICENSE.txt: its SHA-256 differs from the listed one",
            "web/index.html: 16 bytes, listed 15",
        ])

    def test_refuses_what_a_release_may_not_carry(self):
        for bad in ["web/.hidden.js", "web/a b.js", 'web/q"uote.js', "web/ü.js"]:
            with self.subTest(name=bad):
                path = self.folder / bad
                path.write_bytes(b"")
                with self.assertRaises(manifest.ManifestError):
                    manifest.write(self.folder, "2026-11-20")
                path.unlink()
        (self.folder / "graceui.dll").unlink()
        with self.assertRaisesRegex(manifest.ManifestError, "no graceui.dll"):
            manifest.write(self.folder, "2026-11-20")

    def test_refuses_a_link(self):
        (self.folder / "web/link.js").symlink_to(self.folder / "LICENSE.txt")
        with self.assertRaisesRegex(manifest.ManifestError, "not a plain file"):
            manifest.write(self.folder, "2026-11-20")

    def test_the_golden_grace_serve_reads(self):
        # fixtures/release/graceui.toml is what grace-serve's reader is tested on (grace_serve_io_tests): the writer's
        # form, kept in one file the two sides share.
        files = [("LICENSE.txt", 1064, "1" * 64), ("fonts/Cinzel-Regular.ttf", 66996, "2" * 64),
                 ("graceui.dll", 1229824, "3" * 64), ("web/assets/app-1a2b3c.js", 4096, "4" * 64),
                 ("web/index.html", 2311, "5" * 64)]
        golden = manifest.ROOT / "fixtures" / "release" / "graceui.toml"
        self.assertEqual(manifest.render("0.2.0", [1, 0], [1, 0], [], "2026-11-20", files),
                         golden.read_text(encoding="utf-8"))

    def test_refuses_a_bad_date_and_a_bad_entry(self):
        with self.assertRaises(manifest.ManifestError):
            manifest.write(self.folder, "2026-13-01")
        manifest.write(self.folder, "2026-11-20")
        path = self.folder / manifest.NAME
        path.write_text(path.read_text().replace('sha256 = "', 'sha256 = "A', 1)[:-1], encoding="utf-8")
        with self.assertRaises((manifest.ManifestError, tomllib.TOMLDecodeError)):
            manifest.check(self.folder)


if __name__ == "__main__":
    unittest.main()
