#!/usr/bin/env python3
"""Bounded, read-only snapshots BEFORE FH6's watchdog starts DLL cleanup.

Run before one normal native Steam launch. Never launches/stops the game, attaches
a debugger, suspends a thread, changes settings, or reads account/auth data.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

WAIT = re.compile(r'RtlpWaitForCriticalSection section ([0-9A-Fa-f]+).*'
                  r'wait timed out in thread ([0-9A-Fa-f]+), blocked by ([0-9A-Fa-f]+)')


class Tail:
    def __init__(self, path):
        self.path = Path(path)
        self.offset = self.path.stat().st_size if self.path.exists() else 0
        self.pending = b''

    def lines(self):
        if not self.path.exists():
            return []
        if self.path.stat().st_size < self.offset:
            self.offset = 0
            self.pending = b''
        with self.path.open('rb') as stream:
            stream.seek(self.offset)
            data = stream.read(1024 * 1024)
            self.offset = stream.tell()
        parts = (self.pending + data).split(b'\n')
        self.pending = parts.pop()
        # A malformed oversized record must not grow memory forever.
        if len(self.pending) > 256 * 1024:
            self.pending = b''
        return [part.decode('utf-8', 'replace') for part in parts]


def waits(lines):
    for line in lines:
        match = WAIT.search(line)
        if match:
            address, waiter, owner = (int(value, 16) for value in match.groups())
            yield {'address': address, 'waiter': waiter, 'owner': owner}


def game_processes():
    result = subprocess.run(['/bin/ps', '-axo', 'pid=,comm='], text=True,
                            capture_output=True, timeout=5, check=True)
    return [int(parts[0]) for line in result.stdout.splitlines()
            if len(parts := line.strip().split(None, 1)) == 2
            and parts[1].lower().endswith('\\forzahorizon6.exe')]


REPORT_FIELDS = {'UI_SCENE', 'PLM_STATE', 'UPTIME', 'UPTIME_DRIVING', 'REASON',
                 'REASON_INFO', 'EXCEPTION_INPROGRESS', 'D3D_HR_ERROR',
                 'VRAM_BUDGET', 'VRAM_USED', 'MEM_USED', 'MEM_LIMIT',
                 'AUDIO_API_ERROR_COUNT', 'AUDIO_ERROR_COUNT',
                 'LAST_STATE_MACHINE_INSTANCE', 'LAST_STATE_MACHINE_STACK',
                 'PLAYER_CAR_STATE', 'ENTITY_FRAMES_EMPTY', 'ENTITY_FRAMES_MISMATCHED'}


def save_report(path, output, earliest):
    if not path.exists() or path.stat().st_mtime < earliest:
        return
    try:
        root = ET.parse(path)
    except (ET.ParseError, OSError):
        return
    values = {node.tag: node.get('Value') for node in root.iter()
              if node.tag in REPORT_FIELDS}
    output.write_text(json.dumps(values, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--helper', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=1800)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error('--seconds must be between 1 and 3600')
    support = Path.home() / 'Library/Application Support/protonish/launchers/2483190'
    profile = support / 'rosetta-profile'
    trace = Path.home() / 'Library/Application Support/Steam/steamapps/common/ForzaHorizon6/OptiScaler-FH6-SDK'
    dispatch = Tail(support / 'arm64-dispatch.log')
    frames = Tail(trace / 'fh6-fg-chain.jsonl')
    exits = Tail(trace / 'fh6-process-exit.jsonl')
    spec = importlib.util.spec_from_file_location('fh6_profile', support / 'rosetta_profile.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    env = module.environment(profile)
    env['WINEDEBUG'] = '-all'
    args.output.mkdir(parents=True, exist_ok=True)
    started = time.time()
    deadline = time.monotonic() + args.seconds
    pid = None
    seen = set()
    count = 0
    next_snapshot = None

    def record(event):
        event['wallTime'] = time.time()
        with (args.output / 'events.jsonl').open('a') as stream:
            stream.write(json.dumps(event) + '\n')
        print(json.dumps(event), flush=True)

    def snapshot(reason, lock=None):
        nonlocal count
        if count >= 16 or pid is None:
            return
        count += 1
        commands = ['all', '--no-scan'] if lock is None else [
            hex(lock['owner']), hex(lock['waiter']), '--no-scan',
            '--critical=' + format(lock['address'], 'x')]
        name = f'{count:02d}-{reason}.txt'
        before = time.monotonic()
        try:
            with (args.output / name).open('w') as stream:
                result = subprocess.run([str(profile / 'engine/bin/wine'),
                                         str(args.helper.resolve()), str(pid), *commands],
                                        env=env, stdout=stream, stderr=subprocess.STDOUT,
                                        timeout=20)
            record({'event': 'snapshot', 'reason': reason, 'file': name,
                    'returncode': result.returncode, 'seconds': time.monotonic() - before,
                    'lock': lock})
        except subprocess.TimeoutExpired:
            record({'event': 'helper_timeout', 'file': name})

    record({'event': 'armed', 'seconds': args.seconds, 'existingProcesses': game_processes()})
    while time.monotonic() < deadline:
        for line in frames.lines():
            try:
                row = json.loads(line)
            except ValueError:
                continue
            if row.get('event') == 'observer_ready':
                if pid is not None:
                    record({'event': 'second_run_seen_stopping'})
                    return
                candidates = game_processes()
                if len(candidates) != 1:
                    record({'event': 'ambiguous_processes_stopping', 'candidates': candidates})
                    return
                pid = int(row['pid'])
                next_snapshot = time.monotonic() + 30
                record({'event': 'target', 'winePid': pid, 'macPid': candidates[0],
                        'tick': row.get('tick')})
        for line in exits.lines():
            try:
                row = json.loads(line)
            except ValueError:
                continue
            if pid is not None and row.get('pid') == pid:
                record({'event': 'exit_seen', 'request': row})
                save_report(profile / 'prefix/drive_c/users/tonytan/AppData/Local/ForzaHorizon6/HangReport.xml',
                            args.output / 'hang-report-redacted.json', started)
                return
        # Only lock metadata is retained, never arbitrary Wine stderr or URLs.
        for lock in waits(dispatch.lines()):
            key = tuple(lock.values())
            if pid is not None and key not in seen:
                seen.add(key)
                record({'event': 'critical_timeout', **lock})
                snapshot('lock-owner', lock)
                snapshot('all-at-timeout')
        if pid is not None and next_snapshot is not None and time.monotonic() >= next_snapshot:
            snapshot('periodic')
            next_snapshot = time.monotonic() + 60
        time.sleep(1)
    record({'event': 'deadline', 'snapshots': count})


if __name__ == '__main__':
    main()
