# SPDX-License-Identifier: MIT
"""Tests of tools/protocol/generate.py (D159): the outputs current, and each check of the schema failing on a schema
seeded to break it.

    python3 -I -m unittest discover -s tools/protocol
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import generate  # noqa: E402


def schema():
    return generate.SCHEMA.read_text(encoding="utf-8")


class Outputs(unittest.TestCase):
    def test_outputs_are_current(self):
        for path, text in generate.outputs(generate.load()).items():
            self.assertEqual(path.read_text(encoding="utf-8"), text, path.name)

    def test_every_kind_in_every_output(self):
        data = generate.load()
        header, _ = generate.build_cpp(data)
        ts = generate.build_ts(data)
        for name in data["messages"]:
            self.assertIn(f"struct {generate.camel(name)} {{", header)
            self.assertIn(f'  t: "{name}";', ts)
        for name in data["commands"]:
            self.assertIn(f'"{name}": {generate.camel(name)}Args;', ts)


class Seeded(unittest.TestCase):
    def refused(self, old, new, words):
        text = schema()
        self.assertIn(old, text)
        with self.assertRaises(generate.SchemaError) as caught:
            generate.load(text.replace(old, new, 1))
        self.assertIn(words, str(caught.exception))

    def test_unknown_type(self):
        self.refused('{ name = "rev", type = "u64" }', '{ name = "rev", type = "u128" }', "unknown type")

    def test_unbounded_string(self):
        self.refused('{ name = "detail", type = "string", max = 256 }', '{ name = "detail", type = "string" }',
                     "needs max")

    def test_unbounded_list(self):
        self.refused('{ name = "caps", type = "list.id", max = 64 },\n  { name = "ticket"',
                     '{ name = "caps", type = "list.id" },\n  { name = "ticket"', "needs max items")

    def test_field_twice(self):
        self.refused('{ name = "topic", type = "id" },\n  { name = "rev"',
                     '{ name = "topic", type = "id" },\n  { name = "topic", type = "id" },\n  { name = "rev"', "twice")

    def test_envelope_field(self):
        self.refused('{ name = "reason", type = "enum.bye_reason" }',
                     '{ name = "reason", type = "enum.bye_reason" },\n  { name = "space", type = "id" }', "envelope")

    def test_unknown_enum(self):
        self.refused('type = "enum.bye_reason"', 'type = "enum.farewell"', "no enum named")

    def test_bad_direction(self):
        self.refused('[messages.bye]\nfrom = "grace"', '[messages.bye]\nfrom = "anyone"', "from is not one of")


if __name__ == "__main__":
    unittest.main()
