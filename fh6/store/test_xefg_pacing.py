import unittest
from install_xefg_pacing import integrated_nvapi


class OverrideTests(unittest.TestCase):
    def test_shared_override_retains_unrelated_dlls(self):
        self.assertEqual(integrated_nvapi('d3d12,nvapi64,dxgi=n,b;winhttp=n'),
                         'd3d12,dxgi=n,b;winhttp=n;nvapi64=d')

    def test_duplicate_old_entries_and_repeated_application(self):
        first = integrated_nvapi('NVAPI64=n,b;version=n,b;nvapi64=b')
        self.assertEqual(first, 'version=n,b;nvapi64=d')
        self.assertEqual(integrated_nvapi(first), first)


if __name__ == '__main__':
    unittest.main()
