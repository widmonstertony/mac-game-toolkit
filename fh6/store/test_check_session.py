import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from check_session import AUTH_METADATA, NETWORK_METADATA, metadata_rows, session_graphics, summarize_metadata


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

    def test_truncated_same_inode_does_not_reuse_old_observer(self):
        self.trace.write_bytes(OLD_RUN + NEW_RUN)
        self.capture_offset()
        self.trace.write_bytes(OLD_RUN)
        self.assertEqual(session_graphics(self.trace, self.run), {})

    def test_tick_wrap_and_no_account_fields_in_merged_timeline(self):
        status = self.run / 'status.log'
        network = self.run / 'net.log'
        status.write_text('AUTH_META phase=xsts hr=00000000 http=200 user=1 requested_title=1 actual_title=0 tick=4294967286 thread=999\n')
        network.write_text('NET_META phase=websocket-upgrade service=xbox-live status=101 error=0 tick=13\n')
        auth, net, timeline = summarize_metadata(metadata_rows(status, AUTH_METADATA),
                                                 metadata_rows(network, NETWORK_METADATA))
        authorization = {'phase': 'xsts', 'hresult': '0x00000000', 'http': 200,
                         'requested_title': True, 'actual_title': False}
        self.assertEqual(auth, [{**authorization, 'count': 1}])
        self.assertEqual(net[0]['count'], 1)
        self.assertEqual(timeline, [
            {'elapsed_ms': 0, 'source': 'auth', **authorization},
            {'elapsed_ms': 23, 'source': 'network', 'phase': 'websocket-upgrade', 'service': 'xbox-live', 'status': 101, 'error': 0}
        ])
