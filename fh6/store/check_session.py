"""Summarize the latest Xbox FH6 launch without printing account data or resource addresses."""

from collections import Counter
import json
import os
from pathlib import Path
import re


AUTH_METADATA = re.compile(
    r'^AUTH_META phase=([a-z0-9-]+) hr=([0-9a-fA-F]{8}) http=(\d{1,3})(?: |$)'
)
NETWORK_METADATA = re.compile(
    r'^NET_META phase=([a-z0-9-]+) service=([a-z0-9-]+) status=(\d{1,4}) error=(\d{1,10})(?: |$)'
)
TICK = re.compile(r'(?:^| )tick=(\d{1,10})(?: |$)')
TITLE_ROUTE = re.compile(r' requested_title=([01]) actual_title=([01])(?: |$)')


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
        elif counts['observer_ready'] and event == 'prepare' and row.get('result') == 0:
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
        if counts['observer_ready'] and event == 'gpu_capture_saved':
            counts['gpu_captures_completed' if row.get('success') is True else 'gpu_captures_failed'] += 1
    return dict(counts)


def session_graphics(trace, run):
    if not trace.is_file():
        return {}
    receipt = run / 'launch.json'
    with trace.open('rb') as stream:
        stat = os.fstat(stream.fileno())
        if receipt.is_file():
            data = json.loads(receipt.read_text())
            offset = data['traceBytes']
            if (stat.st_dev, stat.st_ino) == (data['traceDevice'], data['traceInode']):
                # With an in-place truncation there is no verified boundary
                # between previous and current launch data.
                if not 0 <= offset <= stat.st_size:
                    return {}
                stream.seek(offset)
        elif stat.st_mtime < run.stat().st_birthtime:
            return {}
        return latest_graphics(stream)


def metadata_rows(file, pattern):
    if not file.is_file():
        return []
    with file.open('rb') as stream:
        data = stream.read(32769)
    if len(data) > 32768:
        return []
    rows = []
    for line in data.decode('ascii', errors='replace').splitlines():
        match, tick = pattern.match(line), TICK.search(line)
        if not match:
            continue
        value = int(tick[1]) if tick else None
        route = TITLE_ROUTE.search(line) if pattern is AUTH_METADATA else None
        rows.append((match.groups(), value if value is not None and value <= 0xffffffff else None,
                     route.groups() if route else ()))
    return rows


def summarize_metadata(auth_rows, net_rows):
    auth = [({'phase': phase, 'hresult': '0x' + hr, 'http': int(http),
              **dict(zip(('requested_title', 'actual_title'), (flag == '1' for flag in route)))}, tick)
            for (phase, hr, http), tick, route in auth_rows]
    net = [({'phase': phase, 'service': service, 'status': int(status), 'error': int(error)}, tick)
           for (phase, service, status, error), tick, _ in net_rows]
    timeline = [(tick, source, event) for source, rows in (('auth', auth), ('network', net))
                for event, tick in rows if tick is not None]
    if timeline:
        # GetTickCount wraps every 49.7 days. A run covering a few minutes
        # can cross that boundary, so order relative to one record first.
        anchor = timeline[0][0]
        timeline = sorted((((tick - anchor + 2**31) % 2**32 - 2**31, source, event)
                           for tick, source, event in timeline), key=lambda row: row[0])
        start = timeline[0][0]
        timeline = [{'elapsed_ms': tick - start, 'source': source, **event}
                    for tick, source, event in timeline[-40:]]
    return count_events(auth), count_events(net), timeline


def count_events(rows):
    return [dict(fields, count=count)
            for fields, count in Counter(tuple(event.items()) for event, _ in rows).items()]


def main():
    prefix = Path.home() / 'Library/Application Support/MACxus/bottles/xbox-highball/drive_c'
    trace = prefix / 'highball/store-plugins/fh6-fg-chain.jsonl'
    runs = [directory for directory in prefix.glob('fh6-xbox-status-*') if directory.is_dir()]
    latest = max(runs, default=None, key=lambda directory: directory.stat().st_birthtime)
    auth, net, timeline = [], [], []
    if latest:
        auth, net, timeline = summarize_metadata(metadata_rows(latest / 'status.log', AUTH_METADATA),
                                                metadata_rows(latest / 'net.log', NETWORK_METADATA))
    graphics = session_graphics(trace, latest) if latest else {}
    print(json.dumps({'launch_recorded': latest is not None, 'launch_status': auth,
                      'network': net, 'last_timed_events': timeline,
                      'graphics': graphics}, indent=2))


if __name__ == '__main__':
    main()
