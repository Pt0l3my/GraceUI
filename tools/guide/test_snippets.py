# SPDX-License-Identifier: MIT
"""Tests of tools/guide/snippets.py (D284): the guide's code is the examples' own, and each rule fails on a guide seeded
to break it.

    python3 -I -m unittest discover -s tools/guide
"""
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import snippets  # noqa: E402

SOURCE = """int start(void) {
  int found = 0;

  if (found == 0) {
    return 1;
  }
  return 0;
}
"""


class Snippets(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root)
        (self.root / "docs" / "modders").mkdir(parents=True)
        (self.root / "examples" / "hello").mkdir(parents=True)
        (self.root / "examples" / "hello" / "hello.c").write_text(SOURCE, encoding="utf-8")
        (self.root / "outside.c").write_text(SOURCE, encoding="utf-8")
        (self.root / "src" / "graceui-mock" / "tests").mkdir(parents=True)
        (self.root / "src" / "graceui-mock" / "tests" / "a_test.c").write_text(SOURCE, encoding="utf-8")
        saved = (snippets.ROOT, snippets.GUIDE, snippets.EXAMPLES, snippets.MOCK)
        snippets.ROOT, snippets.GUIDE = self.root, self.root / "docs" / "modders"
        snippets.EXAMPLES, snippets.MOCK = self.root / "examples", self.root / "src" / "graceui-mock"
        self.addCleanup(lambda: setattr(snippets, "ROOT", saved[0]) or setattr(snippets, "GUIDE", saved[1]) or
                        setattr(snippets, "EXAMPLES", saved[2]) or setattr(snippets, "MOCK", saved[3]))

    def guide(self, text):
        (snippets.GUIDE / "part.md").write_text(text, encoding="utf-8")
        return snippets.check()

    def test_a_marked_block_found_in_its_file(self):
        failures, checked = self.guide("Its check:\n\n<!-- snippet: examples/hello/hello.c -->\n\n"
                                       "    if (found == 0) {\n      return 1;\n    }\n\nAfter it.\n")
        self.assertEqual((failures, checked), ([], 1))

    def test_a_block_with_a_blank_line_inside_it(self):
        failures, checked = self.guide("<!-- snippet: examples/hello/hello.c -->\n\n"
                                       "    int found = 0;\n\n    if (found == 0) {\n")
        self.assertEqual((failures, checked), ([], 1))

    def test_its_lines_relative_indentation_counts(self):
        failures, _ = self.guide("<!-- snippet: examples/hello/hello.c -->\n\n    }\n    return 0;\n    }\n")
        self.assertEqual(len(failures), 1)  # in the file the last } is two spaces left of the others

    def test_a_changed_line_fails(self):
        failures, _ = self.guide("<!-- snippet: examples/hello/hello.c -->\n\n    if (found != 0) {\n      return 1;\n    }\n")
        self.assertIn("not a run of examples/hello/hello.c", failures[0])

    def test_another_indentation_fails(self):
        failures, _ = self.guide("<!-- snippet: examples/hello/hello.c -->\n\n    if (found == 0) {\n    return 1;\n    }\n")
        self.assertEqual(len(failures), 1)

    def test_a_block_with_no_marker_fails(self):
        failures, _ = self.guide("Its check:\n\n    if (found == 0) {\n")
        self.assertIn("no snippet marker", failures[0])

    def test_a_marker_with_no_block_fails(self):
        failures, _ = self.guide("<!-- snippet: examples/hello/hello.c -->\n\nNo code here.\n")
        self.assertIn("no code block after it", failures[0])

    def test_a_marker_outside_the_examples_fails(self):
        for where in ("outside.c", "examples/../outside.c", "examples/hello/missing.c", "src/graceui-mock/../../outside.c",
                      "src/other/a_test.c"):
            failures, _ = self.guide(f"<!-- snippet: {where} -->\n\n    int found = 0;\n")
            self.assertIn("no file of examples/", failures[0], where)

    def test_a_block_of_the_mocks(self):
        failures, checked = self.guide("<!-- snippet: src/graceui-mock/tests/a_test.c -->\n\n    int found = 0;\n")
        self.assertEqual((failures, checked), ([], 1))

    def test_commands_and_frames_are_not_looked_for(self):
        failures, checked = self.guide("<!-- snippet: shell -->\n\n    python3 -I tools/x.py\n\n"
                                       "<!-- snippet: frame -->\n\n    {\"v\":1,\"t\":\"state\"}\n")
        self.assertEqual((failures, checked), ([], 0))

    def test_a_list_items_indented_paragraph_is_not_code(self):
        failures, checked = self.guide("- An item\n\n    its next paragraph, indented.\n\n| a | b |\n\n    a cell's.\n")
        self.assertEqual((failures, checked), ([], 0))


class TheGuide(unittest.TestCase):
    def test_the_guide_passes(self):
        failures, checked = snippets.check()
        self.assertEqual(failures, [])
        self.assertGreater(checked, 0)


if __name__ == "__main__":
    unittest.main()
