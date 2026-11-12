import importlib.util
from pathlib import Path
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("network_install", HERE / "install.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class IntegrationTests(unittest.TestCase):
    def test_only_one_addition_and_existing_code_preserved(self):
        original = "before\n" + module.ANCHOR + "after\n"
        updated = module.patched_helper(original)
        self.assertEqual(updated.replace(module.ADDITION, ""), original)
        self.assertEqual(module.patched_helper(updated), updated)

    def test_unknown_helpers_are_not_overwritten(self):
        for source in ("unknown", module.ANCHOR * 2, module.ANCHOR + '.fh6-network modified'):
            with self.assertRaises(ValueError):
                module.patched_helper(source)

    def test_shipped_payload_is_exactly_the_two_verified_modules(self):
        self.assertEqual(set(module.payload()), set(module.MODULES))

    def test_repo_bridge_and_installer_agree(self):
        bridge = (HERE.parent / "native_steam.py").read_text()
        self.assertEqual(module.patched_helper(bridge), bridge)
        self.assertEqual(bridge.count(module.ADDITION), 1)


if __name__ == "__main__":
    unittest.main()
