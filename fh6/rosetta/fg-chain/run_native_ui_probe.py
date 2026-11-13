#!/usr/bin/env python3
"""Validate the FH6 candidate against the installed real FFX provider on D3DMetal."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pe_imports(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count, optional_size = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    if data[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise RuntimeError('Expected the owned x64 PE build')
    sections = []
    for i in range(count):
        size, rva, raw_size, raw = struct.unpack_from('<IIII', data, optional + optional_size + i * 40 + 8)
        sections.append((rva, max(size, raw_size), raw))
    def offset(rva):
        for start, size, raw in sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise RuntimeError('Invalid PE RVA')
    entry = offset(struct.unpack_from('<I', data, optional + 120)[0])
    result = []
    while any(data[entry:entry + 20]):
        name = offset(struct.unpack_from('<I', data, entry + 12)[0])
        result.append(data[name:data.index(b'\0', name)].decode().lower())
        entry += 20
    return sorted(result)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--artifacts', type=Path, required=True)
    p.add_argument('--history', action='store_true', help='Validate the history visibility provider too')
    p.add_argument('--capture', action='store_true', help='Also validate bounded fenced seven-image synthetic GPU captures')
    args = p.parse_args()
    out = args.artifacts.resolve()
    candidate_name = 'framegeneration-history-ui.dll' if args.history else 'framegeneration-native-ui.dll'
    manifest = json.loads((out / 'build-manifest.json').read_text())
    for name, expected in manifest['artifacts'].items():
        if sha(out / name) != expected:
            raise RuntimeError('Build artifact changed: ' + name)
    imports = pe_imports(out / candidate_name)
    original_imports = pe_imports(out / 'framegeneration-audit.dll')
    if set(imports) != set(original_imports) or any(name.startswith(('d3d', 'dxgi')) for name in imports):
        raise RuntimeError('Candidate introduced a graphics/compiler DLL preload at startup')
    support = Path.home() / 'Library/Application Support/protonish/launchers/2483190'
    profile = support / 'rosetta-profile'
    spec = importlib.util.spec_from_file_location('fh6_profile', support / 'rosetta_profile.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    env = module.environment(profile)
    env['WINEDEBUG'] = '-all'
    original = Path.home() / 'Library/Application Support/Steam/steamapps/common/ForzaHorizon6/OptiScaler/amd_fidelityfx_framegeneration_dx12.dll'
    candidate = out / ('history-ui-real' if args.history else 'native-ui-real')
    candidate.mkdir(exist_ok=True)
    link = candidate / 'framegeneration-original.dll'
    if link.is_symlink() and link.resolve() != original.resolve():
        raise RuntimeError('Unexpected original provider link')
    if not link.exists():
        link.symlink_to(original)
    shutil.copyfile(out / candidate_name, candidate / candidate_name)
    captures = candidate / 'captures'
    before_captures = set(captures.glob('*/capture.json')) if captures.exists() else set()
    if args.capture:
        (candidate / 'capture-request').write_text('synthetic GPU validation, two frames\n')
    trace = out / 'fh6-fg-chain.jsonl'
    offset = trace.stat().st_size if trace.exists() else 0
    def win(path):
        return 'Z:' + str(path).replace('/', '\\')
    command = [str(profile / 'engine/bin/wine'), str(out / 'native-ui-probe.exe'),
               win(original), win(candidate / candidate_name)]
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
    if any(row['event']=='native_ui_retained_after_gpu_timeout' or
           (row['event']=='native_ui_passthrough' and not (args.history and 500<=row.get('frame',-1)<520)) for row in rows):
        raise RuntimeError('Candidate unexpectedly declined the valid synthetic inputs')
    created = [row for row in rows if row['event'] == 'native_ui_context']
    pipelines = [row for row in rows if row['event'] == 'native_ui_pipeline']
    if len(created) != 2 or not all(row.get('pipelineDeferred') for row in created) or len(pipelines) != 1 or pipelines[0]['pipelineHr']:
        raise RuntimeError('Opening context did not remain deferred or active-frame pipeline failed')
    capture_results = []
    if args.capture:
        files = sorted(set(captures.glob('*/capture.json')) - before_captures)
        if len(files) != 2:
            raise RuntimeError('Expected exactly two completed synthetic GPU captures: ' + str(len(files)))
        for file in files:
            meta = json.loads(file.read_text())
            if not meta['gpu_fence_completed'] or not meta['pixel_files_complete'] or len(meta['images']) != 7:
                raise RuntimeError('Incomplete or unfenced GPU capture')
            for image in meta['images']:
                pixel_file = file.parent / (image['name'] + '.bin')
                if pixel_file.stat().st_size != image['bytes']:
                    raise RuntimeError('Truncated GPU capture: ' + image['name'])
            capture_results.append({'frame': meta['frame'], 'images': len(meta['images']), 'gpu_fence_completed': True,
                                    'bytes': sum(image['bytes'] for image in meta['images'])})
        print('PASS fenced seven-image GPU readbacks, two synthetic frames:', capture_results)
    (out / 'native-ui-probe-trace.jsonl').write_text('\n'.join(json.dumps(row) for row in rows) + '\n')
    receipt = {'passed': True, 'kind': 'synthetic real D3DMetal GPU / official FFX 3.1.6 / full candidate provider and real DXGI queue',
               'game_visual_verified': False,
               'manifest_sha256': sha(out / 'build-manifest.json'), 'artifacts': manifest['artifacts'],
               'original_provider_sha256': sha(original), 'log_sha256': sha(log),
               'trace_sha256': sha(out / 'native-ui-probe-trace.jsonl'), 'native_ui_applied_samples': len(applied),
               'opening_context_deferred': True, 'runtime_imports': imports, 'same_imports_as_audit': True,
               'synthetic_pixel_captures': capture_results}
    if args.history:
        corrected = [row for row in rows if row['event']=='history_post' and row['dispatched']]
        if len(corrected)<6 or any(row.get('pipelineHr',0) for row in rows if row['event']=='history_prepare'):
            raise RuntimeError('History provider did not exercise the full runtime path')
        if 'Enabled FG loading screen: owned history pipelines=0 expected=0' not in contents or any(row['event']=='history_prepare' and 500<=row['frame']<520 for row in rows):
            raise RuntimeError('Enabled FG loading screen unexpectedly initialized history')
        receipt['history_dispatched_samples'] = len(corrected)
        receipt['enabled_loading_frames_forwarded_without_history_initialization'] = 20
    (out / ('history-native-ui-gpu-verification.json' if args.history else 'native-ui-gpu-verification.json')).write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS complete candidate provider, unchanged caller descriptors, actual intermediate output and native UI GPU contrast')


if __name__ == '__main__':
    main()
