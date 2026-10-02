#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import hashlib
import json
import os
from pathlib import Path
import plistlib
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zlib


REPO = Path(__file__).resolve().parents[1]


def load_module(test_home: Path, applications: Path):
    os.environ["SKY_YYB_TEST_HOME"] = str(test_home)
    os.environ["SKY_YYB_TEST_APPLICATIONS"] = str(applications)
    spec = importlib.util.spec_from_file_location("sky_yyb_fix", REPO / "sky_yyb_fix.py")
    module = importlib.util.module_from_spec(spec)
    assert spec.loader
    spec.loader.exec_module(module)
    return module


def make_preferences() -> bytes:
    names = [
        b"quality_fps\0",
        b"kUserPreference_MotionBlurScalar\0",
        b"kUserPreference_Fullscreen\0",
    ]
    counts = (3, 0, 0, 0)
    string_base = 28 + 24
    strings = b"".join(names)
    header = b"PREF" + b"\0" * 4 + struct.pack("<4I", *counts) + struct.pack("<I", string_base)
    records = (
        struct.pack("<II", 0, 30)
        + struct.pack("<II", len(names[0]), 0x3F800000)
        + struct.pack("<II", len(names[0]) + len(names[1]), 1)
    )
    return header + records + strings


def preference_u32(data: bytes, name: bytes) -> int:
    count = sum(struct.unpack_from("<4I", data, 8))
    base = struct.unpack_from("<I", data, 24)[0]
    for index in range(count):
        offset, value = struct.unpack_from("<II", data, 28 + index * 8)
        if data[base + offset:].split(b"\0", 1)[0] == name:
            return value
    raise AssertionError(f"missing preference: {name!r}")


class FixTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.home = root / "home"
        self.apps = root / "Applications"
        self.module = load_module(self.home, self.apps)
        self.module.YYB_APP.mkdir(parents=True)
        self.module.PREFIX.mkdir(parents=True)
        self.module.SKY_DIR.mkdir(parents=True)
        self.module.SKY_EXE.write_bytes(b"MZ-test")
        self.module.USER_REG.write_text("WINE REGISTRY Version 2\n", encoding="utf-8")
        self.module.SYSTEM_REG.write_text("WINE REGISTRY Version 2\n", encoding="utf-8")
        self.module.PREFERENCES.parent.mkdir(parents=True)
        self.module.PREFERENCES.write_bytes(make_preferences())

        shortcut = self.module.PREFIX / "drive_c/users/test/AppData/Roaming/Microsoft/Windows/Start Menu/Programs/光·遇.url"
        shortcut.parent.mkdir(parents=True)
        shortcut.write_bytes(b"[InternetShortcut]\r\nURL=fevergames://mygame/?gameId=63\r\n")
        self.shortcut = shortcut

        self.module.APPS_DB.parent.mkdir(parents=True)
        self.module.APPS_DB.write_text(json.dumps({
            self.module.PACKAGE_PARENT: {
                "entry_path": r"C:\\old.exe",
                "install_path": r"C:\\old",
            },
            self.module.PACKAGE_SKY_PREFIX + "fixture": {
                "game_id": "63",
                "install_path": r"C:\\FeverApps\\sky",
                "entry_path": r"C:\\FeverApps\\sky\\Sky.exe",
            },
        }), encoding="utf-8")

        self.module.PUBLIC_MMKV.parent.mkdir(parents=True)
        payload = b"exe_app_retina_zoom_ratio_" + self.module.PACKAGE_PARENT.encode() + b"\x04\x031.0"
        blob = struct.pack("<I", len(payload)) + payload + b"\0" * 16
        self.module.PUBLIC_MMKV.write_bytes(blob)
        meta = bytearray(112)
        struct.pack_into("<III", meta, 0, zlib.crc32(payload) & 0xFFFFFFFF, 4, 1)
        struct.pack_into("<III", meta, 28, len(payload), len(payload), zlib.crc32(payload) & 0xFFFFFFFF)
        self.module.PUBLIC_MMKV_CRC.write_bytes(meta)

    def tearDown(self):
        self.temp.cleanup()
        os.environ.pop("SKY_YYB_TEST_HOME", None)
        os.environ.pop("SKY_YYB_TEST_APPLICATIONS", None)
        os.environ.pop("SKY_YYB_TEST_CHIP", None)

    def test_complete_fix_and_restore(self):
        original_user = self.module.USER_REG.read_bytes()
        original_preferences = self.module.PREFERENCES.read_bytes()
        changes = self.module.apply_fix(60)
        self.assertTrue(changes)

        database = json.loads(self.module.APPS_DB.read_text())
        child = database[self.module.PACKAGE_SKY_PREFIX + "fixture"]
        self.assertEqual(child["entry_path"], "fevergames://mygame/?gameId=63&autoRun=1")
        self.assertIn('"RetinaMode"="Y"', self.module.USER_REG.read_text())
        self.assertIn(
            '"C:\\\\FeverApps\\\\sky\\\\Sky.exe"="~ HIGHDPIAWARE"',
            self.module.USER_REG.read_text(),
        )
        self.assertIn(b"\x04\x032.0", self.module.PUBLIC_MMKV.read_bytes())
        self.assertIn(b"gameId=63&autoRun=1", self.shortcut.read_bytes())

        prefs = self.module.PREFERENCES.read_bytes()
        self.assertEqual(preference_u32(prefs, b"quality_fps"), 60)
        self.assertEqual(preference_u32(prefs, b"kUserPreference_Fullscreen"), 0)
        self.module.restore_latest()
        self.assertEqual(self.module.USER_REG.read_bytes(), original_user)
        self.assertEqual(self.module.PREFERENCES.read_bytes(), original_preferences)
        self.assertNotIn(b"autoRun=1", self.shortcut.read_bytes())

    def test_restore_preserves_executable_mode(self):
        executable = self.home / "tool.dylib"
        executable.write_bytes(b"original")
        executable.chmod(0o755)
        backups = self.module.BackupSet()
        backups.capture(executable)
        executable.write_bytes(b"changed")
        executable.chmod(0o600)
        backups.finish()

        self.module.restore_from(backups.root, announce=False)

        self.assertEqual(executable.read_bytes(), b"original")
        self.assertEqual(executable.stat().st_mode & 0o777, 0o755)

    def test_unknown_preferences_are_rejected(self):
        self.module.PREFERENCES.write_bytes(b"not-a-preference-file")
        with self.assertRaises(self.module.FixError):
            self.module.apply_fix(60)

    def test_first_session_preferences_do_not_rollback_hd(self):
        name = b"kUserPreference_EnableReadbackBuffer\0"
        data = b"PREF" + struct.pack("<6I", 2, 1, 0, 0, 0, 36) + struct.pack("<II", 0, 1) + name
        self.module.PREFERENCES.write_bytes(data)
        self.module.apply_fix(60)
        self.assertEqual(self.module.PREFERENCES.read_bytes(), data)
        self.assertIn('"RetinaMode"="Y"', self.module.USER_REG.read_text())

    def test_fever_window_fits_retina_visible_frame_without_lowering_backing_scale(self):
        self.module.USER_REG.write_text(
            "WINE REGISTRY Version 2\n\n"
            "[Software\\\\FeverGames\\\\FeverGamesInstaller\\\\window] 1\n"
            '"DefaultSize"="@Size(1280 712)"\n'
            '"SizeChanged"="@Size(1280 712)"\n',
            encoding="utf-8",
        )

        changes = self.module.patch_registries(self.module.BackupSet())

        registry = self.module.USER_REG.read_text(encoding="utf-8")
        self.assertIn('"DefaultSize"="@Size(1240 650)"', registry)
        self.assertIn('"SizeChanged"="@Size(1240 650)"', registry)
        self.assertIn('"RetinaMode"="Y"', registry)
        self.assertTrue(any("1240×650" in item for item in changes))

    def test_window_wineloader_patch_is_hash_and_offset_guarded(self):
        original = bytearray(self.module.WINDOW_WINELOADER_PATCH_OFFSET + 64)
        start = self.module.WINDOW_WINELOADER_PATCH_OFFSET
        before = self.module.WINDOW_WINELOADER_ORIGINAL
        original[start:start + len(before)] = before
        self.module.WINDOW_WINELOADER_SHA256 = hashlib.sha256(original).hexdigest()
        self.module.WINDOW_WINELOADER_BUILDS = (
            (self.module.WINDOW_WINELOADER_SHA256, start),
        )

        patched = self.module.patch_window_wineloader_image(bytes(original))

        replacement = self.module.WINDOW_WINELOADER_REPLACEMENT
        self.assertEqual(patched[start:start + len(replacement)], replacement)
        tampered = bytearray(original)
        tampered[start] ^= 1
        self.module.WINDOW_WINELOADER_SHA256 = hashlib.sha256(tampered).hexdigest()
        self.module.WINDOW_WINELOADER_BUILDS = (
            (self.module.WINDOW_WINELOADER_SHA256, start),
        )
        with self.assertRaises(self.module.FixError):
            self.module.patch_window_wineloader_image(bytes(tampered))

    def test_window_launch_agent_uses_current_home_without_shell_expansion(self):
        payload = self.module.window_launch_agent_payload()

        self.assertEqual(payload["Label"], "com.skyyybmacfix.window")
        self.assertEqual(payload["ProgramArguments"], [str(self.module.WINDOW_WATCH)])
        self.assertTrue(str(payload["StandardOutPath"]).startswith(str(self.home)))
        self.assertNotIn("/Users/tonytan", plistlib.dumps(payload).decode("utf-8"))

    def test_fresh_mmkv_gets_missing_retina_keys_and_valid_metadata(self):
        payload = b"\x00\x03foo\x04\x03bar"
        blob = struct.pack("<I", len(payload)) + payload + b"\0" * 1024
        meta = bytearray(112)
        crc = zlib.crc32(payload) & 0xffffffff
        struct.pack_into("<III", meta, 0, crc, 4, 7)
        struct.pack_into("<III", meta, 28, len(payload), len(payload), crc)
        self.module.PUBLIC_MMKV.write_bytes(blob)
        self.module.PUBLIC_MMKV_CRC.write_bytes(meta)
        self.module.apply_fix(60)
        result = self.module.PUBLIC_MMKV.read_bytes()
        result_meta = self.module.PUBLIC_MMKV_CRC.read_bytes()
        size = struct.unpack_from("<I", result)[0]
        self.assertTrue(result[4:].startswith(payload))
        self.assertIn(b"exe_app_retina_zoom_ratio_" + self.module.PACKAGE_PARENT.encode() + b"\x04\x032.0", result[:size+4])
        self.assertIn(b"fixture\x04\x032.0", result[:size+4])
        self.assertEqual(struct.unpack_from("<III", result_meta, 28), (size, size, zlib.crc32(result[4:size+4]) & 0xffffffff))
        self.module.restore_latest()
        self.assertEqual(self.module.PUBLIC_MMKV.read_bytes(), blob)
        self.assertEqual(self.module.PUBLIC_MMKV_CRC.read_bytes(), meta)

    def test_discovers_both_yyb_internal_and_user_facing_shortcuts(self):
        package = self.module.PACKAGE_PARENT
        expected = []
        for root in (
            self.module.YYB_INTERNAL_SHORTCUTS,
            self.module.YYB_SHORTCUTS,
        ):
            app = root / f"{package}.app"
            app.joinpath("Contents").mkdir(parents=True)
            with app.joinpath("Contents/Info.plist").open("wb") as stream:
                plistlib.dump({"YYBPackageName": package}, stream)
            expected.append(app)
        self.assertEqual(self.module.all_shortcuts_for(package), expected)

    def test_m4_can_wrap_only_the_broken_sky_shortcut(self):
        child_package = self.module.PACKAGE_SKY_PREFIX + "fixture"
        apps = {}
        for package in (self.module.PACKAGE_PARENT, child_package):
            app = self.module.YYB_INTERNAL_SHORTCUTS / f"{package}.app"
            executable = app / "Contents/MacOS/YYBPackage"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(("original-" + package).encode())
            with (app / "Contents/Info.plist").open("wb") as stream:
                plistlib.dump({"YYBPackageName": package}, stream)
            apps[package] = executable

        self.module.SHORTCUT_WRAPPER.parent.mkdir(parents=True)
        wrapper = b"test-" + self.module.SHORTCUT_MARKER
        self.module.SHORTCUT_WRAPPER.write_bytes(wrapper)
        self.module.is_apple_m4 = lambda: True
        self.module.ensure_shortcut_wrapper_assets = lambda: None
        self.module.sign_generated_shortcut = lambda _app: None

        backups = self.module.BackupSet()
        changes = self.module.install_shortcut_wrappers(backups, [child_package])
        self.assertTrue(changes)
        self.assertEqual(apps[self.module.PACKAGE_PARENT].read_bytes(),
                         ("original-" + self.module.PACKAGE_PARENT).encode())
        self.assertEqual(apps[child_package].read_bytes(), wrapper)
        sibling = apps[child_package].with_name(self.module.SHORTCUT_ORIGINAL_NAME)
        self.assertEqual(sibling.read_bytes(), ("original-" + child_package).encode())

    def test_m2_vulkan_transform_is_exact_and_idempotent(self):
        end = max(
            offset + len(original)
            for offset, original, _replacement in self.module.M2_WINEVULKAN_PATCHES
        )
        image = bytearray(end + 32)
        for offset, original, _replacement in self.module.M2_WINEVULKAN_PATCHES:
            image[offset:offset + len(original)] = original

        patched = self.module.patch_m2_winevulkan_image(bytes(image))
        for offset, _original, replacement in self.module.M2_WINEVULKAN_PATCHES:
            self.assertEqual(
                patched[offset:offset + len(replacement)],
                replacement,
            )
        self.assertEqual(
            self.module.patch_m2_winevulkan_image(patched),
            patched,
        )

    def test_runtime_dlls_follow_the_current_engine_build(self):
        old_original = b"a-old"
        old_patched = b"A-old"
        new_original = b"b-new"
        new_patched = b"B-new"
        self.module.WINEVULKAN_BUILDS = (
            (
                hashlib.sha256(old_original).hexdigest(),
                hashlib.sha256(old_patched).hexdigest(),
                ((0, b"a", b"A"),),
            ),
            (
                hashlib.sha256(new_original).hexdigest(),
                hashlib.sha256(new_patched).hexdigest(),
                ((0, b"b", b"B"),),
            ),
        )
        self.module.detected_chip = lambda: "Apple M2"
        engine = self.module.YYB_DATA / self.module.M2_WINEVULKAN_RELATIVE
        system = self.module.PREFIX / "drive_c/windows/system32/winevulkan.dll"
        local = self.module.SKY_DIR / "winevulkan.dll"
        for path, data in (
            (engine, new_original),
            (system, new_original),
            (local, old_patched),
        ):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

        changes = self.module.patch_m2_vulkan_compat(self.module.BackupSet())

        self.assertIn("版本同步 1", changes[0])
        self.assertEqual(engine.read_bytes(), new_patched)
        self.assertEqual(system.read_bytes(), new_patched)
        self.assertEqual(local.read_bytes(), new_patched)
        self.assertTrue(self.module.verified_winevulkan_patch_active())

    def test_yyb_package_route_avoids_generated_helper_bundle(self):
        with mock.patch.dict(os.environ, {"SKY_YYB_TEST_HOME": ""}), mock.patch.object(
            self.module.subprocess, "run"
        ) as run, mock.patch.object(
            self.module, "LSREGISTER", self.home / "missing-lsregister"
        ):
            self.module.open_yyb_package(self.module.PACKAGE_PARENT)
        run.assert_called_once_with(
            [
                "open",
                "androws://app/callAppAutoAdaptiveEnv?pkgname="
                + self.module.PACKAGE_PARENT,
            ],
            check=True,
        )

    def test_m2_vulkan_transform_rejects_unknown_binary(self):
        end = max(
            offset + len(original)
            for offset, original, _replacement in self.module.M2_WINEVULKAN_PATCHES
        )
        with self.assertRaises(self.module.FixError):
            self.module.patch_m2_winevulkan_image(bytes(end + 32))

    def test_engine_123_transform_is_exact_and_idempotent(self):
        patches = self.module.WINEVULKAN_123_PATCHES
        end = max(offset + len(original) for offset, original, _ in patches)
        image = bytearray(end + 32)
        for offset, original, _ in patches:
            image[offset:offset + len(original)] = original
        patched = self.module.patch_winevulkan_image(bytes(image), patches)
        for offset, _original, replacement in patches:
            self.assertEqual(patched[offset:offset + len(replacement)], replacement)
        self.assertEqual(self.module.patch_winevulkan_image(patched, patches), patched)
        reverse = tuple((offset, replacement, original) for offset, original, replacement in patches)
        self.assertEqual(self.module.patch_winevulkan_image(patched, reverse), bytes(image))

    def test_engine_124_transform_is_exact_and_idempotent(self):
        patches = self.module.WINEVULKAN_124_PATCHES
        end = max(offset + len(original) for offset, original, _ in patches)
        image = bytearray(end + 32)
        for offset, original, _ in patches:
            image[offset:offset + len(original)] = original
        patched = self.module.patch_winevulkan_image(bytes(image), patches)
        for offset, _original, replacement in patches:
            self.assertEqual(patched[offset:offset + len(replacement)], replacement)
        self.assertEqual(self.module.patch_winevulkan_image(patched, patches), patched)

    def test_m4_uses_same_hash_pinned_vulkan_transform(self):
        os.environ["SKY_YYB_TEST_CHIP"] = "Apple M4"
        engine_dll = self.module.YYB_DATA / self.module.M2_WINEVULKAN_RELATIVE
        engine_dll.parent.mkdir(parents=True)
        end = max(
            offset + len(original)
            for offset, original, _replacement in self.module.M2_WINEVULKAN_PATCHES
        )
        image = bytearray(end + 32)
        for offset, original, _replacement in self.module.M2_WINEVULKAN_PATCHES:
            image[offset:offset + len(original)] = original
        engine_dll.write_bytes(image)
        self.module.M2_WINEVULKAN_ORIGINAL_SHA256 = self.module.sha256(engine_dll)
        transformed = self.module.patch_m2_winevulkan_image(bytes(image))
        self.module.M2_WINEVULKAN_PATCHED_SHA256 = __import__("hashlib").sha256(
            transformed
        ).hexdigest()
        self.module.WINEVULKAN_BUILDS = (
            (
                self.module.M2_WINEVULKAN_ORIGINAL_SHA256,
                self.module.M2_WINEVULKAN_PATCHED_SHA256,
                self.module.M2_WINEVULKAN_PATCHES,
            ),
        )
        backups = self.module.BackupSet()
        changes = self.module.patch_m2_vulkan_compat(backups)
        self.assertTrue(changes)
        self.assertTrue(self.module.verified_winevulkan_patch_active())
        stable = self.module.winevulkan_original_backup(engine_dll)
        self.assertTrue(stable.exists())
        self.assertEqual(self.module.sha256(stable), self.module.M2_WINEVULKAN_ORIGINAL_SHA256)
        self.assertEqual(self.module.restore_winevulkan_originals(), 1)
        self.assertEqual(self.module.sha256(engine_dll), self.module.M2_WINEVULKAN_ORIGINAL_SHA256)

    def test_m4_removes_superseded_injection_environment(self):
        os.environ["SKY_YYB_TEST_CHIP"] = "Apple M4"
        self.module.USER_REG.write_text(
            'WINE REGISTRY Version 2\n\n[Environment] 1\n'
            '"DYLD_INSERT_LIBRARIES"="/tmp/old.dylib"\n'
            '"SKY_YYB_GPU_COMPAT"="1"\n'
            '"TEMP"="C:\\\\Temp"\n',
            encoding="utf-8",
        )
        backups = self.module.BackupSet()
        self.module.patch_registries(backups)
        result = self.module.USER_REG.read_text(encoding="utf-8")
        self.assertNotIn("DYLD_INSERT_LIBRARIES", result)
        self.assertNotIn("SKY_YYB_GPU_COMPAT", result)
        self.assertIn('"TEMP"="C:\\\\Temp"', result)


if __name__ == "__main__":
    unittest.main()
