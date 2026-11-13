#!/usr/bin/env python3
"""Compare saved FFX 3.1.6 disocclusion GPU output with its scalar math.

Only the standalone replay's zero-distortion, valid-vector interior is covered.
This does not validate the SDK's visibility heuristic or all driver operations.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct


def verify(folder, step=16):
    folder = Path(folder)
    cb = (folder / 'interpolation-constants.bin').read_bytes()
    rw, rh, w, h = struct.unpack_from('<4I', cb)
    c, e, a, b = struct.unpack_from('<4f', cb, 48)
    if len(cb) != 192 or rw > w or rh > h or min(rw, rh) < 4:
        raise ValueError('Unexpected standalone constant layout')
    fields = {}
    for m in re.finditer(r'Field (\d+) name=(\S+) format=(\d+) size=(\d+)x(\d+) pitch=(\d+) rowBytes=(\d+) bytes=(\d+)',
                         (folder / 'replay.log').read_text(errors='replace')):
        index, name, fmt, width, height, pitch, row, size = m.groups()
        data = (folder / f'field-{int(index):02}.bin').read_bytes()
        if len(data) != int(size):
            raise ValueError('Truncated field: ' + name)
        fields[name] = (data, int(pitch), int(width), int(height))
    def f32(name, x, y):
        data, pitch, width, height = fields[name]
        return struct.unpack_from('<f', data, y * pitch + x * 4)[0] if 0 <= x < width and 0 <= y < height else 0.
    def half(name, x, y):
        data, pitch, _, _ = fields[name]
        return struct.unpack_from('<e', data, y * pitch + x * 4)[0]
    xname, yname = 'FI_GameMotionVectorFieldX', 'FI_GameMotionVectorFieldY'
    # Choose the SDK buffer actually in NON_PIXEL_SHADER_RESOURCE at this point.
    states = {m[0]: int(m[1]) for m in re.findall(r'SDK resource name=(\S+) .* state=(\d+)',
                                                (folder / 'replay.log').read_text())}
    current = [n for n in fields if n.startswith('FISHARED_DilatedDepth') and states.get(n) == 64]
    if len(current) != 1:
        raise ValueError('Cannot identify the active dilated depth')
    suffix = current[0].removeprefix('FISHARED_DilatedDepth')
    prev = 'FISHARED_ReconstructedPrevNearestDepth' + suffix
    view = lambda z: e / (z - c)
    threshold_scale = struct.unpack('>d', bytes.fromhex('3EECBB21A0000000'))[0]
    samples = mismatches = 0
    examples = []
    for y in range(8, rh - 8, step):
        for x in range(8, rw - 8, step):
            data, pitch, _, _ = fields[xname]
            packed = struct.unpack_from('<I', data, y * pitch + x * 4)[0]
            if not ((packed >> 21) & 1023):
                continue  # inpainting pyramid fallback is outside this check
            mv = half(xname, x, y), half(yname, x, y)
            z = f32('FI_ReconstructedDepthInterpolatedFrame', x, y)
            vz = view(z)
            result = []
            for sign, name in ((1, prev), (-1, current[0])):
                sx, sy = x + sign * mv[0] * rw, y + sign * mv[1] * rh
                xx, yy = math.floor(sx), math.floor(sy)
                fx, fy = sx - xx, sy - yy
                total = good = 0.
                for ox, oy, weight in ((0, 0, (1-fx)*(1-fy)), (1, 0, fx*(1-fy)),
                                       (0, 1, (1-fx)*fy), (1, 1, fx*fy)):
                    dx, dy = xx + ox, yy + oy
                    if not (0 <= dx < rw and 0 <= dy < rh) or weight <= .001:
                        continue
                    zz = f32(name, dx, dy)
                    vz2 = view(zz)
                    delta = vz - vz2
                    if delta <= 0:
                        continue
                    mn = view(min(zz, z))
                    length = mn * math.sqrt(a*a + b*b + 1)
                    center = mn * math.sqrt(((int(rw*.5)/rw*2-1)*a)**2 + ((1-int(rh*.5)/rh*2)*b)**2 + 1)
                    threshold = math.hypot(rw, rh) * threshold_scale * min(vz, vz2) * length / center
                    good += weight * (threshold / delta >= 1)
                    total += weight
                valid = good / total if total else 1.
                outside = not (0 <= int(x+.5+2*sign*mv[0]*rw) < rw and
                               0 <= int(y+.5+2*sign*mv[1]*rh) < rh)
                result.append(255 * int(valid >= .001 or outside))
            data, pitch, _, _ = fields['FI_DisocclusionMask']
            actual = list(data[y*pitch+x*2:y*pitch+x*2+2])
            samples += 1
            if actual != result:
                mismatches += 1
                if len(examples) < 12:
                    examples.append(dict(render_xy=[x, y], gpu=actual, cpu=result))
    return dict(samples=samples, mismatches=mismatches, examples=examples,
                scope='Actual standalone 3.1.6 mask math, valid-vector interior, zero distortion; not a whole-driver test')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace', type=Path)
    p.add_argument('--step', type=int, default=16)
    args = p.parse_args()
    if args.step < 1:
        p.error('step must be positive')
    result = verify(args.trace, args.step)
    print(json.dumps(result, indent=2))
    raise SystemExit(bool(result['mismatches']))
