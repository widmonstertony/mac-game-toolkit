#!/usr/bin/env python3
"""Install the GPU-verified frame capture provider in the FH6 Store prefix."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from prior_fixes import replace_file, sha

BASELINE = '4b3b9a6ff9b21120ac17452f06b065d8bf245743a12224a68942a8bf4ab9e506'
COLOR_CAPTURE = '9cdc49afcaf1e98bc25b363ef6faf908bf888d1507ba3524f41b02248d35cb0d'
PROVIDER = 'framegeneration-history-ui.dll'


def install(artifacts):
    root = Path.home() / 'Library/Application Support/MACxus'
    runtime = root / 'external-highball'
    profile = runtime / 'fh6-profile.json'
    record = json.loads(profile.read_text())
    verified = record['verifiedFiles']
    provider_dir = root/'bottles/xbox-highball/drive_c/highball/fg-native-ui'
    target = provider_dir/'amd_fidelityfx_framegeneration_dx12.dll'
    original = provider_dir/'framegeneration-original.dll'
    if not target.is_file() or target.is_symlink() or verified.get(str(target)) != sha(target):
        raise RuntimeError('Installed FH6 provider differs from the Store profile')
    candidate = artifacts/PROVIDER
    digest = sha(candidate)
    original_digest = sha(original)
    if original_digest != verified.get(str(original)):
        raise RuntimeError('Installed original frame generator differs from the Store profile')
    manifest_digest = sha(artifacts/'build-manifest.json')
    for file, missing_ui, image_count in (
        ('missing-ui-gpu-verification.json', True, 4),
        ('history-native-ui-gpu-verification.json', False, 7),
    ):
        receipt = json.loads((artifacts/file).read_text())
        captures = receipt['synthetic_pixel_captures']
        if (receipt.get('passed') is not True or receipt.get('store_renderer') is not True
                or receipt.get('missing_ui') is not missing_ui
                or receipt['manifest_sha256'] != manifest_digest
                or receipt['artifacts'][PROVIDER] != digest
                or receipt['original_provider_sha256'] != original_digest
                or len(captures) != 2 or {sample['frame'] for sample in captures} != {1, 2}
                or any(sample['images'] != image_count or sample['gpu_fence_completed'] is not True
                       for sample in captures)):
            raise RuntimeError('Store GPU proof does not match the candidate: ' + file)
    expected = verified[str(target)]
    if expected == digest:
        print('The verified FH6 Store capture provider is already installed.')
        return
    if expected not in (BASELINE, COLOR_CAPTURE):
        raise RuntimeError('Review the different installed FH6 frame generator before replacing it')
    processes = subprocess.check_output(['/bin/ps', '-axo', 'command='], text=True)
    if any('forzahorizon6.exe' in line.lower() or 'runtime/runner.mjs run ' in line.lower()
           or 'external-highball/runner.mjs run ' in line.lower() for line in processes.splitlines()):
        raise RuntimeError('Close FH6 before updating its frame capture provider')
    node = shutil.which('node')
    if not node:
        raise RuntimeError('Native Node.js is required for the Store profile check')
    verified[str(target)] = digest
    with tempfile.TemporaryDirectory(dir=runtime) as directory:
        tmp = Path(directory)
        shutil.copy2(target, tmp/'provider.previous')
        shutil.copy2(profile, tmp/'profile.previous')
        (tmp/'profile.json').write_text(json.dumps(record, indent=2) + '\n')
        try:
            replace_file(candidate, target)
            replace_file(tmp/'profile.json', profile)
            subprocess.run([node, runtime/'runner.mjs', 'check'], check=True, timeout=30)
        except BaseException:
            replace_file(tmp/'provider.previous', target)
            replace_file(tmp/'profile.previous', profile)
            raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', type=Path)
    install(parser.parse_args().artifacts)
