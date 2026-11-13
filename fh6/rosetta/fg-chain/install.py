#!/usr/bin/env python3
"""Install the FH6-only read-only chain observer, without selecting another SR/FG backend."""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from install_optiscaler import atomic


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    support = Path.home() / 'Library/Application Support/protonish/launchers/2483190'
    profile = support / 'rosetta-profile'
    if not (support / 'rosetta-profile-enabled').exists():
        raise RuntimeError('This observer targets the selected FH6 Rosetta route only')
    game = (Path.home() / 'Library/Application Support/Steam/steamapps/common/ForzaHorizon6').resolve(strict=True)
    ini = game / 'OptiScaler.ini'
    before = ini.read_text()
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read_string(before)
    fg = dict(config['FrameGen'])
    if (fg.get('Enabled'), fg.get('FGInput'), fg.get('FGOutput')) != ('true', 'dlssg', 'fsrfg'):
        raise RuntimeError('Expected the existing active DLSS-input/FSR-output FG route')
    manifest = json.loads((args.artifacts / 'build-manifest.json').read_text())
    owned = ['fh6-fg-chain.asi', 'upscaler-audit.dll', 'framegeneration-audit.dll']
    for name in owned:
        if digest(args.artifacts / name) != manifest['artifacts'][name]:
            raise RuntimeError('Build manifest mismatch: ' + name)
    here = Path(__file__).resolve().parent
    for name, expected in manifest['source_sha256'].items():
        if digest(here / name) != expected:
            raise RuntimeError('Rebuild after the source change: ' + name)
    backend = game / 'OptiScaler'
    originals = {
        'upscaler-original.dll': backend / 'amd_fidelityfx_upscaler_dx12.dll',
        'framegeneration-original.dll': backend / 'amd_fidelityfx_framegeneration_dx12.dll',
    }
    for path in originals.values():
        if not path.is_file():
            raise RuntimeError('Missing existing official provider: ' + path.name)
    dest = profile / 'prefix/drive_c/highball/fg-chain'
    plugin = game / 'OptiScaler-FH6-SDK/fh6-fg-chain.asi'
    receipt_file = profile / 'fg-chain-audit.json'
    previous = json.loads(receipt_file.read_text()) if receipt_file.exists() else None
    if plugin.exists() and (not previous or digest(plugin) != previous['artifacts']['fh6-fg-chain.asi']):
        raise RuntimeError('An unknown observer already exists; no replacement made')
    expected = r'C:\highball\fg-chain'
    changed = before
    for key in ['FfxDx12SRPath', 'FfxDx12FGPath']:
        old = config.get('Libraries', key)
        if old not in ('.\\OptiScaler', expected):
            raise RuntimeError('Unexpected active provider path: ' + old)
        pattern = re.compile(r'(?m)^(' + re.escape(key) + r'\s*=)[^\r\n]*$')
        changed, count = pattern.subn(lambda m: m[1] + ' ' + expected, changed)
        if count != 1:
            raise RuntimeError('Ambiguous library path in configuration: ' + key)
    check = configparser.ConfigParser(interpolation=None)
    check.optionxform = str
    check.read_string(changed)
    for section in config.sections():
        a, b = dict(config[section]), dict(check[section])
        if section == 'Libraries':
            for key in ['FfxDx12SRPath', 'FfxDx12FGPath']:
                a.pop(key, None); b.pop(key, None)
        if a != b:
            raise RuntimeError('Unexpected non-observer configuration change: ' + section)
    receipt = {
        'purpose': 'diagnosis only, not a claimed ghosting repair',
        'before_libraries': previous['before_libraries'] if previous else dict(config['Libraries']),
        'artifacts': {name: manifest['artifacts'][name] for name in owned},
        'original_providers': {name: {'sha256': digest(path), 'path': str(path)} for name, path in originals.items()},
        'framegen_unchanged': fg == dict(check['FrameGen']),
        'ini_before_sha256': hashlib.sha256(before.encode()).hexdigest(),
        'ini_after_sha256': hashlib.sha256(changed.encode()).hexdigest(),
    }
    print(json.dumps(receipt, ensure_ascii=False, indent=2))
    if not args.apply:
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).lower()
    if 'forzahorizon6.exe' in processes:
        raise RuntimeError('FH6 is running; keep the current game untouched')
    if ini.read_text() != before:
        raise RuntimeError('Configuration changed during preparation')
    dest.mkdir(parents=True, exist_ok=True)
    for name, path in originals.items():
        link = dest / name
        if link.exists() and not link.is_symlink():
            raise RuntimeError('Refusing to overwrite a non-symlink provider')
        if link.is_symlink():
            if link.resolve() == path.resolve():
                continue
            raise RuntimeError('Unexpected provider symlink')
        link.symlink_to(path)
    atomic(dest / 'amd_fidelityfx_upscaler_dx12.dll', (args.artifacts / 'upscaler-audit.dll').read_bytes())
    atomic(dest / 'amd_fidelityfx_framegeneration_dx12.dll', (args.artifacts / 'framegeneration-audit.dll').read_bytes())
    atomic(plugin, (args.artifacts / 'fh6-fg-chain.asi').read_bytes())
    atomic(ini, changed.encode())
    atomic(receipt_file, (json.dumps(receipt, ensure_ascii=False, indent=2) + '\n').encode())
    print('Installed observers only. Native Steam, SR selection, FG selection/2x, display/audio/cache/saves unchanged.')


if __name__ == '__main__':
    main()
