"""Request one two-frame capture after actual camera movement, not FOV alone.

Uses the already installed observer. Does not inject code or alter graphics.
Run with --arm to create its capture-request file; default is read-only.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import time


class MovingFrameGate:
    def __init__(self, minimum_distance=2.0):
        self.previous = None
        self.ready = None
        self.minimum_distance = minimum_distance

    def consume(self, event):
        kind = event.get('event')
        if kind == 'observer_ready':
            self.previous = self.ready = None
        if kind == 'prepare':
            self.ready = None
            native = event.get('native', {})
            frame = event.get('contextFrame')
            bits = native.get('cameraBasisBits', [])
            flags = native.get('flags', [])
            if (frame != native.get('frame') or not native.get('constants')
                    or len(bits) != 12 or event.get('result') != 0
                    or len(flags) != 6 or flags[5]):
                self.previous = None
                return None
            position = struct.unpack('<3f', struct.pack('<3I', *bits[:3]))
            if not all(math.isfinite(x) for x in position):
                self.previous = None
                return None
            tags = {t['type']: t for t in native.get('tags', [])}
            if any(tags.get(t, {}).get('ptr', '0') == '0' for t in (0, 1, 2)):
                self.previous = None
                return None
            previous = self.previous
            self.previous = (frame, position)
            if previous and 0 < frame - previous[0] <= 120:
                distance = math.dist(position, previous[1])
                # Reject teleports and camera cuts as well as parked/menu frames.
                if self.minimum_distance <= distance <= 100:
                    self.ready = dict(frame=frame, camera_distance=distance,
                                      sampled_frame_delta=frame - previous[0])
        if (kind == 'native_ui_dispatch' and self.ready
                and event.get('frame') == self.ready['frame']
                and event.get('frames') == 1 and event.get('result') == 0):
            result, self.ready = self.ready, None
            return result
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arm', action='store_true')
    parser.add_argument('--seconds', type=int, default=180)
    parser.add_argument('--root', type=Path, default=Path.home() /
        'Library/Application Support/MACxus/bottles/xbox-highball/drive_c/highball')
    args = parser.parse_args()
    log = args.root / 'store-plugins/fh6-fg-chain.jsonl'
    request = args.root / 'fg-native-ui/capture-request'
    marker = 'moving-camera-capture-' + str(time.time_ns()) + '\n'
    gate, armed, saved = MovingFrameGate(), False, 0
    deadline = time.monotonic() + args.seconds
    try:
        with log.open('rb') as source:
            source.seek(0, 2)
            while time.monotonic() < deadline:
                offset = source.tell()
                line = source.readline()
                if not line.endswith(b'\n'):
                    source.seek(offset)
                    time.sleep(.1)
                    continue
                try:
                    event = json.loads(line)
                except ValueError:
                    continue
                if event.get('event') == 'observer_ready':
                    print('Session changed; stopping without arming.', flush=True)
                    break
                moving = gate.consume(event)
                if moving and not armed:
                    print(json.dumps(moving), flush=True)
                    if not args.arm:
                        return
                    try:
                        with request.open('x') as output:
                            output.write(marker)
                    except FileExistsError:
                        continue
                    armed = True
                if armed and event.get('event') == 'gpu_capture_saved':
                    print(json.dumps(event), flush=True)
                    if event.get('success'):
                        saved += 1
                    if saved == 2:
                        return
        print('No complete moving capture within the deadline.', flush=True)
    finally:
        if armed:
            try:
                if request.read_text() == marker:
                    request.unlink()
            except FileNotFoundError:
                pass


if __name__ == '__main__':
    main()
