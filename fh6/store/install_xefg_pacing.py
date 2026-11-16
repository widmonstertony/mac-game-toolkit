#!/usr/bin/env python3
"""Select OptiScaler's integrated NVAPI for the FH6 XeFG profile only."""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re
import subprocess

from install_xefg_quality import change_key

OPTISCALER = 'f1b7e510b340dca338a0e0b79d791d2ea59ee61ab2ff047df3a5014833f4d168'


def integrated_nvapi(overrides):
    result = []
    for entry in overrides.split(';'):
        if not entry:
            continue
        names, mode = entry.split('=', 1)
        retained = [name for name in names.split(',') if name.lower() != 'nvapi64']
        if retained:
            result.append(','.join(retained) + '=' + mode)
    return ';'.join(result + ['nvapi64=d'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    root = Path.home() / 'Library/Application Support/MACxus/external-highball'
    profile_file = root / 'fh6-profile.json'
    profile = json.loads(profile_file.read_text())
    game = Path(profile['gamePath'])
    ini = game / 'OptiScaler.ini'
    original = ini.read_text(encoding='utf-8-sig')
    parsed = configparser.ConfigParser(interpolation=None)
    parsed.read_string(original)
    assert profile['resolution'] == [3472, 2266]
    assert hashlib.sha256((game / 'version.dll').read_bytes()).hexdigest() == OPTISCALER
    assert parsed.getboolean('FrameGen', 'Enabled')
    assert parsed['FrameGen']['FGInput'] == 'dlssg'
    assert parsed['FrameGen']['FGOutput'] == 'xefg'
    assert parsed['Upscalers']['Dx12Upscaler'] == 'xess'
    assert parsed.getint('XeFG', 'InterpolationCount') == 1
    assert parsed.getfloat('QualityOverrides', 'QualityRatioQuality') == 1.5
    changed = change_key(original, 'Log', 'LogLevel', '2')
    if not parsed.has_section('fakenvapi'):
        changed = changed.rstrip() + '\n\n[fakenvapi]\nUseFakenvapi = true\n'
    elif parsed.has_option('fakenvapi', 'UseFakenvapi'):
        changed = change_key(changed, 'fakenvapi', 'UseFakenvapi', 'true')
    else:
        changed, count = re.subn(r'(?m)^(\[fakenvapi\][^\S\n]*\n)',
                                r'\1UseFakenvapi = true\n', changed)
        assert count == 1
    after = configparser.ConfigParser(interpolation=None)
    after.read_string(changed)
    allowed = {('Log', 'loglevel'), ('fakenvapi', 'usefakenvapi')}
    for section in parsed:
        for key in parsed[section]:
            if (section, key) not in allowed:
                assert parsed[section][key] == after[section][key]
    profile['environment']['WINEDLLOVERRIDES'] = integrated_nvapi(
        profile['environment']['WINEDLLOVERRIDES'])
    profile['xefgPacing'] = {
        'nvapiProvider': 'OptiScaler integrated',
        'isolatedLoaderAndInitializePassed': True,
        'liveValidationPassed': False,
        'reason': 'External fakenvapi 1.4.1 was loaded alongside OptiScaler integrated latency routing',
    }
    print(json.dumps({'scope': 'FH6 Store child process only',
                      'external_nvapi_loading': 'disabled; OptiScaler supplies the interface',
                      'preserved': ['3472x2266', 'Quality XeSS', 'DLSSG input', 'XeFG 2x',
                                    'audio', 'network', 'cloud', 'caches'],
                      'live_validation_pending': True}))
    if not args.apply:
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).lower()
    if 'forzahorizon6.exe' in processes:
        raise RuntimeError('Stop FH6 before changing the launch profile')
    old_profile = profile_file.read_bytes()
    old_ini = ini.read_bytes()
    for file in (profile_file, ini):
        backup = file.with_name(file.name + '.before-integrated-nvapi-20261116')
        if not backup.exists():
            backup.write_bytes(file.read_bytes())
    try:
        ini.write_text(changed, encoding='utf-8')
        profile_file.write_text(json.dumps(profile, indent=2) + '\n')
    except BaseException:
        profile_file.write_bytes(old_profile)
        ini.write_bytes(old_ini)
        raise


if __name__ == '__main__':
    main()
