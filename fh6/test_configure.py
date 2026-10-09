import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import configure as c


def fixture():
    root = ET.Element("UserConfig", Version="52", HardwareHash="12345")
    settings = ET.SubElement(root, "settings")
    for tag in set(c.SETTINGS) | set(c.DYNAMIC) | {"ResolutionWidth", "ResolutionHeight"}:
        ET.SubElement(settings, tag, value="old")
    ET.SubElement(settings, "MasterVolume", value="0.63")
    ET.SubElement(settings, "FollowHighFOV", value="73")
    ET.SubElement(settings, "MegaTextureHighSpeedBudget", value="0.777")
    selections = ET.SubElement(root, "selections")
    for key in c.OPTIONS:
        ET.SubElement(selections, "option", id=key, value="0")
    ET.SubElement(selections, "option", id="AudioQuality", value="3")
    return ET.tostring(root)


class PresetTests(unittest.TestCase):
    def test_only_graphics_changes(self):
        result = ET.fromstring(c.graphics(fixture(), (2560, 1600)))
        self.assertEqual(result.find("settings/ResolutionWidth").get("value"), "2560")
        self.assertEqual(result.find("selections/option[@id='FSR3Mode']").get("value"), "2")
        self.assertEqual(result.find("selections/option[@id='DLSSGMode']").get("value"), "0")
        for tag, value in (("MasterVolume", "0.63"), ("FollowHighFOV", "73"), ("MegaTextureHighSpeedBudget", "0.777")):
            self.assertEqual(result.find("settings/" + tag).get("value"), value)
        self.assertEqual(result.get("HardwareHash"), "12345")
        self.assertEqual(result.find("selections/option[@id='AudioQuality']").get("value"), "3")

    def test_idempotent(self):
        first = c.graphics(fixture(), (1920, 1200))
        self.assertEqual(c.graphics(first, (1920, 1200)), first)

    def test_unknown_schema_and_missing_option_rejected(self):
        with self.assertRaises(c.ConfigError):
            c.graphics(fixture().replace(b'Version="52"', b'Version="53"'), (2560, 1600))
        root = ET.fromstring(fixture())
        selections = root.find("selections")
        selections.remove(selections.find("option[@id='FSR3Mode']"))
        with self.assertRaises(c.ConfigError):
            c.graphics(ET.tostring(root), (2560, 1600))

    def test_runtime_merge_preserves_other_settings(self):
        original = {"formatVersion": 3, "engineID": "x64-crossover26.3-r20", "frameGen": 1,
                    "pins": [{"name": "Steam", "renderer": "dxmt"}],
                    "environment": {"KEEP": "yes"}, "gameEnvironment": {"other-game": {"FOO": "bar"}}}
        result = json.loads(c.runtime(json.dumps(original).encode()))
        self.assertEqual(result["environment"]["KEEP"], "yes")
        self.assertEqual(result["pins"], original["pins"])
        self.assertEqual(result["gameEnvironment"]["other-game"], {"FOO": "bar"})
        self.assertEqual(result["gameEnvironment"]["forza-horizon-6"], c.GPU)
        self.assertEqual(result["frameGen"], 1)
        original["engineID"] = "unknown"
        with self.assertRaises(c.ConfigError):
            c.runtime(json.dumps(original).encode())

    def test_process_guard(self):
        result = type("Result", (), {"stdout": "/Applications/Highball.app/Contents/MacOS/Highball\n"})()
        with patch.object(c.subprocess, "run", return_value=result):
            with self.assertRaises(c.ConfigError):
                c.assert_idle()
        with patch.object(c.subprocess, "run", side_effect=OSError("denied")):
            with self.assertRaises(c.ConfigError):
                c.assert_idle()


class TransactionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.bottle = Path(self.temp.name) / "bottles/Games"
        self.xml = self.bottle / "drive_c/users/tester" / c.CONFIG_SUFFIX
        self.xml.parent.mkdir(parents=True)
        self.old = fixture()
        self.new = c.graphics(self.old, (2560, 1600))
        self.xml.write_bytes(self.old)
        (self.bottle / "bottle.json").write_text('{"formatVersion":3}')

    def tearDown(self):
        self.temp.cleanup()

    def test_backup_restore_and_privacy_modes(self):
        saved = c.apply_changes(self.bottle, [(self.xml, self.old, self.new)])
        self.assertEqual(self.xml.read_bytes(), self.new)
        self.assertEqual((saved / "0.original").read_bytes(), self.old)
        self.assertEqual(saved.stat().st_mode & 0o777, 0o700)
        self.assertEqual((saved / "0.original").stat().st_mode & 0o777, 0o600)
        with patch.object(c, "assert_idle"):
            c.restore(self.bottle, saved, apply=True)
        self.assertEqual(self.xml.read_bytes(), self.old)

    def test_restore_preserves_later_user_edit(self):
        saved = c.apply_changes(self.bottle, [(self.xml, self.old, self.new)])
        self.xml.write_bytes(self.new + b"<!-- later edit -->")
        with self.assertRaises(c.ConfigError):
            c.restore(self.bottle, saved)
        self.assertIn(b"later edit", self.xml.read_bytes())

    def test_duplicate_config_and_external_symlink_rejected(self):
        second = self.bottle / "drive_c/users/other" / c.CONFIG_SUFFIX
        second.parent.mkdir(parents=True)
        second.write_bytes(self.old)
        with self.assertRaises(c.ConfigError):
            c.config_path(self.bottle)
        second.unlink()
        self.xml.unlink()
        outside = Path(self.temp.name) / "external"
        outside.write_bytes(self.old)
        self.xml.symlink_to(outside)
        with self.assertRaises(c.ConfigError):
            c.config_path(self.bottle)

    def test_rollback_partial_write(self):
        bottle_config = self.bottle / "bottle.json"
        old = bottle_config.read_bytes()
        actual_write = c.atomic_write
        count = 0

        def fail_second(path, data, mode):
            nonlocal count
            count += 1
            if count == 2:
                raise OSError("simulated write failure")
            actual_write(path, data, mode)

        with patch.object(c, "atomic_write", side_effect=fail_second):
            with self.assertRaises(OSError):
                c.apply_changes(self.bottle, [(self.xml, self.old, self.new), (bottle_config, old, b"{}")])
        self.assertEqual(self.xml.read_bytes(), self.old)
        self.assertEqual(bottle_config.read_bytes(), old)

    def test_concurrent_edit_rejected(self):
        self.xml.write_bytes(b"changed")
        with self.assertRaises(c.ConfigError):
            c.apply_changes(self.bottle, [(self.xml, self.old, self.new)])
        self.assertFalse(c.backup_root(self.bottle).exists())

    def test_restore_rejects_arbitrary_target(self):
        saved = c.apply_changes(self.bottle, [(self.xml, self.old, self.new)])
        manifest = json.loads((saved / "manifest.json").read_text())
        manifest["files"][0]["path"] = "../../external"
        (saved / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaises(c.ConfigError):
            c.restore(self.bottle, saved)


if __name__ == "__main__":
    unittest.main()
