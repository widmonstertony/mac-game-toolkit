import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from check_session import session_graphics


OLD_RUN = b'{"event":"observer_ready"}\n{"event":"upscale","result":0}\n'
NEW_RUN = b'{"event":"observer_ready"}\n{"event":"interpolate","frames":1,"result":0}\n'


class SessionGraphicsTests(unittest.TestCase):
    def setUp(self):
        temporary = TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        folder = Path(temporary.name)
        self.trace = folder / 'trace.jsonl'
        self.run = folder / 'run'
        self.run.mkdir()

    def capture_offset(self):
        stat = self.trace.stat()
        receipt = {'traceDevice': stat.st_dev, 'traceInode': stat.st_ino, 'traceBytes': stat.st_size}
        (self.run / 'launch.json').write_text(json.dumps(receipt))

    def test_only_appended_observer_session_is_counted(self):
        self.trace.write_bytes(OLD_RUN)
        self.capture_offset()
        self.assertEqual(session_graphics(self.trace, self.run), {})
        with self.trace.open('ab') as stream:
            stream.write(NEW_RUN)
        self.assertEqual(session_graphics(self.trace, self.run),
                         {'observer_ready': 1, 'sampled_intermediate_requests': 1})

    def test_replaced_log_starts_at_offset_zero(self):
        self.trace.write_bytes(OLD_RUN)
        self.capture_offset()
        replacement = self.trace.with_suffix('.tmp')
        replacement.write_bytes(NEW_RUN)
        replacement.replace(self.trace)
        self.assertEqual(session_graphics(self.trace, self.run),
                         {'observer_ready': 1, 'sampled_intermediate_requests': 1})
