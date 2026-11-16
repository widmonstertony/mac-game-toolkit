#!/usr/bin/env python3
"""Install hash-checked XeSS-FG for FH6 only, retaining DLSSG input and Quality SR."""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def change_key(text, section, key, value):
    pattern = r'(?ms)(^\[' + re.escape(section) + r'\]\s*\n)(.*?)(?=^\[|\Z)'
    matches = list(re.finditer(pattern, text))
    if len(matches) != 1:
        raise RuntimeError('Missing or duplicate section: ' + section)
    match = matches[0]
    body, count = re.subn(r'(?m)^(' + re.escape(key) + r'\s*=\s*)[^\r\n]*',
                         lambda m: m[1] + value, match[2])
    if count != 1:
        raise RuntimeError('Missing or duplicate key: ' + section + '.' + key)
    return text[:match.start()] + match[1] + body + text[match.end():]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, required=True, help='Official Intel SDK bin directory')
    p.add_argument('--apply', action='store_true')
    args = p.parse_args()
    receipt = json.loads(Path(__file__).with_name('xefg-gpu-verification.json').read_text())
    profile_file = Path.home() / 'Library/Application Support/MACxus/external-highball/fh6-profile.json'
    profile = json.loads(profile_file.read_text())
    game = Path(profile['gamePath'])
    ini_file = game / 'OptiScaler.ini'
    original = ini_file.read_text(encoding='utf-8-sig')
    before = configparser.ConfigParser(interpolation=None)
    before.read_string(original)
    assert profile['resolution'] == [3472, 2266]
    assert before.getboolean('FrameGen', 'Enabled')
    assert before['FrameGen']['FGInput'] == 'dlssg'
    assert before['FrameGen']['FGOutput'] in ('fsrfg', 'xefg')
    assert before['Upscalers']['Dx12Upscaler'] == 'xess'
    assert before.getboolean('QualityOverrides', 'QualityRatioOverrideEnabled')
    assert float(before['QualityOverrides']['QualityRatioQuality']) == 1.5
    sr_receipt = json.loads(Path(__file__).with_name('xess-gpu-verification.json').read_text())
    assert digest(game / 'libxess.dll') == sr_receipt['dll_sha256']
    # Inspect the actual loaded OptiScaler, not merely upstream's latest version.
    opti = (game / 'version.dll').read_bytes()
    assert b'XeFG_Dx12::CreateSwapchainContext' in opti and b'InputCommon::acquire_xefg_xell' in opti
    for name, expected in receipt['files'].items():
        assert digest(args.sdk / name) == expected, 'SDK changed: ' + name
        if (game / name).exists():
            assert digest(game / name) == expected, 'Unrecognized existing library: ' + name
    assert receipt['synthetic']['successful_two_frame_presents'] == 15
    assert receipt['capturedPair']['followingOriginalMatchesInput']
    changes = {('FrameGen', 'FGOutput'): 'xefg', ('XeFG', 'InterpolationCount'): '1',
               ('XeFG', 'IgnoreInitChecks'): 'false'}
    changed = original
    for (section, key), value in changes.items():
        changed = change_key(changed, section, key, value)
    after = configparser.ConfigParser(interpolation=None)
    after.read_string(changed)
    for section in before.sections():
        for key in before[section]:
            if (section, key) not in {(s, k.lower()) for s, k in changes}:
                assert before[section][key] == after[section][key]
    print(json.dumps({'selection': 'XeSS SR Quality + XeSS-FG 2x via existing DLSSG inputs',
                      'resolution': profile['resolution'], 'live_validation_pending': True,
                      'changes': [s + '.' + k for s, k in changes]}))
    if not args.apply:
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).lower()
    if 'forzahorizon6.exe' in processes:
        raise RuntimeError('Close FH6 before changing its libraries')
    for file in [ini_file, profile_file]:
        backup = file.with_name(file.name + '.before-xefg-20261115')
        if not backup.exists():
            backup.write_bytes(file.read_bytes())
    for name, expected in receipt['files'].items():
        destination = game / name
        destination.write_bytes((args.sdk / name).read_bytes())
        assert digest(destination) == expected
        profile['verifiedFiles'][str(destination)] = expected
    ini_file.write_text(changed, encoding='utf-8')
    profile['frameGeneration'] = 'XeSS-FG 1.3.1, one interpolated frame (2x), DLSSG input; live validation pending'
    profile['xefgVerification'] = receipt
    profile_file.write_text(json.dumps(profile, indent=2) + '\n')


if __name__ == '__main__':
    main()
