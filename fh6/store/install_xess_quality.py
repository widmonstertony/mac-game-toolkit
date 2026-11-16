#!/usr/bin/env python3
"""Select the locally GPU-checked XeSS SR, without replacing the FG provider."""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--apply', action='store_true')
    args = p.parse_args()
    root = Path.home() / 'Library/Application Support/MACxus/external-highball'
    profile_file = root / 'fh6-profile.json'
    profile = json.loads(profile_file.read_text())
    game = Path(profile['gamePath'])
    ini_file = game / 'OptiScaler.ini'
    source = ini_file.read_text(encoding='utf-8-sig')
    ini = configparser.ConfigParser(interpolation=None)
    ini.read_string(source)
    assert profile['resolution'] == [3472, 2266], 'Unexpected output size'
    assert ini.getboolean('FrameGen', 'Enabled')
    assert ini['FrameGen']['FGInput'] == 'dlssg'
    assert ini['FrameGen']['FGOutput'] in ('fsrfg', 'xefg')
    assert ini.getboolean('QualityOverrides', 'QualityRatioOverrideEnabled')
    assert float(ini['QualityOverrides']['QualityRatioQuality']) == 1.5
    dll = game / 'libxess.dll'
    digest = hashlib.sha256(dll.read_bytes()).hexdigest()
    # Match the actual DLL used by the independent D3DMetal GPU fixture.
    receipt = json.loads((Path(__file__).with_name('xess-gpu-verification.json')).read_text())
    assert digest == receipt['dll_sha256'], 'XeSS DLL changed; repeat GPU verification'
    assert receipt['create'] == receipt['init'] == 0 and receipt['execute'] == [0, 0, 0]
    assert receipt['completed_fence'] == 4 and receipt['nonzero_output_bytes'] > 0
    changed, count = re.subn(r'(?m)^(Dx12Upscaler\s*=\s*)(?:ffx|xess)\s*$', r'\g<1>xess', source)
    assert count == 1, 'Unexpected upscaler selection'
    print(json.dumps({'candidate': 'XeSS 2.0.2 Quality SR; existing FG provider retained',
                      'frame_generation_provider': ini['FrameGen']['FGOutput'],
                      'resolution': profile['resolution'], 'gpu_fixture_passed': True,
                      'game_ghosting_verified': False, 'only_ini_key_changed': 'Upscalers.Dx12Upscaler'}))
    if not args.apply:
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).lower()
    if 'forzahorizon6.exe' in processes:
        raise RuntimeError('FH6 is running; do not change its active configuration')
    for file in [ini_file, profile_file]:
        backup = file.with_name(file.name + '.before-xess-quality-20261115')
        if not backup.exists():
            backup.write_bytes(file.read_bytes())
    ini_file.write_text(changed, encoding='utf-8')
    profile['upscaler'] = 'XeSS 2.0.2, explicit 1.5x Quality render ratio via game DLSS inputs'
    profile['verifiedFiles'][str(dll)] = digest
    profile['xessVerification'] = {'independentGpu': True, 'gameplay': False,
                                  'ghostingResolved': False, 'receipt': receipt}
    profile_file.write_text(json.dumps(profile, indent=2) + '\n')


if __name__ == '__main__':
    main()
