#!/usr/bin/env python3
"""Compare the game's same-frame Streamline tags with actual FFX dispatches."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path


def close(a, b):
    return math.isfinite(a) and math.isfinite(b) and math.isclose(a, b, abs_tol=1e-4, rel_tol=1e-4)


def analyze(lines):
    counts = Counter()
    findings = []
    exports = set()
    session = None
    sr = {}
    for number, line in enumerate(lines, 1):
        try:
            row = json.loads(line)
        except (ValueError, TypeError):
            counts['invalid_records'] += 1
            continue
        kind = row.get('event')
        if kind == 'observer_ready':
            # The log is append-only. Do not compare reused frame IDs across processes.
            counts.clear(); findings.clear(); exports.clear(); sr.clear()
            session = {k: row.get(k) for k in ('pid', 'tick', 'abi')}
        counts[kind] += 1
        if kind == 'capture_export':
            exports.add(row['name'])
        elif kind == 'upscale' and row.get('frameKnown'):
            sr[row['frame']] = row
        elif kind == 'prepare':
            native = row.get('native')
            if not native:
                counts['prepare_without_native_frame'] += 1
                continue
            frame = row['contextFrame']
            if native['frame'] != frame:
                findings.append({'line': number, 'frame': frame, 'issue': 'frame_identity_mismatch'})
            tags = native['tags']
            for index, key in [(0, 'depth'), (1, 'motion')]:
                tag = tags[index]
                if not tag['ptr'] or tag['ptr'] == '0':
                    counts[key + '_not_tagged'] += 1
                elif tag['ptr'].lower() != row[key]['ptr'].lower():
                    # A copy or format conversion can be legitimate. Never call it stale from its address alone.
                    counts[key + '_different_resource_needs_copy_provenance'] += 1
                else:
                    counts[key + '_same_native_resource'] += 1
            if native['constants']:
                if not all(close(a, b) for a, b in zip(row['jitter'], native['jitter'])):
                    findings.append({'line': number, 'frame': frame, 'issue': 'jitter_mismatch',
                                     'native': native['jitter'], 'fg': row['jitter']})
                motion = tags[1]
                size = motion['extent'][2:] if all(motion['extent'][2:]) else motion['size']
                if all(size):
                    expected = [a * b for a, b in zip(native['scale'], size)]
                    if not all(close(a, b) for a, b in zip(row['scale'], expected)):
                        findings.append({'line': number, 'frame': frame, 'issue': 'motion_scale_mismatch',
                                         'native_pixel_scale': expected, 'fg': row['scale']})
                if native['flags'][1] == 0:
                    counts['camera_motion_excluded_requires_reconstruction_check'] += 1
            upscale = sr.get(frame)
            if upscale:
                counts['same_frame_sr_and_fg'] += 1
                if upscale['depth']['ptr'] == row['depth']['ptr'] and upscale['motion']['ptr'] == row['motion']['ptr']:
                    counts['sr_fg_same_depth_and_motion'] += 1
                    if not all(close(a, b) for a, b in zip(upscale['scale'], row['scale'])):
                        findings.append({'line': number, 'frame': frame, 'issue': 'sr_fg_motion_scale_mismatch',
                                         'sr': upscale['scale'], 'fg': row['scale']})
        elif kind == 'interpolate':
            left, top, width, height = row['rect']
            output_width, output_height = row['present']['size']
            if width <= 0 or height <= 0 or left < 0 or top < 0 or left + width > output_width or top + height > output_height:
                findings.append({'line': number, 'frame': row['frame'], 'issue': 'invalid_generation_rect',
                                 'rect': row['rect'], 'present_size': row['present']['size']})
            if row['frames'] == 1 and row['result'] == 0:
                counts['successful_one_intermediate_request'] += 1
    required = {'slSetConstants', 'slSetTagForFrame', 'slEvaluateFeature'}
    return {'session': session, 'counts': dict(counts), 'captured_exports': sorted(exports),
            'coverage_complete': required <= exports and counts['prepare_without_native_frame'] == 0 and counts['prepare'] > 0
                                 and counts['depth_not_tagged'] == 0 and counts['motion_not_tagged'] == 0,
            'findings': findings[:200], 'finding_count': len(findings),
            'limits': 'Pointer copies require provenance; timestamps are not frame identity. Successful dispatches do not prove displayed 2x output, pixel correctness or a ghosting repair.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    with args.log.open(errors='replace') as f:
        print(json.dumps(analyze(f), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
