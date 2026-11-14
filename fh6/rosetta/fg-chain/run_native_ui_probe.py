#!/usr/bin/env python3
"""Validate the FH6 candidate against the installed real FFX provider on D3DMetal."""
import argparse
import hashlib
import importlib.util
import json
import os
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


def store_test_runtime(prefix):
    data = Path.home() / 'Library/Application Support/MACxus'
    config = json.loads((data / 'local-highball.json').read_text())
    layer = data / 'external-highball/fh6-prior-fixes'
    renderer, shim = Path(config['rendererRoot']), Path(config['shimRoot'])
    frameworks = Path(config['engineRoot']) / 'frameworks'
    dlls = ':'.join(map(str, (shim/'wine', renderer/'wine', layer/'engine/lib/wine')))
    libraries = ':'.join(map(str, (renderer/'external', frameworks,
                         frameworks/'GStreamer.framework/Versions/1.0/lib', Path(config['libraryRoot']))))
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('XODUS_', 'XUSER_', 'NEXUS_'))}
    env.update(WINEPREFIX=str(prefix), WINEDLLOVERRIDES='winemenubuilder.exe=d;d3d12,d3d12core,dxgi=n,b',
               WINEDLLPATH=dlls, WINEDLLPATH_PREPEND=dlls, DYLD_FALLBACK_LIBRARY_PATH=libraries,
               DYLD_FALLBACK_FRAMEWORK_PATH=str(frameworks) + ':/System/Library/Frameworks',
               WINEMSYNC='1', WINEESYNC='0', CX_D3DMETALPATH=str(renderer/'external'), D3DM_MTL4='0',
               HB_D3D12_REAL='Z:' + str(shim/'wine/x86_64-windows/apd12.dll').replace('/', '\\'))
    for name in ('d3d12.dll', 'd3d12core.dll', 'dxgi.dll'):
        if sha(prefix/'drive_c/windows/system32'/name) != sha(layer/'system32'/name):
            raise RuntimeError('Test prefix needs the verified Store ' + name)
    original = data/'bottles/xbox-highball/drive_c/highball/fg-native-ui/framegeneration-original.dll'
    return layer/'engine/bin/wine', env, original


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--artifacts', type=Path, required=True)
    p.add_argument('--history', action='store_true', help='Validate the history visibility provider too')
    p.add_argument('--capture', action='store_true', help='Also validate bounded fenced synthetic GPU captures')
    p.add_argument('--missing-ui', action='store_true', help='Validate passthrough when the game does not supply UI or HUDless color')
    p.add_argument('--store-test-prefix', type=Path, help='Use a separate test prefix with the Store renderer DLLs')
    args = p.parse_args()
    if args.missing_ui and not args.capture:
        p.error('--missing-ui requires --capture')
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
    if args.store_test_prefix:
        prefix = args.store_test_prefix.resolve(strict=True)
        if not prefix.is_relative_to(Path('/private/tmp')):
            p.error('Use a separately prepared test prefix under /private/tmp')
        wine, env, original = store_test_runtime(prefix)
    else:
        support = Path.home() / 'Library/Application Support/protonish/launchers/2483190'
        profile = support / 'rosetta-profile'
        spec = importlib.util.spec_from_file_location('fh6_profile', support / 'rosetta_profile.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        wine, env = profile/'engine/bin/wine', module.environment(profile)
        original = Path.home() / 'Library/Application Support/Steam/steamapps/common/ForzaHorizon6/OptiScaler/amd_fidelityfx_framegeneration_dx12.dll'
    env['WINEDEBUG'] = '-all'
    candidate = out / ('missing-ui-real' if args.missing_ui else
                       ('history-ui-real' if args.history else 'native-ui-real'))
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
    command = [str(wine), str(out / 'native-ui-probe.exe'),
               win(original), win(candidate / candidate_name)]
    if args.missing_ui:
        command.append('--missing-ui')
    log = candidate / 'native-ui-probe.log' if args.missing_ui else out / 'native-ui-probe.log'
    with log.open('w') as f:
        result = subprocess.run(command, cwd=out, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=180)
    contents = log.read_text(errors='replace')
    print(contents)
    expected = ('PASS missing HUDless/UI passthrough matches the original provider byte for byte'
                if args.missing_ui else 'PASS official FFX generated output')
    if result.returncode or expected not in contents or 'ui_errors=0' not in contents:
        raise RuntimeError('Native UI GPU contrast failed: ' + str(result.returncode))
    rows = [json.loads(line) for line in trace.read_bytes()[offset:].decode().splitlines()]
    applied = [row for row in rows if row['event'] == 'native_ui_dispatch']
    if len(applied) != (0 if args.missing_ui else 8) or any(row['result'] or row['frames'] != 1 for row in applied):
        raise RuntimeError('Real candidate did not execute the validated native UI path')
    if any(row['event']=='native_ui_retained_after_gpu_timeout' or
           (row['event']=='native_ui_passthrough' and not (args.missing_ui or (args.history and 500<=row.get('frame',-1)<520))) for row in rows):
        raise RuntimeError('Candidate unexpectedly declined the valid synthetic inputs')
    created = [row for row in rows if row['event'] == 'native_ui_context']
    pipelines = [row for row in rows if row['event'] == 'native_ui_pipeline']
    if (len(created) != 2 or not all(row.get('pipelineDeferred') for row in created)
            or len(pipelines) != (0 if args.missing_ui else 1)
            or any(row['pipelineHr'] for row in pipelines)):
        raise RuntimeError('Opening context did not remain deferred or active-frame pipeline failed')
    capture_results = []
    if args.capture:
        files = sorted(set(captures.glob('*/capture.json')) - before_captures)
        if len(files) != 2:
            raise RuntimeError('Expected exactly two completed synthetic GPU captures: ' + str(len(files)))
        for file in files:
            meta = json.loads(file.read_text())
            expected_images = ({'depth','motion','present','generated-provider-output'} if args.missing_ui else
                               {'depth','motion','scene','ui','present','generated-before-ui','generated-after-ui'})
            if (not meta['gpu_fence_completed'] or not meta['pixel_files_complete']
                    or {i['name'] for i in meta['images']} != expected_images
                    or meta.get('native_ui_composited') != (not args.missing_ui)):
                raise RuntimeError('Incomplete or unfenced GPU capture')
            for image in meta['images']:
                pixel_file = file.parent / (image['name'] + '.bin')
                if pixel_file.stat().st_size != image['bytes']:
                    raise RuntimeError('Truncated GPU capture: ' + image['name'])
            if args.missing_ui:
                validate_prepare_pixels(file.parent, meta)
            capture_results.append({'frame': meta['frame'], 'images': len(meta['images']), 'gpu_fence_completed': True,
                                    'bytes': sum(image['bytes'] for image in meta['images'])})
        print('PASS fenced GPU readbacks, two synthetic frames:', capture_results)
    (out / 'native-ui-probe-trace.jsonl').write_text('\n'.join(json.dumps(row) for row in rows) + '\n')
    receipt = {'passed': True, 'kind': 'synthetic real D3DMetal GPU / official FFX 3.1.6 / full candidate provider and real DXGI queue',
               'missing_ui': args.missing_ui, 'store_renderer': bool(args.store_test_prefix),
               'game_visual_verified': False,
               'manifest_sha256': sha(out / 'build-manifest.json'), 'artifacts': manifest['artifacts'],
               'original_provider_sha256': sha(original), 'log_sha256': sha(log),
               'trace_sha256': sha(out / 'native-ui-probe-trace.jsonl'), 'native_ui_applied_samples': len(applied),
               'opening_context_deferred': True, 'runtime_imports': imports, 'same_imports_as_audit': True,
               'synthetic_pixel_captures': capture_results}
    if args.history:
        corrected = [row for row in rows if row['event']=='history_post' and row['dispatched']]
        if len(corrected)<(0 if args.missing_ui else 6) or any(row.get('pipelineHr',0) for row in rows if row['event']=='history_prepare'):
            raise RuntimeError('History provider did not exercise the full runtime path')
        if 'Enabled FG loading screen: owned history pipelines=0 expected=0' not in contents or any(row['event']=='history_prepare' and 500<=row['frame']<520 for row in rows):
            raise RuntimeError('Enabled FG loading screen unexpectedly initialized history')
        receipt['history_dispatched_samples'] = len(corrected)
        receipt['enabled_loading_frames_forwarded_without_history_initialization'] = 20
    filename = 'missing-ui-gpu-verification.json' if args.missing_ui else (
        'history-native-ui-gpu-verification.json' if args.history else 'native-ui-gpu-verification.json')
    (out / filename).write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS complete candidate provider and GPU pixel comparison')


def validate_prepare_pixels(folder, meta):
    for name, fmt in (('depth', 19), ('motion', 34)):
        image = next(i for i in meta['images'] if i['name'] == name)
        if image['source_format'] != fmt or image['row_bytes'] != image['width'] * 4:
            raise RuntimeError('Wrong Prepare format or row layout: ' + name)
        data = (folder / (name + '.bin')).read_bytes()
        frame = meta['frame']
        for y in range(image['height']):
            for x in range(image['width']):
                car = 20 + frame * 2 <= x < 52 + frame * 2 and 40 <= y < 72
                offset = y * image['row_pitch'] + x * 4
                expected = struct.pack('<f', .8 if car else .2) if name == 'depth' else (
                    struct.pack('<HH', 0xc000 if car else 0, 0))
                if data[offset:offset + 4] != expected:
                    raise RuntimeError('Prepare pixel does not match the frame input: ' + name)


if __name__ == '__main__':
    main()
