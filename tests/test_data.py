import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from wolf_data import FormatError, Reader, command, load_project
from prepare_game import DEFAULT_GAME, all_commands


class BinarySafetyTests(unittest.TestCase):
    def test_truncation_is_an_error_with_location(self):
        with self.assertRaisesRegex(FormatError, "fixture:0x0"):
            Reader(b"\x00", "fixture").u32()

    def test_unknown_command_is_retained(self):
        raw = b"\x02" + struct.pack("<Ii", 765432, -123) + b"\x03\x00\x00"
        parsed = command(Reader(raw))
        self.assertEqual((parsed["code"], parsed["args"], parsed["indent"]), (765432, [-123], 3))

    def test_malformed_string_is_rejected(self):
        with self.assertRaises(FormatError):
            Reader(struct.pack("<I", 2) + b"ab").string()

    def test_trailing_data_is_not_silently_ignored(self):
        with self.assertRaises(FormatError):
            Reader(b"garbage").finish()


@unittest.skipUnless(DEFAULT_GAME.exists(), "Original game is intentionally not committed")
class OriginalGameTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.project = load_project(DEFAULT_GAME)

    def test_complete_common_file(self):
        events = self.project["common"]["events"]
        self.assertEqual(len(events), 550)
        self.assertEqual(sum(len(e["commands"]) for e in events), 167843)
        self.assertEqual(len({e["id"] for e in events}), 550)

    def test_original_display_settings(self):
        game = self.project["game"]
        self.assertEqual((game["width"], game["height"], game["fps"]), (1024, 576, 60))

    def test_all_maps_and_databases(self):
        self.assertEqual(len(self.project["maps"]), 9)
        self.assertEqual({k: len(v["types"]) for k, v in self.project["databases"].items()},
                         {"user": 40, "mutable": 30, "system": 28})
        for db in self.project["databases"].values():
            for entry in db["types"]:
                self.assertEqual(len(entry["positions"]), len(entry["schema"]["fields"]))
                for row in entry["rows"]:
                    self.assertEqual(len(row), len(entry["positions"]))

    def test_map_only_commands_are_included(self):
        common = {c["code"] for e in self.project["common"]["events"] for c in e["commands"]}
        all_codes = {c["code"] for _, c in all_commands(self.project)}
        self.assertEqual(all_codes - common, {151, 161, 162})


if __name__ == "__main__":
    unittest.main()
