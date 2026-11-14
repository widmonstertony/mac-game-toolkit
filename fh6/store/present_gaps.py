"""Locate long gaps in an existing OptiScaler debug log without printing its contents."""

import argparse
from collections import deque
import json
from pathlib import Path
import re
from statistics import median


CLOCK = re.compile(rb'^\[(\d\d):(\d\d):(\d\d)\.(\d{1,9})\] \[[A-Z]\] (.*)')
TAIL_BYTES = 16 * 1024 * 1024


def stage(body):
    if body.startswith(b'LocalPresent Original present result: 0'):
        return 'present_returned_success'
    if body.startswith(b'LocalPresent SyncInterval:'):
        return 'present_entry'
    if body.startswith(b'NVSDK_NGX_DLSS_GetOptimalSettingsCallback Display Resolution:'):
        return 'next_upscaler_settings'
    return 'other'


def summarize(stream, seconds, threshold):
    stream.seek(0, 2)
    size = stream.tell()
    if size > TAIL_BYTES:
        stream.seek(size - TAIL_BYTES)
        stream.readline()
    else:
        stream.seek(0)
    previous = first = None
    day = 0
    gaps = deque()
    for line in stream:
        match = CLOCK.match(line)
        if not match:
            continue
        hour, minute, second, fraction, body = match.groups()
        stamp = day + int(hour) * 3600 + int(minute) * 60 + int(second) + int(fraction) / 10**len(fraction)
        if previous and stamp < previous[0]:
            if previous[0] - stamp > 43200:
                day += 86400
                stamp += 86400
            else:
                previous = stamp, body
                continue
        if first is None:
            first = stamp
        if previous and stamp - previous[0] >= threshold:
            gaps.append((stamp, stamp - previous[0], stage(previous[1]), stage(body)))
        while gaps and gaps[0][0] < stamp - seconds:
            gaps.popleft()
        previous = stamp, body
    if previous is None:
        return {'sampled_seconds': 0, 'long_gaps': 0, 'after_present_before_upscale': 0}
    selected = [gap for gap in gaps if gap[2:] == ('present_returned_success', 'next_upscaler_settings')]
    times = [gap[1] * 1000 for gap in selected]
    intervals = [next_gap[0] - gap[0] for gap, next_gap in zip(selected, selected[1:])]
    return {
        'sampled_seconds': round(min(seconds, previous[0] - first), 3),
        'long_gaps': len(gaps),
        'after_present_before_upscale': len(selected),
        'gap_ms_min_median_max': [round(value, 1) for value in (min(times), median(times), max(times))] if times else [],
        'gap_spacing_seconds_median': round(median(intervals), 3) if intervals else None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--threshold-ms', type=float, default=120)
    args = parser.parse_args()
    if not 0 < args.seconds <= 180 or not 0 < args.threshold_ms <= 60000:
        parser.error('seconds must be in (0, 180], threshold-ms in (0, 60000]')
    with args.log.open('rb') as stream:
        print(json.dumps(summarize(stream, args.seconds, args.threshold_ms / 1000), indent=2))


if __name__ == '__main__':
    main()
