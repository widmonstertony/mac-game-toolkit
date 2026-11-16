#!/usr/bin/env python3
"""Give Motorfest's Ubisoft browser private DXMT without replacing game DX12."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    support = Path.home() / 'Library/Application Support'
    prefix = support / 'Steam/steamapps/compatdata/2698940/pfx'
    app = prefix / 'drive_c/Program Files (x86)/Ubisoft/Ubisoft Game Launcher'
    if not (app / 'UbisoftConnect.exe').exists():
        raise RuntimeError('Install the official bundled Ubisoft Connect first')
    source = support / 'Highball/engines/x64-crossover26.3-r28/renderers/dxmt/wine/x86_64-windows'
    payload = {}
    hashes = {}
    for name in ('d3d11.dll', 'dxgi.dll'):
        data = bytearray((source / name).read_bytes())
        if data[64:81] != b'Wine builtin DLL\0':
            raise RuntimeError('Unexpected renderer format')
        old_hash = hashlib.sha256(data).hexdigest()
        # Wine otherwise redirects these tagged private copies to WINEDLLPATH.
        # Remove only the DOS marker; all code and PE sections remain intact.
        data[64:81] = bytes(17)
        digest = hashlib.sha256(data).hexdigest()
        target = app / name
        if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() not in (old_hash, digest):
            raise RuntimeError('Unrecognized existing browser-local DLL: ' + name)
        payload[name] = data
        hashes[name] = {'source': old_hash, 'installed': digest}
    registry = prefix / 'user.reg'
    old = registry.read_text()
    changed = old
    for exe in ('upc.exe', 'UplayWebCore.exe'):
        key = r'Software\\Wine\\AppDefaults\\' + exe + r'\\DllOverrides'
        expression = r'(?m)^\[' + re.escape(key) + r'\][^\n]*\n(?P<body>(?:(?!\n\[).)*)(?=\n\[|\Z)'
        match = re.search(expression, changed, re.S)
        section = match.group(0) if match else '[' + key + ']\n'
        for dll in ('d3d11', 'dxgi'):
            line = '"' + dll + '"="native,builtin"'
            if re.search(r'(?m)^"' + dll + '"=', section):
                section = re.sub(r'(?m)^"' + dll + r'"=.*$', line, section)
            else:
                section = section.rstrip() + '\n' + line + '\n'
        changed = changed[:match.start()] + section + changed[match.end():] if match else changed + '\n' + section
    print('Motorfest only: Ubisoft browser-local DXMT; game renderer and other containers untouched.')
    if not args.apply:
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).lower()
    if any(name in processes for name in ('thecrewmotorfest', 'upc.exe', 'uplaywebcore.exe', 'ubisoftconnect.exe')):
        raise RuntimeError('Close Motorfest and its Ubisoft launcher first')
    if registry.read_text() != old:
        raise RuntimeError('Registry changed during preparation')
    backup = registry.with_name('user.reg.before-ubisoft-dxmt-20261116')
    if not backup.exists():
        backup.write_bytes(registry.read_bytes())
        backup.chmod(0o600)
    for name, data in payload.items():
        (app / name).write_bytes(data)
    registry.write_text(changed)
    (app / '.mac-game-toolkit-browser.json').write_text(json.dumps({'files': hashes, 'scope': 'Ubisoft browser in Motorfest prefix only'}, indent=2) + '\n')
    print('Installed; no authentication, anti-cheat, game binary, save or cache modified.')


if __name__ == '__main__':
    main()
