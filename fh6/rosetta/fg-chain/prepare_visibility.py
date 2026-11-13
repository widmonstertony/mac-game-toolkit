#!/usr/bin/env python3
"""Prepare an isolated history-visibility GPU control from fenced captures.

No game settings, live resources, or installed libraries are touched. Pixel
files are linked locally, not published. The field trace must come from the
same adjacent pair used by replay-capture.exe.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from analyze_pixels import Frame


def prepare(previous, current, trace, out):
    previous, current, trace, out = map(Path, (previous, current, trace, out))
    records = [json.loads((p / 'capture.json').read_text()) for p in (previous, current)]
    a, b = records
    if not all(m['gpu_fence_completed'] and m['pixel_files_complete'] for m in records):
        raise ValueError('Both captures must have completed GPU fences')
    if b['frame'] != a['frame'] + 1:
        raise ValueError('Requires adjacent source frames')
    native = Frame.from_buffer_copy((current / 'native-frame.bin').read_bytes())
    if not (native.inverted == 1 and native.camera_motion == 1 and native.motion_3d == 0
            and native.jittered == 0 and native.frame == b['frame']):
        raise ValueError('Control requires captured reversed-Z 2D camera motion')
    if not all(m['prepare']['scale'] == [m['prepare']['render'][0] / 2,
                                       -m['prepare']['render'][1] / 2] for m in records):
        raise ValueError('This control requires the validated NDC motion convention')
    images = [{i['name']: i for i in m['images']} for m in records]
    if any(images[0][key][dim] != images[1][key][dim]
           for key in ('depth', 'motion', 'scene') for dim in ('width', 'height', 'source_format')):
        raise ValueError('History dimensions/formats changed')
    fields = {}
    pattern = (r'Field (\d+) name=(\S+) format=(\d+) size=(\d+)x(\d+) '
               r'pitch=(\d+) rowBytes=(\d+) bytes=(\d+)')
    for match in re.finditer(pattern, (trace / 'replay.log').read_text(errors='replace')):
        index, name, fmt, w, h, pitch, row_bytes, size = match.groups()
        fields[name] = dict(index=int(index), format=int(fmt), width=int(w), height=int(h),
                            pitch=int(pitch), row_bytes=int(row_bytes), bytes=int(size))
    fx, fy, mask = [fields[n] for n in
                    ('FI_GameMotionVectorFieldX', 'FI_GameMotionVectorFieldY', 'FI_DisocclusionMask')]
    w, h = images[1]['scene']['width'], images[1]['scene']['height']
    rw, rh = b['prepare']['render']
    if any((v['width'], v['height']) != (w, h) for v in (fx, fy, mask)):
        raise ValueError('Field maximum size does not match the captured display')
    if (fx['format'], fy['format'], mask['format']) != (42, 42, 49) or fx['pitch'] != fy['pitch']:
        raise ValueError('Unrecognized SDK field layout')
    cb = (trace / 'interpolation-constants.bin').read_bytes()
    if len(cb) != 192 or struct.unpack_from('<4I', cb) != (rw, rh, w, h):
        raise ValueError('Trace constant buffer does not match this capture')
    # Confirm actual trace identifies the same two source frames.
    log = (trace / 'replay.log').read_text()
    for frame in (a['frame'], b['frame']):
        if f'captured_source_frame={frame} ' not in log:
            raise ValueError('Trace provenance does not identify both source frames')
    header = struct.pack('<Q8IQ4I16f', 0x3153495648364846, 1, 128, w, h, rw, rh,
                         fx['pitch'], mask['pitch'], mask['bytes'],
                         images[0]['depth']['row_pitch'], images[1]['depth']['row_pitch'],
                         images[0]['motion']['row_pitch'], images[1]['motion']['row_pitch'],
                         *native.clip_to_previous)
    assert len(header) == 128
    out.mkdir(parents=True, exist_ok=False)
    (out / 'visibility-input.bin').write_bytes(header)
    links = {'previous-depth.bin': previous / 'depth.bin',
             'previous-motion.bin': previous / 'motion.bin',
             'depth.bin': current / 'depth.bin', 'motion.bin': current / 'motion.bin',
             'field-x.bin': trace / f"field-{fx['index']:02}.bin",
             'field-y.bin': trace / f"field-{fy['index']:02}.bin",
             'mask.bin': trace / f"field-{mask['index']:02}.bin"}
    hashes = {}
    for name, path in links.items():
        (out / name).symlink_to(path.resolve())
        hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    receipt = dict(purpose='standalone GPU visibility experiment, not a deployed repair',
                   source_frames=[a['frame'], b['frame']], input_sha256=hashes,
                   limits=['Conservative heuristic thresholds need broader scene validation.',
                           'Runtime previous-depth ownership and queue ordering are not implemented here.'])
    (out / 'provenance.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('previous', 'current', 'trace', 'out'):
        p.add_argument(name, type=Path)
    args = p.parse_args()
    print(json.dumps(prepare(args.previous, args.current, args.trace, args.out), indent=2))
