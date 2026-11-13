#!/usr/bin/env python3
"""Validate the FH6 candidate against the installed real FFX provider on D3DMetal."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--artifacts', type=Path, required=True)
    args = p.parse_args()
    out = args.artifacts.resolve()
    manifest = json.loads((out / 'build-manifest.json').read_text())
    for name, expected in manifest['artifacts'].items():
        if sha(out / name) != expected:
            raise RuntimeError('Build artifact changed: ' + name)
    support = Path.home() / 'Library/Application Support/protonish/launchers/2483190'
    profile = support / 'rosetta-profile'
    spec = importlib.util.spec_from_file_location('fh6_profile', support / 'rosetta_profile.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    env = module.environment(profile)
    env['WINEDEBUG'] = '-all'
    original = Path.home() / 'Library/Application Support/Steam/steamapps/common/ForzaHorizon6/OptiScaler/amd_fidelityfx_framegeneration_dx12.dll'
    candidate = out / 'native-ui-real'
    candidate.mkdir(exist_ok=True)
    link = candidate / 'framegeneration-original.dll'
    if link.is_symlink() and link.resolve() != original.resolve():
        raise RuntimeError('Unexpected original provider link')
    if not link.exists():
        link.symlink_to(original)
    shutil.copyfile(out / 'framegeneration-native-ui.dll', candidate / 'framegeneration-native-ui.dll')
    trace = out / 'fh6-fg-chain.jsonl'
    offset = trace.stat().st_size if trace.exists() else 0
    def win(path):
        return 'Z:' + str(path).replace('/', '\\')
    command = [str(profile / 'engine/bin/wine'), str(out / 'native-ui-probe.exe'),
               win(original), win(candidate / 'framegeneration-native-ui.dll')]
    log = out / 'native-ui-probe.log'
    with log.open('w') as f:
        result = subprocess.run(command, cwd=out, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=180)
    contents = log.read_text(errors='replace')
    print(contents)
    if result.returncode or 'reference_errors=0' not in contents or 'ui_errors=0' not in contents or 'PASS official FFX generated output' not in contents:
        raise RuntimeError('Native UI GPU contrast failed: ' + str(result.returncode))
    rows = [json.loads(line) for line in trace.read_bytes()[offset:].decode().splitlines()]
    applied = [row for row in rows if row['event'] == 'native_ui_dispatch']
    if len(applied) != 8 or any(row['result'] or row['frames'] != 1 for row in applied):
        raise RuntimeError('Real candidate did not execute the validated native UI path')
    if any(row['event'] in ('native_ui_passthrough', 'native_ui_retained_after_gpu_timeout') for row in rows):
        raise RuntimeError('Candidate unexpectedly declined the valid synthetic inputs')
    (out / 'native-ui-probe-trace.jsonl').write_text('\n'.join(json.dumps(row) for row in rows) + '\n')
    receipt = {'passed': True, 'kind': 'synthetic real D3DMetal GPU / official FFX 3.1.6 / full candidate provider and real DXGI queue',
               'game_visual_verified': False,
               'manifest_sha256': sha(out / 'build-manifest.json'), 'artifacts': manifest['artifacts'],
               'original_provider_sha256': sha(original), 'log_sha256': sha(log),
               'trace_sha256': sha(out / 'native-ui-probe-trace.jsonl'), 'native_ui_applied_samples': len(applied)}
    (out / 'native-ui-gpu-verification.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS complete candidate provider, unchanged caller descriptors, actual intermediate output and native UI GPU contrast')


if __name__ == '__main__':
    main()
