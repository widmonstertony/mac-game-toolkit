#!/usr/bin/env python3
"""Prepare a tiny descriptor and symlinks for adjacent GPU-fenced FH6 captures."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
from analyze_pixels import Frame


def prepare(source, target):
    meta = json.loads((source / 'capture.json').read_text())
    if not meta['gpu_fence_completed'] or not meta['pixel_files_complete'] or meta['transfer'] != 0:
        raise ValueError('Replay currently supports completed SDR captures only')
    raw = (source / 'native-frame.bin').read_bytes()
    native = Frame.from_buffer_copy(raw)
    if len(raw) != 608 or meta['native_frame_size'] != 608 or native.frame != meta['frame']:
        raise ValueError('Unknown frame ABI or mismatched frame ID')
    prep = meta['prepare']
    if not prep['known'] or prep['frame'] != native.frame or not native.inverted or native.jittered:
        raise ValueError('Unsupported or incomplete input convention')
    images = meta['images']
    names = ['depth', 'motion', 'scene', 'ui', 'present', 'generated-before-ui', 'generated-after-ui']
    if [i['name'] for i in images] != names or [i['source_format'] for i in images[:3]] != [19, 34, 28]:
        raise ValueError('Unsupported texture formats')
    if any(i['row_bytes'] != i['width'] * 4 for i in images):
        raise ValueError('Unsupported packed texture layout')
    target.mkdir(parents=True, exist_ok=False)
    evidence = {}
    for name in ['native-frame'] + names:
        path = source / (name + '.bin')
        if name != 'native-frame' and path.stat().st_size != next(i['bytes'] for i in images if i['name'] == name):
            raise ValueError('Truncated input ' + name)
        (target / path.name).symlink_to(path)
        evidence[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    w, h = images[2]['width'], images[2]['height']
    rw, rh = prep['render']
    header = struct.pack('<Q8I5f9I', 0x31594c5052484646, 1, 96, w, h, rw, rh, 8, meta['transfer'],
                         prep['milliseconds'], *prep['scale'], *prep['jitter'],
                         *(i['row_pitch'] for i in images), ctypes.sizeof(Frame), 0)
    assert len(header) == 96
    (target / 'replay-input.bin').write_bytes(header + raw)
    result = {'source': str(source), 'frame': native.frame, 'sha256': evidence,
              'context_flags': 8, 'view_space_to_meters': 0,
              'limitations': ['Four warmup frames reconstruct history; original long history unavailable.',
                             'Full-screen interpolation and SDR match the active route; HDR is rejected.',
                             'Captured prepare camera basis and FOV are used; this does not reproduce live queue overlap.']}
    (target / 'input-provenance.json').write_text(json.dumps(result, indent=2) + '\n')
    return native.frame


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('first', type=Path)
    p.add_argument('second', type=Path)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    ids = [prepare(source.resolve(), args.out / str(i)) for i, source in enumerate((args.first, args.second))]
    if ids[1] != ids[0] + 1:
        raise ValueError('Captures must be adjacent')
    print('Prepared adjacent input frames', ids, 'with symlinks; no duplicated game files')


if __name__ == '__main__':
    main()
