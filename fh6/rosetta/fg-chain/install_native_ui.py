#!/usr/bin/env python3
"""Select only the GPU-validated FH6 native-UI FG candidate; keep SR and game settings."""
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


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse(text):
    value = configparser.ConfigParser(interpolation=None)
    value.optionxform = str
    value.read_string(text)
    return value


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--artifacts', required=True, type=Path)
    p.add_argument('--apply', action='store_true')
    p.add_argument('--history', action='store_true', help='Install the separately GPU-verified runtime visibility repair')
    args = p.parse_args()
    artifacts = args.artifacts.resolve()
    candidate_name = 'framegeneration-history-ui.dll' if args.history else 'framegeneration-native-ui.dll'
    manifest_path = artifacts / 'build-manifest.json'
    manifest = json.loads(manifest_path.read_text())
    verification = json.loads((artifacts / ('history-gpu-verification.json' if args.history else 'native-ui-gpu-verification.json')).read_text())
    if not verification['passed'] or verification['manifest_sha256'] != sha(manifest_path):
        raise RuntimeError('This build has not passed the native UI GPU contrast')
    for name in [candidate_name, 'native-ui-probe.exe']:
        if sha(artifacts / name) != manifest['artifacts'][name] or sha(artifacts / name) != verification['artifacts'][name]:
            raise RuntimeError('GPU-validated artifact changed: ' + name)
    if args.history:
        if verification.get('history_saved_pixel_dispatches') != 4 or verification.get('history_changed_rgb_pixels', 0) < 100:
            raise RuntimeError('Actual captured-pixel runtime correction not verified')
        for name, key in [('history-post-gpu.log', 'history_post_log_sha256'), ('history-provider-gpu.log', 'history_provider_log_sha256'), ('history-provider-trace.jsonl', 'history_provider_trace_sha256')]:
            if sha(artifacts/name) != verification[key]:
                raise RuntimeError('Runtime GPU evidence changed: '+name)
    here = Path(__file__).resolve().parent
    for name, expected in manifest['source_sha256'].items():
        if sha(here / name) != expected:
            raise RuntimeError('Rebuild after source change: ' + name)
    for name, key in [('native-ui-probe.log', 'log_sha256'), ('native-ui-probe-trace.jsonl', 'trace_sha256')]:
        if sha(artifacts / name) != verification[key]:
            raise RuntimeError('GPU evidence changed: ' + name)
    support = Path.home() / 'Library/Application Support/protonish/launchers/2483190'
    profile = support / 'rosetta-profile'
    if not (support / 'rosetta-profile-enabled').exists():
        raise RuntimeError('FH6 private Rosetta route is not selected')
    observer_receipt = json.loads((profile / 'fg-chain-audit.json').read_text())
    game = (Path.home() / 'Library/Application Support/Steam/steamapps/common/ForzaHorizon6').resolve(strict=True)
    observer = game / 'OptiScaler-FH6-SDK/fh6-fg-chain.asi'
    if sha(observer) != observer_receipt['artifacts']['fh6-fg-chain.asi']:
        raise RuntimeError('Unknown observer; native frame ABI not established')
    original = game / 'OptiScaler/amd_fidelityfx_framegeneration_dx12.dll'
    if sha(original) != verification['original_provider_sha256']:
        raise RuntimeError('Official provider changed since GPU validation')
    ini = game / 'OptiScaler.ini'
    before = ini.read_text()
    config = parse(before)
    required = {'Enabled': 'true', 'FGInput': 'dlssg', 'FGOutput': 'fsrfg', 'DrawUIOverFG': 'false'}
    if any(config.get('FrameGen', k) != v for k, v in required.items()) or config.get('FSRFG', 'AllowAsync') != 'false':
        raise RuntimeError('Expected existing single-game-queue 2x FSR route without a second UI overlay')
    expected = r'C:\highball\fg-native-ui'
    old = config.get('Libraries', 'FfxDx12FGPath')
    if old not in (r'C:\highball\fg-chain', expected):
        raise RuntimeError('Unexpected FG provider selection: ' + old)
    changed, count = re.subn(r'(?m)^(FfxDx12FGPath\s*=)[^\r\n]*$', lambda m: m[1] + ' ' + expected, before)
    if count != 1:
        raise RuntimeError('Ambiguous FG path')
    check = parse(changed)
    for section in config.sections():
        a, b = dict(config[section]), dict(check[section])
        if section == 'Libraries':
            a.pop('FfxDx12FGPath'); b.pop('FfxDx12FGPath')
        if a != b:
            raise RuntimeError('Unexpected other setting change: ' + section)
    dest = profile / 'prefix/drive_c/highball/fg-native-ui'
    receipt_file = profile / 'fg-native-ui.json'
    previous = json.loads(receipt_file.read_text()) if receipt_file.exists() else None
    target = dest / 'amd_fidelityfx_framegeneration_dx12.dll'
    if target.exists() and (not previous or sha(target) != previous['candidate_sha256']):
        raise RuntimeError('Unknown existing candidate; no overwrite')
    link = dest / 'framegeneration-original.dll'
    if link.exists() and (not link.is_symlink() or link.resolve() != original.resolve()):
        raise RuntimeError('Unknown original-provider path')
    receipt = {'kind': ('FH6-only GPU history visibility repair plus native premultiplied UI' if args.history else 'FH6-only native premultiplied UI on generated frames'),
               'game_visual_verified': False,
               'previous_fg_path': previous['previous_fg_path'] if previous else old,
               'candidate_sha256': sha(artifacts / candidate_name),
               'original_provider_sha256': sha(original), 'gpu_verification': verification,
               'ini_before_sha256': hashlib.sha256(before.encode()).hexdigest(),
               'ini_after_sha256': hashlib.sha256(changed.encode()).hexdigest()}
    print(json.dumps(receipt, indent=2))
    if not args.apply:
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).lower()
    if 'forzahorizon6.exe' in processes:
        raise RuntimeError('FH6 is still running. Do not replace this round or its DLL.')
    if ini.read_text() != before:
        raise RuntimeError('Configuration changed during preparation')
    dest.mkdir(parents=True, exist_ok=True)
    if not link.exists():
        link.symlink_to(original)
    atomic(target, (artifacts / candidate_name).read_bytes())
    if changed != before:
        atomic(ini, changed.encode())
    atomic(receipt_file, (json.dumps(receipt, indent=2) + '\n').encode())
    print('Selected FH6 native-UI candidate only; same native Steam, official SR/FG algorithms, resolution, audio, cache, saves and cloud.')


if __name__ == '__main__':
    main()
