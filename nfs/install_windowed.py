#!/usr/bin/env python3
"""Select bordered window mode for the two installed NFS games only."""
import argparse
from pathlib import Path
import re
import subprocess

VALUES = {
    'GstRender.FullscreenEnabled': '0',
    'GstRender.FullscreenMode': '0',
    'GstRender.WindowBordersEnable': '1',
}
GAMES = ('Need for Speed Heat', 'Need For Speed(TM) Unbound')


def patch(text):
    newline = '\r\n' if '\r\n' in text else '\n'
    lines = text.splitlines(keepends=True)
    for key, value in VALUES.items():
        indices = [i for i, line in enumerate(lines)
                   if re.match(re.escape(key) + r'\s', line)]
        if len(indices) > 1:
            raise ValueError('Duplicate display setting: ' + key)
        replacement = key + ' ' + value + newline
        if indices:
            lines[indices[0]] = replacement
        else:
            if lines and not lines[-1].endswith(('\r', '\n')):
                lines[-1] += newline
            lines.append(replacement)
    return ''.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    steam = Path.home() / 'Library/Application Support/Steam/steamapps'
    prefix = steam / 'compatdata/1222680/pfx'
    if (steam / 'compatdata/1846380/pfx').resolve() != prefix.resolve():
        raise RuntimeError('NFS container layout changed; inspect before applying')
    planned = []
    for game in GAMES:
        path = prefix / 'drive_c/users/steamuser/Documents' / game / 'settings/PROFILEOPTIONS_profile'
        if not path.parent.is_dir():
            raise RuntimeError('Missing existing game settings directory: ' + game)
        data = path.read_bytes() if path.exists() else b''
        changed = patch(data.decode('utf-8')).encode('utf-8')
        planned.append((path, data, changed))
        print(game + ': windowed (0), exclusive fullscreen off, window border on')
    if not args.apply:
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).lower()
    if 'needforspeed' in processes:
        raise RuntimeError('Close NFS before editing its settings')
    for path, old, new in planned:
        if old == new:
            continue
        if path.exists() and path.read_bytes() != old:
            raise RuntimeError('Settings changed during preparation')
        backup = path.with_name(path.name + '.before-windowed-20261116')
        if old and not backup.exists():
            backup.write_bytes(old)
        if not old and not path.exists():
            path.with_name(path.name + '.created-windowed-20261116').write_text('Created display-only profile; remove to undo.\n')
        temporary = path.with_name(path.name + '.windowed-tmp')
        temporary.write_bytes(new)
        temporary.replace(path)
    print('Applied. Resolution, quality, audio, binary profile, saves and caches unchanged.')


if __name__ == '__main__':
    main()
