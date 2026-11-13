import tempfile
from pathlib import Path
import unittest

from watch_loading import Tail, save_report, waits


class LoadingWatcherTests(unittest.TestCase):
    def test_append_only_log_skips_old_runs_and_handles_partial_lines(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'trace'
            path.write_bytes(b'old run\n')
            tail = Tail(path)
            self.assertEqual(tail.lines(), [])
            with path.open('ab') as stream:
                stream.write(b'{"new":')
            self.assertEqual(tail.lines(), [])
            with path.open('ab') as stream:
                stream.write(b'1}\n')
            self.assertEqual(tail.lines(), ['{"new":1}'])
            path.write_bytes(b'x\n')
            self.assertEqual(tail.lines(), ['x'])

    def test_hex_lock_owner_is_not_decimal_or_waiter(self):
        lines = ['01d0:err:sync:RtlpWaitForCriticalSection section 00000002538D04B0 "?" '
                 'wait timed out in thread 01d0, blocked by 01d4, retrying (60 sec)']
        self.assertEqual(list(waits(lines)), [{'address': 0x2538d04b0,
                                             'waiter': 0x1d0, 'owner': 0x1d4}])

    def test_report_only_exports_allowlist_and_rejects_stale_report(self):
        with tempfile.TemporaryDirectory() as folder:
            source, output = Path(folder) / 'report.xml', Path(folder) / 'clean.json'
            source.write_text('<report><REASON Value="hang34"/>'
                              '<SESSION Value="private"/><USER_ID Value="private"/></report>')
            save_report(source, output, source.stat().st_mtime + 1)
            self.assertFalse(output.exists())
            save_report(source, output, 0)
            self.assertIn('hang34', output.read_text())
            self.assertNotIn('private', output.read_text())


if __name__ == '__main__':
    unittest.main()
