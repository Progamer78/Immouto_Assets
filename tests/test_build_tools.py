import datetime
from pathlib import Path
import stat
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from extract_assets import extract
from signing_options import export_options


class BuildToolTests(unittest.TestCase):
    def test_escaping_zip_member_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with zipfile.ZipFile(root / "bad.zip", "w") as archive:
                archive.writestr("../escape.txt", "bad")
            with self.assertRaises(ValueError):
                extract(root / "bad.zip", root / "out")
            self.assertFalse((root / "escape.txt").exists())

    def test_zip_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            member = zipfile.ZipInfo("link")
            member.create_system = 3
            member.external_attr = (stat.S_IFLNK | 0o777) << 16
            with zipfile.ZipFile(root / "bad.zip", "w") as archive:
                archive.writestr(member, "../escape")
            with self.assertRaises(ValueError):
                extract(root / "bad.zip", root / "out")

    def test_signing_profile_identity_and_export_method(self):
        profile = {"TeamIdentifier": ["TEAM"], "UUID": "profile-id", "ProvisionedDevices": ["phone"],
                   "ExpirationDate": datetime.datetime(2099, 1, 1),
                   "Entitlements": {"application-identifier": "TEAM.com.local.*", "get-task-allow": True}}
        result = export_options(profile, "com.local.monochromeport")
        self.assertEqual(result["method"], "debugging")
        profile["Entitlements"]["get-task-allow"] = False
        self.assertEqual(export_options(profile, "com.local.monochromeport")["method"], "release-testing")
        with self.assertRaises(ValueError):
            export_options(profile, "com.other.game")
        profile["ExpirationDate"] = datetime.datetime(2000, 1, 1)
        with self.assertRaises(ValueError):
            export_options(profile, "com.local.monochromeport")


if __name__ == "__main__":
    unittest.main()
