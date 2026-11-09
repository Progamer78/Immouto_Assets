import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from resolve_data import ROOT, node_executable, resolve_data


class SourceSelectionTests(unittest.TestCase):
    def test_loose_only_requires_no_node(self):
        with tempfile.TemporaryDirectory() as temp:
            game = Path(temp) / "game"
            sources, details = resolve_data(game, Path(temp) / "cache")
            self.assertEqual(sources["BasicData"], (game / "Data/BasicData").resolve())
            self.assertEqual(details["MapData"]["kind"], "loose")

    def test_original_directory_cannot_be_used_as_extraction_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError, "outside the original"):
                resolve_data(temp, Path(temp) / "cache")

    def test_archive_wins_and_cache_corruption_is_detected(self):
        try:
            node = node_executable()
        except RuntimeError:
            self.skipTest("Node 22+ is required for the archive round-trip test")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            packed = root / "source"
            packed.mkdir()
            (packed / "CommonEvent.dat").write_bytes(b"archived-data")
            game = root / "game"
            loose = game / "Data/BasicData"
            loose.mkdir(parents=True)
            (loose / "CommonEvent.dat").write_bytes(b"stale-loose-data")
            archive = game / "Data/BasicData.wolf"
            module = (ROOT / "tools/vendor/wolftools/archive/dx8.mjs").as_uri()
            script = f"import {{packDx8NoKeyArchive}} from {json.dumps(module)}; await packDx8NoKeyArchive(process.argv[2], process.argv[3]);"
            subprocess.run([node, "--input-type=module", "-", str(packed), str(archive)],
                           input=script, text=True, check=True, capture_output=True, encoding="utf-8")
            sources, details = resolve_data(game, root / "cache")
            selected = sources["BasicData"] / "CommonEvent.dat"
            self.assertEqual(selected.read_bytes(), b"archived-data")
            self.assertEqual(details["BasicData"]["different_from_loose"], ["CommonEvent.dat"])
            self.assertEqual((loose / "CommonEvent.dat").read_bytes(), b"stale-loose-data")
            self.assertEqual(resolve_data(game, root / "cache")[0], sources)
            selected.write_bytes(b"changed")
            with self.assertRaisesRegex(RuntimeError, "incomplete or changed"):
                resolve_data(game, root / "cache")
