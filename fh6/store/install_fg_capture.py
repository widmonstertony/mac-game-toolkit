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
MATCHED_PREPARE_CAPTURE = 'eb7fd784ee4f40e16293b72f9c1233555a8cc9c5940821c2ff8d8286a1a56713'
PROVIDER = 'framegeneration-history-ui.dll'


def install(artifacts, renderer=None):
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
    if expected == digest and renderer is None:
        print('The verified FH6 Store capture provider is already installed.')
        return
    if expected not in (digest, BASELINE, COLOR_CAPTURE, MATCHED_PREPARE_CAPTURE):
        raise RuntimeError('Review the different installed FH6 frame generator before replacing it')
    processes = subprocess.check_output(['/bin/ps', '-axo', 'command='], text=True)
    if any('forzahorizon6.exe' in line.lower() or 'runtime/runner.mjs run ' in line.lower()
           or 'external-highball/runner.mjs run ' in line.lower() for line in processes.splitlines()):
        raise RuntimeError('Close FH6 before updating its frame capture provider')
    renderer_files = {}
    if renderer is not None:
        # Both copies are pinned by the Store-only runtime receipt.
        prior = runtime/'fh6-prior-fixes'
        prior_receipt = prior/'prior-fixes.json'
        prior_record = json.loads(prior_receipt.read_text())
        old_digest = prior_record['files']['system32/d3d12.dll']
        private_copy = prior/'system32/d3d12.dll'
        installed_copy = provider_dir.parent.parent/'windows/system32/d3d12.dll'
        if any(path.is_symlink() or sha(path) != old_digest for path in (private_copy, installed_copy)):
            raise RuntimeError('The Store D3D12 shim differs from the verified baseline')
        # This optional build is the pinned MetalHorizon source, verified with
        # both pixel-level VRS controls and the real FFX candidate above.
        if sha(renderer) != '823874378d67b02286f8d58419e25f910b8571e8e15dcbeb6426b7981b8dc988':
            raise RuntimeError('Unrecognized Store D3D12 shim build')
        prior_record['files']['system32/d3d12.dll'] = sha(renderer)
        renderer_files = {private_copy: renderer, installed_copy: renderer}
    node = shutil.which('node')
    if not node:
        raise RuntimeError('Native Node.js is required for the Store profile check')
    verified[str(target)] = digest
    with tempfile.TemporaryDirectory(dir=runtime) as directory:
        tmp = Path(directory)
        staged_profile = tmp/'profile.json'
        staged_profile.write_text(json.dumps(record, indent=2) + '\n')
        staged = {target: candidate, profile: staged_profile, **renderer_files}
        if renderer is not None:
            staged_receipt = tmp/'prior-fixes.json'
            staged_receipt.write_text(json.dumps(prior_record, indent=2) + '\n')
            staged[prior_receipt] = staged_receipt
        previous = {}
        changed = []
        for index, path in enumerate(staged):
            previous[path] = tmp/f'{index}.previous'
            shutil.copy2(path, previous[path])
        try:
            for path, replacement in staged.items():
                replace_file(replacement, path)
                changed.append(path)
            subprocess.run([node, runtime/'runner.mjs', 'check'], check=True, timeout=30)
        except BaseException:
            for path in reversed(changed):
                replace_file(previous[path], path)
            raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', type=Path)
    parser.add_argument('--renderer', type=Path, help='Also install the validated private MetalHorizon D3D12 shim')
    args = parser.parse_args()
    install(args.artifacts, args.renderer)
