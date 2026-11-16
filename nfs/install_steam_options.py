#!/usr/bin/env python3
"""Persist NFS-only renderer/focus settings in native Steam's game launch options."""
import argparse
from pathlib import Path
import re
import subprocess

OPTIONS = {
    '1222680': 'CX_GRAPHICS_BACKEND=dxmt DXMT_CONFIG=dxgi.handleAltTab=True %command%',
    '1846380': 'CX_GRAPHICS_BACKEND=d3dmetal %command%',
}


def spans(text):
    tokens = list(re.finditer(r'//[^\n]*|"(?:\\.|[^"\\])*"|[{}]|\s+', text))
    if ''.join(t[0] for t in tokens) != text:
        raise ValueError('Unrecognized VDF syntax')
    tokens = [t for t in tokens if not t[0].isspace() and not t[0].startswith('//')]
    position = 0
    found = {}

    def parse(path=(), nested=False):
        nonlocal position
        while position < len(tokens):
            key = tokens[position]
            if key[0] == '}':
                if not nested:
                    raise ValueError('Unexpected closing brace')
                position += 1
                found[path] = ('object', key.start(), key.start())
                return
            if not key[0].startswith('"'):
                raise ValueError('Expected quoted key')
            position += 1
            value = tokens[position]
            position += 1
            child = path + (key[0][1:-1].lower(),)
            if child in found:
                raise ValueError('Duplicate VDF key')
            if value[0] == '{':
                parse(child, True)
            elif value[0].startswith('"'):
                found[child] = ('string', value.start(), value.end())
            else:
                raise ValueError('Expected quoted value or object')
        if nested:
            raise ValueError('Unclosed object')

    parse()
    return found


def update(text):
    nodes = spans(text)
    edits = []
    for appid, options in OPTIONS.items():
        matches = [p for p in nodes if p[-2:] == ('apps', appid) and p[-5:-2] == ('software', 'valve', 'steam')]
        if len(matches) != 1:
            raise ValueError('Expected one Steam app configuration: ' + appid)
        path = matches[0]
        field = nodes.get(path + ('launchoptions',))
        encoded = '"' + options + '"'
        if field:
            kind, start, end = field
            if kind != 'string' or text[start:end] not in ('""', encoded):
                raise ValueError('Existing custom launch options; preserve and inspect first: ' + appid)
            if text[start:end] != encoded:
                edits.append((start, end, encoded))
        else:
            _, close, _ = nodes[path]
            line = text.rfind('\n', 0, close) + 1
            indent = text[line:close]
            if indent.strip():
                raise ValueError('Unexpected Steam formatting')
            edits.append((line, line, indent + '\t"LaunchOptions"\t\t' + encoded + '\n'))
    changed = text
    for start, end, value in sorted(edits, reverse=True):
        changed = changed[:start] + value + changed[end:]
    result = spans(changed)
    # Every pre-existing non-target string must remain byte-identical.
    for path, (kind, start, end) in nodes.items():
        if kind != 'string' or path[-2:] in [(a, 'launchoptions') for a in OPTIONS]:
            continue
        _, a, b = result[path]
        if text[start:end] != changed[a:b]:
            raise AssertionError('Unrelated Steam value changed')
    return changed


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--apply', action='store_true')
    p.add_argument('--include-motorfest', action='store_true', help='Also set DX12 for the installed Motorfest game, with its Ubisoft browser environment')
    args = p.parse_args()
    if args.include_motorfest:
        OPTIONS['2698940'] = 'CX_GRAPHICS_BACKEND=d3dmetal DXMT_ALLOW_CROSS_PROCESS_SWAPCHAIN=1 WINE_SIMULATE_WRITECOPY=1 %command%'
    configs = list((Path.home() / 'Library/Application Support/Steam/userdata').glob('*/config/localconfig.vdf'))
    eligible = []
    for file in configs:
        text = file.read_text()
        if all('"' + a + '"' in text for a in OPTIONS):
            eligible.append((file, text))
    if len(eligible) != 1:
        raise RuntimeError('Select the account containing both NFS games before changing options')
    file, original = eligible[0]
    changed = update(original)
    for appid, options in OPTIONS.items():
        print(appid + ': ' + options)
    if not args.apply or original == changed:
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).lower().splitlines()
    if any(p.strip().endswith('/contents/macos/steam_osx') or any(n in p for n in ('needforspeed', 'thecrewmotorfest', 'upc.exe')) for p in processes):
        raise RuntimeError('Quit native Steam and the targeted games/launchers before applying')
    if file.read_text() != original:
        raise RuntimeError('Steam configuration changed during preparation')
    backup = file.with_name('localconfig.vdf.before-motorfest-options-20261116' if args.include_motorfest else 'localconfig.vdf.before-nfs-options-20261115')
    if not backup.exists():
        backup.write_bytes(file.read_bytes())
        backup.chmod(0o600)
    temporary = file.with_name('.nfs-localconfig.vdf.tmp')
    temporary.write_text(changed)
    temporary.chmod(file.stat().st_mode & 0o777)
    temporary.replace(file)


if __name__ == '__main__':
    main()
