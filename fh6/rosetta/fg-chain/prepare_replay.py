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
    by_name = {image['name']: image for image in meta['images']}
    names = ['depth', 'motion', 'scene', 'ui', 'present', 'generated-before-ui', 'generated-after-ui']
    exact = meta.get('native_capture_path') is True
    source_names = dict(zip(names, names))
    if exact:
        exact_inputs = ('consumed-prepare-depth', 'consumed-prepare-motion', 'scene-before-fg')
        if set(by_name) != set(names).union(exact_inputs):
            raise ValueError('Incomplete native input capture')
        for canonical, captured in zip(names[:3], exact_inputs):
            image = by_name[captured]
            if image.get('source_frame') != native.frame or image.get('copy_stage') != 'before-native-evaluate':
                raise ValueError('Missing native input capture provenance: ' + captured)
            source_names[canonical] = captured
    composited = list(by_name) == ['depth', 'motion', 'present', 'generated-provider-output']
    if composited:
        names = list(by_name)
        source_names = dict(zip(names, names))
    elif not exact and list(by_name) != names:
        raise ValueError('Unsupported capture layout')
    images = [by_name[source_names[name]] for name in names]
    expected_formats = [19, 34, 28, 28] if composited else [19, 34, 28, 28, 28, 28, 28]
    if ((composited and meta.get('native_ui_composited') is not False)
            or [i['source_format'] for i in images] != expected_formats):
        raise ValueError('Unsupported texture formats')
    if any(i['row_bytes'] != i['width'] * 4 for i in images):
        raise ValueError('Unsupported packed texture layout')
    target.mkdir(parents=True, exist_ok=False)
    evidence = {}
    for name in ['native-frame', *names]:
        captured = source_names.get(name, name)
        path = source / (captured + '.bin')
        if name != 'native-frame' and path.stat().st_size != by_name[captured]['bytes']:
            raise ValueError('Truncated input ' + name)
        destination = name + '.bin'
        (target / destination).symlink_to(path)
        evidence[destination] = hashlib.sha256(path.read_bytes()).hexdigest()
    w, h = images[2]['width'], images[2]['height']
    rw, rh = prep['render']
    if any([i['width'], i['height']] != [rw, rh] for i in images[:2]) or any(
            [i['width'], i['height']] != [w, h] for i in images[2:]):
        raise ValueError('Prepare or presentation dimensions differ from the captures')
    pitches = [i['row_pitch'] for i in images]
    if composited:
        pitches = pitches[:2] + [0, 0] + pitches[2:] + [0]
    # Reserved is a composited-only marker. The previous v1 descriptor used zero.
    header = struct.pack('<Q8I5f9I', 0x31594c5052484646, 1, 96, w, h, rw, rh, 8, meta['transfer'],
                         prep['milliseconds'], *prep['scale'], *prep['jitter'], *pitches,
                         ctypes.sizeof(Frame), int(composited))
    assert len(header) == 96
    (target / 'replay-input.bin').write_bytes(header + raw)
    result = {'source': str(source), 'frame': native.frame, 'sha256': evidence, 'composited_input_required': composited,
              'source_names': source_names, 'exact_native_inputs': exact,
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
