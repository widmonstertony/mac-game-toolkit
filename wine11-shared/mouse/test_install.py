import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("pointer_install", Path(__file__).with_name("install.py"))
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class OptionsTests(unittest.TestCase):
    @staticmethod
    def game(app_id, options):
        return '\t' * 5 + f'"{app_id}"\n' + '\t' * 5 + '{\n' + '\t' * 6 + f'"LaunchOptions"\t\t"{options}"\n' + '\t' * 5 + '}\n'

    def test_only_stellar_changes_and_enable_is_idempotent(self):
        wukong = self.game("2358720", "NOTPROTON_WUKONG_AUDIO_RT=1 %command%")
        forza = self.game("2483190", "%command%")
        original = self.game("3489700", "D3DM_ENABLE_METALFX=1 NOTPROTON_FSR4_METALFX=0 %command%")
        updated, _, _ = installer.enable(wukong + original + forza)
        self.assertIn(wukong, updated)
        self.assertIn(forza, updated)
        self.assertEqual(installer.enable(updated)[0], updated)
        self.assertIn("D3DM_ENABLE_METALFX=1 NOTPROTON_FSR4_METALFX=0 %command%", updated)

    def test_updates_previous_switch_and_keeps_escaped_options(self):
        text = self.game("3489700", r'NOTPROTON_STELLAR_POINTER=0 %command% -name=\"example\"')
        updated, _, _ = installer.enable(text)
        self.assertEqual(updated.count("NOTPROTON_STELLAR_POINTER="), 1)
        self.assertIn(r'-name=\"example\"', updated)

    def test_reject_ambiguous_or_unexpected_options(self):
        text = self.game("3489700", "%command%")
        for data in ("", text + text, self.game("3489700", "unexpected.exe")):
            with self.assertRaises(ValueError):
                installer.enable(data)


if __name__ == "__main__":
    unittest.main()
