#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import tempfile
import unittest
from unittest import mock

import stellar_blade_fix as fix


class FakeBackups:
    def __init__(self) -> None:
        self.paths: list[Path] = []

    def capture(self, path: Path) -> None:
        self.paths.append(path)


class StellarBladeFixTests(unittest.TestCase):
    def test_ini_patch_is_scoped_and_idempotent(self):
        original = "[First]\nValue=old\n\n[Target]\nValue=old\n"
        once = fix.set_ini_values(original, "Target", {"Value": "new", "Added": "yes"})
        twice = fix.set_ini_values(once, "Target", {"Value": "new", "Added": "yes"})
        self.assertEqual(once, twice)
        self.assertIn("[First]\nValue=old", once)
        self.assertIn("[Target]\nValue=new\nAdded=yes", once)

    def test_ini_removal_is_scoped_and_idempotent(self):
        original = "[First]\nKeep=1\n\n[Target]\nRemove=1\nKeep=2\n"
        once = fix.remove_ini_values(original, "Target", ("Remove",))
        twice = fix.remove_ini_values(once, "Target", ("Remove",))
        self.assertEqual(once, twice)
        self.assertIn("[First]\nKeep=1", once)
        self.assertIn("[Target]\nKeep=2", once)
        self.assertNotIn("Remove=", once)

    def test_steam_launch_options_are_written_to_app_block(self):
        with tempfile.TemporaryDirectory() as temp:
            userdata = Path(temp) / "userdata"
            config = userdata / "123/config/localconfig.vdf"
            config.parent.mkdir(parents=True)
            config.write_text(
                '"UserLocalConfigStore"\n{\n'
                '\t"Software"\n\t{\n\t\t"Valve"\n\t\t{\n'
                '\t\t\t"Steam"\n\t\t\t{\n\t\t\t\t"apps"\n'
                '\t\t\t\t{\n\t\t\t\t\t"3489700"\n'
                '\t\t\t\t\t{\n\t\t\t\t\t\t"LastPlayed"\t\t"1"\n'
                '\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n',
                encoding="utf-8",
            )
            backups = FakeBackups()
            with mock.patch.object(fix, "STEAM_USERDATA", userdata):
                fix.patch_steam_launch_options(backups)
                first = config.read_text(encoding="utf-8")
                fix.patch_steam_launch_options(backups)
                second = config.read_text(encoding="utf-8")
            self.assertEqual(first, second)
            self.assertEqual(first.count('"LaunchOptions"'), 1)
            self.assertIn("-NoStartupMovies", first)
            self.assertEqual(backups.paths, [config, config])

    def test_m4_performance_profile_keeps_4k_and_prioritizes_character(self):
        with tempfile.TemporaryDirectory() as temp:
            config = Path(temp) / "GameUserSettings.ini"
            engine = Path(temp) / "Engine.ini"
            config.write_text("[/Script/SB.SBGameUserSettings]\n", encoding="utf-8")
            backups = FakeBackups()
            with (
                mock.patch.object(fix, "USER_SETTINGS", config),
                mock.patch.object(fix, "ENGINE_INI", engine),
            ):
                fix.patch_game_settings(backups)
                fix.patch_movie_settings(backups)

            settings = config.read_text(encoding="utf-8")
            self.assertIn("ResolutionSizeX=3840", settings)
            self.assertIn("ResolutionSizeY=2160", settings)
            self.assertIn("CharacterObjectDetail=SB_GAMEUSERSETTINGS_HIGH", settings)
            self.assertIn("CharacterTextures=SB_GAMEUSERSETTINGS_VERYHIGH", settings)
            self.assertIn("MaterialQuality=SB_GAMEUSERSETTINGS_VERYHIGH", settings)
            self.assertIn("Lighting=SB_GAMEUSERSETTINGS_HIGH", settings)
            self.assertIn("AmbientOcclusion=SB_GAMEUSERSETTINGS_HIGH", settings)
            self.assertIn("EnviromentObjectDetail=SB_GAMEUSERSETTINGS_LOW", settings)
            self.assertIn("AmdFSR3=SB_GAMEUSERSETTINGS_OFF", settings)
            self.assertIn("AmdFrameInterpolation=SB_GAMEUSERSETTINGS_OFF", settings)
            self.assertIn("AntiAliasing=SB_GAMEUSERSETTINGS_MEDIUM", settings)
            self.assertIn("UpscalerType=SB_GAMEUSERSETTINGS_LOW", settings)
            self.assertIn("FrameRateLimit=120.000000", settings)

            engine_settings = engine.read_text(encoding="utf-8")
            self.assertNotIn("r.FidelityFX.FSR3.QualityMode=", engine_settings)
            self.assertNotIn("r.FidelityFX.FSR3.Enabled=", engine_settings)
            self.assertIn("r.FidelityFX.FI.Enabled=0", engine_settings)
            self.assertIn("r.ScreenPercentage=100", engine_settings)
            self.assertIn("r.Streaming.PoolSize=0", engine_settings)
            self.assertIn("r.Streaming.FullyLoadUsedTextures=1", engine_settings)
            self.assertIn("r.SkeletalMeshLODBias=-3", engine_settings)
            self.assertIn("r.MipMapLODBias=-3", engine_settings)
            self.assertIn("r.MaxAnisotropy=16", engine_settings)
            self.assertIn("r.SSS.HalfRes=0", engine_settings)
            self.assertIn("r.ContactShadows=1", engine_settings)
            self.assertIn("r.SceneColorFringe.Max=0", engine_settings)
            self.assertIn("r.TemporalAA.Algorithm=0", engine_settings)
            self.assertIn("r.TemporalAA.Upsampling=0", engine_settings)
            self.assertIn("r.TemporalAASamples=8", engine_settings)
            self.assertIn("r.TemporalAACatmullRom=1", engine_settings)
            self.assertIn("t.MaxFPS=120", engine_settings)

if __name__ == "__main__":
    unittest.main()
