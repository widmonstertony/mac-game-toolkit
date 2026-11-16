#!/usr/bin/env python3
"""Install the GPU-tested, bounded XeFG status observer for FH6 only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staging', required=True, type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    receipt = json.loads((Path(__file__).parent / 'xefg-validation/status-observer-verification.json').read_text())
    assert receipt['observerStatusMatchesFixture'] and receipt['presentsReportingTwoFramesAndSuccess'] == 15
    observer = args.staging / 'libxess_fg.dll'
    original = args.staging / 'xefg_intel_real.dll'
    assert digest(observer) == receipt['observerSha256']
    assert digest(original) == receipt['originalIntelSha256']
    profile_path = Path.home() / 'Library/Application Support/MACxus/external-highball/fh6-profile.json'
    old_profile = profile_path.read_bytes()
    profile = json.loads(old_profile)
    assert profile['resolution'] == [3472, 2266]
    game = Path(profile['gamePath'])
    target = game / 'libxess_fg.dll'
    real = game / 'xefg_intel_real.dll'
    if digest(target) == receipt['observerSha256']:
        assert digest(real) == receipt['originalIntelSha256']
        assert profile['verifiedFiles'].get(str(target)) == receipt['observerSha256']
        assert profile['verifiedFiles'].get(str(real)) == receipt['originalIntelSha256']
        print('Already installed and hash-verified; no changes.')
        return
    assert digest(target) == receipt['originalIntelSha256']
    assert profile['verifiedFiles'].get(str(target)) == receipt['originalIntelSha256']
    assert not real.exists(), 'Unexpected existing real DLL; review before installing'
    backup = profile_path.with_name(profile_path.name + '.before-xefg-status-20261116')
    assert not backup.exists() or backup.read_bytes() == old_profile
    profile['verifiedFiles'][str(target)] = receipt['observerSha256']
    profile['verifiedFiles'][str(real)] = receipt['originalIntelSha256']
    profile['xefgStatusObserver'] = {
        'purpose': 'Read actual SDK presentation status; does not change rendering',
        'gpuFixturePassed': True, 'liveVerified': False,
        'logFilename': 'XeFG-present-status.jsonl',
        'logLocation': 'Loaded DLL directory; Xodus materialized view during Store launch',
    }
    print('Ready: only FH6 XeFG status forwarding and two verified-file hashes change.')
    if not args.apply:
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).lower()
    if 'forzahorizon6.exe' in processes:
        raise RuntimeError('FH6 must be stopped before replacing its DLL')
    old_dll = target.read_bytes()
    temporary = target.with_name(target.name + '.status-observer-install.tmp')
    assert not temporary.exists()
    if not backup.exists():
        backup.write_bytes(old_profile)
    try:
        real.write_bytes(old_dll)
        temporary.write_bytes(observer.read_bytes())
        os.replace(temporary, target)
        profile_path.write_text(json.dumps(profile, indent=2) + '\n')
        assert digest(target) == receipt['observerSha256']
        assert digest(real) == receipt['originalIntelSha256']
    except BaseException:
        target.write_bytes(old_dll)
        profile_path.write_bytes(old_profile)
        if real.exists() and digest(real) == receipt['originalIntelSha256']:
            real.unlink()
        temporary.unlink(missing_ok=True)
        raise
    print('Installed. Original Intel DLL preserved. Live validation still required.')


if __name__ == '__main__':
    main()
