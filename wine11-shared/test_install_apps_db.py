import json
import tempfile
import unittest
from pathlib import Path

from install_apps_db import install


class AppsDatabaseTest(unittest.TestCase):
    def test_installs_three_shared_runtime_entries_and_keeps_existing_apps(self):
        with tempfile.TemporaryDirectory() as directory:
            db = Path(directory) / "apps.db"
            db.write_text(json.dumps({"existing": {"exe_name": "Keep Me"}}), encoding="utf-8")

            backup = install(db)
            result = json.loads(db.read_text(encoding="utf-8"))

            self.assertTrue(backup.exists())
            self.assertEqual(result["existing"]["exe_name"], "Keep Me")
            self.assertEqual(result["com.tencent.macexe.com.yybea11"]["exe_name"], "EA App")
            self.assertEqual(result["com.tencent.macexe.com.yybepic11"]["exe_name"], "Epic Games")
            rockstar = result["com.tencent.macexe.com.yybrockstar11"]
            self.assertEqual(rockstar["display_version"], "Wine 11")
            self.assertEqual(rockstar["shortcut_path"], r"C:\YYBFH6\YYBRockstar11Launcher.exe")


if __name__ == "__main__":
    unittest.main()
