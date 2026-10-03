#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import importlib.util
import io
import os
from pathlib import Path
import plistlib
import struct
import sys
import tempfile
import unittest
import zlib
from unittest import mock


REPO = Path(__file__).resolve().parents[1]


def load_script(name: str, path: Path, test_home: Path):
    os.environ["YYB_INTEL_TEST_HOME"] = str(test_home)
    module_name = f"test_{name}_{id(test_home)}"
    spec = importlib.util.spec_from_file_location(module_name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


class FakeResponse(io.BytesIO):
    status = 200

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()


def make_preferences() -> bytes:
    names = [
        b"quality_fps\0",
        b"kUserPreference_MotionBlurScalar\0",
        b"kUserPreference_Fullscreen\0",
    ]
    counts = (3, 0, 0, 0)
    string_base = 28 + 24
    header = b"PREF" + b"\0" * 4 + struct.pack("<4I", *counts) + struct.pack("<I", string_base)
    records = (
        struct.pack("<II", 0, 30)
        + struct.pack("<II", len(names[0]), 0x3F800000)
        + struct.pack("<II", len(names[0]) + len(names[1]), 1)
    )
    return header + records + b"".join(names)


def preference_u32(data: bytes, name: bytes) -> int:
    count = sum(struct.unpack_from("<4I", data, 8))
    base = struct.unpack_from("<I", data, 24)[0]
    for index in range(count):
        offset, value = struct.unpack_from("<II", data, 28 + index * 8)
        if data[base + offset:].split(b"\0", 1)[0] == name:
            return value
    raise AssertionError(f"missing preference: {name!r}")


def make_preferences_without_fps() -> bytes:
    name = b"some_flag\0"
    string_base = 28 + 8
    header = b"PREF" + b"\0" * 4 + struct.pack("<4I", 1, 0, 0, 0)
    return header + struct.pack("<I", string_base) + struct.pack("<II", 0, 1) + name


def make_mmkv() -> tuple[bytes, bytes]:
    # Four-byte MMKV sequence header, followed by one key/value record.
    payload = b"\x01\x02\x03\x04" + b"\x03foo\x04\x03one"
    blob = struct.pack("<I", len(payload)) + payload
    blob += b"\0" * (4096 - len(blob))
    crc = struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF) + b"\0" * 28
    return blob, crc


class IntelFixTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.home = Path(self.temp.name) / "home"

    def tearDown(self):
        self.temp.cleanup()
        os.environ.pop("YYB_INTEL_TEST_HOME", None)

    def test_retina_registry_patch_is_idempotent_and_preserves_paths(self):
        module = load_script(
            "intel_retina", REPO / "intel/apply-intel-retina.py", self.home
        )
        module.PREFIX.mkdir(parents=True)
        module.USER_REG.write_text("WINE REGISTRY Version 2\n", encoding="utf-8")
        module.SYSTEM_REG.write_text("WINE REGISTRY Version 2\n", encoding="utf-8")
        module.PREFERENCES.parent.mkdir(parents=True)
        original_preferences = make_preferences()
        module.PREFERENCES.write_bytes(original_preferences)
        module.PUBLIC_MMKV.parent.mkdir(parents=True)
        original_mmkv, original_crc = make_mmkv()
        module.PUBLIC_MMKV.write_bytes(original_mmkv)
        module.PUBLIC_MMKV_CRC.write_bytes(original_crc)

        backup = module.apply()
        self.assertIsNotNone(backup)
        first_user = module.USER_REG.read_text(encoding="utf-8")
        first_system = module.SYSTEM_REG.read_text(encoding="utf-8")
        self.assertIn('"RetinaMode"="Y"', first_user)
        self.assertIn(
            '"C:\\\\FeverApps\\\\sky\\\\Sky.exe"="~ HIGHDPIAWARE"', first_user
        )
        self.assertIn('"dpiAwareness"=dword:00000002', first_system)
        self.assertIn('"LogPixels"=dword:000000c0', first_user)
        self.assertIn('"Win8DpiScaling"=dword:00000001', first_user)
        self.assertIn('"LogPixels"=dword:000000c0', first_system)
        patched_preferences = module.PREFERENCES.read_bytes()
        self.assertEqual(preference_u32(patched_preferences, b"quality_fps"), 60)
        self.assertEqual(preference_u32(patched_preferences, b"kUserPreference_Fullscreen"), 0)
        self.assertNotEqual(original_preferences, patched_preferences)
        patched_mmkv = module.PUBLIC_MMKV.read_bytes()
        for package in module.RETINA_PACKAGES:
            key = ("exe_app_retina_zoom_ratio_" + package).encode("ascii")
            self.assertIn(key + b"\x04\x032.0", patched_mmkv)
        actual_size = struct.unpack_from("<I", patched_mmkv)[0]
        expected_crc = zlib.crc32(patched_mmkv[4 : 4 + actual_size]) & 0xFFFFFFFF
        self.assertEqual(
            struct.unpack_from("<I", module.PUBLIC_MMKV_CRC.read_bytes())[0],
            expected_crc,
        )
        self.assertIsNone(module.apply())
        self.assertEqual(first_user, module.USER_REG.read_text(encoding="utf-8"))

        module.USER_REG.write_text("changed\n", encoding="utf-8")
        restored_from = module.restore_latest()
        self.assertEqual(restored_from, backup)
        self.assertEqual("WINE REGISTRY Version 2\n", module.USER_REG.read_text(encoding="utf-8"))
        self.assertEqual(original_preferences, module.PREFERENCES.read_bytes())
        self.assertEqual(original_mmkv, module.PUBLIC_MMKV.read_bytes())
        self.assertEqual(original_crc, module.PUBLIC_MMKV_CRC.read_bytes())

    def test_launchpad_windows_paths_keep_single_and_escaped_slashes(self):
        module = load_script(
            "intel_launchpad", REPO / "intel/sync-launchpad-apps.py", self.home
        )
        self.assertEqual(
            module.windows_path(r"C:\Games\Sky"), module.DRIVE_C / "Games/Sky"
        )
        self.assertEqual(
            module.windows_path(r"C:\\Games\\Sky"), module.DRIVE_C / "Games/Sky"
        )
        module.USER_REG.parent.mkdir(parents=True)
        module.USER_REG.write_text("WINE REGISTRY Version 2\n", encoding="utf-8")
        sky = module.DRIVE_C / "FeverApps/sky/Sky.exe"
        sky.parent.mkdir(parents=True)
        sky.write_bytes(b"MZ-fixture")
        games = module.discover_netease_games()
        self.assertEqual([(game.game_id, game.name) for game in games], [("63", "光·遇")])
        self.assertEqual(
            games[0].launch_arguments,
            ["netease-game", "63"],
        )
        self.assertEqual(games[0].remote_artwork, module.SKY_ICON_URL)

    def test_launcher_icons_are_generated_for_native_library_cards(self):
        module = load_script(
            "intel_launcher_icons", REPO / "intel/sync-launchpad-apps.py", self.home
        )
        netease = module.APP_ROOT / "网易游戏启动器.app"
        steam = module.APP_ROOT / "Steam（Windows）.app"
        netease.mkdir(parents=True)
        steam.mkdir(parents=True)
        for app in (netease, steam):
            (app / "Contents").mkdir()
            with (app / "Contents/Info.plist").open("wb") as stream:
                plistlib.dump({"CFBundleVersion": "1"}, stream)
        steam_source = module.STEAM_ROOT / "public/steam_tray.ico"
        steam_source.parent.mkdir(parents=True)
        steam_source.write_bytes(b"steam-icon")
        official_logo = self.home / "official-fever.png"
        official_logo.write_bytes(b"fever-logo")

        def render(_source, output):
            output.write_bytes(b"icns")
            return True

        with mock.patch.object(module, "fetch_artwork", return_value=official_logo), \
             mock.patch.object(module, "make_icns", side_effect=render), \
             mock.patch.object(module.subprocess, "run"):
            updated = module.install_launcher_icons()

        self.assertEqual(set(updated), {netease, steam})
        self.assertEqual(
            (netease / "Contents/Resources/FeverGames-v2.icns").read_bytes(), b"icns"
        )
        self.assertEqual(
            (steam / "Contents/Resources/Steam-v2.icns").read_bytes(), b"icns"
        )
        for app, icon in ((netease, "FeverGames-v2"), (steam, "Steam-v2")):
            with (app / "Contents/Info.plist").open("rb") as stream:
                info = plistlib.load(stream)
            self.assertEqual(info["CFBundleIconFile"], icon)
            self.assertEqual(info["CFBundleVersion"], "2")

        gui = (REPO / "intel/gui/YYBGameLauncher.m").read_text(encoding="utf-8")
        self.assertIn("initWithContentsOfFile:iconPath", gui)

    def test_launchpad_removes_obsolete_managed_duplicate(self):
        module = load_script(
            "intel_duplicate_apps", REPO / "intel/sync-launchpad-apps.py", self.home
        )
        bundle_id = "local.yybintel.game.netease.63"
        current = module.APP_ROOT / "光·遇.app"
        obsolete = module.APP_ROOT / "光·遇（网易）.app"
        for app in (current, obsolete):
            contents = app / "Contents"
            contents.mkdir(parents=True)
            with (contents / "Info.plist").open("wb") as stream:
                plistlib.dump(
                    {"CFBundleIdentifier": bundle_id, module.MANAGED_KEY: True}, stream
                )

        with mock.patch.object(module.subprocess, "run"):
            removed = module.remove_duplicate_apps({bundle_id: current})

        self.assertEqual(removed, 1)
        self.assertTrue(current.exists())
        self.assertFalse(obsolete.exists())

    def test_official_download_is_verified_and_rejects_path_traversal(self):
        module = load_script(
            "intel_download", REPO / "intel/download-sky.py", self.home
        )
        payload = b"official-sky-fixture"
        expected = hashlib.md5(payload).hexdigest()
        original_urlopen = module.urllib.request.urlopen
        module.urllib.request.urlopen = lambda *_args, **_kwargs: FakeResponse(payload)
        try:
            size = module.complete_file(
                {
                    "path": "fixture/data.bin",
                    "url": "https://example.invalid/data.bin",
                    "md5": expected,
                    "size": len(payload),
                }
            )
        finally:
            module.urllib.request.urlopen = original_urlopen
        self.assertEqual(size, len(payload))
        self.assertEqual((module.SKY_DIR / "fixture/data.bin").read_bytes(), payload)
        with self.assertRaises(module.DownloadError):
            module.target_for("../outside.bin")

    def test_manifest_retries_transient_tls_timeout(self):
        module = load_script(
            "intel_download_manifest_retry", REPO / "intel/download-sky.py", self.home
        )
        payload = (
            b'{"data":{"main_content":{"version_code":"v3_retry",'
            b'"files":[{"path":"Sky.exe"}]}}}'
        )
        responses = [TimeoutError("TLS handshake timed out"), FakeResponse(payload)]

        def urlopen(*_args, **_kwargs):
            result = responses.pop(0)
            if isinstance(result, Exception):
                raise result
            return result

        with mock.patch.object(module.urllib.request, "urlopen", side_effect=urlopen), \
             mock.patch.object(module.time, "sleep") as sleep:
            version, files = module.fetch_manifest()

        self.assertEqual(version, "v3_retry")
        self.assertEqual(files, [{"path": "Sky.exe"}])
        sleep.assert_called_once_with(1)

    def test_official_download_commits_launcher_installed_version(self):
        module = load_script(
            "intel_download_finalize", REPO / "intel/download-sky.py", self.home
        )
        module.USER_REG.parent.mkdir(parents=True)
        module.USER_REG.write_text(
            "WINE REGISTRY Version 2\n\n"
            "[Software\\\\FeverGames\\\\FeverGamesInstaller\\\\game\\\\63] 1\n"
            '"DownloadVersionCode"="old"\n'
            '"InstallPath"="C:\\\\FeverApps\\\\sky"\n',
            encoding="utf-8",
        )
        sky = module.SKY_DIR / "Sky.exe"
        sky.parent.mkdir(parents=True)
        sky.write_bytes(b"MZ-fixture")
        backup = module.finalize_install("v3_fixture")
        self.assertIsNotNone(backup)
        registry = module.USER_REG.read_text(encoding="utf-8")
        self.assertIn('"VersionCode"="v3_fixture"', registry)
        self.assertIn('"DownloadVersionCode"="v3_fixture"', registry)
        self.assertIn('"StartupPath"="Sky.exe"', registry)
        self.assertIn('"StartupParams"="--start_from_launcher=1"', registry)
        self.assertIn('"UpdateFlag"="false"', registry)

    def test_first_run_preferences_wait_for_game_to_add_fps_field(self):
        module = load_script(
            "intel_retina_first_run",
            REPO / "intel/apply-intel-retina.py",
            self.home,
        )
        module.PREFIX.mkdir(parents=True)
        module.USER_REG.write_text("WINE REGISTRY Version 2\n", encoding="utf-8")
        module.SYSTEM_REG.write_text("WINE REGISTRY Version 2\n", encoding="utf-8")
        module.PREFERENCES.parent.mkdir(parents=True)
        module.PREFERENCES.write_bytes(make_preferences_without_fps())
        self.assertIsNone(module.patched_preferences(60))
        self.assertIsNotNone(module.apply(60))

        source = (REPO / "intel/apply-intel-retina.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("seen_sky = False", source)
        self.assertIn("if seen_sky and has_fps and not running", source)

    def test_setup_patches_real_engine_retina_scale(self):
        launcher = (REPO / "intel/launch-windows-app").read_text(encoding="utf-8")
        setup = (REPO / "intel/1-setup.command").read_text(encoding="utf-8")
        patcher = (REPO / "intel/patch_retina_engine.c").read_text(encoding="utf-8")
        self.assertNotIn("DYLD_INSERT_LIBRARIES", launcher)
        self.assertIn("patch-retina-engine", setup)
        self.assertIn("$WINEMAC_LIB.yyb-intel-original", setup)
        self.assertIn("winemac.so", patcher)
        self.assertIn("0xdd48a", patcher)
        self.assertIn("0x1900a8", patcher)
        self.assertNotIn("patch_at(file, 0x12e3a0", patcher)

    def test_fps_watcher_recognizes_wine_windows_process_path(self):
        module = load_script(
            "intel_retina_process", REPO / "intel/apply-intel-retina.py", self.home
        )
        with mock.patch.dict(os.environ, {"YYB_INTEL_TEST_HOME": ""}), mock.patch.object(
            module.subprocess, "check_output",
            return_value=" 123 C:\\FeverApps\\sky\\Sky.exe --start_from_launcher=1\n"
        ):
            self.assertTrue(module.sky_is_running())

    def test_generated_apps_use_native_dock_host(self):
        setup = (REPO / "intel/7-install-launchpad-sync.command").read_text(
            encoding="utf-8"
        )
        launcher = (REPO / "intel/launch-windows-app").read_text(encoding="utf-8")
        host = (REPO / "intel/dock_app_host.m").read_text(encoding="utf-8")
        self.assertIn("dock-app-host", setup)
        self.assertIn("NSApplicationActivationPolicyRegular", host)
        self.assertIn("applicationShouldTerminate", host)
        self.assertIn('mode" == "--stop"', launcher)
        self.assertIn("isWindowsModeRunning", host)
        self.assertIn("NSTerminateCancel", host)
        self.assertIn("com.tencent.yyb.wine.appWindowShown", host)
        self.assertNotIn("sky-dock-watch.py", launcher)
        self.assertIn('mode" == "steam-game"', launcher)
        self.assertIn("fevergames://mygame/?gameId=63&autoRun=1", launcher)
        self.assertIn("everGamesWeb", launcher)
        self.assertIn('nohup "$bridge" "$fever_launcher"', launcher)
        self.assertNotIn('"$support_root/wine-loader" "$fever_launcher"', launcher)
        self.assertIn("YYB_LIFETIME_EXECUTABLE", launcher)

        bridge = (REPO / "intel/launch_via_engine.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("executeExeFile", bridge)
        self.assertIn("directChildrenWithExecutableName", bridge)
        self.assertIn("lifetimeTarget != target", bridge)

        agent = (REPO / "intel/launchpad-sync-agent.plist").read_text(encoding="utf-8")
        self.assertNotIn("StartInterval", agent)

    def test_native_library_gui_is_installed_and_uses_existing_adapter(self):
        installer = (REPO / "intel/install-gui.command").read_text(encoding="utf-8")
        sync_installer = (REPO / "intel/7-install-launchpad-sync.command").read_text(
            encoding="utf-8"
        )
        gui = (REPO / "intel/gui/YYBGameLauncher.m").read_text(encoding="utf-8")
        notice = (REPO / "intel/gui/NOTICE.md").read_text(encoding="utf-8")
        self.assertIn("-framework Cocoa", installer)
        self.assertIn("x86_64", installer)
        self.assertIn("Windows 游戏.app", installer)
        self.assertIn("install-gui.command", sync_installer)
        self.assertIn("YYBIntelLaunchpadManaged", gui)
        self.assertIn("sync-launchpad-apps.py", gui)
        self.assertIn("apply-intel-retina.py", gui)
        self.assertIn("Mac Wine Launcher", notice)

    def test_process_helper_scopes_each_windows_app(self):
        module = load_script(
            "intel_process_helper", REPO / "intel/windows-app-process.py", self.home
        )
        listing = (
            " 101 C:\\FeverApps\\sky\\Sky.exe --start_from_launcher=1\n"
            " 102 C:\\Program Files (x86)\\Steam\\Steam.exe\n"
            " 103 C:\\Program Files\\FeverGames\\1.0\\FeverGamesInstaller.exe\n"
            " 104 /prefix/drive_c/FeverApps/sky/Sky.exe --start_from_launcher=1\n"
            " 105 /prefix/drive_c/Program Files/FeverGames/1.0/FeverGamesInstaller.exe\n"
            " 107 /prefix/com.tencent.yybmac.wine.engine/links/FeverGamesInstaller.exe\n"
            " 108 C:\\Program Files\\FeverGames\\1.0\\FeverGamesWeb --type=renderer\n"
            " 109 /Users/example/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/ipcserver\n"
        )
        with mock.patch.object(module.subprocess, "check_output", return_value=listing):
            self.assertEqual(set(module.processes("netease-game")), {101, 104})
            self.assertEqual(set(module.processes("steam")), {102})
            self.assertEqual(set(module.processes("netease")), {103, 105, 108})
            self.assertEqual(
                set(
                    module.processes(
                        "steam-game",
                        [str(self.home / "prefix/drive_c/Games/My Game")],
                    )
                ),
                set(),
            )

        steam_game = self.home / "prefix/drive_c/Program Files (x86)/Steam/steamapps/common/Game"
        listing += f" 106 {steam_game}/Binaries/Game.exe\n"
        with mock.patch.object(module.subprocess, "check_output", return_value=listing):
            self.assertEqual(
                set(module.processes("steam-game", [str(steam_game)])), {106}
            )


if __name__ == "__main__":
    unittest.main()
