"""Metadata tests only; real GPU results are recorded separately."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
from analyze_pixels import Frame
from prepare_visibility import prepare

class VisibilityInputTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.frames = []
        for index, frame_id in enumerate((20, 21)):
            folder = self.root / str(index)
            folder.mkdir()
            f = Frame(); f.frame = frame_id; f.inverted = f.camera_motion = 1
            (folder / 'native-frame.bin').write_bytes(bytes(f))
            images = []
            for name, fmt, size, pitch in [('depth', 19, 8, 256), ('motion', 34, 8, 64), ('scene', 28, 16, 128)]:
                (folder / (name + '.bin')).write_bytes(bytes(pitch * size))
                images.append(dict(name=name, source_format=fmt, width=size, height=size, row_pitch=pitch))
            meta = dict(frame=frame_id, gpu_fence_completed=True, pixel_files_complete=True,
                        prepare=dict(render=[8, 8], scale=[4, -4]), images=images)
            (folder / 'capture.json').write_text(json.dumps(meta))
            self.frames.append(folder)
        self.trace = self.root / 'trace'; self.trace.mkdir()
        lines = ['GPU completed captured_source_frame=20 reset=0', 'GPU completed captured_source_frame=21 reset=0']
        for i, name, fmt, pitch, row_bytes, size in [
            (0, 'FI_GameMotionVectorFieldX', 42, 64, 64, 1024),
            (1, 'FI_GameMotionVectorFieldY', 42, 64, 64, 1024),
            (2, 'FI_DisocclusionMask', 49, 256, 32, 3872)]:
            lines.append(f'Field {i} name={name} format={fmt} size=16x16 pitch={pitch} rowBytes={row_bytes} bytes={size}')
            (self.trace / f'field-{i:02}.bin').write_bytes(bytes(size))
        (self.trace / 'replay.log').write_text('\n'.join(lines))
        (self.trace / 'interpolation-constants.bin').write_bytes(struct.pack('<4I', 8, 8, 16, 16) + bytes(176))

    def test_preserves_real_distinct_depth_motion_pitches(self):
        out = self.root / 'out'
        prepare(*self.frames, self.trace, out)
        header = (out / 'visibility-input.bin').read_bytes()
        self.assertEqual(len(header), 128)
        self.assertEqual(struct.unpack_from('<4I', header, 48), (256, 256, 64, 64))
        self.assertEqual((out / 'previous-depth.bin').resolve(), (self.frames[0] / 'depth.bin').resolve())

    def test_rejects_gap_before_creating_output(self):
        p = self.frames[1] / 'capture.json'; m = json.loads(p.read_text()); m['frame'] = 22; p.write_text(json.dumps(m))
        with self.assertRaisesRegex(ValueError, 'adjacent'):
            prepare(*self.frames, self.trace, self.root / 'out')
        self.assertFalse((self.root / 'out').exists())

    def test_rejects_unfinished_gpu_capture(self):
        p = self.frames[0] / 'capture.json'; m = json.loads(p.read_text()); m['gpu_fence_completed'] = False; p.write_text(json.dumps(m))
        with self.assertRaisesRegex(ValueError, 'fences'):
            prepare(*self.frames, self.trace, self.root / 'out')

    def test_rejects_unrelated_trace(self):
        p = self.trace / 'replay.log'; p.write_text(p.read_text().replace('frame=21 ', 'frame=99 '))
        with self.assertRaisesRegex(ValueError, 'provenance'):
            prepare(*self.frames, self.trace, self.root / 'out')

if __name__ == '__main__': unittest.main()
