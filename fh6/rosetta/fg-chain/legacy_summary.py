#!/usr/bin/env python3
"""Retain bounded evidence from the prior verbose log before another launch."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    a = p.parse_args()
    digest = hashlib.sha256()
    tags, lag, callbacks, warnings = Counter(), Counter(), Counter(), Counter()
    first, last = None, None
    examples = []
    count = 0
    with a.log.open('rb') as f:
        for count, raw in enumerate(f, 1):
            digest.update(raw)
            line = raw.decode('utf-8', errors='replace').rstrip()
            m = re.search(r'Reporting SL resource type: (\d+) lifecycle: (\w+) frameId: (\d+)', line)
            if m:
                tags[f'{m[1]}:{m[2]}'] += 1
            m = re.search(r'_frameCount: (\d+), willDispatchFrame: (\d+), fIndex: (\d+)', line)
            if m:
                current, dispatched, index = map(int, m.groups())
                lag[str(current - dispatched)] += 1
                first = dispatched if first is None else first
                last = dispatched
            m = re.search(r'frameID: (\d+).*numGeneratedFrames: (\d+)', line)
            if m:
                callbacks[m[2]] += 1
            for word in ['not found in tracking', 'YOLOing resource validity', 'Depth or Velocity is not ready',
                         'Dispatched with the same frame id', 'Wrong constant struct version']:
                if word in line:
                    warnings[word] += 1
                    if len(examples) < 12:
                        examples.append({'line': count, 'text': line})
    print(json.dumps({'log_sha256': digest.hexdigest(), 'lines': count, 'tag_counts': dict(tags),
                      'dispatch_frame_delta_counts': dict(lag), 'first_dispatch_frame': first, 'last_dispatch_frame': last,
                      'callback_intermediate_requests': dict(callbacks), 'warnings': dict(warnings), 'examples': examples,
                      'limits': 'The old log does not contain raw native constants, resource extents, complete copy provenance or GPU pixel contents. Matching frame integers alone cannot prove matching images.'}, indent=2))


if __name__ == '__main__':
    main()
