"""Summarize the latest Xbox FH6 launch without printing account data or resource addresses."""

from collections import Counter
import json
from pathlib import Path
import re


AUTH_METADATA = re.compile(
    r'^AUTH_META phase=([a-z0-9-]+) hr=([0-9a-fA-F]{8}) http=(\d{1,3})(?: |$)'
)


def latest_graphics(trace):
    counts = Counter()
    for line in trace:
        try:
            row = json.loads(line)
        except ValueError:
            continue
        event = row.get('event')
        if event == 'observer_ready':
            counts.clear()
            counts['observer_ready'] = 1
        elif counts['observer_ready'] and event == 'prepare':
            counts['sampled_prepares'] += 1
            tags = (row.get('native') or {}).get('tags', [])
            available = {tag['type'] for tag in tags if tag.get('ptr') not in ('0', '', None)}
            if {2, 23} <= available:
                counts['prepares_with_hudless_and_ui'] += 1
        elif counts['observer_ready'] and row.get('result') == 0:
            if event == 'upscale':
                counts['sampled_upscales'] += 1
            if event == 'interpolate' and row.get('frames') == 1:
                counts['sampled_intermediate_requests'] += 1
        if counts['observer_ready'] and event in ('native_ui_dispatch', 'history_post'):
            counts[event] += 1
    return dict(counts)


def latest_auth(prefix):
    paths = list(prefix.glob('fh6-xbox-status-*/status.log'))
    if not paths:
        return None, []
    latest = max(paths, key=lambda p: p.parent.stat().st_mtime_ns)
    counts = Counter()
    if latest.stat().st_size <= 32768:
        with latest.open() as stream:
            for line in stream:
                match = AUTH_METADATA.match(line)
                if match:
                    counts[match.groups()] += 1
    return latest.parent.stat().st_mtime_ns, [
        {'phase': phase, 'hresult': '0x' + hr, 'http': int(http), 'count': count}
        for (phase, hr, http), count in counts.items()
    ]


def main():
    prefix = Path.home() / 'Library/Application Support/MACxus/bottles/xbox-highball/drive_c'
    trace = prefix / 'highball/store-plugins/fh6-fg-chain.jsonl'
    auth_time, auth = latest_auth(prefix)
    graphics = {}
    if trace.is_file() and (auth_time is None or trace.stat().st_mtime_ns >= auth_time):
        with trace.open() as stream:
            graphics = latest_graphics(stream)
    print(json.dumps({'launch_status': auth, 'graphics': graphics}, indent=2))


if __name__ == '__main__':
    main()
